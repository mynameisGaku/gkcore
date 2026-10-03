#include "GlbLoader.h"
#include "../image/ImageLoader.h"
#include "../foundation/Memory.h"
#include "../../third_party/cgltf/cgltf.h"
#include <math.h>
#include <string.h>

/**
 * Internal conversion from validated glTF data into retained model payloads.
 */
namespace gk::detail {
/**
 * Bounds-checked helpers used only while parsing a GLB asset.
 */
namespace {
const uint32_t maxModelFileBytes = 64u * 1024u * 1024u;
const uint32_t maxOutputVertices = 4000000u;
const uint32_t maxOutputIndices = 6000000u;
const uint32_t maxSourceValues = 2000000u;
const uint32_t missingIndex = 0xffffffffu;
/**
 * Rejects non-finite values before storing transformed vertex attributes.
 */
bool IsFinite(float value) { return isfinite(value) != 0; }

/**
 * Applies a column-major world matrix to one position.
 */
void TransformPosition(const float matrix[16], const float source[3], float output[3]) {
    output[0] = matrix[0]*source[0] + matrix[4]*source[1] + matrix[8]*source[2] + matrix[12];
    output[1] = matrix[1]*source[0] + matrix[5]*source[1] + matrix[9]*source[2] + matrix[13];
    output[2] = matrix[2]*source[0] + matrix[6]*source[1] + matrix[10]*source[2] + matrix[14];
}
/**
 * Applies the inverse-transpose linear matrix to a normal, preserving it for singular transforms.
 */
void TransformNormal(const float matrix[16], const float source[3], float output[3]) {
    const float a=matrix[0], b=matrix[4], c=matrix[8], d=matrix[1], e=matrix[5], f=matrix[9], g=matrix[2], h=matrix[6], i=matrix[10];
    const float determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if (fabsf(determinant) < 1.0e-12f) { output[0]=source[0]; output[1]=source[1]; output[2]=source[2]; return; }
    const float inv=1.0f/determinant;
    const float n00=(e*i-f*h)*inv, n01=(f*g-d*i)*inv, n02=(d*h-e*g)*inv;
    const float n10=(c*h-b*i)*inv, n11=(a*i-c*g)*inv, n12=(b*g-a*h)*inv;
    const float n20=(b*f-c*e)*inv, n21=(c*d-a*f)*inv, n22=(a*e-b*d)*inv;
    output[0]=n00*source[0]+n10*source[1]+n20*source[2];
    output[1]=n01*source[0]+n11*source[1]+n21*source[2];
    output[2]=n02*source[0]+n12*source[1]+n22*source[2];
}

/**
 * Maps a source material, or its glTF default, to a deduplicated model entry.
 */
int32_t AddMaterial(cgltf_data* data, cgltf_material* source, ModelResource& model, uint32_t* textureMap, String& error) {
    ModelMaterial material{};
    material.baseColorFactor[0]=1.0f; material.baseColorFactor[1]=1.0f; material.baseColorFactor[2]=1.0f; material.baseColorFactor[3]=1.0f;
    material.metallicFactor=1.0f; material.roughnessFactor=1.0f; material.baseColorTextureIndex=-1;
    if (source) {
        const cgltf_size sourceIndex = cgltf_material_index(data, source);
        if (sourceIndex >= data->materials_count) return -1;
    }
    if (source && source->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr=source->pbr_metallic_roughness;
        for (uint32_t i=0;i<4;++i) material.baseColorFactor[i]=pbr.base_color_factor[i];
        material.metallicFactor=pbr.metallic_factor;
        material.roughnessFactor=pbr.roughness_factor;
        if (pbr.base_color_texture.texture) {
            const cgltf_size textureIndex=static_cast<cgltf_size>(pbr.base_color_texture.texture-data->textures);
            if (textureIndex>=data->textures_count) { error.Assign("GLB material has an invalid base-color texture"); return -1; }
            if (textureMap[textureIndex]==missingIndex) {
                cgltf_image* sourceImage=pbr.base_color_texture.texture->image;
                if (!sourceImage||!sourceImage->buffer_view||!sourceImage->mime_type||strcmp(sourceImage->mime_type,"image/png")!=0) {
                    error.Assign("GLB base-color image must be embedded PNG data"); return -1;
                }
                const uint8_t* encoded=cgltf_buffer_view_data(sourceImage->buffer_view);
                if (!encoded||sourceImage->buffer_view->size>maxModelFileBytes) { error.Assign("GLB image buffer is invalid or too large"); return -1; }
                ImageResource* image=DecodeImagePayload(encoded,static_cast<uint32_t>(sourceImage->buffer_view->size),error);
                if (!image) return -1;
                textureMap[textureIndex]=model.textures.Count();
                if (!model.textures.Append(image)) { Release(&image->reference); error.Assign("GLB material texture allocation failed"); return -1; }
            }
            material.baseColorTextureIndex=static_cast<int32_t>(textureMap[textureIndex]);
        }
    }
    for (uint32_t i = 0; i < model.materials.Count(); ++i) {
        const ModelMaterial& existing = model.materials.At(i);
        bool equal = existing.metallicFactor == material.metallicFactor &&
                     existing.roughnessFactor == material.roughnessFactor &&
                     existing.baseColorTextureIndex == material.baseColorTextureIndex;
        for (uint32_t component = 0; component < 4; ++component)
            equal = equal && existing.baseColorFactor[component] == material.baseColorFactor[component];
        if (equal) return static_cast<int32_t>(i);
    }
    if (model.materials.Count() == 1 && model.primitives.Count() == 0) {
        model.materials.At(0) = material;
        return 0;
    }
    if (!model.materials.Append(material)) { error.Assign("GLB material allocation failed"); return -1; }
    return static_cast<int32_t>(model.materials.Count()-1);
}

/**
 * Validates one indexed triangle primitive and appends transformed vertices, indices, and material.
 */
bool AppendGlbPrimitive(cgltf_data* data, cgltf_primitive* primitive, const float matrix[16],
                        ModelResource& model, uint32_t* textureMap, String& error) {
    if (primitive->type != cgltf_primitive_type_triangles || primitive->targets_count || primitive->has_draco_mesh_compression) {
        error.Assign("GLB supports static triangle primitives without morph or Draco data"); return false;
    }
    const cgltf_accessor* position=cgltf_find_accessor(primitive,cgltf_attribute_type_position,0);
    const cgltf_accessor* normal=cgltf_find_accessor(primitive,cgltf_attribute_type_normal,0);
    const cgltf_accessor* uv=cgltf_find_accessor(primitive,cgltf_attribute_type_texcoord,0);
    if (!position||position->type!=cgltf_type_vec3||position->component_type!=cgltf_component_type_r_32f||position->is_sparse||
        !position->buffer_view||position->count==0||position->count>maxOutputVertices-model.vertices.Count()) {
        error.Assign("GLB primitive has invalid or excessive positions"); return false;
    }
    if ((normal&&(normal->type!=cgltf_type_vec3||normal->count!=position->count||normal->is_sparse||!normal->buffer_view)) ||
        (uv&&(uv->type!=cgltf_type_vec2||uv->count!=position->count||uv->is_sparse||!uv->buffer_view))) {
        error.Assign("GLB primitive has incompatible normals or texture coordinates"); return false;
    }
    const uint32_t firstVertex=model.vertices.Count();
    for (cgltf_size i=0;i<position->count;++i) {
        ModelVertex vertex{};
        float value[4]{};
        if (!cgltf_accessor_read_float(position,i,value,3)) { error.Assign("GLB position accessor could not be read"); return false; }
        TransformPosition(matrix,value,vertex.position);
        if (normal) {
            if (!cgltf_accessor_read_float(normal,i,value,3)) { error.Assign("GLB normal accessor could not be read"); return false; }
            TransformNormal(matrix,value,vertex.normal);
        }
        if (uv) {
            if (!cgltf_accessor_read_float(uv,i,value,2)) { error.Assign("GLB texture accessor could not be read"); return false; }
            vertex.uv[0]=value[0]; vertex.uv[1]=value[1];
        }
        if (!IsFinite(vertex.position[0])||!IsFinite(vertex.position[1])||!IsFinite(vertex.position[2])||
            !IsFinite(vertex.normal[0])||!IsFinite(vertex.normal[1])||!IsFinite(vertex.normal[2])||
            !IsFinite(vertex.uv[0])||!IsFinite(vertex.uv[1])||!model.vertices.Append(vertex)) {
            error.Assign("GLB vertex values or allocation are invalid"); return false;
        }
    }
    const uint32_t firstIndex=model.indices.Count();
    const cgltf_size indexCount=primitive->indices?primitive->indices->count:position->count;
    if (indexCount==0||indexCount%3!=0||indexCount>maxOutputIndices-model.indices.Count()) {
        error.Assign("GLB primitive index count is invalid or excessive"); return false;
    }
    for (cgltf_size i=0;i<indexCount;++i) {
        const cgltf_size index=primitive->indices?cgltf_accessor_read_index(primitive->indices,i):i;
        if (index>=position->count||!model.indices.Append(firstVertex+static_cast<uint32_t>(index))) {
            error.Assign("GLB primitive contains an invalid index or could not allocate indices"); return false;
        }
    }
    const int32_t materialIndex=AddMaterial(data,primitive->material,model,textureMap,error);
    if (materialIndex<0) return false;
    ModelPrimitive group={firstIndex,static_cast<uint32_t>(indexCount),materialIndex};
    if (!model.primitives.Append(group)) { error.Assign("GLB primitive allocation failed"); return false; }
    return true;
}

/**
 * Appends every primitive in a mesh using its node's world transform.
 */
bool AppendGlbMesh(cgltf_data* data, cgltf_mesh* mesh, const float matrix[16],
                   ModelResource& model, uint32_t* textureMap, String& error) {
    for (cgltf_size i=0;i<mesh->primitives_count;++i)
        if (!AppendGlbPrimitive(data,&mesh->primitives[i],matrix,model,textureMap,error)) return false;
    return true;
}

/**
 * Traverses a selected scene subtree while enforcing static geometry depth and skin limits.
 */
bool VisitGlbNode(cgltf_data* data, cgltf_node* node, uint32_t depth,
                  ModelResource& model, uint32_t* textureMap, String& error) {
    if (!node||depth>64||node->skin) { error.Assign("GLB node tree is invalid or uses unsupported skinning"); return false; }
    float matrix[16];
    cgltf_node_transform_world(node,matrix);
    if (node->mesh&&!AppendGlbMesh(data,node->mesh,matrix,model,textureMap,error)) return false;
    for (cgltf_size i=0;i<node->children_count;++i)
        if (!VisitGlbNode(data,node->children[i],depth+1,model,textureMap,error)) return false;
    return true;
}

/**
 * Detects cycles and rejects any node whose parent depth exceeds the transform traversal limit.
 */
bool ValidateParentGraph(cgltf_data* data, String& error) {
    if (data->nodes_count > maxSourceValues) {
        error.Assign("GLB contains too many nodes");
        return false;
    }
    Array<uint8_t> state;
    Array<uint32_t> chain;
    Array<uint32_t> depths;
    const uint32_t nodeCount = static_cast<uint32_t>(data->nodes_count);
    if (!state.Reserve(nodeCount) || !depths.Reserve(nodeCount) || !chain.Reserve(nodeCount)) {
        error.Assign("GLB parent graph allocation failed"); return false;
    }
    for (uint32_t i = 0; i < nodeCount; ++i) {
        if (!state.Append(0) || !depths.Append(0)) { error.Assign("GLB parent graph allocation failed"); return false; }
    }
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node* node = &data->nodes[i];
        chain.Clear();
        bool hasKnownParentDepth = false;
        uint32_t parentDepth = 0;
        while (node) {
            const ptrdiff_t index = node - data->nodes;
            if (index < 0 || static_cast<cgltf_size>(index) >= data->nodes_count) { error.Assign("GLB node parent reference is invalid"); return false; }
            const uint8_t mark = state.At(static_cast<uint32_t>(index));
            if (mark == 1) { error.Assign("GLB node parent graph contains a cycle"); return false; }
            if (mark == 2) {
                hasKnownParentDepth = true;
                parentDepth = depths.At(static_cast<uint32_t>(index));
                break;
            }
            state.At(static_cast<uint32_t>(index)) = 1;
            if (!chain.Append(static_cast<uint32_t>(index))) { error.Assign("GLB parent graph allocation failed"); return false; }
            node = node->parent;
        }
        for (uint32_t j = chain.Count(); j > 0; --j) {
            const uint32_t index = chain.At(j - 1);
            const uint32_t depth = hasKnownParentDepth ? parentDepth + 1 : 0;
            if (depth > 64) { error.Assign("GLB node hierarchy exceeds 64 levels"); return false; }
            depths.At(index) = depth;
            state.At(index) = 2;
            parentDepth = depth;
            hasKnownParentDepth = true;
        }
    }
    return true;
}

/**
 * Parses, validates, and converts one embedded-buffer GLB document.
 */
bool LoadGlb(const uint8_t* bytes, uint32_t size, ModelResource& model, String& error) {
    cgltf_options options{};
    options.type=cgltf_file_type_glb;
    cgltf_data* data=nullptr;
    if (cgltf_parse(&options,bytes,size,&data)!=cgltf_result_success||!data) { error.Assign("GLB 2.0 parsing failed"); return false; }
    bool success=false;
    uint32_t* textureMap=nullptr;
    if (data->file_type!=cgltf_file_type_glb||!data->asset.version||strcmp(data->asset.version,"2.0")!=0||
        data->buffers_count!=1||data->buffers[0].uri||data->skins_count||!ValidateParentGraph(data,error)||
        cgltf_load_buffers(&options,data,nullptr)!=cgltf_result_success||cgltf_validate(data)!=cgltf_result_success) {
        if (error.Empty()) error.Assign("GLB must be valid version 2.0 with one embedded static buffer");
        goto finish;
    }
    if (data->textures_count>maxSourceValues||data->nodes_count>maxSourceValues||data->meshes_count>maxSourceValues) {
        error.Assign("GLB contains too many textures, nodes, or meshes");
        goto finish;
    }
    if (data->textures_count) {
        textureMap=static_cast<uint32_t*>(Allocate(sizeof(uint32_t)*data->textures_count));
        if (!textureMap) { error.Assign("GLB texture map allocation failed"); goto finish; }
        for (cgltf_size i=0;i<data->textures_count;++i) textureMap[i]=missingIndex;
    }
    if (data->scene) {
        for (cgltf_size i=0;i<data->scene->nodes_count;++i)
            if (!VisitGlbNode(data,data->scene->nodes[i],0,model,textureMap,error)) goto finish;
    } else {
        bool foundRoot=false;
        for (cgltf_size i=0;i<data->nodes_count;++i) {
            if (!data->nodes[i].parent) {
                foundRoot=true;
                if (!VisitGlbNode(data,&data->nodes[i],0,model,textureMap,error)) goto finish;
            }
        }
        if (!foundRoot) {
            for (cgltf_size i=0;i<data->meshes_count;++i) {
                const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                if (!AppendGlbMesh(data,&data->meshes[i],identity,model,textureMap,error)) goto finish;
            }
        }
    }
    if (!model.indices.Count()) { error.Assign("GLB contains no static triangles"); goto finish; }
    success=true;
finish:
    Deallocate(textureMap);
    cgltf_free(data);
    return success;
}
}

