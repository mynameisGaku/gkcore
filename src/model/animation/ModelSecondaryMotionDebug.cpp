// SPDX-License-Identifier: NOASSERTION
#include <gkcore/ModelSecondaryMotion.h>
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/ModelSnapshot.h"
#include "model/animation/ModelSecondaryMotionColliders.h"
#include "model/animation/ModelSecondaryMotionContactPoints.h"
#include "model/animation/FModelSecondaryMotionState.h"
#include "core/Context.h"
#include <limits.h>
#include <math.h>
#include <string.h>

/**
 * 接触設定を現在の描画姿勢に合わせて複製する処理。
 */
namespace gk
{
int GetModelSecondaryMotionDebugShapes(ModelHandle handle, FModelSecondaryMotionDebugShape* output, uint32_t capacity)
{
    auto* resource = detail::FindModel(handle);
    auto* transform = resource ? detail::FindModelTransform(handle) : nullptr;
    if (!transform || (capacity > 0 && !output) || (capacity == 0 && output))
    {
        return detail::SetError("invalid secondary motion debug output");
    }
    const auto* state = transform->playback ? transform->playback->secondaryMotion : nullptr;
    if (!state)
    {
        detail::ClearError();
        return 0;
    }
    if (!resource->animation || !resource->animation->source)
    {
        return detail::SetError("secondary motion debug requires a skeletal source");
    }
    // 必要数を先に検査して、容量不足時は出力を触らない。
    uint64_t required = static_cast<uint64_t>(state->colliders.Count()) + state->links.Count() + state->distanceConstraints.Count() + state->contactLinks.Count() + state->contactPoints.Count();
    for (uint32_t index = 0; index < state->chains.Count(); ++index)
    {
        const auto* chain = state->chains.At(index);
        if (!chain || chain->bones.Count() == 0 || !isfinite(chain->collisionRadius) || chain->collisionRadius < 0.0f)
        {
            return detail::SetError("secondary motion debug chain is invalid");
        }
        required += static_cast<uint64_t>(chain->bones.Count()) + 1;
    }
    if (required > INT_MAX || (capacity > 0 && capacity < required))
    {
        return detail::SetError("secondary motion debug output capacity is insufficient");
    }
    if (capacity == 0)
    {
        detail::ClearError();
        return static_cast<int>(required);
    }
    String error;
    model::animation::FModelPose pose;
    Array<float> matrices;
    const auto& skeleton = resource->animation->source->Skeleton();
    if (!model::EvaluateModelPlaybackPose(*resource, *transform->playback, pose, error) || !model::animation::EvaluateModelPose(skeleton, pose, matrices, error))
    {
        return detail::SetError(error.CStr());
    }
    Array<model::animation::FModelSecondaryMotionCollisionShape> bodies;
    if (!model::EvaluateSecondaryMotionColliders(skeleton, pose, matrices, state->colliders.Data(), state->colliders.Count(), state->colliderMargin, bodies, error))
    {
        return detail::SetError(error.CStr());
    }
    Array<FModelSecondaryMotionDebugShape> candidate;
    if (!candidate.Reserve(static_cast<uint32_t>(required)))
    {
        return detail::SetError("secondary motion debug allocation failed");
    }
    for (uint32_t index = 0; index < bodies.Count(); ++index)
    {
        FModelSecondaryMotionDebugShape shape{};
        shape.start = bodies.At(index).start;
        shape.end = bodies.At(index).end;
        shape.radius = bodies.At(index).radius;
        shape.bone = state->colliders.At(index).bone;
        shape.body = true;
        if (!candidate.Append(shape))
        {
            return detail::SetError("secondary motion debug body allocation failed");
        }
    }
    for (uint32_t index = 0; index < state->chains.Count(); ++index)
    {
        const auto& chain = *state->chains.At(index);
        for (uint32_t point = 0; point <= chain.bones.Count(); ++point)
        {
            const bool virtualTip = point == chain.bones.Count();
            const uint32_t bone = chain.bones.At(virtualTip ? point - 1 : point);
            if (bone >= skeleton.parents.Count())
            {
                return detail::SetError("secondary motion debug bone is out of range");
            }
            const float* matrix = matrices.Data() + bone * 16u;
            FModelSecondaryMotionDebugShape shape{};
            shape.start = { matrix[12], matrix[13], matrix[14] };
            if (virtualTip)
            {
                const auto tip = chain.settings.endOffset;
                shape.start = { matrix[12] + matrix[0] * tip.x + matrix[4] * tip.y + matrix[8] * tip.z, matrix[13] + matrix[1] * tip.x + matrix[5] * tip.y + matrix[9] * tip.z, matrix[14] + matrix[2] * tip.x + matrix[6] * tip.y + matrix[10] * tip.z };
            }
            if (!isfinite(shape.start.x) || !isfinite(shape.start.y) || !isfinite(shape.start.z))
            {
                return detail::SetError("secondary motion debug position is non-finite");
            }
            shape.end = shape.start;
            shape.radius = chain.collisionRadius;
            shape.bone = bone;
            shape.fixed = point == 0;
            shape.virtualTip = virtualTip;
            if (!candidate.Append(shape))
            {
                return detail::SetError("secondary motion debug point allocation failed");
            }
        }
    }
    for (uint32_t index = 0; index < state->links.Count(); ++index)
    {
        const auto& link = state->links.At(index);
        if (link.firstBone >= skeleton.parents.Count() || link.secondBone >= skeleton.parents.Count() || !isfinite(link.radius) || link.radius < 0.0f)
        {
            return detail::SetError("secondary motion debug link is invalid");
        }
        const float* first = matrices.Data() + static_cast<uint64_t>(link.firstBone) * 16u;
        const float* second = matrices.Data() + static_cast<uint64_t>(link.secondBone) * 16u;
        FModelSecondaryMotionDebugShape shape{};
        shape.start = { first[12], first[13], first[14] };
        shape.end = { second[12], second[13], second[14] };
        shape.radius = link.radius;
        shape.bone = link.firstBone;
        if (!candidate.Append(shape))
        {
            return detail::SetError("secondary motion debug link allocation failed");
        }
    }
    // 距離制約は厚みを持たない接続線として、現在の骨原点間を表示する。
    for (uint32_t index = 0; index < state->distanceConstraints.Count(); ++index)
    {
        const auto& constraint = state->distanceConstraints.At(index);
        if (constraint.firstBone >= skeleton.parents.Count() || constraint.secondBone >= skeleton.parents.Count())
        {
            return detail::SetError("secondary motion debug distance bone is out of range");
        }
        const float* first = matrices.Data() + static_cast<uint64_t>(constraint.firstBone) * 16u;
        const float* second = matrices.Data() + static_cast<uint64_t>(constraint.secondBone) * 16u;
        FModelSecondaryMotionDebugShape shape{};
        shape.start = { first[12], first[13], first[14] };
        shape.end = { second[12], second[13], second[14] };
        shape.bone = constraint.firstBone;
        if (!candidate.Append(shape))
        {
            return detail::SetError("secondary motion debug distance allocation failed");
        }
    }
    // 骨原点から離して配置した衣服側の補助形状も、実際の描画FKで表示する。
    for (uint32_t index = 0; index < state->contactLinks.Count(); ++index)
    {
        const auto& contact = state->contactLinks.At(index);
        if (contact.firstBone >= skeleton.parents.Count() || contact.secondBone >= skeleton.parents.Count() || !isfinite(contact.radius) || contact.radius < 0.0f)
            return detail::SetError("secondary motion debug attachment is invalid");
        const float* first = matrices.Data() + static_cast<uint64_t>(contact.firstBone) * 16u;
        const float* second = matrices.Data() + static_cast<uint64_t>(contact.secondBone) * 16u;
        FModelSecondaryMotionDebugShape shape{};
        shape.start = { first[12] + first[0] * contact.firstOffset.x + first[4] * contact.firstOffset.y + first[8] * contact.firstOffset.z, first[13] + first[1] * contact.firstOffset.x + first[5] * contact.firstOffset.y + first[9] * contact.firstOffset.z, first[14] + first[2] * contact.firstOffset.x + first[6] * contact.firstOffset.y + first[10] * contact.firstOffset.z };
        shape.end = { second[12] + second[0] * contact.secondOffset.x + second[4] * contact.secondOffset.y + second[8] * contact.secondOffset.z, second[13] + second[1] * contact.secondOffset.x + second[5] * contact.secondOffset.y + second[9] * contact.secondOffset.z, second[14] + second[2] * contact.secondOffset.x + second[6] * contact.secondOffset.y + second[10] * contact.secondOffset.z };
        shape.radius = contact.radius;
        shape.bone = contact.firstBone;
        if (!isfinite(shape.start.x) || !isfinite(shape.start.y) || !isfinite(shape.start.z) || !isfinite(shape.end.x) || !isfinite(shape.end.y) || !isfinite(shape.end.z) || !candidate.Append(shape))
            return detail::SetError("secondary motion debug attachment is outside range or allocation failed");
    }
    // 複数boneで動く接触球も、表示中のFKと同じweightから中心を求める。
    for (uint32_t index = 0; index < state->contactPoints.Count(); ++index)
    {
        FVector3d centre{};
        if (!model::EvaluateRenderedSecondaryMotionContactPoint(*state, matrices, index, centre, error))
        {
            return detail::SetError(error.CStr());
        }
        FModelSecondaryMotionDebugShape shape{};
        shape.start = { static_cast<float>(centre.value[0]), static_cast<float>(centre.value[1]), static_cast<float>(centre.value[2]) };
        shape.end = shape.start;
        shape.radius = state->contactPoints.At(index).radius;
        shape.bone = state->contactAnchors.At(state->contactPoints.At(index).firstAnchor).bone;
        if (!isfinite(shape.start.x) || !isfinite(shape.start.y) || !isfinite(shape.start.z) || !candidate.Append(shape))
        {
            return detail::SetError("secondary weighted contact debug position or allocation failed");
        }
    }
    if (candidate.Count() > 0)
    {
        memcpy(output, candidate.Data(), static_cast<size_t>(candidate.Count()) * sizeof(FModelSecondaryMotionDebugShape));
    }
    detail::ClearError();
    return static_cast<int>(candidate.Count());
}
}
