// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FbxAnimation.h"
#include "foundation/Memory.h"
#include <ufbx/ufbx.h>
#include <math.h>
#include <string.h>
#ifdef GKCORE_TESTING
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif
#endif

/**
 * FBX animation sourceを共通model animation形式へ変換する。
 */
namespace gk::model
{
#ifdef GKCORE_TESTING
namespace
{
// CPU検証で直前のDeform経路を記録する。
bool lastFbxDeformUsedFastPath = false;
// 直前のfast deformをCPU段階ごとに計測する。
struct FLastFbxDeformProfile
{
    double milliseconds[7] = {};
    uint32_t counts[6] = {};
};
FLastFbxDeformProfile lastFbxDeformProfile;

/**
 * QPCまたは同等の単調時計から高分解能tickを取得する。
 */
int64_t FbxTestCounter()
{
#ifdef _WIN32
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
#else
    timespec counter{};
    clock_gettime(CLOCK_MONOTONIC, &counter);
    return static_cast<int64_t>(counter.tv_sec) * 1000000000ll + counter.tv_nsec;
#endif
}

/**
 * tick差をmillisecondへ変換する。
 */
double FbxTestMilliseconds(int64_t begin, int64_t end)
{
#ifdef _WIN32
    static const double millisecondsPerTick = []()
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return frequency.QuadPart ? 1000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
    }();
    return static_cast<double>(end - begin) * millisecondsPerTick;
#else
    return static_cast<double>(end - begin) / 1000000.0;
#endif
}
}
#endif
namespace
{
const uint32_t maxFbxSceneElements = 1000000u;
const uint32_t maxFbxAnimationBytes = 64u * 1024u * 1024u;

/**
 * FBX parser用のfoundation allocation関数。
 */
void* AllocateFbxMemory(void*, size_t size)
{
    return Allocate(size);
}

/**
 * FBX parser用のfoundation再allocation関数。
 */
void* ReallocateFbxMemory(void*, void* memory, size_t, size_t newSize)
{
    return Reallocate(memory, newSize);
}

/**
 * FBX parser用のfoundation解放関数。
 */
void FreeFbxMemory(void*, void* memory, size_t)
{
    Deallocate(memory);
}

/**
 * ufbxへ渡すfoundation allocator設定を返す。
 */
ufbx_allocator_opts MakeAllocatorOptions(size_t memoryLimit, size_t allocationLimit)
{
    ufbx_allocator_opts options{};
    options.allocator.alloc_fn = AllocateFbxMemory;
    options.allocator.realloc_fn = ReallocateFbxMemory;
    options.allocator.free_fn = FreeFbxMemory;
    options.memory_limit = memoryLimit;
    options.allocation_limit = allocationLimit;
    return options;
}

/**
 * ufbx parse/evaluate errorを固定長buffer経由で保存する。
 */
void AssignFbxError(const ufbx_error& source, String& error)
{
    char message[1024];
    const size_t length = ufbx_format_error(message, sizeof(message), &source);
    message[sizeof(message) - 1] = '\0';
    if (length && length < sizeof(message))
    {
        error.Assign(message);
    }
    else
    {
        error.Assign("FBX animation evaluation failed");
    }
}

/**
 * source transformを有限な共通TRSへ変換する。
 */
bool CopyTransform(const ufbx_transform& source, animation::FModelBoneTransform& output)
{
    const double rotationLength = static_cast<double>(source.rotation.x) * source.rotation.x + static_cast<double>(source.rotation.y) * source.rotation.y + static_cast<double>(source.rotation.z) * source.rotation.z + static_cast<double>(source.rotation.w) * source.rotation.w;
    if (!isfinite(source.translation.x) || !isfinite(source.translation.y) || !isfinite(source.translation.z) || !isfinite(source.rotation.x) || !isfinite(source.rotation.y) || !isfinite(source.rotation.z) || !isfinite(source.rotation.w) || !isfinite(source.scale.x) || !isfinite(source.scale.y) || !isfinite(source.scale.z) || !isfinite(rotationLength) || rotationLength <= 1.0e-24)
    {
        return false;
    }
    const double inverseRotationLength = 1.0 / sqrt(rotationLength);
    output.position[0] = static_cast<float>(source.translation.x);
    output.position[1] = static_cast<float>(source.translation.y);
    output.position[2] = static_cast<float>(source.translation.z);
    output.rotation[0] = static_cast<float>(source.rotation.x * inverseRotationLength);
    output.rotation[1] = static_cast<float>(source.rotation.y * inverseRotationLength);
    output.rotation[2] = static_cast<float>(source.rotation.z * inverseRotationLength);
    output.rotation[3] = static_cast<float>(source.rotation.w * inverseRotationLength);
    output.scale[0] = static_cast<float>(source.scale.x);
    output.scale[1] = static_cast<float>(source.scale.y);
    output.scale[2] = static_cast<float>(source.scale.z);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(output.position[axis]) || !isfinite(output.scale[axis]))
        {
            return false;
        }
    }
    for (uint32_t axis = 0; axis < 4; ++axis)
    {
        if (!isfinite(output.rotation[axis]))
        {
            return false;
        }
    }
    return true;
}

/**
 * 共通姿勢のTRSをufbx transformへ変換する。
 */
bool CopyTransform(const animation::FModelBoneTransform& source, ufbx_transform& output)
{
    const double rotationLength = static_cast<double>(source.rotation[0]) * source.rotation[0] + static_cast<double>(source.rotation[1]) * source.rotation[1] + static_cast<double>(source.rotation[2]) * source.rotation[2] + static_cast<double>(source.rotation[3]) * source.rotation[3];
    if (!isfinite(rotationLength) || rotationLength <= 1.0e-24)
    {
        return false;
    }
    const double inverseRotationLength = 1.0 / sqrt(rotationLength);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(source.position[axis]) || !isfinite(source.scale[axis]))
        {
            return false;
        }
    }
    for (uint32_t axis = 0; axis < 4; ++axis)
    {
        if (!isfinite(source.rotation[axis]))
        {
            return false;
        }
    }
    output.translation = { source.position[0], source.position[1], source.position[2] };
    output.rotation = { static_cast<ufbx_real>(source.rotation[0] * inverseRotationLength), static_cast<ufbx_real>(source.rotation[1] * inverseRotationLength), static_cast<ufbx_real>(source.rotation[2] * inverseRotationLength), static_cast<ufbx_real>(source.rotation[3] * inverseRotationLength) };
    output.scale = { source.scale[0], source.scale[1], source.scale[2] };
    return true;
}

/**
 * 暗黙rootの姿勢をsource rest値と比較する。
 */
bool IsSameTransform(const animation::FModelBoneTransform& left, const animation::FModelBoneTransform& right)
{
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (left.position[axis] != right.position[axis] || left.scale[axis] != right.scale[axis])
        {
            return false;
        }
    }
    for (uint32_t axis = 0; axis < 4; ++axis)
    {
        if (left.rotation[axis] != right.rotation[axis])
        {
            return false;
        }
    }
    return true;
}

/**
 * indexed vec3 attributeからひとつのcorner値を読む。
 */
bool ReadAttribute(const ufbx_vertex_vec3& attribute, uint32_t corner, ufbx_vec3& value)
{
    if (!attribute.exists || !attribute.indices.data || !attribute.values.data || corner >= attribute.indices.count)
    {
        return false;
    }
    const uint32_t valueIndex = attribute.indices.data[corner];
    if (valueIndex >= attribute.values.count)
    {
        return false;
    }
    value = attribute.values.data[valueIndex];
    return isfinite(static_cast<double>(value.x)) && isfinite(static_cast<double>(value.y)) && isfinite(static_cast<double>(value.z));
}

/**
 * model-space normalをinstance geometry transformで変換し正規化する。
 */
bool TransformNormal(const ufbx_matrix& matrix, ufbx_vec3 source, float output[3])
{
    const ufbx_real determinant = ufbx_matrix_determinant(&matrix);
    if (!isfinite(static_cast<double>(determinant)) || fabs(static_cast<double>(determinant)) <= 1.0e-30)
    {
        return false;
    }
    const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&matrix);
    const ufbx_vec3 transformed = ufbx_transform_direction(&normalMatrix, source);
    const double x = static_cast<double>(transformed.x);
    const double y = static_cast<double>(transformed.y);
    const double z = static_cast<double>(transformed.z);
    const double lengthSquared = x * x + y * y + z * z;
    if (!isfinite(lengthSquared) || lengthSquared <= 1.0e-30)
    {
        return false;
    }
    const double inverseLength = 1.0 / sqrt(lengthSquared);
    output[0] = static_cast<float>(x * inverseLength);
    output[1] = static_cast<float>(y * inverseLength);
    output[2] = static_cast<float>(z * inverseLength);
    return isfinite(output[0]) && isfinite(output[1]) && isfinite(output[2]);
}

/**
 * local positionをinstance geometry transformで変換し有限値を確認する。
 */
bool TransformPosition(const ufbx_matrix& matrix, ufbx_vec3 source, float output[3])
{
    const ufbx_vec3 transformed = ufbx_transform_position(&matrix, source);
    output[0] = static_cast<float>(transformed.x);
    output[1] = static_cast<float>(transformed.y);
    output[2] = static_cast<float>(transformed.z);
    return isfinite(output[0]) && isfinite(output[1]) && isfinite(output[2]);
}

/**
 * FBX共通node/morph姿勢とmesh instance対応を保持する。
 */
class FbxAnimationSource final : public AModelAnimationSource
{
  public:
    explicit FbxAnimationSource(ufbx_scene* scene) : scene_(scene), modelVertexCount_(0), modelIndexCount_(0), modelPrimitiveCount_(0), modelMaterialCount_(0), fastDeformSupported_(false), fastGpuSkinningSupported_(false)
    {
    }

    ~FbxAnimationSource() override
    {
        if (scene_)
        {
            ufbx_free_scene(scene_);
        }
    }

