// SPDX-License-Identifier: NOASSERTION
#include "FbxAnimation.h"
#include "../../foundation/Memory.h"
#include "../../../third_party/ufbx/ufbx.h"
#include <math.h>
#include <string.h>

/**
 * FBX animation sourceを共通model animation形式へ変換する。
 */
namespace gk::model
{
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
    explicit FbxAnimationSource(ufbx_scene* scene) : scene_(scene), modelVertexCount_(0), modelIndexCount_(0), modelPrimitiveCount_(0), modelMaterialCount_(0)
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
                for (size_t clusterIndex = 0; clusterIndex < skin->clusters.count; ++clusterIndex)
                {
                    const ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
                    if (!cluster || !cluster->bone_node || !cluster->vertices.data || !cluster->weights.data || cluster->vertices.count != cluster->weights.count)
                    {
                        error.Assign("FBX skin cluster has incomplete bone or weight data");
                        return false;
                    }
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
        // ufbx evaluatorで単位・軸変換後のnode姿勢を得る。
        ufbx_evaluate_opts evaluateOptions{};
        evaluateOptions.temp_allocator = MakeAllocatorOptions(64u * 1024u * 1024u, 500000u);
        evaluateOptions.result_allocator = MakeAllocatorOptions(128u * 1024u * 1024u, 1000000u);
        ufbx_error evaluationError{};
        ufbx_scene* evaluatedScene = ufbx_evaluate_scene(scene_, stack->anim, sampleTime, &evaluateOptions, &evaluationError);
        if (!evaluatedScene)
        {
            AssignFbxError(evaluationError, error);
            return false;
        }
        animation::FModelPose staged;
        bool success = true;
        for (uint32_t nodeIndex = 0; nodeIndex < skeleton_.parents.Count(); ++nodeIndex)
        {
            if (nodeIndex >= evaluatedScene->nodes.count || !evaluatedScene->nodes.data[nodeIndex])
            {
                error.Assign("FBX evaluated node hierarchy does not match the retained skeleton");
                success = false;
                break;
            }
            animation::FModelBoneTransform transform{};
            if (!CopyTransform(evaluatedScene->nodes.data[nodeIndex]->local_transform, transform) || !staged.localTransforms.Append(transform))
            {
                error.Assign("FBX animation produced an invalid node transform");
                success = false;
                break;
            }
        }
        for (uint32_t morph = 0; success && morph < skeleton_.restMorphWeights.Count(); ++morph)
        {
            const ufbx_real weight = ufbx_evaluate_blend_weight(stack->anim, scene_->blend_channels.data[morph], sampleTime);
            const float converted = static_cast<float>(weight);
            if (!isfinite(static_cast<double>(weight)) || !isfinite(converted) || !staged.morphWeights.Append(converted))
            {
                error.Assign("FBX animation produced an invalid morph weight");
                success = false;
            }
        }
        ufbx_free_scene(evaluatedScene);
        if (!success)
        {
            return false;
        }
        output.localTransforms.MoveFrom(staged.localTransforms);
        output.morphWeights.MoveFrom(staged.morphWeights);
        error.Clear();
        return true;
    }

    bool Deform(const animation::FModelPose& pose, detail::ModelResource& output, String& error) const override
    {
        if (!scene_ || segments_.Count() == 0 || modelVertexCount_ == 0 || output.vertices.Count() != modelVertexCount_ || output.indices.Count() != modelIndexCount_ || output.primitives.Count() != modelPrimitiveCount_ || output.materials.Count() != modelMaterialCount_ || pose.localTransforms.Count() != skeleton_.parents.Count() || pose.morphWeights.Count() != skeleton_.restMorphWeights.Count())
        {
            error.Assign("FBX deformation target or common pose does not match its source");
            return false;
        }
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
    ufbx_scene* scene_;
    animation::FModelSkeleton skeleton_;
    Array<detail::FFbxGeometrySegment> segments_;
    uint32_t modelVertexCount_;
    uint32_t modelIndexCount_;
    uint32_t modelPrimitiveCount_;
    uint32_t modelMaterialCount_;
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