#if defined(GKCORE_TESTING)
/**
 * Builds a temporary parent graph to test traversal limits independent of glTF scene-root validation.
 */
bool ValidateGlbParentsForTesting(const uint32_t* parentIndices, uint32_t nodeCount,
                                  uint32_t selectedSceneNode, String& error) {
    if (!parentIndices || !nodeCount || nodeCount > maxSourceValues || selectedSceneNode >= nodeCount) {
        error.Assign("test parent graph arguments are invalid");
        return false;
    }
    cgltf_node* nodes = static_cast<cgltf_node*>(Allocate(sizeof(cgltf_node) * nodeCount));
    if (!nodes) {
        error.Assign("test parent graph allocation failed");
        return false;
    }
    memset(nodes, 0, sizeof(cgltf_node) * nodeCount);
    for (uint32_t i = 0; i < nodeCount; ++i) {
        const uint32_t parent = parentIndices[i];
        if (parent != missingIndex) {
            if (parent >= nodeCount) {
                Deallocate(nodes);
                error.Assign("test parent index is invalid");
                return false;
            }
            nodes[i].parent = &nodes[parent];
        }
    }
    cgltf_node* sceneNode = &nodes[selectedSceneNode];
    cgltf_scene scene{};
    scene.nodes = &sceneNode;
    scene.nodes_count = 1;
    cgltf_data data{};
    data.nodes = nodes;
    data.nodes_count = nodeCount;
    data.scene = &scene;
    const bool result = ValidateParentGraph(&data, error);
    Deallocate(nodes);
    return result;
}
#endif

/**
 * Internal entry point called by the format-neutral model loader.
 */
bool LoadGlbPayload(const uint8_t* bytes, uint32_t size, ModelResource& model, String& error) {
    return LoadGlb(bytes, size, model, error);
}
}
