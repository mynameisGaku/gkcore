#include "FbxGeometry.h"
#include "FbxMaterial.h"
#include "../foundation/Memory.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/**
 * Static FBX node geometry conversion into gkcore model payloads.
 */
namespace gk::detail {
/**
 * Bounds and transforms private to the FBX geometry conversion routine.
 */
namespace {
const uint32_t maxOutputVertices = 4000000u;
const uint32_t maxOutputIndices = 6000000u;
const uint32_t missingIndex = 0xffffffffu;

/**
 * Rejects non-finite scalar values before they enter a model payload.
 */
bool IsFinite(ufbx_real value) { return isfinite(static_cast<double>(value)) != 0; }

/**
 * Returns the determinant of the linear portion of an affine transform.
 */
ufbx_real LinearDeterminant(const ufbx_matrix& matrix) {
    const ufbx_vec3& x=matrix.cols[0];
    const ufbx_vec3& y=matrix.cols[1];
    const ufbx_vec3& z=matrix.cols[2];
    return x.x*(y.y*z.z-y.z*z.y)-y.x*(x.y*z.z-x.z*z.y)+z.x*(x.y*y.z-x.z*y.y);
}

/**
 * Copies one finite transformed position into the model vertex format.
 */
bool TransformPosition(const ufbx_matrix& matrix, ufbx_vec3 source, float output[3]) {
    const ufbx_vec3 transformed=ufbx_transform_position(&matrix,source);
    if (!IsFinite(transformed.x)||!IsFinite(transformed.y)||!IsFinite(transformed.z)) return false;
    output[0]=static_cast<float>(transformed.x);
    output[1]=static_cast<float>(transformed.y);
    output[2]=static_cast<float>(transformed.z);
    return isfinite(output[0])&&isfinite(output[1])&&isfinite(output[2]);
}

/**
 * Transforms and normalizes one normal using the inverse-transpose matrix.
 */
bool TransformNormal(const ufbx_matrix& normalMatrix, ufbx_vec3 source, float output[3]) {
    const ufbx_vec3 transformed=ufbx_transform_direction(&normalMatrix,source);
    const double x=static_cast<double>(transformed.x);
    const double y=static_cast<double>(transformed.y);
    const double z=static_cast<double>(transformed.z);
    const double lengthSquared=x*x+y*y+z*z;
    if (!isfinite(lengthSquared)) return false;
    if (lengthSquared<=1.0e-30) {
        output[0]=output[1]=output[2]=0.0f;
        return true;
    }
    const double inverseLength=1.0/sqrt(lengthSquared);
    output[0]=static_cast<float>(x*inverseLength);
    output[1]=static_cast<float>(y*inverseLength);
    output[2]=static_cast<float>(z*inverseLength);
    return isfinite(output[0])&&isfinite(output[1])&&isfinite(output[2]);
}

/**
 * Reads a mesh-indexed vector attribute after validating both index and value bounds.
 */
bool ReadVec3(const ufbx_vertex_vec3& attribute, uint32_t meshIndex, ufbx_vec3& output) {
    if (!attribute.exists||!attribute.indices.data||!attribute.values.data||meshIndex>=attribute.indices.count) return false;
    const uint32_t valueIndex=attribute.indices.data[meshIndex];
    if (valueIndex>=attribute.values.count) return false;
    output=attribute.values.data[valueIndex];
    return IsFinite(output.x)&&IsFinite(output.y)&&IsFinite(output.z);
}

/**
 * Reads a mesh-indexed UV after validating both index and value bounds.
 */
bool ReadVec2(const ufbx_vertex_vec2& attribute, uint32_t meshIndex, ufbx_vec2& output) {
    if (!attribute.exists) { output=ufbx_zero_vec2; return true; }
    if (!attribute.indices.data||!attribute.values.data||meshIndex>=attribute.indices.count) return false;
    const uint32_t valueIndex=attribute.indices.data[meshIndex];
    if (valueIndex>=attribute.values.count) return false;
    output=attribute.values.data[valueIndex];
    return IsFinite(output.x)&&IsFinite(output.y);
}

/**
 * Selects the node-instance material associated with one source face.
 */
bool ResolveFaceMaterial(const ufbx_node* node, const ufbx_mesh* mesh, uint32_t faceIndex,
                         const char* utf8ModelPath, ModelResource& model,
                         FbxMaterialContext& materialContext, int32_t& outputIndex,
                         String& error) {
    const ufbx_material* source=nullptr;
    if (mesh->face_material.data) {
        if (faceIndex>=mesh->face_material.count) {
            error.Assign("FBX face material index array is truncated");
            return false;
        }
        const uint32_t materialIndex=mesh->face_material.data[faceIndex];
        if (materialIndex!=missingIndex) {
            if (node->materials.count) {
                if (!node->materials.data||materialIndex>=node->materials.count) {
                    error.Assign("FBX node material mapping is invalid");
                    return false;
                }
                source=node->materials.data[materialIndex];
            } else if (mesh->materials.count) {
                if (!mesh->materials.data||materialIndex>=mesh->materials.count) {
                    error.Assign("FBX mesh material mapping is invalid");
                    return false;
                }
                source=mesh->materials.data[materialIndex];
            } else {
                error.Assign("FBX face refers to a missing material mapping");
                return false;
            }
        }
    }
    if (source&&!ValidateFbxMaterialUvSet(source,mesh,error)) return false;
    return LoadFbxMaterial(source,utf8ModelPath,model,materialContext,outputIndex,error);
}

/**
 * Adds an ordered material run, extending the previous run when it is contiguous.
 */
bool AppendMaterialRun(ModelResource& model, uint32_t firstIndex, uint32_t indexCount,
                       int32_t materialIndex, String& error) {
    if (!indexCount) return true;
    if (model.primitives.Count()) {
        ModelPrimitive& previous=model.primitives.At(model.primitives.Count()-1);
        if (previous.materialIndex==materialIndex&&previous.firstIndex+previous.indexCount==firstIndex) {
            if (indexCount>0xffffffffu-previous.indexCount) {
                error.Assign("FBX material run exceeds the supported index range");
                return false;
            }
            previous.indexCount+=indexCount;
            return true;
        }
    }
    const ModelPrimitive primitive={firstIndex,indexCount,materialIndex};
    if (!model.primitives.Append(primitive)) {
        error.Assign("FBX primitive allocation failed");
        return false;
    }
    return true;
}

/**
 * Tests a transformed triangle for zero area without unstable single-precision math.
 */
bool IsDegenerate(const ModelVertex& a, const ModelVertex& b, const ModelVertex& c) {
    const double abx=static_cast<double>(b.position[0])-a.position[0];
    const double aby=static_cast<double>(b.position[1])-a.position[1];
    const double abz=static_cast<double>(b.position[2])-a.position[2];
    const double acx=static_cast<double>(c.position[0])-a.position[0];
    const double acy=static_cast<double>(c.position[1])-a.position[1];
    const double acz=static_cast<double>(c.position[2])-a.position[2];
    const double x=aby*acz-abz*acy;
    const double y=abz*acx-abx*acz;
    const double z=abx*acy-aby*acx;
    return !isfinite(x*x+y*y+z*z)||(x*x+y*y+z*z)<=1.0e-24;
}
}

/**
 * Appends one node's mesh after applying its geometry-to-world transform.
 */
bool AppendFbxNodeGeometry(const ufbx_node* node, const char* utf8ModelPath,
                           ModelResource& model, FbxMaterialContext& materialContext,
                           uint32_t* degenerateFaceCount, String& error) {
    if (!node||!node->mesh||!utf8ModelPath) {
        error.Assign("FBX geometry node or model path is invalid");
        return false;
    }
    const ufbx_mesh* mesh=node->mesh;
    if (!mesh->vertex_position.exists||!mesh->vertex_normal.exists||!mesh->num_indices||
        model.vertices.Count()>maxOutputVertices||mesh->num_indices>maxOutputVertices-model.vertices.Count()||
        mesh->num_indices>0xffffffffu||mesh->num_faces>0xffffffffu||mesh->faces.count!=mesh->num_faces||
        model.indices.Count()>maxOutputIndices||
        !mesh->faces.data||mesh->vertex_position.indices.count<mesh->num_indices||
        mesh->vertex_normal.indices.count<mesh->num_indices) {
        error.Assign("FBX mesh has missing or excessive static position/normal data");
        return false;
    }
    if (mesh->skin_deformers.count||mesh->blend_deformers.count||mesh->cache_deformers.count) {
        error.Assign("FBX mesh deformation is unsupported for static model loading");
        return false;
    }
    const ufbx_real determinant=LinearDeterminant(node->geometry_to_world);
    if (!IsFinite(determinant)||fabs(static_cast<double>(determinant))<1.0e-20) {
        error.Assign("FBX mesh transform is singular or non-finite");
        return false;
    }
    const ufbx_matrix normalMatrix=ufbx_matrix_for_normals(&node->geometry_to_world);
    const uint32_t firstVertex=model.vertices.Count();
    for (uint32_t index=0;index<static_cast<uint32_t>(mesh->num_indices);++index) {
        ufbx_vec3 position{},normal{};
        ufbx_vec2 uv{};
        if (!ReadVec3(mesh->vertex_position,index,position)||!ReadVec3(mesh->vertex_normal,index,normal)||
            !ReadVec2(mesh->vertex_uv,index,uv)) {
            error.Assign("FBX mesh has an invalid vertex attribute reference");
            return false;
        }
        ModelVertex vertex{};
        if (!TransformPosition(node->geometry_to_world,position,vertex.position)||
            !TransformNormal(normalMatrix,normal,vertex.normal)) {
            error.Assign("FBX transformed vertex or normal is non-finite or degenerate");
            return false;
        }
        vertex.uv[0]=static_cast<float>(uv.x);
        vertex.uv[1]=static_cast<float>(1.0-uv.y);
        if (!isfinite(vertex.uv[0])||!isfinite(vertex.uv[1])||!model.vertices.Append(vertex)) {
            error.Assign("FBX vertex allocation or UV validation failed");
            return false;
        }
    }

    uint32_t maxTriangleIndices=0;
    for (uint32_t faceIndex=0;faceIndex<mesh->num_faces;++faceIndex) {
        const ufbx_face& face=mesh->faces.data[faceIndex];
        if (mesh->face_hole.data&&faceIndex>=mesh->face_hole.count) {
            error.Assign("FBX face-hole array is truncated");
            return false;
        }
        if (face.index_begin>mesh->num_indices||face.num_indices>mesh->num_indices-face.index_begin) {
            error.Assign("FBX face index range is invalid");
            return false;
        }
        if (face.num_indices<3||(mesh->face_hole.data&&mesh->face_hole.data[faceIndex])) continue;
        const uint64_t needed=static_cast<uint64_t>(face.num_indices-2)*3u;
        if (needed>maxOutputIndices) {
            error.Assign("FBX polygon exceeds the supported triangulation limit");
            return false;
        }
        if (needed>maxTriangleIndices) maxTriangleIndices=static_cast<uint32_t>(needed);
    }
    uint32_t* triangles=nullptr;
    if (maxTriangleIndices) {
        triangles=static_cast<uint32_t*>(Allocate(sizeof(uint32_t)*maxTriangleIndices));
        if (!triangles) { error.Assign("FBX triangulation allocation failed"); return false; }
    }

    bool success=false;
    uint32_t plannedIndexCount=model.indices.Count();
    for (uint32_t faceIndex=0;faceIndex<mesh->num_faces;++faceIndex) {
        const ufbx_face& face=mesh->faces.data[faceIndex];
        if (mesh->face_hole.data&&mesh->face_hole.data[faceIndex]) continue;
        if (face.num_indices<3) {
            if (degenerateFaceCount) ++*degenerateFaceCount;
            continue;
        }
        const uint32_t triangleCount=ufbx_triangulate_face(triangles,maxTriangleIndices,mesh,face);
        if (!triangleCount||triangleCount>(maxOutputIndices-plannedIndexCount)/3u) {
            error.Assign("FBX face triangulation failed or exceeds the supported index limit");
            goto finish;
        }
        plannedIndexCount+=triangleCount*3u;
        int32_t materialIndex=0;
        if (!ResolveFaceMaterial(node,mesh,faceIndex,utf8ModelPath,model,materialContext,materialIndex,error)) goto finish;

        const uint32_t runStart=model.indices.Count();
        uint32_t faceAdded=0;
        bool faceDegenerate=false;
        for (uint32_t triangle=0;triangle<triangleCount;++triangle) {
            uint32_t source[3]={triangles[triangle*3],triangles[triangle*3+1],triangles[triangle*3+2]};
            for (uint32_t corner=0;corner<3;++corner) {
                if (source[corner]<face.index_begin||source[corner]>=face.index_begin+face.num_indices) {
                    error.Assign("FBX triangulation returned an out-of-face index");
                    goto finish;
                }
            }
            if (determinant<0.0) { const uint32_t swap=source[1]; source[1]=source[2]; source[2]=swap; }
            const ModelVertex& a=model.vertices.At(firstVertex+source[0]);
            const ModelVertex& b=model.vertices.At(firstVertex+source[1]);
            const ModelVertex& c=model.vertices.At(firstVertex+source[2]);
            if (IsDegenerate(a,b,c)) { faceDegenerate=true; continue; }
            for (uint32_t corner=0;corner<3;++corner) {
                const ModelVertex& vertex=model.vertices.At(firstVertex+source[corner]);
                const double nx=vertex.normal[0],ny=vertex.normal[1],nz=vertex.normal[2];
                if (nx*nx+ny*ny+nz*nz<=1.0e-20) {
                    error.Assign("FBX non-degenerate triangle has a zero-length normal");
                    goto finish;
                }
            }
            for (uint32_t corner=0;corner<3;++corner) {
                if (!model.indices.Append(firstVertex+source[corner])) {
                    error.Assign("FBX index allocation failed");
                    goto finish;
                }
                ++faceAdded;
            }
        }
        if (faceDegenerate&&degenerateFaceCount) ++*degenerateFaceCount;
        if (!AppendMaterialRun(model,runStart,faceAdded,materialIndex,error)) goto finish;
    }
    success=true;
finish:
    Deallocate(triangles);
    return success;
}
} // namespace gk::detail
