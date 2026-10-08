// SPDX-License-Identifier: NOASSERTION
#include "GlbAnimation.h"
#include "GlbMorphGeometry.h"
#include "ModelPose.h"
#include "../../foundation/Memory.h"
#include "../../foundation/Array.h"
#include "../Model.h"
#include "../../../third_party/cgltf/cgltf.h"
#include <float.h>
#include <math.h>
#include <string.h>

namespace gk::model
{
namespace
{
const uint32_t kInvalid = 0xffffffffu;
const uint32_t kMaxNodes = 2000000u;

struct FChannel
{
    uint32_t node;
    uint32_t sampler;
    uint32_t path;
};

struct FMorphRange
{
    uint32_t node;
    uint32_t target;
    uint32_t poseIndex;
    const char* meshName;
};

struct FPrimitiveRef
{
    uint32_t firstSource;
    uint32_t vertexCount;
    uint32_t firstModelVertex;
    uint32_t modelVertexCount;
    uint32_t firstModelIndex;
    uint32_t modelIndexCount;
    uint32_t node;
    bool generateMorphNormals;
    bool generateMorphTangents;
    cgltf_primitive* primitive;
};

class AGlbAnimationSource final : public AModelAnimationSource
{
  public:
    cgltf_data* data = nullptr;
    uint8_t* bytes = nullptr;
    uint32_t byteCount = 0;
    animation::FModelSkeleton skeleton;
    gk::Array<uint32_t> nodeToBone;
    gk::Array<uint32_t> boneToNode;
    gk::Array<FChannel> channels;
    gk::Array<uint32_t> clipFirstChannel;
    gk::Array<uint32_t> clipChannelCount;
    gk::Array<FMorphRange> morphRanges;
    gk::Array<uint32_t> morphNameOffsets;
    gk::Array<char> morphNameBytes;
    gk::Array<FPrimitiveRef> primitives;
    gk::Array<detail::ModelVertex> restVertices;
    bool geometryMapped = false;

    ~AGlbAnimationSource() override
    {
        if (data)
            cgltf_free(data);
        Deallocate(bytes);
    }