    bool Initialize(const detail::ModelResource& geometry, const Array<detail::FFbxGeometrySegment>& segments, String& error)
    {
        if (!scene_ || scene_->nodes.count > maxFbxSceneElements || scene_->blend_channels.count > maxFbxSceneElements || scene_->anim_stacks.count > maxFbxSceneElements || (scene_->nodes.count && !scene_->nodes.data) || (scene_->blend_channels.count && !scene_->blend_channels.data) || (scene_->anim_stacks.count && !scene_->anim_stacks.data))
        {
            error.Assign("FBX animation hierarchy exceeds its limits or is incomplete");
            return false;
        }
        if (scene_->nodes.count > static_cast<size_t>(INT32_MAX) || scene_->blend_channels.count > UINT32_MAX || scene_->anim_stacks.count > UINT32_MAX)
        {
            error.Assign("FBX animation hierarchy exceeds the common index range");
            return false;
        }
        for (uint32_t nodeIndex = 0; nodeIndex < static_cast<uint32_t>(scene_->nodes.count); ++nodeIndex)
        {
            const ufbx_node* node = scene_->nodes.data[nodeIndex];
            if (!node || node->typed_id != nodeIndex)
            {
                error.Assign("FBX node order is not stable for common parent indices");
                return false;
            }
            const int32_t parentIndex = node->parent ? static_cast<int32_t>(node->parent->typed_id) : -1;
            if (parentIndex >= static_cast<int32_t>(nodeIndex))
            {
                error.Assign("FBX parent nodes do not precede their children");
                return false;
            }
            animation::FModelBoneTransform restTransform{};
            if (!CopyTransform(node->local_transform, restTransform) || !skeleton_.parents.Append(parentIndex) || !skeleton_.restLocalTransforms.Append(restTransform))
            {
                error.Assign("FBX common skeleton allocation or transform validation failed");
                return false;
            }
        }
        for (uint32_t morphIndex = 0; morphIndex < static_cast<uint32_t>(scene_->blend_channels.count); ++morphIndex)
        {
            const ufbx_blend_channel* channel = scene_->blend_channels.data[morphIndex];
            if (!channel || !isfinite(static_cast<double>(channel->weight)) || !skeleton_.restMorphWeights.Append(static_cast<float>(channel->weight)))
            {
                error.Assign("FBX morph channel is invalid or could not be retained");
                return false;
            }
        }
        modelVertexCount_ = geometry.vertices.Count();
        modelIndexCount_ = geometry.indices.Count();
        modelPrimitiveCount_ = geometry.primitives.Count();
        modelMaterialCount_ = geometry.materials.Count();
        if (segments.Count() == 0)
        {
            if (modelVertexCount_ != 0 || modelIndexCount_ != 0)
            {
                error.Assign("FBX geometry segments are missing from the animation source");
                return false;
            }
        }
        uint32_t coveredVertices = 0;
        for (uint32_t segmentIndex = 0; segmentIndex < segments.Count(); ++segmentIndex)
        {
            const detail::FFbxGeometrySegment& segment = segments.At(segmentIndex);
            if (segment.nodeTypedId >= scene_->nodes.count || segment.meshTypedId >= scene_->meshes.count || segment.firstModelVertex != coveredVertices || segment.vertexCount == 0 || segment.vertexCount > UINT32_MAX - coveredVertices || segment.vertexCount > modelVertexCount_ - coveredVertices)
            {
                error.Assign("FBX geometry segment has an invalid node, mesh, or output range");
                return false;
            }
            const ufbx_node* node = scene_->nodes.data[segment.nodeTypedId];
            const ufbx_mesh* mesh = scene_->meshes.data[segment.meshTypedId];
            if (!node || node->mesh != mesh || !mesh || mesh->num_indices != segment.vertexCount)
            {
                error.Assign("FBX geometry segment does not match its source mesh instance");
                return false;
            }
            if (mesh->cache_deformers.count)
            {
                error.Assign("FBX geometry caches are unsupported for animation deformation");
                return false;
            }
            if (mesh->skin_deformers.count > 1 || (mesh->skin_deformers.count && mesh->instances.count > 1))
            {
                error.Assign("FBX multiple skin deformers or skinned mesh instances are unsupported");
                return false;
            }
            for (size_t skinIndex = 0; skinIndex < mesh->skin_deformers.count; ++skinIndex)
            {
                const ufbx_skin_deformer* skin = mesh->skin_deformers.data[skinIndex];
                if (!skin || !skin->clusters.data || skin->clusters.count == 0)
                {
                    error.Assign("FBX skin deformer has no valid bone clusters");
                    return false;
                }
                // deformerに有効な正weightが含まれるか記録する。
                bool hasWeightedCluster = false;
                for (size_t clusterIndex = 0; clusterIndex < skin->clusters.count; ++clusterIndex)
                {
                    const ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
                    if (!cluster || !cluster->bone_node)
                    {
                        error.Assign("FBX skin cluster has incomplete bone or weight data");
                        return false;
                    }
                    // 頂点に結び付かないbone clusterは、空配列のまま保持される。
                    if (cluster->num_weights == 0 && cluster->vertices.count == 0 && cluster->weights.count == 0)
                    {
                        continue;
                    }
                    if (!cluster->vertices.data || !cluster->weights.data || cluster->num_weights != cluster->vertices.count || cluster->num_weights != cluster->weights.count)
                    {
                        error.Assign("FBX skin cluster has incomplete bone or weight data");
                        return false;
                    }
                    // 配列対応と頂点範囲を保ち、実際に変形へ使うweightを探す。
                    for (size_t weightIndex = 0; weightIndex < cluster->weights.count; ++weightIndex)
                    {
                        // 検証する頂点weight。
                        const double weight = static_cast<double>(cluster->weights.data[weightIndex]);
                        if (!isfinite(weight) || weight < 0.0 || cluster->vertices.data[weightIndex] >= mesh->num_vertices)
                        {
                            error.Assign("FBX skin cluster has an invalid vertex or weight");
                            return false;
                        }
                        hasWeightedCluster = hasWeightedCluster || weight > 0.0;
                    }
                }
                if (!hasWeightedCluster)
                {
                    error.Assign("FBX skin deformer has no weighted clusters");
                    return false;
                }
            }
            if (!segments_.Append(segment))
            {
                error.Assign("FBX geometry segment allocation failed");
                return false;
            }
            coveredVertices += segment.vertexCount;
        }
        if (coveredVertices != modelVertexCount_ || (segments.Count() && (modelIndexCount_ == 0 || modelPrimitiveCount_ == 0)))
        {
            error.Assign("FBX geometry segments do not cover the flattened model vertices");
            return false;
        }
        BuildFastSkinningCache();
        error.Clear();
        return true;
    }

    EModelAnimationFormat Format() const override
    {
        return EModelAnimationFormat::Fbx;
    }