    EModelAnimationFormat Format() const override
    {
        return EModelAnimationFormat::Glb;
    }
    const animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton;
    }
    const char* BoneName(uint32_t bone) const override
    {
        if (bone >= boneToNode.Count())
            return nullptr;
        return data->nodes[boneToNode.At(bone)].name;
    }
    const char* MorphName(uint32_t morph) const override
    {
        if (morph >= morphNameOffsets.Count())
            return nullptr;
        return morphNameOffsets.At(morph) == kInvalid ? nullptr : morphNameBytes.Data() + morphNameOffsets.At(morph);
    }
    uint32_t ClipCount() const override
    {
        return static_cast<uint32_t>(data->animations_count);
    }
    const char* ClipName(uint32_t clip) const override
    {
        return clip < data->animations_count ? data->animations[clip].name : nullptr;
    }
    double ClipDuration(uint32_t clip) const override;
    bool Sample(uint32_t clip, double seconds, animation::FModelPose& output, String& error) const override;
    bool Deform(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const override;
};

bool IsFinite(float value)
{
    return isfinite(value) != 0;
}

/**
 * morph blendで得た方向を安全な単位ベクトルへ変換する。
 */
bool NormalizeMorphDirection(float value[3])
{
    const double length = sqrt(static_cast<double>(value[0]) * value[0] + static_cast<double>(value[1]) * value[1] + static_cast<double>(value[2]) * value[2]);
    if (!(length > 1.0e-20) || !isfinite(length))
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
        value[axis] = static_cast<float>(static_cast<double>(value[axis]) / length);
    return IsFinite(value[0]) && IsFinite(value[1]) && IsFinite(value[2]);
}

double LinearDeterminant(const float matrix[16])
{
    const double a = matrix[0], b = matrix[4], c = matrix[8];
    const double d = matrix[1], e = matrix[5], f = matrix[9];
    const double g = matrix[2], h = matrix[6], i = matrix[10];
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

bool BuildPrimitiveRefs(AGlbAnimationSource& source, const detail::ModelResource& geometry, String& error);

bool FindNodeIndex(const cgltf_data* data, const cgltf_node* node, uint32_t& output)
{
    if (!data || !node || !data->nodes || node < data->nodes || node >= data->nodes + data->nodes_count)
        return false;
    output = static_cast<uint32_t>(node - data->nodes);
    return true;
}

bool ContainsSkin(const cgltf_data* data, const cgltf_skin* skin)
{
    if (!data || !skin)
        return false;
    for (cgltf_size i = 0; i < data->skins_count; ++i)
        if (&data->skins[i] == skin)
            return true;
    return false;
}

bool TransformNormal(const float matrix[16], const float source[3], float output[3])
{
    const double a = matrix[0], b = matrix[4], c = matrix[8], d = matrix[1], e = matrix[5], f = matrix[9], g = matrix[2], h = matrix[6], i = matrix[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!isfinite(determinant) || fabs(determinant) < 1.0e-20)
        return false;
    const double value[3] = { ((e * i - f * h) * source[0] + (f * g - d * i) * source[1] + (d * h - e * g) * source[2]) / determinant, ((c * h - b * i) * source[0] + (a * i - c * g) * source[1] + (b * g - a * h) * source[2]) / determinant, ((b * f - c * e) * source[0] + (c * d - a * f) * source[1] + (a * e - b * d) * source[2]) / determinant };
    const double length = sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (!(length > 1.0e-20) || !isfinite(length))
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
        output[axis] = static_cast<float>(value[axis] / length);
    return IsFinite(output[0]) && IsFinite(output[1]) && IsFinite(output[2]);
}

bool RestoreLocalNormal(const float matrix[16], const float source[3], float output[3])
{
    const double value[3] = { matrix[0] * source[0] + matrix[1] * source[1] + matrix[2] * source[2], matrix[4] * source[0] + matrix[5] * source[1] + matrix[6] * source[2], matrix[8] * source[0] + matrix[9] * source[1] + matrix[10] * source[2] };
    const double length = sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (!(length > 1.0e-20) || !isfinite(length))
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
        output[axis] = static_cast<float>(value[axis] / length);
    return IsFinite(output[0]) && IsFinite(output[1]) && IsFinite(output[2]);
}

bool RestoreLocalTangent(const float matrix[16], const float source[3], float output[3])
{
    const double a = matrix[0], b = matrix[4], c = matrix[8], d = matrix[1], e = matrix[5], f = matrix[9], g = matrix[2], h = matrix[6], i = matrix[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!isfinite(determinant) || fabs(determinant) < 1.0e-20)
        return false;
    const double value[3] = { ((e * i - f * h) * source[0] + (c * h - b * i) * source[1] + (b * f - c * e) * source[2]) / determinant, ((f * g - d * i) * source[0] + (a * i - c * g) * source[1] + (c * d - a * f) * source[2]) / determinant, ((d * h - e * g) * source[0] + (b * g - a * h) * source[1] + (a * e - b * f) * source[2]) / determinant };
    const double length = sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (!(length > 1.0e-20) || !isfinite(length))
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
        output[axis] = static_cast<float>(value[axis] / length);
    return IsFinite(output[0]) && IsFinite(output[1]) && IsFinite(output[2]);
}

bool AppendMorphName(AGlbAnimationSource& source, uint32_t nodeIndex, uint32_t targetIndex, uint32_t poseIndex)
{
    const cgltf_node& node = source.data->nodes[nodeIndex];
    const char* target = node.mesh && targetIndex < node.mesh->target_names_count ? node.mesh->target_names[targetIndex] : nullptr;
    char number[16]{};
    uint32_t numberLength = 0;
    if (!target)
    {
        uint32_t value = targetIndex;
        do
        {
            number[numberLength++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value && numberLength < sizeof(number));
    }
    if (!node.name)
    {
        if (!source.morphNameOffsets.Append(kInvalid))
            return false;
        FMorphRange range{};
        range.node = nodeIndex;
        range.target = targetIndex;
        range.poseIndex = poseIndex;
        range.meshName = node.mesh ? node.mesh->name : nullptr;
        return source.morphRanges.Append(range);
    }
    const uint32_t offset = source.morphNameBytes.Count();
    if (!source.morphNameOffsets.Append(offset) || !source.morphNameBytes.AppendRange(node.name, static_cast<uint32_t>(strlen(node.name))) || !source.morphNameBytes.Append('/'))
        return false;
    if (target)
    {
        if (!source.morphNameBytes.AppendRange(target, static_cast<uint32_t>(strlen(target))))
            return false;
    }
    else
    {
        while (numberLength)
        {
            --numberLength;
            if (!source.morphNameBytes.Append(number[numberLength]))
                return false;
        }
    }
    if (!source.morphNameBytes.Append('\0'))
        return false;
    FMorphRange range{};
    range.node = nodeIndex;
    range.target = targetIndex;
    range.poseIndex = poseIndex;
    range.meshName = node.mesh ? node.mesh->name : nullptr;
    return source.morphRanges.Append(range);
}

bool AddNode(AGlbAnimationSource& source, uint32_t index, uint32_t depth, String& error)
{
    if (depth > 64 || index >= source.data->nodes_count)
    {
        error.Assign("GLB animation hierarchy is invalid or too deep");
        return false;
    }
    const cgltf_node& node = source.data->nodes[index];
    const uint32_t parentIndex = node.parent ? static_cast<uint32_t>(node.parent - source.data->nodes) : kInvalid;
    const int32_t parent = parentIndex != kInvalid ? static_cast<int32_t>(source.nodeToBone.At(parentIndex)) : -1;
    const uint32_t boneIndex = source.boneToNode.Count();
    if (!source.boneToNode.Append(index) || !source.skeleton.parents.Append(parent))
    {
        error.Assign("GLB animation skeleton allocation failed");
        return false;
    }
    source.nodeToBone.At(index) = boneIndex;
    animation::FModelBoneTransform transform{};
    if (node.has_matrix)
    {
        const float* m = node.matrix;
        const double sx = sqrt(static_cast<double>(m[0]) * m[0] + static_cast<double>(m[1]) * m[1] + static_cast<double>(m[2]) * m[2]);
        const double sy = sqrt(static_cast<double>(m[4]) * m[4] + static_cast<double>(m[5]) * m[5] + static_cast<double>(m[6]) * m[6]);
        const double sz = sqrt(static_cast<double>(m[8]) * m[8] + static_cast<double>(m[9]) * m[9] + static_cast<double>(m[10]) * m[10]);
        if (!(sx > 0.0) || !(sy > 0.0) || !(sz > 0.0) || !isfinite(sx) || !isfinite(sy) || !isfinite(sz))
        {
            error.Assign("GLB animated node matrix cannot be decomposed");
            return false;
        }
        const double d01 = (m[0] * m[4] + m[1] * m[5] + m[2] * m[6]) / (sx * sy);
        const double d02 = (m[0] * m[8] + m[1] * m[9] + m[2] * m[10]) / (sx * sz);
        const double d12 = (m[4] * m[8] + m[5] * m[9] + m[6] * m[10]) / (sy * sz);
        if (fabs(d01) > 1.0e-5 || fabs(d02) > 1.0e-5 || fabs(d12) > 1.0e-5)
        {
            error.Assign("GLB animated node matrix contains shear");
            return false;
        }
        const double determinant = (m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]));
        const double signedSx = determinant < 0.0 ? -sx : sx;
        const double r00 = m[0] / signedSx, r10 = m[1] / signedSx, r20 = m[2] / signedSx;
        const double r01 = m[4] / sy, r11 = m[5] / sy, r21 = m[6] / sy;
        const double r02 = m[8] / sz, r12 = m[9] / sz, r22 = m[10] / sz;
        transform.position[0] = m[12];
        transform.position[1] = m[13];
        transform.position[2] = m[14];
        transform.scale[0] = static_cast<float>(signedSx);
        transform.scale[1] = static_cast<float>(sy);
        transform.scale[2] = static_cast<float>(sz);
        const double trace = r00 + r11 + r22;
        if (trace > 0.0)
        {
            const double s = sqrt(trace + 1.0) * 2.0;
            transform.rotation[3] = static_cast<float>(0.25 * s);
            transform.rotation[0] = static_cast<float>((r21 - r12) / s);
            transform.rotation[1] = static_cast<float>((r02 - r20) / s);
            transform.rotation[2] = static_cast<float>((r10 - r01) / s);
        }
        else if (r00 > r11 && r00 > r22)
        {
            const double s = sqrt(1.0 + r00 - r11 - r22) * 2.0;
            transform.rotation[3] = static_cast<float>((r21 - r12) / s);
            transform.rotation[0] = static_cast<float>(0.25 * s);
            transform.rotation[1] = static_cast<float>((r01 + r10) / s);
            transform.rotation[2] = static_cast<float>((r02 + r20) / s);
        }
        else if (r11 > r22)
        {
            const double s = sqrt(1.0 + r11 - r00 - r22) * 2.0;
            transform.rotation[3] = static_cast<float>((r02 - r20) / s);
            transform.rotation[0] = static_cast<float>((r01 + r10) / s);
            transform.rotation[1] = static_cast<float>(0.25 * s);
            transform.rotation[2] = static_cast<float>((r12 + r21) / s);
        }
        else
        {
            const double s = sqrt(1.0 + r22 - r00 - r11) * 2.0;
            transform.rotation[3] = static_cast<float>((r10 - r01) / s);
            transform.rotation[0] = static_cast<float>((r02 + r20) / s);
            transform.rotation[1] = static_cast<float>((r12 + r21) / s);
            transform.rotation[2] = static_cast<float>(0.25 * s);
        }
    }
    else
    {
        for (uint32_t i = 0; i < 3; ++i)
        {
            transform.position[i] = node.has_translation ? node.translation[i] : 0.0f;
            transform.scale[i] = node.has_scale ? node.scale[i] : 1.0f;
        }
        for (uint32_t i = 0; i < 4; ++i)
            transform.rotation[i] = node.has_rotation ? node.rotation[i] : (i == 3 ? 1.0f : 0.0f);
    }
    for (uint32_t i = 0; i < 3; ++i)
        if (!IsFinite(transform.position[i]) || !IsFinite(transform.scale[i]))
        {
            error.Assign("GLB animation node transform is non-finite");
            return false;
        }
    for (uint32_t i = 0; i < 4; ++i)
        if (!IsFinite(transform.rotation[i]))
        {
            error.Assign("GLB animation node rotation is non-finite");
            return false;
        }
    if (!source.skeleton.restLocalTransforms.Append(transform))
    {
        error.Assign("GLB animation rest pose allocation failed");
        return false;
    }
    if (node.mesh)
    {
        const cgltf_size targetCount = node.mesh->primitives_count ? node.mesh->primitives[0].targets_count : 0;
        for (cgltf_size target = 0; target < targetCount; ++target)
        {
            const uint32_t morphIndex = source.skeleton.restMorphWeights.Count();
            const float weight = node.weights && target < node.weights_count ? node.weights[target] : (target < node.mesh->weights_count ? node.mesh->weights[target] : 0.0f);
            if (!IsFinite(weight) || !source.skeleton.restMorphWeights.Append(weight) || !AppendMorphName(source, index, static_cast<uint32_t>(target), morphIndex))
            {
                error.Assign("GLB morph rest data is invalid or could not be stored");
                return false;
            }
        }
    }
    for (cgltf_size child = 0; child < node.children_count; ++child)
    {
        uint32_t childIndex = 0;
        if (!FindNodeIndex(source.data, node.children[child], childIndex) || !AddNode(source, childIndex, depth + 1, error))
            return false;
    }
    return true;
}

bool BuildSkeleton(AGlbAnimationSource& source, String& error)
{
    const uint32_t count = static_cast<uint32_t>(source.data->nodes_count);
    if (count > kMaxNodes || !source.nodeToBone.Reserve(count) || !source.boneToNode.Reserve(count))
    {
        error.Assign("GLB animation node count is excessive or allocation failed");
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
        if (!source.nodeToBone.Append(kInvalid))
        {
            error.Assign("GLB animation node map allocation failed");
            return false;
        }
    for (uint32_t i = 0; i < count; ++i)
        if (!source.data->nodes[i].parent && !AddNode(source, i, 0, error))
            return false;
    return source.boneToNode.Count() == count;
}

bool AddPrimitiveRefs(AGlbAnimationSource& source, const detail::ModelResource& geometry, uint32_t nodeIndex, cgltf_mesh* mesh, uint32_t& nextSource, uint32_t& nextModelVertex, uint32_t& nextModelIndex, String& error)
{
    if (!mesh)
        return true;
    for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
    {
        cgltf_primitive* primitive = &mesh->primitives[p];
        const cgltf_accessor* position = cgltf_find_accessor(primitive, cgltf_attribute_type_position, 0);
        const cgltf_accessor* normal = cgltf_find_accessor(primitive, cgltf_attribute_type_normal, 0);
        const cgltf_accessor* tangent = normal ? cgltf_find_accessor(primitive, cgltf_attribute_type_tangent, 0) : nullptr;
        const bool hasNormalTexture = primitive->material && primitive->material->normal_texture.texture;
        const uint32_t vertexCount = position ? static_cast<uint32_t>(position->count) : 0;
        const cgltf_size indexCountValue = primitive->indices ? primitive->indices->count : (position ? position->count : 0);
        if (!position || position->count > UINT32_MAX - nextSource || indexCountValue > UINT32_MAX - nextModelIndex || (source.geometryMapped && (nextModelVertex > geometry.vertices.Count() || nextModelIndex > geometry.indices.Count())))
        {
            error.Assign("GLB animation primitive source or output range is invalid");
            return false;
        }
        const uint32_t indexCount = static_cast<uint32_t>(indexCountValue);
        const bool generateMorphNormals = !normal && primitive->targets_count != 0;
        const bool generateMorphTangents = normal && hasNormalTexture && !tangent && primitive->targets_count != 0;
        uint32_t modelVertexCount = 0;
        if (source.geometryMapped && (generateMorphNormals || generateMorphTangents))
        {
            modelVertexCount = indexCount;
            if (modelVertexCount > geometry.vertices.Count() - nextModelVertex)
            {
                error.Assign("GLB morph corner vertices do not match their primitive");
                return false;
            }
        }
        else if (source.geometryMapped)
        {
            const uint32_t sourceEnd = nextSource + vertexCount;
            while (nextModelVertex < geometry.vertices.Count())
            {
                const uint32_t sourceIndex = geometry.vertices.At(nextModelVertex).sourceIndex;
                if (sourceIndex < nextSource || sourceIndex >= sourceEnd)
                    break;
                ++modelVertexCount;
                ++nextModelVertex;
            }
        }
        if (source.geometryMapped && (generateMorphNormals || generateMorphTangents))
            nextModelVertex += modelVertexCount;
        if (source.geometryMapped && (!modelVertexCount || indexCount > geometry.indices.Count() - nextModelIndex || !source.primitives.Append({ nextSource, vertexCount, nextModelVertex - modelVertexCount, modelVertexCount, nextModelIndex, indexCount, nodeIndex, generateMorphNormals, generateMorphTangents, primitive })))
        {
            error.Assign("GLB animation primitive source map is invalid or allocation failed");
            return false;
        }
        nextModelIndex += indexCount;
        const cgltf_accessor* joints = cgltf_find_accessor(primitive, cgltf_attribute_type_joints, 0);
        const cgltf_accessor* weights = cgltf_find_accessor(primitive, cgltf_attribute_type_weights, 0);
        const cgltf_accessor* extraJoints = cgltf_find_accessor(primitive, cgltf_attribute_type_joints, 1);
        const cgltf_accessor* extraWeights = cgltf_find_accessor(primitive, cgltf_attribute_type_weights, 1);
        cgltf_node* node = nodeIndex == kInvalid ? nullptr : &source.data->nodes[nodeIndex];
        const bool skinValid = !node || !node->skin || (ContainsSkin(source.data, node->skin) && node->skin->joints && node->skin->joints_count > 0 && (!node->skin->inverse_bind_matrices || (node->skin->inverse_bind_matrices->type == cgltf_type_mat4 && node->skin->inverse_bind_matrices->component_type == cgltf_component_type_r_32f && node->skin->inverse_bind_matrices->count == node->skin->joints_count && !node->skin->inverse_bind_matrices->is_sparse && node->skin->inverse_bind_matrices->buffer_view)));
        const bool influencesValid = node && node->skin && joints && weights && !extraJoints && !extraWeights && joints->type == cgltf_type_vec4 && joints->count == position->count && !joints->normalized && (joints->component_type == cgltf_component_type_r_8u || joints->component_type == cgltf_component_type_r_16u) && weights->type == cgltf_type_vec4 && weights->count == position->count && (weights->component_type == cgltf_component_type_r_32f || (weights->normalized && (weights->component_type == cgltf_component_type_r_8u || weights->component_type == cgltf_component_type_r_16u)));
        if (!skinValid || (node && node->skin && !influencesValid))
        {
            error.Assign("GLB skinned primitive needs compatible JOINTS_0 and WEIGHTS_0 accessors");
            return false;
        }
        for (cgltf_size target = 0; target < primitive->targets_count; ++target)
        {
            for (cgltf_size attributeIndex = 0; attributeIndex < primitive->targets[target].attributes_count; ++attributeIndex)
            {
                const cgltf_attribute& attribute = primitive->targets[target].attributes[attributeIndex];
                if ((attribute.type == cgltf_attribute_type_position || attribute.type == cgltf_attribute_type_normal || attribute.type == cgltf_attribute_type_tangent) && (!attribute.data || attribute.data->type != cgltf_type_vec3 || attribute.data->component_type != cgltf_component_type_r_32f || attribute.data->count != position->count || attribute.data->is_sparse || !attribute.data->buffer_view))
                {
                    error.Assign("GLB morph attribute accessor is incompatible with its primitive");
                    return false;
                }
            }
        }
        nextSource += static_cast<uint32_t>(position->count);
    }
    return true;
}

bool AddNodePrimitiveRefs(AGlbAnimationSource& source, const detail::ModelResource& geometry, uint32_t nodeIndex, uint32_t depth, uint32_t& nextSource, uint32_t& nextModelVertex, uint32_t& nextModelIndex, String& error)
{
    if (depth > 64 || nodeIndex >= source.data->nodes_count)
        return false;
    cgltf_node& node = source.data->nodes[nodeIndex];
    if (!AddPrimitiveRefs(source, geometry, nodeIndex, node.mesh, nextSource, nextModelVertex, nextModelIndex, error))
        return false;
    for (cgltf_size i = 0; i < node.children_count; ++i)
    {
        uint32_t child = 0;
        if (!FindNodeIndex(source.data, node.children[i], child) || !AddNodePrimitiveRefs(source, geometry, child, depth + 1, nextSource, nextModelVertex, nextModelIndex, error))
            return false;
    }
    return true;
}

bool BuildPrimitiveRefs(AGlbAnimationSource& source, const detail::ModelResource& geometry, String& error)
{
    source.geometryMapped = geometry.vertices.Count() != 0;
    uint32_t nextSource = 0;
    uint32_t nextModelVertex = 0;
    uint32_t nextModelIndex = 0;
    if (source.data->scene)
    {
        for (cgltf_size i = 0; i < source.data->scene->nodes_count; ++i)
        {
            uint32_t root = 0;
            if (!FindNodeIndex(source.data, source.data->scene->nodes[i], root) || !AddNodePrimitiveRefs(source, geometry, root, 0, nextSource, nextModelVertex, nextModelIndex, error))
                return false;
        }
    }
    else
    {
        bool foundRoot = false;
        for (cgltf_size i = 0; i < source.data->nodes_count; ++i)
            if (!source.data->nodes[i].parent)
            {
                foundRoot = true;
                if (!AddNodePrimitiveRefs(source, geometry, static_cast<uint32_t>(i), 0, nextSource, nextModelVertex, nextModelIndex, error))
                    return false;
            }
        if (!foundRoot)
            for (cgltf_size i = 0; i < source.data->meshes_count; ++i)
                if (!AddPrimitiveRefs(source, geometry, kInvalid, &source.data->meshes[i], nextSource, nextModelVertex, nextModelIndex, error))
                    return false;
    }
    if (source.geometryMapped)
    {
        for (uint32_t i = 0; i < geometry.vertices.Count(); ++i)
        {
            if (geometry.vertices.At(i).sourceIndex >= nextSource)
            {
                error.Assign("GLB model vertices do not match animation source indices");
                return false;
            }
        }
        if (nextModelVertex != geometry.vertices.Count() || nextModelIndex != geometry.indices.Count())
        {
            error.Assign("GLB animation primitive ranges do not cover model geometry");
            return false;
        }
    }
    return true;
}

bool ValidateAnimations(AGlbAnimationSource& source, String& error)
{
    for (cgltf_size clip = 0; clip < source.data->animations_count; ++clip)
    {
        const cgltf_animation& animation = source.data->animations[clip];
        if (!source.clipFirstChannel.Append(source.channels.Count()))
            return false;
        for (cgltf_size c = 0; c < animation.channels_count; ++c)
        {
            const cgltf_animation_channel& channel = animation.channels[c];
            uint32_t node = 0;
            if (!channel.sampler || !FindNodeIndex(source.data, channel.target_node, node) || node >= source.nodeToBone.Count() || source.nodeToBone.At(node) == kInvalid || channel.target_path < cgltf_animation_path_type_translation || channel.target_path > cgltf_animation_path_type_weights || (channel.target_node->has_matrix && channel.target_path != cgltf_animation_path_type_weights))
            {
                error.Assign("GLB animation channel has an invalid node or path");
                return false;
            }
            const cgltf_animation_sampler* samplerBegin = animation.samplers;
            const cgltf_animation_sampler* samplerEnd = animation.samplers ? animation.samplers + animation.samplers_count : nullptr;
            if (!samplerBegin || channel.sampler < samplerBegin || channel.sampler >= samplerEnd)
            {
                error.Assign("GLB animation channel sampler reference is invalid");
                return false;
            }
            const cgltf_accessor* input = channel.sampler->input;
            const cgltf_accessor* output = channel.sampler->output;
            if (!input || !output || input->type != cgltf_type_scalar || input->component_type != cgltf_component_type_r_32f || input->count == 0 || input->is_sparse || !input->buffer_view || input->normalized || output->component_type != cgltf_component_type_r_32f || output->count == 0 || output->is_sparse || !output->buffer_view || channel.sampler->interpolation > cgltf_interpolation_type_cubic_spline)
            {
                error.Assign("GLB animation sampler accessors are invalid");
                return false;
            }
            const uint32_t keyStride = channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? 3u : 1u;
            uint32_t morphCount = 1;
            uint32_t components = channel.target_path == cgltf_animation_path_type_rotation ? 4u : (channel.target_path == cgltf_animation_path_type_weights ? 1u : 3u);
            if (channel.target_path == cgltf_animation_path_type_weights)
            {
                morphCount = 0;
                for (uint32_t m = 0; m < source.morphRanges.Count(); ++m)
                    if (source.morphRanges.At(m).node == node)
                        ++morphCount;
            }
            const cgltf_type expectedType = components == 4 ? cgltf_type_vec4 : (components == 3 ? cgltf_type_vec3 : cgltf_type_scalar);
            const uint64_t expectedValues = static_cast<uint64_t>(input->count) * keyStride * morphCount;
            if (!morphCount || output->type != expectedType || expectedValues != output->count)
            {
                error.Assign("GLB animation sampler output count or type does not match its channel");
                return false;
            }
            for (cgltf_size valueIndex = 0; valueIndex < output->count; ++valueIndex)
            {
                float value[4]{};
                if (!cgltf_accessor_read_float(output, valueIndex, value, components))
                {
                    error.Assign("GLB animation output accessor could not be read");
                    return false;
                }
                for (uint32_t component = 0; component < components; ++component)
                    if (!IsFinite(value[component]))
                    {
                        error.Assign("GLB animation output contains a non-finite value");
                        return false;
                    }
            }
            float previousTime = -1.0f;
            for (cgltf_size key = 0; key < input->count; ++key)
            {
                float keyTime = 0.0f;
                if (!cgltf_accessor_read_float(input, key, &keyTime, 1) || !IsFinite(keyTime) || keyTime < 0.0f || (key && !(keyTime > previousTime)))
                {
                    error.Assign("GLB animation input times must be finite and strictly increasing");
                    return false;
                }
                previousTime = keyTime;
            }
            for (cgltf_size previous = 0; previous < c; ++previous)
                if (animation.channels[previous].target_node == channel.target_node && animation.channels[previous].target_path == channel.target_path)
                {
                    error.Assign("GLB animation targets one node path more than once");
                    return false;
                }
            if (!source.channels.Append({ node, static_cast<uint32_t>(channel.sampler - animation.samplers), static_cast<uint32_t>(channel.target_path) }))
            {
                error.Assign("GLB animation channel allocation failed");
                return false;
            }
        }
        if (!source.clipChannelCount.Append(source.channels.Count() - source.clipFirstChannel.At(source.clipFirstChannel.Count() - 1)))
        {
            error.Assign("GLB animation clip allocation failed");
            return false;
        }
    }
    return true;
}

double FindDuration(const cgltf_animation& animation)
{
    double duration = 0.0;
    for (cgltf_size i = 0; i < animation.samplers_count; ++i)
    {
        const cgltf_accessor* input = animation.samplers[i].input;
        if (!input || input->type != cgltf_type_scalar || input->count == 0)
            return -1.0;
        float time = 0.0f;
        if (!cgltf_accessor_read_float(input, input->count - 1, &time, 1) || !IsFinite(time) || time < 0.0f)
            return -1.0;
        if (time > duration)
            duration = time;
    }
    return duration;
}
}

double AGlbAnimationSource::ClipDuration(uint32_t clip) const
{
    return clip < data->animations_count ? FindDuration(data->animations[clip]) : -1.0;
}

bool AGlbAnimationSource::Sample(uint32_t clip, double seconds, animation::FModelPose& output, String& error) const
{
    if (clip >= data->animations_count || !isfinite(seconds))
    {
        error.Assign("GLB animation clip or time is invalid");
        return false;
    }
    animation::FModelPose candidate;
    if (!animation::InitializeModelPose(skeleton, candidate, error))
        return false;
    const cgltf_animation& sourceAnimation = data->animations[clip];
    const double duration = FindDuration(sourceAnimation);
    if (duration < 0.0)
    {
        error.Assign("GLB animation sampler has invalid input times");
        return false;
    }
    const double time = fmax(0.0, fmin(duration, seconds));
    for (cgltf_size c = 0; c < sourceAnimation.channels_count; ++c)
    {
        const cgltf_animation_channel& channel = sourceAnimation.channels[c];
        const cgltf_animation_sampler& sampler = *channel.sampler;
        const cgltf_accessor& input = *sampler.input;
        const cgltf_accessor& values = *sampler.output;
        if (input.type != cgltf_type_scalar || input.count == 0 || input.is_sparse || !input.buffer_view || sampler.interpolation > cgltf_interpolation_type_cubic_spline)
        {
            error.Assign("GLB animation sampler input is invalid");
            return false;
        }
        const uint32_t components = channel.target_path == cgltf_animation_path_type_rotation ? 4u : (channel.target_path == cgltf_animation_path_type_weights ? 1u : 3u);
        const uint32_t keyScale = sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3u : 1u;
        uint32_t morphCount = 1;
        if (channel.target_path == cgltf_animation_path_type_weights)
        {
            morphCount = 0;
            for (uint32_t m = 0; m < morphRanges.Count(); ++m)
                if (morphRanges.At(m).node == static_cast<uint32_t>(channel.target_node - data->nodes))
                    ++morphCount;
            if (!morphCount)
                continue;
        }
        const uint64_t expectedCount = static_cast<uint64_t>(input.count) * keyScale * morphCount;
        if (expectedCount != values.count || values.type != (components == 4 ? cgltf_type_vec4 : (components == 3 ? cgltf_type_vec3 : cgltf_type_scalar)) || values.component_type != cgltf_component_type_r_32f || values.is_sparse || !values.buffer_view)
        {
            error.Assign("GLB animation sampler output shape is invalid");
            return false;
        }
        uint32_t left = 0;
        float t0 = 0.0f, t1 = 0.0f;
        if (!cgltf_accessor_read_float(&input, 0, &t0, 1) || !IsFinite(t0))
            return false;
        while (left + 1 < input.count)
        {
            if (!cgltf_accessor_read_float(&input, left + 1, &t1, 1) || !IsFinite(t1) || !(t1 > t0))
            {
                error.Assign("GLB animation input times must be finite and strictly increasing");
                return false;
            }
            if (time < t1)
                break;
            ++left;
            t0 = t1;
        }
        const uint32_t right = left + 1 < input.count ? left + 1 : left;
        if (right == left)
        {
            if (!cgltf_accessor_read_float(&input, left, &t0, 1))
                return false;
            t1 = t0;
        }
        const double channelTime = fmax(t0, fmin(time, FindDuration(sourceAnimation)));
        const double alpha = right == left ? 0.0 : (channelTime - t0) / (t1 - t0);
        for (uint32_t morphOrdinal = 0; morphOrdinal < morphCount; ++morphOrdinal)
        {
            uint32_t poseIndex = 0;
            if (channel.target_path == cgltf_animation_path_type_weights)
            {
                uint32_t seen = 0;
                for (uint32_t m = 0; m < morphRanges.Count(); ++m)
                    if (morphRanges.At(m).node == static_cast<uint32_t>(channel.target_node - data->nodes) && seen++ == morphOrdinal)
                    {
                        poseIndex = morphRanges.At(m).poseIndex;
                        break;
                    }
            }
            float a[4]{}, b[4]{}, inTangent[4]{}, outTangent[4]{};
            const uint32_t valueIndexA = (left * keyScale + (keyScale == 3 ? 1u : 0u)) * morphCount + (components == 1 ? morphOrdinal : 0u);
            const uint32_t valueIndexB = (right * keyScale + (keyScale == 3 ? 1u : 0u)) * morphCount + (components == 1 ? morphOrdinal : 0u);
            if (!cgltf_accessor_read_float(&values, valueIndexA, a, components) || !cgltf_accessor_read_float(&values, valueIndexB, b, components))
            {
                error.Assign("GLB animation output could not be read");
                return false;
            }
            if (sampler.interpolation == cgltf_interpolation_type_cubic_spline && right != left)
            {
                if (!cgltf_accessor_read_float(&values, (left * 3 + 2) * morphCount + (components == 1 ? morphOrdinal : 0u), outTangent, components) || !cgltf_accessor_read_float(&values, (right * 3) * morphCount + (components == 1 ? morphOrdinal : 0u), inTangent, components))
                {
                    error.Assign("GLB cubic spline tangent could not be read");
                    return false;
                }
            }
            float result[4]{};
            if (channel.target_path == cgltf_animation_path_type_rotation && sampler.interpolation == cgltf_interpolation_type_linear && right != left)
            {
                double qa[4]{}, qb[4]{}, dot = 0.0;
                for (uint32_t component = 0; component < 4; ++component)
                {
                    qa[component] = a[component];
                    qb[component] = b[component];
                    dot += qa[component] * qb[component];
                }
                if (dot < 0.0)
                {
                    dot = -dot;
                    for (uint32_t component = 0; component < 4; ++component)
                        qb[component] = -qb[component];
                }
                dot = fmin(1.0, dot);
                if (dot > 0.9995)
                {
                    for (uint32_t component = 0; component < 4; ++component)
                        result[component] = static_cast<float>(qa[component] + (qb[component] - qa[component]) * alpha);
                }
                else
                {
                    const double angle = acos(dot);
                    const double divisor = sin(angle);
                    const double leftWeight = sin((1.0 - alpha) * angle) / divisor;
                    const double rightWeight = sin(alpha * angle) / divisor;
                    for (uint32_t component = 0; component < 4; ++component)
                        result[component] = static_cast<float>(qa[component] * leftWeight + qb[component] * rightWeight);
                }
            }
            else
                for (uint32_t component = 0; component < components; ++component)
                {
                    if (!IsFinite(a[component]) || !IsFinite(b[component]) || !IsFinite(inTangent[component]) || !IsFinite(outTangent[component]))
                    {
                        error.Assign("GLB animation output contains a non-finite value");
                        return false;
                    }
                    if (sampler.interpolation == cgltf_interpolation_type_step || right == left)
                        result[component] = a[component];
                    else if (sampler.interpolation == cgltf_interpolation_type_linear)
                        result[component] = static_cast<float>(a[component] + (b[component] - a[component]) * alpha);
                    else
                    {
                        const double u = alpha, u2 = u * u, u3 = u2 * u, dt = t1 - t0;
                        const double value = (2 * u3 - 3 * u2 + 1) * a[component] + (u3 - 2 * u2 + u) * dt * outTangent[component] + (-2 * u3 + 3 * u2) * b[component] + (u3 - u2) * dt * inTangent[component];
                        if (!isfinite(value) || fabs(value) > FLT_MAX)
                        {
                            error.Assign("GLB cubic spline result exceeds float range");
                            return false;
                        }
                        result[component] = static_cast<float>(value);
                    }
                }
            if (channel.target_path == cgltf_animation_path_type_translation)
                memcpy(candidate.localTransforms.At(nodeToBone.At(static_cast<uint32_t>(channel.target_node - data->nodes))).position, result, sizeof(float) * 3);
            else if (channel.target_path == cgltf_animation_path_type_scale)
                memcpy(candidate.localTransforms.At(nodeToBone.At(static_cast<uint32_t>(channel.target_node - data->nodes))).scale, result, sizeof(float) * 3);
            else if (channel.target_path == cgltf_animation_path_type_rotation)
            {
                const double length = sqrt(static_cast<double>(result[0]) * result[0] + static_cast<double>(result[1]) * result[1] + static_cast<double>(result[2]) * result[2] + static_cast<double>(result[3]) * result[3]);
                if (!(length > 0.0) || !isfinite(length))
                {
                    error.Assign("GLB animation rotation is invalid");
                    return false;
                }
                for (uint32_t component = 0; component < 4; ++component)
                    candidate.localTransforms.At(nodeToBone.At(static_cast<uint32_t>(channel.target_node - data->nodes))).rotation[component] = static_cast<float>(result[component] / length);
            }
            else
                candidate.morphWeights.At(poseIndex) = result[0];
        }
    }
    output.localTransforms.MoveFrom(candidate.localTransforms);
    output.morphWeights.MoveFrom(candidate.morphWeights);
    error.Clear();
    return true;
}

bool AGlbAnimationSource::Deform(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const
{
    if (!geometryMapped)
    {
        error.Assign("GLB animation source has no bound model geometry");
        return false;
    }
    gk::Array<float> world;
    if (pose.localTransforms.Count() != skeleton.parents.Count() || pose.morphWeights.Count() != skeleton.restMorphWeights.Count() || !animation::EvaluateModelPose(skeleton, pose, world, error))
        return false;
    animation::FModelPose restPose;
    gk::Array<float> restWorld;
    if (!animation::InitializeModelPose(skeleton, restPose, error) || !animation::EvaluateModelPose(skeleton, restPose, restWorld, error) || restVertices.Count() != output.vertices.Count())
    {
        if (error.Empty())
            error.Assign("GLB deformation base vertex layout does not match its source");
        return false;
    }
    gk::Array<detail::ModelVertex> candidate;
    if (!candidate.AppendRange(output.vertices.Data(), output.vertices.Count()))
    {
        error.Assign("GLB deformation snapshot allocation failed");
        return false;
    }
    Array<detail::ModelVertex> morphFrames;
    if (!morphFrames.Reserve(candidate.Count()))
    {
        error.Assign("GLB morph frame storage allocation failed");
        return false;
    }
    for (uint32_t vertex = 0; vertex < candidate.Count(); ++vertex)
        if (!morphFrames.Append({}))
        {
            error.Assign("GLB morph frame storage allocation failed");
            return false;
        }
    for (uint32_t primitiveIndex = 0; primitiveIndex < primitives.Count(); ++primitiveIndex)
    {
        const FPrimitiveRef& ref = primitives.At(primitiveIndex);
        if (!ref.generateMorphNormals && !ref.generateMorphTangents)
            continue;
        if (ref.modelVertexCount != ref.modelIndexCount || ref.modelVertexCount % 3 != 0 || ref.firstModelVertex > candidate.Count() || ref.modelVertexCount > candidate.Count() - ref.firstModelVertex)
        {
            error.Assign("GLB morph corner mapping does not match its model vertices");
            return false;
        }
        cgltf_primitive& primitive = *ref.primitive;
        const cgltf_accessor* position = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        const cgltf_accessor* normal = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
        const bool generateTangents = ref.generateMorphTangents || (ref.generateMorphNormals && primitive.material && primitive.material->normal_texture.texture);
        Array<detail::ModelVertex> baseCorners;
        if (!position || !baseCorners.Reserve(ref.modelIndexCount))
        {
            error.Assign("GLB morph corner input allocation failed");
            return false;
        }
        for (uint32_t corner = 0; corner < ref.modelIndexCount; ++corner)
        {
            const cgltf_size sourceIndex = primitive.indices ? cgltf_accessor_read_index(primitive.indices, corner) : corner;
            if (sourceIndex >= position->count)
            {
                error.Assign("GLB morph corner source index is invalid");
                return false;
            }
            detail::ModelVertex vertex{};
            vertex.sourceIndex = static_cast<uint32_t>(sourceIndex);
            const detail::ModelVertex& restVertex = restVertices.At(ref.firstModelVertex + corner);
            memcpy(vertex.normalUv, restVertex.normalUv, sizeof(vertex.normalUv));
            float value[4]{};
            if (!cgltf_accessor_read_float(position, sourceIndex, value, 3))
            {
                error.Assign("GLB morph position accessor could not be read");
                return false;
            }
            memcpy(vertex.position, value, sizeof(vertex.position));
            if (normal)
            {
                if (!cgltf_accessor_read_float(normal, sourceIndex, value, 3))
                {
                    error.Assign("GLB morph normal accessor could not be read");
                    return false;
                }
                memcpy(vertex.normal, value, sizeof(vertex.normal));
            }
            if (!baseCorners.Append(vertex))
            {
                error.Assign("GLB morph corner input allocation failed");
                return false;
            }
        }
        Array<detail::ModelVertex> baseFrame;
        const bool baseGenerated = ref.generateMorphNormals ? animation::GenerateGlbMorphedFlatFrame(baseCorners, generateTangents, baseFrame, error) : animation::GenerateGlbMorphedTangentFrame(baseCorners, baseFrame, error);
        if (!baseGenerated)
            return false;
        Array<detail::ModelVertex> blendedFrame;
        if (!blendedFrame.AppendRange(baseFrame.Data(), baseFrame.Count()))
        {
            error.Assign("GLB morph blended frame allocation failed");
            return false;
        }
        for (cgltf_size target = 0; target < primitive.targets_count; ++target)
        {
            uint32_t morphPoseIndex = kInvalid;
            for (uint32_t morph = 0; morph < morphRanges.Count(); ++morph)
                if (morphRanges.At(morph).node == ref.node && morphRanges.At(morph).target == target)
                {
                    morphPoseIndex = morphRanges.At(morph).poseIndex;
                    break;
                }
            if (morphPoseIndex == kInvalid || morphPoseIndex >= pose.morphWeights.Count())
            {
                error.Assign("GLB generated morph frame has no matching weight");
                return false;
            }
            const float weight = pose.morphWeights.At(morphPoseIndex);
            if (!IsFinite(weight))
            {
                error.Assign("GLB generated morph frame weight is non-finite");
                return false;
            }
            if (weight == 0.0f)
                continue;
            Array<detail::ModelVertex> targetCorners;
            if (!targetCorners.AppendRange(baseCorners.Data(), baseCorners.Count()))
            {
                error.Assign("GLB morph target corner allocation failed");
                return false;
            }
            for (cgltf_size attributeIndex = 0; attributeIndex < primitive.targets[target].attributes_count; ++attributeIndex)
            {
                const cgltf_attribute& attribute = primitive.targets[target].attributes[attributeIndex];
                if (attribute.type != cgltf_attribute_type_position && (ref.generateMorphNormals || attribute.type != cgltf_attribute_type_normal))
                    continue;
                if (attribute.type == cgltf_attribute_type_tangent)
                    continue;
                for (uint32_t corner = 0; corner < ref.modelIndexCount; ++corner)
                {
                    const cgltf_size sourceIndex = primitive.indices ? cgltf_accessor_read_index(primitive.indices, corner) : corner;
                    float delta[4]{};
                    if (!attribute.data || !cgltf_accessor_read_float(attribute.data, sourceIndex, delta, 3))
                    {
                        error.Assign("GLB morph frame delta could not be read");
                        return false;
                    }
                    float* destination = attribute.type == cgltf_attribute_type_position ? targetCorners.At(corner).position : targetCorners.At(corner).normal;
                    for (uint32_t axis = 0; axis < 3; ++axis)
                        destination[axis] += delta[axis];
                }
            }
            Array<detail::ModelVertex> targetFrame;
            const bool targetGenerated = ref.generateMorphNormals ? animation::GenerateGlbMorphedFlatFrame(targetCorners, generateTangents, targetFrame, error) : animation::GenerateGlbMorphedTangentFrame(targetCorners, targetFrame, error);
            if (!targetGenerated)
                return false;
            for (uint32_t corner = 0; corner < ref.modelIndexCount; ++corner)
            {
                detail::ModelVertex& blended = blendedFrame.At(corner);
                const detail::ModelVertex& base = baseFrame.At(corner);
                const detail::ModelVertex& targetVertex = targetFrame.At(corner);
                if (ref.generateMorphNormals)
                    for (uint32_t axis = 0; axis < 3; ++axis)
                        blended.normal[axis] += weight * (targetVertex.normal[axis] - base.normal[axis]);
                if (generateTangents)
                    for (uint32_t axis = 0; axis < 3; ++axis)
                        blended.tangent[axis] += weight * (targetVertex.tangent[axis] - base.tangent[axis]);
            }
        }
        for (uint32_t corner = 0; corner < ref.modelIndexCount; ++corner)
        {
            detail::ModelVertex& frame = blendedFrame.At(corner);
            if (ref.generateMorphNormals && !NormalizeMorphDirection(frame.normal))
            {
                error.Assign("GLB morphed flat normal could not be normalized");
                return false;
            }
            if (generateTangents)
                frame.tangent[3] = baseFrame.At(corner).tangent[3];
            morphFrames.At(ref.firstModelVertex + corner) = frame;
        }
    }
    for (uint32_t i = 0; i < candidate.Count(); ++i)
    {
        detail::ModelVertex& vertex = candidate.At(i);
        const FPrimitiveRef* ref = nullptr;
        uint32_t localIndex = 0;
        for (uint32_t p = 0; p < primitives.Count(); ++p)
        {
            const FPrimitiveRef& current = primitives.At(p);
            if (vertex.sourceIndex >= current.firstSource && vertex.sourceIndex - current.firstSource < current.vertexCount)
            {
                ref = &current;
                localIndex = vertex.sourceIndex - current.firstSource;
                break;
            }
        }
        if (!ref)
        {
            error.Assign("GLB deformation source vertex is not mapped");
            return false;
        }
        cgltf_primitive& primitive = *ref->primitive;
        const cgltf_accessor* position = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        const cgltf_accessor* normal = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
        // NORMAL補完時は読み込み時と同じく生成した接線を使う。
        const cgltf_accessor* tangent = normal ? cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0) : nullptr;
        const cgltf_accessor* joints = cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0);
        const cgltf_accessor* weights = cgltf_find_accessor(&primitive, cgltf_attribute_type_weights, 0);
        float sourcePosition[4]{}, sourceNormal[4]{}, sourceTangent[4]{};
        const detail::ModelVertex& restVertex = restVertices.At(i);
        if (!position || !cgltf_accessor_read_float(position, localIndex, sourcePosition, 3) || (normal && !cgltf_accessor_read_float(normal, localIndex, sourceNormal, 3)) || (tangent && !cgltf_accessor_read_float(tangent, localIndex, sourceTangent, 4)))
        {
            error.Assign("GLB deformation source attributes could not be read");
            return false;
        }
        float restNodeMatrix[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        if (ref->node != kInvalid)
            memcpy(restNodeMatrix, restWorld.Data() + nodeToBone.At(ref->node) * 16u, sizeof(restNodeMatrix));
        const detail::ModelVertex& morphFrame = morphFrames.At(i);
        if (ref->generateMorphNormals)
            memcpy(sourceNormal, morphFrame.normal, sizeof(morphFrame.normal));
        else if (!normal && !RestoreLocalNormal(restNodeMatrix, restVertex.normal, sourceNormal))
        {
            error.Assign("GLB generated normal cannot be restored to mesh space");
            return false;
        }
        const bool hasGeneratedTangent = tangent || restVertex.tangent[0] != 0.0f || restVertex.tangent[1] != 0.0f || restVertex.tangent[2] != 0.0f;
        if (ref->generateMorphTangents)
            memcpy(sourceTangent, morphFrame.tangent, sizeof(morphFrame.tangent));
        else if (!tangent && hasGeneratedTangent && !RestoreLocalTangent(restNodeMatrix, restVertex.tangent, sourceTangent))
        {
            error.Assign("GLB generated tangent cannot be restored to mesh space");
            return false;
        }
        const double restDeterminant = LinearDeterminant(restNodeMatrix);
        if (!ref->generateMorphTangents)
            sourceTangent[3] = restVertex.tangent[3] * (restDeterminant < 0.0 ? -1.0f : 1.0f);
        for (cgltf_size target = 0; target < primitive.targets_count; ++target)
        {
            uint32_t morphIndex = kInvalid;
            for (uint32_t m = 0; m < morphRanges.Count(); ++m)
                if (morphRanges.At(m).node == ref->node && morphRanges.At(m).target == target)
                {
                    morphIndex = morphRanges.At(m).poseIndex;
                    break;
                }
            const float weight = morphIndex == kInvalid ? 0.0f : pose.morphWeights.At(morphIndex);
            if (!IsFinite(weight))
            {
                error.Assign("GLB deformation morph weight is non-finite");
                return false;
            }
            for (cgltf_size a = 0; a < primitive.targets[target].attributes_count; ++a)
            {
                const cgltf_attribute& attribute = primitive.targets[target].attributes[a];
                const uint32_t components = attribute.type == cgltf_attribute_type_position || attribute.type == cgltf_attribute_type_normal ? 3u : (attribute.type == cgltf_attribute_type_tangent ? 3u : 0u);
                if (!components)
                    continue;
                if ((!normal && attribute.type != cgltf_attribute_type_position) || (ref->generateMorphTangents && attribute.type == cgltf_attribute_type_tangent))
                    continue;
                float delta[4]{};
                if (!attribute.data || !cgltf_accessor_read_float(attribute.data, localIndex, delta, components))
                {
                    error.Assign("GLB morph accessor could not be read");
                    return false;
                }
                float* destination = attribute.type == cgltf_attribute_type_position ? sourcePosition : (attribute.type == cgltf_attribute_type_normal ? sourceNormal : sourceTangent);
                for (uint32_t c = 0; c < components; ++c)
                    destination[c] += delta[c] * weight;
            }
        }

        float deformedPosition[3]{};
        float deformedNormal[3]{};
        float deformedTangent[3]{};
        float tangentTransform[16]{};
        double totalWeight = 0.0;
        cgltf_node* node = ref->node == kInvalid ? nullptr : &data->nodes[ref->node];
        if (node && node->skin)
        {
            if (!joints || !weights || !node->skin->joints_count)
            {
                error.Assign("GLB skinned primitive has no joint weights");
                return false;
            }
            float jointValues[4]{}, weightValues[4]{};
            if (!cgltf_accessor_read_float(joints, localIndex, jointValues, 4) || !cgltf_accessor_read_float(weights, localIndex, weightValues, 4))
            {
                error.Assign("GLB skin vertex attributes could not be read");
                return false;
            }
            for (uint32_t influence = 0; influence < 4; ++influence)
            {
                const double weight = weightValues[influence];
                const uint32_t joint = static_cast<uint32_t>(jointValues[influence]);
                if (!isfinite(weight) || weight < 0.0 || (weight > 0.0 && joint >= node->skin->joints_count))
                {
                    error.Assign("GLB skin influence is invalid");
                    return false;
                }
                if (weight == 0.0)
                    continue;
                uint32_t jointNode = 0;
                if (!FindNodeIndex(data, node->skin->joints[joint], jointNode) || nodeToBone.At(jointNode) == kInvalid)
                {
                    error.Assign("GLB skin joint is outside the animation hierarchy");
                    return false;
                }
                const float* jointWorld = world.Data() + nodeToBone.At(jointNode) * 16u;
                float inverseBind[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                if (node->skin->inverse_bind_matrices && !cgltf_accessor_read_float(node->skin->inverse_bind_matrices, joint, inverseBind, 16))
                {
                    error.Assign("GLB inverse bind matrix could not be read");
                    return false;
                }
                float matrix[16]{};
                for (uint32_t column = 0; column < 4; ++column)
                    for (uint32_t row = 0; row < 4; ++row)
                        for (uint32_t k = 0; k < 4; ++k)
                            matrix[column * 4 + row] += jointWorld[k * 4 + row] * inverseBind[column * 4 + k];
                const float p[4] = { sourcePosition[0], sourcePosition[1], sourcePosition[2], 1.0f };
                for (uint32_t axis = 0; axis < 3; ++axis)
                    deformedPosition[axis] += static_cast<float>(weight * (matrix[axis] * p[0] + matrix[4 + axis] * p[1] + matrix[8 + axis] * p[2] + matrix[12 + axis]));
                float transformedNormal[3]{};
                if (!TransformNormal(matrix, sourceNormal, transformedNormal))
                {
                    error.Assign("GLB skin matrix cannot transform a normal");
                    return false;
                }
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    deformedNormal[axis] += static_cast<float>(weight * transformedNormal[axis]);
                    deformedTangent[axis] += static_cast<float>(weight * (matrix[axis] * sourceTangent[0] + matrix[4 + axis] * sourceTangent[1] + matrix[8 + axis] * sourceTangent[2]));
                }
                for (uint32_t element = 0; element < 16; ++element)
                    tangentTransform[element] += static_cast<float>(weight * matrix[element]);
                totalWeight += weight;
            }
        }
        else
        {
            float matrix[16];
            if (node)
                memcpy(matrix, world.Data() + nodeToBone.At(ref->node) * 16u, sizeof(matrix));
            else
            {
                const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                memcpy(matrix, identity, sizeof(matrix));
            }
            memcpy(tangentTransform, matrix, sizeof(tangentTransform));
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                deformedPosition[axis] = matrix[axis] * sourcePosition[0] + matrix[4 + axis] * sourcePosition[1] + matrix[8 + axis] * sourcePosition[2] + matrix[12 + axis];
                if (!TransformNormal(matrix, sourceNormal, deformedNormal))
                {
                    error.Assign("GLB node matrix cannot transform a normal");
                    return false;
                }
                deformedTangent[axis] = matrix[axis] * sourceTangent[0] + matrix[4 + axis] * sourceTangent[1] + matrix[8 + axis] * sourceTangent[2];
            }
            totalWeight = 1.0;
        }
        if (!(totalWeight > 0.0) || !isfinite(totalWeight))
        {
            error.Assign("GLB skin weights have no positive total");
            return false;
        }
        double normalLength = sqrt(static_cast<double>(deformedNormal[0]) * deformedNormal[0] + static_cast<double>(deformedNormal[1]) * deformedNormal[1] + static_cast<double>(deformedNormal[2]) * deformedNormal[2]);
        if (!(normalLength > 1.0e-20) || !isfinite(normalLength))
        {
            error.Assign("GLB deformation produced a zero normal");
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const double p = deformedPosition[axis] / totalWeight;
            const double n = deformedNormal[axis] / normalLength;
            const double t = deformedTangent[axis] / totalWeight;
            if (!isfinite(p) || fabs(p) > FLT_MAX || !isfinite(n) || fabs(n) > FLT_MAX || !isfinite(t) || fabs(t) > FLT_MAX)
            {
                error.Assign("GLB deformation exceeds the numeric range");
                return false;
            }
            vertex.position[axis] = static_cast<float>(p);
            vertex.normal[axis] = static_cast<float>(n);
            if (hasGeneratedTangent)
                vertex.tangent[axis] = static_cast<float>(t);
        }
        if (hasGeneratedTangent)
        {
            const double projection = static_cast<double>(vertex.tangent[0]) * vertex.normal[0] + static_cast<double>(vertex.tangent[1]) * vertex.normal[1] + static_cast<double>(vertex.tangent[2]) * vertex.normal[2];
            double tangentValue[3] = { vertex.tangent[0] - projection * vertex.normal[0], vertex.tangent[1] - projection * vertex.normal[1], vertex.tangent[2] - projection * vertex.normal[2] };
            const double tangentLength = sqrt(tangentValue[0] * tangentValue[0] + tangentValue[1] * tangentValue[1] + tangentValue[2] * tangentValue[2]);
            if (!(tangentLength > 1.0e-20) || !isfinite(tangentLength))
            {
                error.Assign("GLB deformation produced a zero tangent");
                return false;
            }
            for (uint32_t axis = 0; axis < 3; ++axis)
                vertex.tangent[axis] = static_cast<float>(tangentValue[axis] / tangentLength);
            const double determinant = LinearDeterminant(tangentTransform);
            if (!isfinite(determinant) || determinant == 0.0)
            {
                error.Assign("GLB deformation tangent transform is singular");
                return false;
            }
            vertex.tangent[3] = sourceTangent[3] * (determinant < 0.0 ? -1.0f : 1.0f);
        }
    }
    output.vertices.MoveFrom(candidate);
    error.Clear();
    return true;
}

AModelAnimationSource* CreateGlbAnimationSource(cgltf_data* ownedData, uint8_t* ownedBytes, uint32_t byteCount, const detail::ModelResource& geometry, String& error)
{
    if (!ownedData || !ownedBytes || !byteCount)
    {
        if (ownedData)
            cgltf_free(ownedData);
        Deallocate(ownedBytes);
        error.Assign("GLB animation source ownership is invalid");
        return nullptr;
    }
    AGlbAnimationSource* source = nullptr;
    try
    {
        source = new AGlbAnimationSource;
    }
    catch (...)
    {
        cgltf_free(ownedData);
        Deallocate(ownedBytes);
        error.Assign("GLB animation source allocation failed");
        return nullptr;
    }
    source->data = ownedData;
    source->bytes = ownedBytes;
    source->byteCount = byteCount;
    bool valid = source->restVertices.AppendRange(geometry.vertices.Data(), geometry.vertices.Count()) && BuildSkeleton(*source, error) && ValidateAnimations(*source, error) && BuildPrimitiveRefs(*source, geometry, error);
    if (!valid)
    {
        delete source;
        if (error.Empty())
            error.Assign("GLB animation source is invalid");
        return nullptr;
    }
    return source;
}

FModelAnimationAsset* LoadGlbAnimation(const uint8_t* bytes, uint32_t size, String& error)
{
    if (!bytes || !size || size > 64u * 1024u * 1024u)
    {
        error.Assign("GLB animation bytes are invalid or too large");
        return nullptr;
    }
    uint8_t* ownedBytes = static_cast<uint8_t*>(Allocate(size));
    if (!ownedBytes)
    {
        error.Assign("GLB animation byte copy allocation failed");
        return nullptr;
    }
    memcpy(ownedBytes, bytes, size);
    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, ownedBytes, size, &data) != cgltf_result_success || !data || data->file_type != cgltf_file_type_glb || !data->asset.version || strcmp(data->asset.version, "2.0") != 0 || data->buffers_count != 1 || data->buffers[0].uri || cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success || cgltf_validate(data) != cgltf_result_success)
    {
        if (data)
            cgltf_free(data);
        Deallocate(ownedBytes);
        error.Assign("GLB animation document is invalid");
        return nullptr;
    }
    AModelAnimationSource* source = CreateGlbAnimationSource(data, ownedBytes, size, detail::ModelResource{}, error);
    if (!source)
        return nullptr;
    return CreateModelAnimationAsset(source, error);
}
}