    const animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton_;
    }

    const char* BoneName(uint32_t bone) const override
    {
        if (!scene_ || bone >= scene_->nodes.count)
        {
            return nullptr;
        }
        const ufbx_node* node = scene_->nodes.data[bone];
        if (!node || !node->name.data || node->name.length == 0)
        {
            return nullptr;
        }
        for (uint32_t other = 0; other < static_cast<uint32_t>(scene_->nodes.count); ++other)
        {
            if (other == bone)
            {
                continue;
            }
            const ufbx_string& name = scene_->nodes.data[other]->name;
            if (name.length == node->name.length && name.data && memcmp(name.data, node->name.data, name.length) == 0)
            {
                return nullptr;
            }
        }
        return node->name.data;
    }

    bool BoneWritable(uint32_t bone) const override
    {
        return scene_ && bone < scene_->nodes.count && scene_->nodes.data[bone] && !scene_->nodes.data[bone]->is_root;
    }

    const char* MorphName(uint32_t morph) const override
    {
        if (!scene_ || morph >= scene_->blend_channels.count)
        {
            return nullptr;
        }
        const ufbx_blend_channel* channel = scene_->blend_channels.data[morph];
        if (!channel || !channel->name.data || channel->name.length == 0)
        {
            return nullptr;
        }
        for (uint32_t other = 0; other < static_cast<uint32_t>(scene_->blend_channels.count); ++other)
        {
            if (other == morph)
            {
                continue;
            }
            const ufbx_string& name = scene_->blend_channels.data[other]->name;
            if (name.length == channel->name.length && name.data && memcmp(name.data, channel->name.data, name.length) == 0)
            {
                return nullptr;
            }
        }
        return channel->name.data;
    }

    uint32_t ClipCount() const override
    {
        return scene_ ? static_cast<uint32_t>(scene_->anim_stacks.count) : 0;
    }

    const char* ClipName(uint32_t clip) const override
    {
        if (!scene_ || clip >= scene_->anim_stacks.count)
        {
            return nullptr;
        }
        const ufbx_anim_stack* stack = scene_->anim_stacks.data[clip];
        return stack && stack->name.data && stack->name.length ? stack->name.data : nullptr;
    }

    double ClipDuration(uint32_t clip) const override
    {
        if (!scene_ || clip >= scene_->anim_stacks.count)
        {
            return -1.0;
        }
        const ufbx_anim_stack* stack = scene_->anim_stacks.data[clip];
        if (!stack || !isfinite(stack->time_begin) || !isfinite(stack->time_end) || stack->time_end < stack->time_begin)
        {
            return -1.0;
        }
        return stack->time_end - stack->time_begin;
    }

    bool Sample(uint32_t clip, double seconds, animation::FModelPose& output, String& error) const override
    {
        if (!scene_ || clip >= scene_->anim_stacks.count || !isfinite(seconds))
        {
            error.Assign("FBX animation clip or sample time is invalid");
            return false;
        }
        const ufbx_anim_stack* stack = scene_->anim_stacks.data[clip];
        if (!stack || !stack->anim || ClipDuration(clip) < 0.0)
        {
            error.Assign("FBX animation clip has invalid time bounds");
            return false;
        }
        const double duration = ClipDuration(clip);
        const double clampedTime = seconds < 0.0 ? 0.0 : (seconds > duration ? duration : seconds);
        const double sampleTime = stack->time_begin + clampedTime;
        if (!isfinite(sampleTime))
        {
            error.Assign("FBX animation sample time is outside the numeric range");
            return false;
        }
        // 配列を先に確保し、標本ごとの再確保を避ける。
        animation::FModelPose staged;
        if (scene_->nodes.count != skeleton_.parents.Count() || scene_->blend_channels.count != skeleton_.restMorphWeights.Count() || !staged.localTransforms.Reserve(skeleton_.parents.Count()) || !staged.morphWeights.Reserve(skeleton_.restMorphWeights.Count()))
        {
            error.Assign("FBX animation pose storage does not match the retained scene");
            return false;
        }
        for (uint32_t nodeIndex = 0; nodeIndex < skeleton_.parents.Count(); ++nodeIndex)
        {
            const ufbx_node* node = scene_->nodes.data[nodeIndex];
            if (!node)
            {
                error.Assign("FBX node hierarchy does not match the retained skeleton");
                return false;
            }
            animation::FModelBoneTransform transform{};
            if (!CopyTransform(ufbx_evaluate_transform(stack->anim, node, sampleTime), transform) || !staged.localTransforms.Append(transform))
            {
                error.Assign("FBX animation produced an invalid node transform");
                return false;
            }
        }
        for (uint32_t morph = 0; morph < skeleton_.restMorphWeights.Count(); ++morph)
        {
            const ufbx_real weight = ufbx_evaluate_blend_weight(stack->anim, scene_->blend_channels.data[morph], sampleTime);
            const float converted = static_cast<float>(weight);
            if (!isfinite(static_cast<double>(weight)) || !isfinite(converted) || !staged.morphWeights.Append(converted))
            {
                error.Assign("FBX animation produced an invalid morph weight");
                return false;
            }
        }
        output.localTransforms.MoveFrom(staged.localTransforms);
        output.morphWeights.MoveFrom(staged.morphWeights);
        error.Clear();
        return true;
    }

    bool Deform(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const override
    {
#ifdef GKCORE_TESTING
        lastFbxDeformUsedFastPath = false;
#endif
        if (!scene_ || segments_.Count() == 0 || modelVertexCount_ == 0 || output.vertices.Count() != modelVertexCount_ || output.indices.Count() != modelIndexCount_ || output.primitives.Count() != modelPrimitiveCount_ || output.materials.Count() != modelMaterialCount_ || pose.localTransforms.Count() != skeleton_.parents.Count() || pose.morphWeights.Count() != skeleton_.restMorphWeights.Count())
        {
            error.Assign("FBX deformation target or common pose does not match its source");
            return false;
        }
        if (fastDeformSupported_ && IsFastPose(pose))
        {
            const bool succeeded = DeformLinearSkin(pose, output, error);
#ifdef GKCORE_TESTING
            lastFbxDeformUsedFastPath = succeeded;
#endif
            return succeeded;
        }
        return DeformWithUfbx(pose, output, error);
    }

    const Array<animation::FModelSparseVertexMap>* SparseVertexMap() const override
    {
        return fastDeformSupported_ ? &fastSparseVertexMap_ : nullptr;
    }

    bool SparseDeformationIsValidated() const override
    {
        return fastDeformSupported_;
    }

    const animation::FModelGpuSkinningGeometry* GpuSkinningGeometry() const override
    {
        return fastGpuSkinningSupported_ ? &fastGpuGeometry_ : nullptr;
    }

    bool SupportsGpuSkinningPose(const animation::FModelPose& pose) const override
    {
        return scene_ && fastGpuSkinningSupported_ && pose.localTransforms.Count() == skeleton_.parents.Count() && IsFastPose(pose);
    }

    bool EvaluateGpuSkinningMatrices(const animation::FModelPose& pose, Array<animation::FModelGpuSkinningGeometry::FMatrix>& output, String& error) const override
    {
        if (!scene_ || !fastGpuSkinningSupported_ || !IsFastPose(pose) || pose.localTransforms.Count() != skeleton_.parents.Count())
        {
            error.Assign("FBX source does not support GPU skinning for this pose");
            return false;
        }
        Array<ufbx_matrix> nodeWorldMatrices;
        Array<animation::FModelGpuSkinningGeometry::FMatrix> candidate;
        if (!nodeWorldMatrices.Reserve(skeleton_.parents.Count()) || !candidate.Reserve(fastGpuGeometry_.clusters.Count()))
        {
            error.Assign("FBX GPU skinning matrix allocation failed");
            return false;
        }
        for (uint32_t nodeIndex = 0; nodeIndex < skeleton_.parents.Count(); ++nodeIndex)
        {
            const ufbx_node* node = scene_->nodes.data[nodeIndex];
            if (node->is_root && !IsSameTransform(pose.localTransforms.At(nodeIndex), skeleton_.restLocalTransforms.At(nodeIndex)))
            {
                error.Assign("FBX implicit root transform cannot be overridden");
                return false;
            }
            ufbx_transform localTransform{};
            if (!CopyTransform(pose.localTransforms.At(nodeIndex), localTransform))
            {
                error.Assign("FBX GPU skinning pose has an invalid node transform");
                return false;
            }
            const ufbx_matrix localMatrix = ufbx_transform_to_matrix(&localTransform);
            const int32_t parentIndex = skeleton_.parents.At(nodeIndex);
            const ufbx_matrix worldMatrix = parentIndex >= 0 ? ufbx_matrix_mul(&nodeWorldMatrices.At(static_cast<uint32_t>(parentIndex)), &localMatrix) : localMatrix;
            for (uint32_t element = 0; element < 12; ++element)
            {
                if (!isfinite(static_cast<double>(worldMatrix.v[element])))
                {
                    error.Assign("FBX GPU skinning pose produced a non-finite node matrix");
                    return false;
                }
            }
            if (!nodeWorldMatrices.Append(worldMatrix))
            {
                error.Assign("FBX GPU skinning node matrix allocation failed");
                return false;
            }
        }
        for (uint32_t clusterIndex = 0; clusterIndex < fastGpuGeometry_.clusters.Count(); ++clusterIndex)
        {
            const animation::FModelGpuSkinningGeometry::FCluster& cluster = fastGpuGeometry_.clusters.At(clusterIndex);
            if (cluster.nodeIndex >= nodeWorldMatrices.Count())
            {
                error.Assign("FBX GPU skinning cluster has an invalid node");
                return false;
            }
            ufbx_matrix geometryToBone{};
            for (uint32_t element = 0; element < 12; ++element)
            {
                geometryToBone.v[element] = cluster.geometryToBone[element];
            }
            const ufbx_matrix matrix = ufbx_matrix_mul(&nodeWorldMatrices.At(cluster.nodeIndex), &geometryToBone);
            animation::FModelGpuSkinningGeometry::FMatrix gpuMatrix{};
            gpuMatrix.value[0] = matrix.m00;
            gpuMatrix.value[1] = matrix.m01;
            gpuMatrix.value[2] = matrix.m02;
            gpuMatrix.value[3] = matrix.m03;
            gpuMatrix.value[4] = matrix.m10;
            gpuMatrix.value[5] = matrix.m11;
            gpuMatrix.value[6] = matrix.m12;
            gpuMatrix.value[7] = matrix.m13;
            gpuMatrix.value[8] = matrix.m20;
            gpuMatrix.value[9] = matrix.m21;
            gpuMatrix.value[10] = matrix.m22;
            gpuMatrix.value[11] = matrix.m23;
            for (uint32_t element = 0; element < 12; ++element)
            {
                if (!isfinite(static_cast<double>(matrix.v[element])) || !isfinite(gpuMatrix.value[element]))
                {
                    error.Assign("FBX GPU skinning matrix is non-finite");
                    return false;
                }
            }
            if (!candidate.Append(gpuMatrix))
            {
                error.Assign("FBX GPU skinning matrix allocation failed");
                return false;
            }
        }
        output.MoveFrom(candidate);
        error.Clear();
        return true;
    }

    bool DeformSparse(const animation::FModelPose& pose, animation::FModelSparsePoseGeometry& output, String& error) const override
    {
        if (!scene_ || !fastDeformSupported_ || modelVertexCount_ == 0 || pose.localTransforms.Count() != skeleton_.parents.Count() || pose.morphWeights.Count() != skeleton_.restMorphWeights.Count() || !IsFastPose(pose))
        {
            error.Assign("FBX source does not support sparse deformation for this pose");
            return false;
        }
        return DeformSparseLinearSkin(pose, output, error);
    }

    bool DeformSparseWithGpuSkinningData(const animation::FModelPose& pose, animation::FModelSparsePoseGeometry& output, Array<animation::FModelGpuSkinningGeometry::FMatrix>& outputMatrices, String& error) const override
    {
        if (!GpuSkinningGeometry())
        {
            error.Assign("FBX source does not support GPU skinning data for this pose");
            return false;
        }
        animation::FModelSparsePoseGeometry geometry;
        Array<animation::FModelGpuSkinningGeometry::FMatrix> matrices;
        if (!DeformSparse(pose, geometry, error) || !EvaluateGpuSkinningMatrices(pose, matrices, error))
        {
            return false;
        }
        output.positions.MoveFrom(geometry.positions);
        output.normals.MoveFrom(geometry.normals);
        outputMatrices.MoveFrom(matrices);
        error.Clear();
        return true;
    }

    bool DeformWithUfbxForTesting(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const
    {
        if (!scene_ || segments_.Count() == 0 || modelVertexCount_ == 0 || output.vertices.Count() != modelVertexCount_ || output.indices.Count() != modelIndexCount_ || output.primitives.Count() != modelPrimitiveCount_ || output.materials.Count() != modelMaterialCount_ || pose.localTransforms.Count() != skeleton_.parents.Count() || pose.morphWeights.Count() != skeleton_.restMorphWeights.Count())
        {
            error.Assign("FBX deformation target or common pose does not match its source");
            return false;
        }
        return DeformWithUfbx(pose, output, error);
    }

    bool DeformWithUfbx(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const
    {
        Array<ufbx_transform_override> transformOverrides;
        Array<ufbx_prop_override_desc> propertyOverrides;
        for (uint32_t nodeIndex = 0; nodeIndex < pose.localTransforms.Count(); ++nodeIndex)
        {
            // ufbxは暗黙rootへのoverrideを無視するため、変更を成功扱いにしない。
            const ufbx_node* node = scene_->nodes.data[nodeIndex];
            if (node->is_root && !IsSameTransform(pose.localTransforms.At(nodeIndex), skeleton_.restLocalTransforms.At(nodeIndex)))
            {
                error.Assign("FBX implicit root transform cannot be overridden");
                return false;
            }
            ufbx_transform_override overrideValue{};
            overrideValue.node_id = nodeIndex;
            if (!CopyTransform(pose.localTransforms.At(nodeIndex), overrideValue.transform) || !transformOverrides.Append(overrideValue))
            {
                error.Assign("FBX deformation pose has an invalid node transform");
                return false;
            }
        }
        const ufbx_string deformPercentName = { "DeformPercent", 13 };
        for (uint32_t morph = 0; morph < pose.morphWeights.Count(); ++morph)
        {
            const float weight = pose.morphWeights.At(morph);
            const double percent = static_cast<double>(weight) * 100.0;
            const float convertedPercent = static_cast<float>(percent);
            if (!isfinite(weight) || !isfinite(percent) || !isfinite(convertedPercent))
            {
                error.Assign("FBX deformation pose has an invalid morph weight");
                return false;
            }
            ufbx_prop_override_desc overrideValue{};
            overrideValue.element_id = scene_->blend_channels.data[morph]->element_id;
            overrideValue.prop_name = deformPercentName;
            overrideValue.value.x = convertedPercent;
            if (!propertyOverrides.Append(overrideValue))
            {
                error.Assign("FBX deformation morph override allocation failed");
                return false;
            }
        }
        ufbx_anim_opts animationOptions{};
        animationOptions.result_allocator = MakeAllocatorOptions(32u * 1024u * 1024u, 100000u);
        animationOptions.transform_overrides.data = transformOverrides.Data();
        animationOptions.transform_overrides.count = transformOverrides.Count();
        animationOptions.prop_overrides.data = propertyOverrides.Data();
        animationOptions.prop_overrides.count = propertyOverrides.Count();
        ufbx_error evaluationError{};
        ufbx_anim* poseAnimation = ufbx_create_anim(scene_, &animationOptions, &evaluationError);
        if (!poseAnimation)
        {
            AssignFbxError(evaluationError, error);
            return false;
        }
        ufbx_evaluate_opts evaluateOptions{};
        evaluateOptions.temp_allocator = MakeAllocatorOptions(64u * 1024u * 1024u, 500000u);
        evaluateOptions.result_allocator = MakeAllocatorOptions(128u * 1024u * 1024u, 1000000u);
        evaluateOptions.evaluate_skinning = true;
        ufbx_scene* evaluatedScene = ufbx_evaluate_scene(scene_, poseAnimation, 0.0, &evaluateOptions, &evaluationError);
        if (!evaluatedScene)
        {
            ufbx_free_anim(poseAnimation);
            AssignFbxError(evaluationError, error);
            return false;
        }
        struct FDeformedVertex
        {
            float position[3];
            float normal[3];
        };
        Array<FDeformedVertex> stagedVertices;
        bool success = true;
        for (uint32_t segmentIndex = 0; segmentIndex < segments_.Count() && success; ++segmentIndex)
        {
            const detail::FFbxGeometrySegment& segment = segments_.At(segmentIndex);
            if (segment.nodeTypedId >= evaluatedScene->nodes.count || segment.meshTypedId >= evaluatedScene->meshes.count)
            {
                error.Assign("FBX evaluated scene no longer matches its geometry mapping");
                success = false;
                break;
            }
            const ufbx_node* node = evaluatedScene->nodes.data[segment.nodeTypedId];
            const ufbx_mesh* mesh = evaluatedScene->meshes.data[segment.meshTypedId];
            if (!node || !mesh || node->mesh != mesh || mesh->num_indices != segment.vertexCount)
            {
                error.Assign("FBX evaluated mesh instance changed its source topology");
                success = false;
                break;
            }
            const ufbx_vertex_vec3& positions = mesh->skinned_position.exists ? mesh->skinned_position : mesh->vertex_position;
            const ufbx_vertex_vec3& normals = mesh->skinned_normal.exists ? mesh->skinned_normal : mesh->vertex_normal;
            for (uint32_t corner = 0; corner < segment.vertexCount; ++corner)
            {
                ufbx_vec3 position{};
                ufbx_vec3 normal{};
                FDeformedVertex vertex{};
                if (!ReadAttribute(positions, corner, position) || !ReadAttribute(normals, corner, normal))
                {
                    error.Assign("FBX evaluated position or normal attribute is invalid");
                    success = false;
                    break;
                }
                if (mesh->skinned_is_local)
                {
                    if (!TransformPosition(node->geometry_to_world, position, vertex.position) || !TransformNormal(node->geometry_to_world, normal, vertex.normal))
                    {
                        error.Assign("FBX evaluated local vertex cannot use its node transform");
                        success = false;
                        break;
                    }
                }
                else
                {
                    vertex.position[0] = static_cast<float>(position.x);
                    vertex.position[1] = static_cast<float>(position.y);
                    vertex.position[2] = static_cast<float>(position.z);
                    const double x = static_cast<double>(normal.x);
                    const double y = static_cast<double>(normal.y);
                    const double z = static_cast<double>(normal.z);
                    const double lengthSquared = x * x + y * y + z * z;
                    if (!isfinite(lengthSquared) || lengthSquared <= 1.0e-30)
                    {
                        error.Assign("FBX evaluated world normal is degenerate");
                        success = false;
                        break;
                    }
                    const double inverseLength = 1.0 / sqrt(lengthSquared);
                    vertex.normal[0] = static_cast<float>(x * inverseLength);
                    vertex.normal[1] = static_cast<float>(y * inverseLength);
                    vertex.normal[2] = static_cast<float>(z * inverseLength);
                    if (!isfinite(vertex.position[0]) || !isfinite(vertex.position[1]) || !isfinite(vertex.position[2]) || !isfinite(vertex.normal[0]) || !isfinite(vertex.normal[1]) || !isfinite(vertex.normal[2]))
                    {
                        error.Assign("FBX evaluated world vertex is non-finite");
                        success = false;
                        break;
                    }
                }
                if (!stagedVertices.Append(vertex))
                {
                    error.Assign("FBX deformed vertex staging allocation failed");
                    success = false;
                    break;
                }
            }
        }
        if (success && stagedVertices.Count() != modelVertexCount_)
        {
            error.Assign("FBX deformed vertices do not cover the target model");
            success = false;
        }
        if (success)
        {
            for (uint32_t vertexIndex = 0; vertexIndex < modelVertexCount_; ++vertexIndex)
            {
                detail::ModelVertex& destination = output.vertices.At(vertexIndex);
                const FDeformedVertex& source = stagedVertices.At(vertexIndex);
                memcpy(destination.position, source.position, sizeof(destination.position));
                memcpy(destination.normal, source.normal, sizeof(destination.normal));
            }
            error.Clear();
        }
        ufbx_free_scene(evaluatedScene);
        ufbx_free_anim(poseAnimation);
        return success;
    }

  private:
    struct FFastInfluence
    {
        uint32_t clusterIndex;
        ufbx_real weight;
    };

    struct FFastInfluenceRange
    {
        uint32_t firstInfluence;
        uint32_t influenceCount;
    };

    struct FFastCluster
    {
        uint32_t nodeIndex;
        ufbx_matrix geometryToBone;
    };

    struct FFastSegment
    {
        uint32_t meshTypedId;
        uint32_t nodeTypedId;
        uint32_t firstSourceVertex;
        uint32_t sourceVertexCount;
        uint32_t firstCorner;
        uint32_t cornerCount;
        uint32_t firstCluster;
        uint32_t firstNormal;
        uint32_t normalCount;
        uint32_t firstSparseNormal;
        uint32_t sparseNormalCount;
        bool hasSkin;
        bool generateNormals;
    };

    struct FFastCache
    {
        Array<FFastSegment> segments;
        Array<FFastInfluence> influences;
        Array<FFastInfluenceRange> influenceRanges;
        Array<FFastCluster> clusters;
        Array<ufbx_vec3> sourcePositions;
        Array<uint32_t> cornerSourceVertices;
        Array<uint32_t> cornerNormalIndices;
        Array<animation::FModelSparseVertexMap> sparseVertexMap;
        animation::FModelGpuSkinningGeometry gpuGeometry;
    };

    /**
     * 単純な親子変換と線形skinだけを高速経路へ登録する。
     */
    void BuildFastSkinningCache()
    {
        if (!scene_ || segments_.Count() == 0)
        {
            return;
        }
        FFastCache cache;
        Array<ufbx_topo_edge> topology;
        Array<uint32_t> normalIndices;
        uint32_t normalValueCount = 0;
        uint32_t sparseNormalValueCount = 0;
        for (uint32_t segmentIndex = 0; segmentIndex < segments_.Count(); ++segmentIndex)
        {
            const detail::FFbxGeometrySegment& mapping = segments_.At(segmentIndex);
            if (mapping.meshTypedId >= scene_->meshes.count || mapping.nodeTypedId >= scene_->nodes.count)
            {
                return;
            }
            const ufbx_mesh* mesh = scene_->meshes.data[mapping.meshTypedId];
            const ufbx_node* node = scene_->nodes.data[mapping.nodeTypedId];
            if (!mesh || !node || mesh->skin_deformers.count > 1 || mesh->num_vertices > UINT32_MAX || mesh->num_indices > UINT32_MAX || mesh->vertices.count != mesh->num_vertices || mesh->vertex_indices.count != mesh->num_indices || mesh->instances.count != 1 || !HasOrdinaryInheritance(node))
            {
                return;
            }
            const ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
            if (skin && (skin->skinning_method != UFBX_SKINNING_METHOD_LINEAR || skin->vertices.count != mesh->num_vertices || (skin->vertices.count && !skin->vertices.data) || (skin->weights.count && !skin->weights.data) || !skin->clusters.data || skin->clusters.count == 0 || skin->clusters.count > UINT32_MAX))
            {
                return;
            }
            if (mesh->num_vertices > UINT32_MAX - cache.sourcePositions.Count() || mesh->num_vertices > UINT32_MAX - cache.influenceRanges.Count() || mesh->num_indices > UINT32_MAX - cache.cornerSourceVertices.Count() || mesh->num_indices > UINT32_MAX - cache.cornerNormalIndices.Count())
            {
                return;
            }

            FFastSegment fastSegment{};
            fastSegment.meshTypedId = mapping.meshTypedId;
            fastSegment.nodeTypedId = mapping.nodeTypedId;
            fastSegment.firstSourceVertex = cache.sourcePositions.Count();
            fastSegment.sourceVertexCount = static_cast<uint32_t>(mesh->num_vertices);
            fastSegment.firstCorner = cache.cornerSourceVertices.Count();
            fastSegment.cornerCount = static_cast<uint32_t>(mesh->num_indices);
            fastSegment.firstCluster = cache.clusters.Count();
            fastSegment.firstNormal = normalValueCount;
            fastSegment.firstSparseNormal = sparseNormalValueCount;
            fastSegment.hasSkin = skin != nullptr;
            fastSegment.generateNormals = skin != nullptr || mesh->blend_deformers.count != 0;
            if (skin)
            {
                for (size_t clusterIndex = 0; clusterIndex < skin->clusters.count; ++clusterIndex)
                {
                    const ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
                    if (!cluster || !cluster->bone_node || cluster->bone_node->typed_id >= scene_->nodes.count || !HasOrdinaryInheritance(cluster->bone_node) || !cache.clusters.Append({ cluster->bone_node->typed_id, cluster->geometry_to_bone }))
                    {
                        return;
                    }
                }
            }
            animation::FModelGpuSkinningGeometry::FSegment gpuSegment{};
            gpuSegment.firstPosition = cache.gpuGeometry.positions.Count();
            gpuSegment.positionCount = fastSegment.sourceVertexCount;
            gpuSegment.firstCluster = cache.gpuGeometry.clusters.Count();
            gpuSegment.clusterCount = skin ? static_cast<uint32_t>(skin->clusters.count) + 1 : 1;
            gpuSegment.nodeIndex = fastSegment.nodeTypedId;
            gpuSegment.firstFace = cache.gpuGeometry.faces.Count();
            const ufbx_node* geometryNode = scene_->nodes.data[fastSegment.nodeTypedId];
            for (uint32_t element = 0; element < 12; ++element)
            {
                gpuSegment.geometryToNode[element] = geometryNode->geometry_to_node.v[element];
                if (!isfinite(gpuSegment.geometryToNode[element]))
                {
                    return;
                }
            }
            if (skin)
            {
                for (uint32_t clusterOffset = 0; clusterOffset < static_cast<uint32_t>(skin->clusters.count); ++clusterOffset)
                {
                    const FFastCluster& sourceCluster = cache.clusters.At(fastSegment.firstCluster + clusterOffset);
                    animation::FModelGpuSkinningGeometry::FCluster gpuCluster{};
                    gpuCluster.nodeIndex = sourceCluster.nodeIndex;
                    for (uint32_t element = 0; element < 12; ++element)
                    {
                        gpuCluster.geometryToBone[element] = sourceCluster.geometryToBone.v[element];
                        if (!isfinite(gpuCluster.geometryToBone[element]))
                        {
                            return;
                        }
                    }
                    if (!cache.gpuGeometry.clusters.Append(gpuCluster))
                    {
                        return;
                    }
                }
            }
            animation::FModelGpuSkinningGeometry::FCluster fallbackCluster{};
            fallbackCluster.nodeIndex = fastSegment.nodeTypedId;
            memcpy(fallbackCluster.geometryToBone, gpuSegment.geometryToNode, sizeof(fallbackCluster.geometryToBone));
            if (!cache.gpuGeometry.clusters.Append(fallbackCluster))
            {
                return;
            }
            if (!cache.sourcePositions.AppendRange(mesh->vertices.data, fastSegment.sourceVertexCount))
            {
                return;
            }
            for (uint32_t vertexIndex = 0; vertexIndex < fastSegment.sourceVertexCount; ++vertexIndex)
            {
                const ufbx_vec3& sourcePosition = mesh->vertices.data[vertexIndex];
                animation::FModelGpuSkinningGeometry::FPosition gpuPosition{};
                gpuPosition.value[0] = sourcePosition.x;
                gpuPosition.value[1] = sourcePosition.y;
                gpuPosition.value[2] = sourcePosition.z;
                if (!isfinite(gpuPosition.value[0]) || !isfinite(gpuPosition.value[1]) || !isfinite(gpuPosition.value[2]) || !cache.gpuGeometry.positions.Append(gpuPosition))
                {
                    return;
                }
            }
            for (uint32_t vertexIndex = 0; vertexIndex < fastSegment.sourceVertexCount; ++vertexIndex)
            {
                FFastInfluenceRange range{};
                range.firstInfluence = cache.influences.Count();
                if (skin)
                {
                    const ufbx_skin_vertex& skinVertex = skin->vertices.data[vertexIndex];
                    if (skinVertex.dq_weight != 0.0f || skinVertex.weight_begin > skin->weights.count || skinVertex.num_weights > skin->weights.count - skinVertex.weight_begin || cache.influences.Count() > UINT32_MAX - skinVertex.num_weights)
                    {
                        return;
                    }
                    range.influenceCount = skinVertex.num_weights;
                    for (uint32_t influenceIndex = 0; influenceIndex < skinVertex.num_weights; ++influenceIndex)
                    {
                        const ufbx_skin_weight& weight = skin->weights.data[skinVertex.weight_begin + influenceIndex];
                        if (weight.cluster_index >= skin->clusters.count || !isfinite(static_cast<double>(weight.weight)) || weight.weight < 0.0f || !cache.influences.Append({ fastSegment.firstCluster + weight.cluster_index, weight.weight }))
                        {
                            return;
                        }
                    }
                }
                if (!cache.influenceRanges.Append(range))
                {
                    return;
                }
                animation::FModelGpuSkinningGeometry::FInfluenceRange gpuRange{};
                gpuRange.firstInfluence = cache.gpuGeometry.influences.Count();
                ufbx_real totalWeight = 0.0f;
                for (uint32_t influenceIndex = 0; influenceIndex < range.influenceCount; ++influenceIndex)
                {
                    const FFastInfluence& influence = cache.influences.At(range.firstInfluence + influenceIndex);
                    totalWeight += influence.weight;
                    animation::FModelGpuSkinningGeometry::FInfluence gpuInfluence{};
                    gpuInfluence.clusterIndex = gpuSegment.firstCluster + (influence.clusterIndex - fastSegment.firstCluster);
                    gpuInfluence.weight = influence.weight;
                    if (!isfinite(gpuInfluence.weight) || !cache.gpuGeometry.influences.Append(gpuInfluence))
                    {
                        return;
                    }
                }
                if (totalWeight <= 0.0f)
                {
                    gpuRange.firstInfluence = cache.gpuGeometry.influences.Count();
                    animation::FModelGpuSkinningGeometry::FInfluence fallbackInfluence{};
                    fallbackInfluence.clusterIndex = gpuSegment.firstCluster + gpuSegment.clusterCount - 1;
                    fallbackInfluence.weight = 1.0f;
                    if (!cache.gpuGeometry.influences.Append(fallbackInfluence))
                    {
                        return;
                    }
                    gpuRange.influenceCount = 1;
                }
                else
                {
                    gpuRange.influenceCount = range.influenceCount;
                    const double epsilon = sizeof(ufbx_real) == sizeof(float) ? 1.0842021795674597e-19 : 1.4916681462400413e-154;
                    if (fabs(static_cast<double>(totalWeight) - 1.0) > epsilon)
                    {
                        const double reciprocalWeight = fabs(static_cast<double>(totalWeight)) > epsilon ? 1.0 / static_cast<double>(totalWeight) : 0.0;
                        for (uint32_t influenceIndex = 0; influenceIndex < gpuRange.influenceCount; ++influenceIndex)
                        {
                            cache.gpuGeometry.influences.At(gpuRange.firstInfluence + influenceIndex).weight *= reciprocalWeight;
                        }
                    }
                }
                if (!cache.gpuGeometry.influenceRanges.Append(gpuRange))
                {
                    return;
                }
            }
            for (uint32_t corner = 0; corner < fastSegment.cornerCount; ++corner)
            {
                const uint32_t sourceVertex = mesh->vertex_indices.data[corner];
                if (sourceVertex >= fastSegment.sourceVertexCount || !cache.cornerSourceVertices.Append(fastSegment.firstSourceVertex + sourceVertex))
                {
                    return;
                }
            }

            if (fastSegment.generateNormals)
            {
                topology.Clear();
                normalIndices.Clear();
                if (!topology.Reserve(fastSegment.cornerCount) || !normalIndices.Reserve(fastSegment.cornerCount))
                {
                    return;
                }
                ufbx_compute_topology(mesh, topology.Data(), fastSegment.cornerCount);
                const size_t normalCount = ufbx_generate_normal_mapping(mesh, topology.Data(), fastSegment.cornerCount, normalIndices.Data(), fastSegment.cornerCount, false);
                if (normalCount == 0 || normalCount > UINT32_MAX || normalCount > UINT32_MAX - normalValueCount)
                {
                    return;
                }
                fastSegment.normalCount = static_cast<uint32_t>(normalCount);
                normalValueCount += fastSegment.normalCount;
                fastSegment.sparseNormalCount = fastSegment.normalCount;
                sparseNormalValueCount += fastSegment.sparseNormalCount;
                for (uint32_t corner = 0; corner < fastSegment.cornerCount; ++corner)
                {
                    // ufbxが作ったcornerからnormalの番号を取得する。
                    const uint32_t normalIndex = normalIndices.Data()[corner];
                    if (normalIndex >= fastSegment.normalCount || !cache.cornerNormalIndices.Append(normalIndex))
                    {
                        return;
                    }
                }
            }
            else
            {
                if (fastSegment.cornerCount > UINT32_MAX - sparseNormalValueCount)
                {
                    return;
                }
                fastSegment.sparseNormalCount = fastSegment.cornerCount;
                sparseNormalValueCount += fastSegment.sparseNormalCount;
                for (uint32_t corner = 0; corner < fastSegment.cornerCount; ++corner)
                {
                    if (!cache.cornerNormalIndices.Append(corner))
                    {
                        return;
                    }
                }
            }
            for (uint32_t corner = 0; corner < fastSegment.cornerCount; ++corner)
            {
                // 描画cornerをunique位置とnormal groupへ固定対応する。
                animation::FModelSparseVertexMap vertexMap{};
                vertexMap.positionIndex = cache.cornerSourceVertices.At(fastSegment.firstCorner + corner);
                vertexMap.normalIndex = fastSegment.firstSparseNormal + cache.cornerNormalIndices.At(fastSegment.firstCorner + corner);
                if (!cache.sparseVertexMap.Append(vertexMap))
                {
                    return;
                }
            }
            if (mesh->num_faces > UINT32_MAX - cache.gpuGeometry.faces.Count() || (mesh->num_faces && !mesh->faces.data))
            {
                return;
            }
            for (uint32_t faceIndex = 0; faceIndex < static_cast<uint32_t>(mesh->num_faces); ++faceIndex)
            {
                const ufbx_face& sourceFace = mesh->faces.data[faceIndex];
                if (sourceFace.num_indices < 3 || sourceFace.index_begin > fastSegment.cornerCount || sourceFace.num_indices > fastSegment.cornerCount - sourceFace.index_begin || cache.gpuGeometry.corners.Count() > UINT32_MAX - sourceFace.num_indices)
                {
                    return;
                }
                animation::FModelGpuSkinningGeometry::FFace gpuFace{};
                gpuFace.firstCorner = cache.gpuGeometry.corners.Count();
                gpuFace.cornerCount = sourceFace.num_indices;
                for (uint32_t faceCorner = 0; faceCorner < sourceFace.num_indices; ++faceCorner)
                {
                    const uint32_t segmentCorner = sourceFace.index_begin + faceCorner;
                    animation::FModelGpuSkinningGeometry::FCorner gpuCorner{};
                    gpuCorner.positionIndex = cache.gpuGeometry.positions.Count() - fastSegment.sourceVertexCount + mesh->vertex_indices.data[segmentCorner];
                    gpuCorner.normalGroupIndex = fastSegment.firstSparseNormal + cache.cornerNormalIndices.At(fastSegment.firstCorner + segmentCorner);
                    if (!cache.gpuGeometry.corners.Append(gpuCorner))
                    {
                        return;
                    }
                }
                if (!cache.gpuGeometry.faces.Append(gpuFace))
                {
                    return;
                }
            }
            gpuSegment.faceCount = cache.gpuGeometry.faces.Count() - gpuSegment.firstFace;
            if (!cache.gpuGeometry.segments.Append(gpuSegment))
            {
                return;
            }
            if (!cache.segments.Append(fastSegment))
            {
                return;
            }
        }
        Array<animation::FModelGpuSkinningGeometry::FNormalGroupRange> gpuNormalRanges;
        Array<uint32_t> gpuNormalCounts;
        Array<uint32_t> gpuNormalCursors;
        if (!gpuNormalRanges.Reserve(sparseNormalValueCount) || !gpuNormalCounts.Reserve(sparseNormalValueCount) || !gpuNormalCursors.Reserve(sparseNormalValueCount))
        {
            return;
        }
        for (uint32_t normalIndex = 0; normalIndex < sparseNormalValueCount; ++normalIndex)
        {
            if (!gpuNormalRanges.Append({}) || !gpuNormalCounts.Append(0) || !gpuNormalCursors.Append(0))
            {
                return;
            }
        }
        for (uint32_t faceIndex = 0; faceIndex < cache.gpuGeometry.faces.Count(); ++faceIndex)
        {
            const animation::FModelGpuSkinningGeometry::FFace& face = cache.gpuGeometry.faces.At(faceIndex);
            for (uint32_t corner = 0; corner < face.cornerCount; ++corner)
            {
                const uint32_t normalIndex = cache.gpuGeometry.corners.At(face.firstCorner + corner).normalGroupIndex;
                if (normalIndex >= gpuNormalCounts.Count() || gpuNormalCounts.At(normalIndex) == UINT32_MAX)
                {
                    return;
                }
                ++gpuNormalCounts.At(normalIndex);
            }
        }
        uint32_t totalIncidences = 0;
        for (uint32_t normalIndex = 0; normalIndex < sparseNormalValueCount; ++normalIndex)
        {
            auto& range = gpuNormalRanges.At(normalIndex);
            range.firstFace = totalIncidences;
            range.faceCount = gpuNormalCounts.At(normalIndex);
            gpuNormalCursors.At(normalIndex) = totalIncidences;
            if (range.faceCount > UINT32_MAX - totalIncidences)
            {
                return;
            }
            totalIncidences += range.faceCount;
        }
        if (!cache.gpuGeometry.normalFaceIds.Reserve(totalIncidences))
        {
            return;
        }
        for (uint32_t incidence = 0; incidence < totalIncidences; ++incidence)
        {
            if (!cache.gpuGeometry.normalFaceIds.Append(0))
            {
                return;
            }
        }
        for (uint32_t faceIndex = 0; faceIndex < cache.gpuGeometry.faces.Count(); ++faceIndex)
        {
            const animation::FModelGpuSkinningGeometry::FFace& face = cache.gpuGeometry.faces.At(faceIndex);
            for (uint32_t corner = 0; corner < face.cornerCount; ++corner)
            {
                const uint32_t normalIndex = cache.gpuGeometry.corners.At(face.firstCorner + corner).normalGroupIndex;
                const uint32_t incidence = gpuNormalCursors.At(normalIndex)++;
                cache.gpuGeometry.normalFaceIds.At(incidence) = faceIndex;
            }
        }
        cache.gpuGeometry.normalGroupRanges.MoveFrom(gpuNormalRanges);
        Array<uint8_t> usedNormalIndices;
        Array<uint32_t> referencedNormalIndices;
        if (cache.sparseVertexMap.Count() != modelVertexCount_ || !usedNormalIndices.Reserve(sparseNormalValueCount))
        {
            return;
        }
        for (uint32_t normalIndex = 0; normalIndex < sparseNormalValueCount; ++normalIndex)
        {
            if (!usedNormalIndices.Append(0))
            {
                return;
            }
        }
        for (uint32_t vertexIndex = 0; vertexIndex < cache.sparseVertexMap.Count(); ++vertexIndex)
        {
            const uint32_t normalIndex = cache.sparseVertexMap.At(vertexIndex).normalIndex;
            if (normalIndex >= sparseNormalValueCount)
            {
                return;
            }
            if (!usedNormalIndices.At(normalIndex))
            {
                usedNormalIndices.At(normalIndex) = 1;
                if (!referencedNormalIndices.Append(normalIndex))
                {
                    return;
                }
            }
        }
        fastSegments_.MoveFrom(cache.segments);
        fastInfluences_.MoveFrom(cache.influences);
        fastInfluenceRanges_.MoveFrom(cache.influenceRanges);
        fastClusters_.MoveFrom(cache.clusters);
        fastSourcePositions_.MoveFrom(cache.sourcePositions);
        fastCornerSourceVertices_.MoveFrom(cache.cornerSourceVertices);
        fastCornerNormalIndices_.MoveFrom(cache.cornerNormalIndices);
        fastSparseVertexMap_.MoveFrom(cache.sparseVertexMap);
        fastGpuGeometry_.positions.MoveFrom(cache.gpuGeometry.positions);
        fastGpuGeometry_.influenceRanges.MoveFrom(cache.gpuGeometry.influenceRanges);
        fastGpuGeometry_.influences.MoveFrom(cache.gpuGeometry.influences);
        fastGpuGeometry_.clusters.MoveFrom(cache.gpuGeometry.clusters);
        fastGpuGeometry_.faces.MoveFrom(cache.gpuGeometry.faces);
        fastGpuGeometry_.corners.MoveFrom(cache.gpuGeometry.corners);
        fastGpuGeometry_.normalGroupRanges.MoveFrom(cache.gpuGeometry.normalGroupRanges);
        fastGpuGeometry_.normalFaceIds.MoveFrom(cache.gpuGeometry.normalFaceIds);
        fastGpuGeometry_.segments.MoveFrom(cache.gpuGeometry.segments);
        fastReferencedNormalIndices_.MoveFrom(referencedNormalIndices);
        fastNormalValueCount_ = normalValueCount;
        fastSparseNormalValueCount_ = sparseNormalValueCount;
        fastDeformSupported_ = fastSegments_.Count() == segments_.Count();
        fastGpuSkinningSupported_ = fastDeformSupported_;
        for (uint32_t segmentIndex = 0; segmentIndex < fastSegments_.Count(); ++segmentIndex)
        {
            fastGpuSkinningSupported_ = fastGpuSkinningSupported_ && fastSegments_.At(segmentIndex).generateNormals;
        }
    }

    /**
     * 祖先を含め、通常の親子行列で表せるnodeかを調べる。
     */
    bool HasOrdinaryInheritance(const ufbx_node* node) const
    {
        for (const ufbx_node* current = node; current; current = current->parent)
        {
            if (current->inherit_mode != UFBX_INHERIT_MODE_NORMAL)
            {
                return false;
            }
        }
        return true;
    }

    /**
     * morph係数がすべて0なら線形skinの専用経路を選べる。
     */
    bool IsFastPose(const animation::FModelPose& pose) const
    {
        for (uint32_t morph = 0; morph < pose.morphWeights.Count(); ++morph)
        {
            if (pose.morphWeights.At(morph) != 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    /**
     * unique位置とnormal groupを計算し、呼出し側のsparse配列を原子的に置き換える。
     */
    bool DeformSparseLinearSkin(const animation::FModelPose& pose, animation::FModelSparsePoseGeometry& output, String& error) const
    {
        using FSparseVector = animation::FModelSparsePoseGeometry::FModelVector4;
        struct FSparseNormalStorage
        {
            // 呼出し内の全normal値を一時保持する領域。
            ufbx_vec3* values;
            ~FSparseNormalStorage()
            {
                if (values)
                {
                    Deallocate(values);
                }
            }
        };
#ifdef GKCORE_TESTING
        memset(&lastFbxDeformProfile, 0, sizeof(lastFbxDeformProfile));
        const int64_t scratchBegin = FbxTestCounter();
#endif
        Array<ufbx_matrix> nodeWorldMatrices;
        Array<ufbx_matrix> clusterWorldMatrices;
        Array<ufbx_vec3> deformedPositions;
        if (fastSparseNormalValueCount_ > SIZE_MAX / sizeof(ufbx_vec3))
        {
            error.Assign("FBX sparse normal scratch size overflow");
            return false;
        }
        const size_t normalBytes = static_cast<size_t>(fastSparseNormalValueCount_) * sizeof(ufbx_vec3);
        FSparseNormalStorage deformedNormals{ fastSparseNormalValueCount_ ? static_cast<ufbx_vec3*>(Allocate(normalBytes)) : nullptr };
        if ((fastSparseNormalValueCount_ && !deformedNormals.values) || !nodeWorldMatrices.Reserve(skeleton_.parents.Count()) || !clusterWorldMatrices.Reserve(fastClusters_.Count()) || !deformedPositions.Reserve(fastSourcePositions_.Count()))
        {
            error.Assign("FBX sparse deformation scratch allocation failed");
            return false;
        }
#ifdef GKCORE_TESTING
        const int64_t scratchEnd = FbxTestCounter();
        lastFbxDeformProfile.milliseconds[0] = FbxTestMilliseconds(scratchBegin, scratchEnd);
        lastFbxDeformProfile.counts[0] = skeleton_.parents.Count();
        lastFbxDeformProfile.counts[1] = fastClusters_.Count();
        lastFbxDeformProfile.counts[2] = fastInfluences_.Count();
        lastFbxDeformProfile.counts[3] = fastSourcePositions_.Count();
        lastFbxDeformProfile.counts[4] = fastCornerSourceVertices_.Count();
        lastFbxDeformProfile.counts[5] = fastSparseNormalValueCount_;
        const int64_t nodeBegin = FbxTestCounter();
#endif
        for (uint32_t nodeIndex = 0; nodeIndex < skeleton_.parents.Count(); ++nodeIndex)
        {
            const ufbx_node* node = scene_->nodes.data[nodeIndex];
            if (node->is_root && !IsSameTransform(pose.localTransforms.At(nodeIndex), skeleton_.restLocalTransforms.At(nodeIndex)))
            {
                error.Assign("FBX implicit root transform cannot be overridden");
                return false;
            }
            ufbx_transform localTransform{};
            if (!CopyTransform(pose.localTransforms.At(nodeIndex), localTransform))
            {
                error.Assign("FBX sparse pose has an invalid node transform");
                return false;
            }
            const ufbx_matrix localMatrix = ufbx_transform_to_matrix(&localTransform);
            const int32_t parentIndex = skeleton_.parents.At(nodeIndex);
            const ufbx_matrix worldMatrix = parentIndex >= 0 ? ufbx_matrix_mul(&nodeWorldMatrices.At(static_cast<uint32_t>(parentIndex)), &localMatrix) : localMatrix;
            for (uint32_t element = 0; element < 12; ++element)
            {
                if (!isfinite(static_cast<double>(worldMatrix.v[element])))
                {
                    error.Assign("FBX sparse pose produced a non-finite node matrix");
                    return false;
                }
            }
            if (!nodeWorldMatrices.Append(worldMatrix))
            {
                error.Assign("FBX sparse node matrix allocation failed");
                return false;
            }
        }
#ifdef GKCORE_TESTING
        const int64_t nodeEnd = FbxTestCounter();
        lastFbxDeformProfile.milliseconds[1] = FbxTestMilliseconds(nodeBegin, nodeEnd);
        const int64_t clusterBegin = FbxTestCounter();
#endif
        for (uint32_t clusterIndex = 0; clusterIndex < fastClusters_.Count(); ++clusterIndex)
        {
            const FFastCluster& cluster = fastClusters_.At(clusterIndex);
            const ufbx_matrix matrix = ufbx_matrix_mul(&nodeWorldMatrices.At(cluster.nodeIndex), &cluster.geometryToBone);
            for (uint32_t element = 0; element < 12; ++element)
            {
                if (!isfinite(static_cast<double>(matrix.v[element])))
                {
                    error.Assign("FBX sparse pose produced a non-finite skin matrix");
                    return false;
                }
            }
            if (!clusterWorldMatrices.Append(matrix))
            {
                error.Assign("FBX sparse skin matrix allocation failed");
                return false;
            }
        }
#ifdef GKCORE_TESTING
        const int64_t clusterEnd = FbxTestCounter();
        lastFbxDeformProfile.milliseconds[2] = FbxTestMilliseconds(clusterBegin, clusterEnd);
#endif
        for (uint32_t segmentIndex = 0; segmentIndex < fastSegments_.Count(); ++segmentIndex)
        {
            const FFastSegment& segment = fastSegments_.At(segmentIndex);
            const ufbx_mesh* mesh = scene_->meshes.data[segment.meshTypedId];
            const ufbx_node* node = scene_->nodes.data[segment.nodeTypedId];
            const ufbx_matrix fallback = ufbx_matrix_mul(&nodeWorldMatrices.At(segment.nodeTypedId), &node->geometry_to_node);
            // 各segmentはnormal storageの連続範囲を全件書き込み、生成normalはufbxが全件を初期化する。
#ifdef GKCORE_TESTING
            const int64_t positionBegin = FbxTestCounter();
#endif
            for (uint32_t vertexIndex = 0; vertexIndex < segment.sourceVertexCount; ++vertexIndex)
            {
                const FFastInfluenceRange& range = fastInfluenceRanges_.At(segment.firstSourceVertex + vertexIndex);
                ufbx_matrix skinMatrix{};
                ufbx_real totalWeight = 0.0f;
                for (uint32_t influenceIndex = 0; influenceIndex < range.influenceCount; ++influenceIndex)
                {
                    const FFastInfluence& influence = fastInfluences_.At(range.firstInfluence + influenceIndex);
                    const ufbx_matrix& matrix = clusterWorldMatrices.At(influence.clusterIndex);
                    totalWeight += influence.weight;
                    for (uint32_t element = 0; element < 12; ++element)
                    {
                        skinMatrix.v[element] += matrix.v[element] * influence.weight;
                    }
                }
                if (totalWeight <= 0.0f)
                {
                    skinMatrix = fallback;
                }
                else
                {
                    const double epsilon = sizeof(ufbx_real) == sizeof(float) ? 1.0842021795674597e-19 : 1.4916681462400413e-154;
                    if (fabs(static_cast<double>(totalWeight) - 1.0) > epsilon)
                    {
                        const ufbx_real reciprocalWeight = fabs(static_cast<double>(totalWeight)) > epsilon ? static_cast<ufbx_real>(1.0 / totalWeight) : 0.0f;
                        for (uint32_t element = 0; element < 12; ++element)
                        {
                            skinMatrix.v[element] *= reciprocalWeight;
                        }
                    }
                }
                const ufbx_vec3 position = ufbx_transform_position(&skinMatrix, fastSourcePositions_.At(segment.firstSourceVertex + vertexIndex));
                if (!isfinite(static_cast<float>(position.x)) || !isfinite(static_cast<float>(position.y)) || !isfinite(static_cast<float>(position.z)) || !deformedPositions.Append(position))
                {
                    error.Assign("FBX sparse skin position is non-finite or out of memory");
                    return false;
                }
            }
#ifdef GKCORE_TESTING
            const int64_t positionEnd = FbxTestCounter();
            lastFbxDeformProfile.milliseconds[3] += FbxTestMilliseconds(positionBegin, positionEnd);
            const int64_t normalBegin = FbxTestCounter();
#endif
            if (segment.generateNormals)
            {
                ufbx_vertex_vec3 positions{};
                positions.exists = true;
                positions.values.data = deformedPositions.Data() + segment.firstSourceVertex;
                positions.values.count = segment.sourceVertexCount;
                positions.indices = mesh->vertex_indices;
                positions.value_reals = 3;
                positions.unique_per_vertex = true;
                ufbx_compute_normals(mesh, &positions, fastCornerNormalIndices_.Data() + segment.firstCorner, segment.cornerCount, deformedNormals.values + segment.firstSparseNormal, segment.sparseNormalCount);
            }
            else
            {
                for (uint32_t corner = 0; corner < segment.cornerCount; ++corner)
                {
                    ufbx_vec3 sourceNormal{};
                    float transformedNormal[3]{};
                    if (!ReadAttribute(mesh->vertex_normal, corner, sourceNormal) || !TransformNormal(fallback, sourceNormal, transformedNormal))
                    {
                        error.Assign("FBX sparse static normal cannot use its node transform");
                        return false;
                    }
                    const uint32_t normalIndex = segment.firstSparseNormal + corner;
                    deformedNormals.values[normalIndex] = { transformedNormal[0], transformedNormal[1], transformedNormal[2] };
                }
            }
#ifdef GKCORE_TESTING
            const int64_t normalEnd = FbxTestCounter();
            lastFbxDeformProfile.milliseconds[4] += FbxTestMilliseconds(normalBegin, normalEnd);
#endif
        }
        for (uint32_t normalIndex = 0; normalIndex < fastReferencedNormalIndices_.Count(); ++normalIndex)
        {
            const ufbx_vec3& normal = deformedNormals.values[fastReferencedNormalIndices_.At(normalIndex)];
            const double x = static_cast<double>(normal.x);
            const double y = static_cast<double>(normal.y);
            const double z = static_cast<double>(normal.z);
            const double lengthSquared = x * x + y * y + z * z;
            if (!isfinite(lengthSquared) || lengthSquared <= 1.0e-30 || !isfinite(static_cast<float>(normal.x)) || !isfinite(static_cast<float>(normal.y)) || !isfinite(static_cast<float>(normal.z)))
            {
                error.Assign("FBX sparse deformation produced a degenerate normal");
                return false;
            }
        }
#ifdef GKCORE_TESTING
        const int64_t conversionBegin = FbxTestCounter();
#endif
        if (!output.positions.Reserve(deformedPositions.Count()) || !output.normals.Reserve(fastSparseNormalValueCount_))
        {
            error.Assign("FBX sparse output allocation failed");
            return false;
        }
        // 容量を確保し全ての有限値検査を終えてから、失敗しない追記だけで置き換える。
        output.positions.Clear();
        output.normals.Clear();
        for (uint32_t positionIndex = 0; positionIndex < deformedPositions.Count(); ++positionIndex)
        {
            const ufbx_vec3& sourcePosition = deformedPositions.At(positionIndex);
            const FSparseVector position = { { static_cast<float>(sourcePosition.x), static_cast<float>(sourcePosition.y), static_cast<float>(sourcePosition.z), 1.0f } };
            output.positions.Append(position);
        }
        for (uint32_t normalIndex = 0; normalIndex < fastSparseNormalValueCount_; ++normalIndex)
        {
            const ufbx_vec3& sourceNormal = deformedNormals.values[normalIndex];
            const FSparseVector normal = { { static_cast<float>(sourceNormal.x), static_cast<float>(sourceNormal.y), static_cast<float>(sourceNormal.z), 0.0f } };
            output.normals.Append(normal);
        }
#ifdef GKCORE_TESTING
        const int64_t conversionEnd = FbxTestCounter();
        lastFbxDeformProfile.milliseconds[5] = FbxTestMilliseconds(conversionBegin, conversionEnd);
        lastFbxDeformProfile.milliseconds[6] = 0.0;
#endif
        error.Clear();
        return true;
    }

    /**
     * retained influenceと共通poseから位置と生成法線を計算する。
     */
    bool DeformLinearSkin(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const
    {
#ifdef GKCORE_TESTING
        memset(&lastFbxDeformProfile, 0, sizeof(lastFbxDeformProfile));
        const int64_t scratchBegin = FbxTestCounter();
#endif
        struct FDeformedVertex
        {
            float position[3];
            float normal[3];
        };
        Array<ufbx_matrix> nodeWorldMatrices;
        Array<ufbx_matrix> clusterWorldMatrices;
        Array<ufbx_vec3> deformedPositions;
        Array<ufbx_vec3> deformedNormals;
        Array<FDeformedVertex> stagedVertices;
        if (!nodeWorldMatrices.Reserve(skeleton_.parents.Count()) || !clusterWorldMatrices.Reserve(fastClusters_.Count()) || !deformedPositions.Reserve(fastSourcePositions_.Count()) || !deformedNormals.Reserve(fastNormalValueCount_) || !stagedVertices.Reserve(modelVertexCount_))
        {
            error.Assign("FBX linear skinning scratch allocation failed");
            return false;
        }
        for (uint32_t normalIndex = 0; normalIndex < fastNormalValueCount_; ++normalIndex)
        {
            if (!deformedNormals.Append(ufbx_zero_vec3))
            {
                error.Assign("FBX generated-normal staging allocation failed");
                return false;
            }
        }
#ifdef GKCORE_TESTING
        const int64_t scratchEnd = FbxTestCounter();
        lastFbxDeformProfile.milliseconds[0] = FbxTestMilliseconds(scratchBegin, scratchEnd);
        lastFbxDeformProfile.counts[0] = skeleton_.parents.Count();
        lastFbxDeformProfile.counts[1] = fastClusters_.Count();
        lastFbxDeformProfile.counts[2] = fastInfluences_.Count();
        lastFbxDeformProfile.counts[3] = fastSourcePositions_.Count();
        lastFbxDeformProfile.counts[4] = fastCornerSourceVertices_.Count();
        lastFbxDeformProfile.counts[5] = fastNormalValueCount_;
        const int64_t nodeBegin = FbxTestCounter();
#endif
        for (uint32_t nodeIndex = 0; nodeIndex < skeleton_.parents.Count(); ++nodeIndex)
        {
            const ufbx_node* node = scene_->nodes.data[nodeIndex];
            if (node->is_root && !IsSameTransform(pose.localTransforms.At(nodeIndex), skeleton_.restLocalTransforms.At(nodeIndex)))
            {
                error.Assign("FBX implicit root transform cannot be overridden");
                return false;
            }
            ufbx_transform localTransform{};
            if (!CopyTransform(pose.localTransforms.At(nodeIndex), localTransform))
            {
                error.Assign("FBX deformation pose has an invalid node transform");
                return false;
            }
            const ufbx_matrix localMatrix = ufbx_transform_to_matrix(&localTransform);
            const int32_t parentIndex = skeleton_.parents.At(nodeIndex);
            const ufbx_matrix worldMatrix = parentIndex >= 0 ? ufbx_matrix_mul(&nodeWorldMatrices.At(static_cast<uint32_t>(parentIndex)), &localMatrix) : localMatrix;
            for (uint32_t element = 0; element < 12; ++element)
            {
                if (!isfinite(static_cast<double>(worldMatrix.v[element])))
                {
                    error.Assign("FBX pose produced a non-finite node matrix");
                    return false;
                }
            }
            if (!nodeWorldMatrices.Append(worldMatrix))
            {
                error.Assign("FBX node matrix staging allocation failed");
                return false;
            }
        }
#ifdef GKCORE_TESTING
        const int64_t nodeEnd = FbxTestCounter();
        lastFbxDeformProfile.milliseconds[1] = FbxTestMilliseconds(nodeBegin, nodeEnd);
        const int64_t clusterBegin = FbxTestCounter();
#endif
        for (uint32_t clusterIndex = 0; clusterIndex < fastClusters_.Count(); ++clusterIndex)
        {
            const FFastCluster& cluster = fastClusters_.At(clusterIndex);
            const ufbx_matrix matrix = ufbx_matrix_mul(&nodeWorldMatrices.At(cluster.nodeIndex), &cluster.geometryToBone);
            for (uint32_t element = 0; element < 12; ++element)
            {
                if (!isfinite(static_cast<double>(matrix.v[element])))
                {
                    error.Assign("FBX pose produced a non-finite skin matrix");
                    return false;
                }
            }
            if (!clusterWorldMatrices.Append(matrix))
            {
                error.Assign("FBX skin matrix staging allocation failed");
                return false;
            }
        }
#ifdef GKCORE_TESTING
        const int64_t clusterEnd = FbxTestCounter();
        lastFbxDeformProfile.milliseconds[2] = FbxTestMilliseconds(clusterBegin, clusterEnd);
        int64_t positionBegin = FbxTestCounter();
#endif
        for (uint32_t segmentIndex = 0; segmentIndex < fastSegments_.Count(); ++segmentIndex)
        {
            const FFastSegment& segment = fastSegments_.At(segmentIndex);
            const ufbx_mesh* mesh = scene_->meshes.data[segment.meshTypedId];
            const ufbx_node* node = scene_->nodes.data[segment.nodeTypedId];
            const ufbx_matrix fallback = ufbx_matrix_mul(&nodeWorldMatrices.At(segment.nodeTypedId), &node->geometry_to_node);
            for (uint32_t vertexIndex = 0; vertexIndex < segment.sourceVertexCount; ++vertexIndex)
            {
                const FFastInfluenceRange& range = fastInfluenceRanges_.At(segment.firstSourceVertex + vertexIndex);
                ufbx_matrix skinMatrix{};
                ufbx_real totalWeight = 0.0f;
                for (uint32_t influenceIndex = 0; influenceIndex < range.influenceCount; ++influenceIndex)
                {
                    const FFastInfluence& influence = fastInfluences_.At(range.firstInfluence + influenceIndex);
                    const ufbx_matrix& matrix = clusterWorldMatrices.At(influence.clusterIndex);
                    totalWeight += influence.weight;
                    for (uint32_t element = 0; element < 12; ++element)
                    {
                        skinMatrix.v[element] += matrix.v[element] * influence.weight;
                    }
                }
                if (totalWeight <= 0.0f)
                {
                    skinMatrix = fallback;
                }
                else
                {
                    const double epsilon = sizeof(ufbx_real) == sizeof(float) ? 1.0842021795674597e-19 : 1.4916681462400413e-154;
                    if (fabs(static_cast<double>(totalWeight) - 1.0) > epsilon)
                    {
                        const ufbx_real reciprocalWeight = fabs(static_cast<double>(totalWeight)) > epsilon ? static_cast<ufbx_real>(1.0 / totalWeight) : 0.0f;
                        for (uint32_t element = 0; element < 12; ++element)
                        {
                            skinMatrix.v[element] *= reciprocalWeight;
                        }
                    }
                }
                const ufbx_vec3 position = ufbx_transform_position(&skinMatrix, fastSourcePositions_.At(segment.firstSourceVertex + vertexIndex));
                if (!isfinite(static_cast<double>(position.x)) || !isfinite(static_cast<double>(position.y)) || !isfinite(static_cast<double>(position.z)) || !deformedPositions.Append(position))
                {
                    error.Assign("FBX linear skinning produced an invalid position");
                    return false;
                }
            }
#ifdef GKCORE_TESTING
            const int64_t positionEnd = FbxTestCounter();
            lastFbxDeformProfile.milliseconds[3] += FbxTestMilliseconds(positionBegin, positionEnd);
            const int64_t normalBegin = FbxTestCounter();
#endif
            ufbx_vertex_vec3 positions{};
            positions.exists = true;
            positions.values.data = deformedPositions.Data() + segment.firstSourceVertex;
            positions.values.count = segment.sourceVertexCount;
            positions.indices = mesh->vertex_indices;
            positions.value_reals = 3;
            positions.unique_per_vertex = true;
            if (segment.generateNormals)
            {
                ufbx_compute_normals(mesh, &positions, fastCornerNormalIndices_.Data() + segment.firstCorner, segment.cornerCount, deformedNormals.Data() + segment.firstNormal, segment.normalCount);
            }
#ifdef GKCORE_TESTING
            const int64_t normalEnd = FbxTestCounter();
            lastFbxDeformProfile.milliseconds[4] += FbxTestMilliseconds(normalBegin, normalEnd);
            const int64_t scatterBegin = FbxTestCounter();
#endif
            for (uint32_t corner = 0; corner < segment.cornerCount; ++corner)
            {
                const uint32_t sourceVertex = fastCornerSourceVertices_.At(segment.firstCorner + corner);
                const ufbx_vec3& position = deformedPositions.At(sourceVertex);
                float outputNormal[3]{};
                if (segment.generateNormals)
                {
                    const uint32_t normalIndex = fastCornerNormalIndices_.At(segment.firstCorner + corner);
                    const ufbx_vec3& normal = deformedNormals.At(segment.firstNormal + normalIndex);
                    const double x = static_cast<double>(normal.x);
                    const double y = static_cast<double>(normal.y);
                    const double z = static_cast<double>(normal.z);
                    const double lengthSquared = x * x + y * y + z * z;
                    if (!isfinite(lengthSquared) || lengthSquared <= 1.0e-30)
                    {
                        error.Assign("FBX generated skin normal is degenerate");
                        return false;
                    }
                    // ufbx_compute_normalsは有効なnormalを既に正規化している。
                    outputNormal[0] = static_cast<float>(normal.x);
                    outputNormal[1] = static_cast<float>(normal.y);
                    outputNormal[2] = static_cast<float>(normal.z);
                }
                else
                {
                    ufbx_vec3 sourceNormal{};
                    if (!ReadAttribute(mesh->vertex_normal, corner, sourceNormal) || !TransformNormal(fallback, sourceNormal, outputNormal))
                    {
                        error.Assign("FBX static node normal cannot use its geometry transform");
                        return false;
                    }
                }
                FDeformedVertex vertex{};
                vertex.position[0] = static_cast<float>(position.x);
                vertex.position[1] = static_cast<float>(position.y);
                vertex.position[2] = static_cast<float>(position.z);
                memcpy(vertex.normal, outputNormal, sizeof(vertex.normal));
                if (!isfinite(vertex.position[0]) || !isfinite(vertex.position[1]) || !isfinite(vertex.position[2]) || !isfinite(vertex.normal[0]) || !isfinite(vertex.normal[1]) || !isfinite(vertex.normal[2]) || !stagedVertices.Append(vertex))
                {
                    error.Assign("FBX staged skin vertex is non-finite or out of memory");
                    return false;
                }
            }
#ifdef GKCORE_TESTING
            const int64_t scatterEnd = FbxTestCounter();
            lastFbxDeformProfile.milliseconds[5] += FbxTestMilliseconds(scatterBegin, scatterEnd);
            positionBegin = FbxTestCounter();
#endif
        }
        if (stagedVertices.Count() != modelVertexCount_)
        {
            error.Assign("FBX staged skin vertices do not cover the target model");
            return false;
        }
#ifdef GKCORE_TESTING
        const int64_t commitBegin = FbxTestCounter();
#endif
        for (uint32_t vertexIndex = 0; vertexIndex < modelVertexCount_; ++vertexIndex)
        {
            detail::ModelVertex& destination = output.vertices.At(vertexIndex);
            const FDeformedVertex& source = stagedVertices.At(vertexIndex);
            memcpy(destination.position, source.position, sizeof(destination.position));
            memcpy(destination.normal, source.normal, sizeof(destination.normal));
        }
#ifdef GKCORE_TESTING
        const int64_t commitEnd = FbxTestCounter();
        lastFbxDeformProfile.milliseconds[6] = FbxTestMilliseconds(commitBegin, commitEnd);
#endif
        error.Clear();
        return true;
    }

    ufbx_scene* scene_;
    animation::FModelSkeleton skeleton_;
    Array<detail::FFbxGeometrySegment> segments_;
    Array<FFastSegment> fastSegments_;
    Array<FFastInfluence> fastInfluences_;
    Array<FFastInfluenceRange> fastInfluenceRanges_;
    Array<FFastCluster> fastClusters_;
    Array<ufbx_vec3> fastSourcePositions_;
    Array<uint32_t> fastCornerSourceVertices_;
    Array<uint32_t> fastCornerNormalIndices_;
    Array<animation::FModelSparseVertexMap> fastSparseVertexMap_;
    animation::FModelGpuSkinningGeometry fastGpuGeometry_;
    Array<uint32_t> fastReferencedNormalIndices_;
    uint32_t modelVertexCount_;
    uint32_t modelIndexCount_;
    uint32_t modelPrimitiveCount_;
    uint32_t modelMaterialCount_;
    uint32_t fastNormalValueCount_ = 0;
    uint32_t fastSparseNormalValueCount_ = 0;
    bool fastDeformSupported_;
    bool fastGpuSkinningSupported_;
};

/**
 * FBX parser settings for geometry and external-animation loads.
 */
ufbx_load_opts MakeLoadOptions(const char* utf8Path)
{
    ufbx_load_opts options{};
    options.temp_allocator = MakeAllocatorOptions(64u * 1024u * 1024u, 1000000u);
    options.result_allocator = MakeAllocatorOptions(128u * 1024u * 1024u, 1000000u);
    options.file_format = UFBX_FILE_FORMAT_FBX;
    options.file_format_lookahead = 0;
    options.node_depth_limit = 64;
    options.load_external_files = false;
    options.ignore_animation = false;
    options.skip_skin_vertices = false;
    options.generate_missing_normals = true;
    options.retain_dom = true;
    options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_PRESERVE;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.force_single_thread_ascii_parsing = true;
    options.strict = true;
    options.index_error_handling = UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
    if (utf8Path)
    {
        options.filename.data = utf8Path;
        options.filename.length = SIZE_MAX;
    }
    return options;
}

/**
 * 指定sceneをsource objectへ移し、失敗時はsceneも解放する。
 */
AModelAnimationSource* CreateSource(ufbx_scene* ownedScene, const detail::ModelResource& geometry, const Array<detail::FFbxGeometrySegment>& segments, String& error)
{
    if (!ownedScene)
    {
        error.Assign("FBX animation scene is missing");
        return nullptr;
    }
    FbxAnimationSource* source = nullptr;
    try
    {
        source = new FbxAnimationSource(ownedScene);
    }
    catch (...)
    {
        ufbx_free_scene(ownedScene);
        error.Assign("FBX animation source allocation failed");
        return nullptr;
    }
    if (!source->Initialize(geometry, segments, error))
    {
        delete source;
        return nullptr;
    }
    return source;
}
}

#ifdef GKCORE_TESTING
/**
 * test用に同じ姿勢を従来のufbx skin評価へ渡す。
 */
bool DeformFbxAnimationWithUfbxForTesting(AModelAnimationSource& source, const animation::FModelPose& pose, detail::ModelResource& output, String& error)
{
    FbxAnimationSource& fbxSource = static_cast<FbxAnimationSource&>(source);
    return fbxSource.DeformWithUfbxForTesting(pose, output, error);
}

bool FbxLastDeformUsedFastPathForTesting()
{
    return lastFbxDeformUsedFastPath;
}

void GetLastFbxDeformProfileForTesting(double milliseconds[7], uint32_t counts[6])
{
    if (milliseconds)
    {
        memcpy(milliseconds, lastFbxDeformProfile.milliseconds, sizeof(lastFbxDeformProfile.milliseconds));
    }
    if (counts)
    {
        memcpy(counts, lastFbxDeformProfile.counts, sizeof(lastFbxDeformProfile.counts));
    }
}
#endif

AModelAnimationSource* CreateFbxAnimationSource(ufbx_scene* ownedScene, const detail::ModelResource& geometry, const Array<detail::FFbxGeometrySegment>& segments, String& error)
{
    return CreateSource(ownedScene, geometry, segments, error);
}

FModelAnimationAsset* LoadFbxAnimation(const uint8_t* bytes, uint32_t size, const char* utf8Path, String& error)
{
    if (!bytes || size == 0 || size > maxFbxAnimationBytes)
    {
        error.Assign("FBX animation file is empty or exceeds 64 MiB");
        return nullptr;
    }
    ufbx_load_opts options = MakeLoadOptions(utf8Path);
    ufbx_error parseError{};
    ufbx_scene* scene = ufbx_load_memory(bytes, size, &options, &parseError);
    if (!scene)
    {
        AssignFbxError(parseError, error);
        return nullptr;
    }
    detail::ModelResource emptyGeometry{};
    Array<detail::FFbxGeometrySegment> noSegments;
    AModelAnimationSource* source = CreateSource(scene, emptyGeometry, noSegments, error);
    if (!source)
    {
        return nullptr;
    }
    return CreateModelAnimationAsset(source, error);
}
}
