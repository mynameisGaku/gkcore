// SPDX-License-Identifier: NOASSERTION
#include "ModelSnapshot.h"
#include "ModelAnimationBinding.h"
#include "ModelPose.h"
#include "ModelIk.h"
#include "../Model.h"
#include <math.h>

/**
 * 描画時点のanimation状態から独立した形状を作る処理。
 */
namespace gk::model
{
/**
 * model形状を複製し、画像参照を個別に保持する。
 */
detail::ModelResource* CloneModelSnapshot(const detail::ModelResource& source, String& error)
{
    auto* result = detail::CreateModelResource();
    if (!result)
    {
        error.Assign("model snapshot allocation failed");
        return nullptr;
    }
    result->materials.Clear();
    bool success = result->vertices.AppendRange(source.vertices.Data(), source.vertices.Count()) && result->indices.AppendRange(source.indices.Data(), source.indices.Count()) && result->primitives.AppendRange(source.primitives.Data(), source.primitives.Count()) && result->materials.AppendRange(source.materials.Data(), source.materials.Count());
    for (uint32_t i = 0; success && i < source.textures.Count(); ++i)
    {
        auto* texture = source.textures.At(i);
        if (texture && !Retain(&texture->reference))
        {
            success = false;
            break;
        }
        if (!result->textures.Append(texture))
        {
            if (texture)
                Release(&texture->reference);
            success = false;
        }
    }
    if (!success)
    {
        Release(&result->reference);
        error.Assign("model snapshot copy failed");
        return nullptr;
    }
    return result;
}

/**
 * 1再生枠の姿勢を適用先へ評価する。OBJ連番はsource自身が変形する。
 */
static bool EvaluateClip(const detail::ModelResource& source, const FModelClipState& state, animation::FModelPose& pose, String& error)
{
    if (state.asset->source->Format() == EModelAnimationFormat::ObjSequence)
        return state.asset->source->Sample(state.clip, state.seconds, pose, error);
    return SampleBoundClip(state, *source.animation, pose, error);
}

detail::ModelResource* EvaluateModelSnapshot(const detail::ModelResource& source, const FModelPlayback* playback, String& error)
{
    if (playback && (!isfinite(playback->blendWeight) || playback->blendWeight < 0.0f || playback->blendWeight > 1.0f))
    {
        error.Assign("model animation blend weight must be finite and in [0, 1]");
        return nullptr;
    }
    // 再生・IKがないモデルは、既存の静的形状をそのまま保持する。
    if (!playback || (!playback->clips[0].asset && playback->ik.Count() == 0))
    {
        auto* result = const_cast<detail::ModelResource*>(&source);
        if (!Retain(&result->reference))
        {
            error.Assign("model reference limit exceeded");
            return nullptr;
        }
        return result;
    }
    auto* result = CloneModelSnapshot(source, error);
    if (!result)
        return nullptr;
    animation::FModelPose pose;
    const auto* primary = playback->clips[0].asset;
    bool success = primary ? EvaluateClip(source, playback->clips[0], pose, error) : source.animation && animation::InitializeModelPose(source.animation->source->Skeleton(), pose, error);
    const bool sequence = primary && primary->source->Format() == EModelAnimationFormat::ObjSequence;
    if (success && playback->clips[1].asset && playback->blendWeight > 0.0f)
    {
        animation::FModelPose secondary;
        success = EvaluateClip(source, playback->clips[1], secondary, error);
        if (success && !sequence)
            success = animation::BlendModelPoses(source.animation->source->Skeleton(), pose, secondary, playback->blendWeight, pose, error);
        else if (success)
        {
            auto* other = CloneModelSnapshot(source, error);
            success = other && primary->source->Deform(pose, *result, error) && playback->clips[1].asset->source->Deform(secondary, *other, error);
            if (success)
            {
                for (uint32_t i = 0; i < result->vertices.Count(); ++i)
                {
                    auto& vertex = result->vertices.At(i);
                    const auto& second = other->vertices.At(i);
                    double lengthSquared = 0.0;
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        vertex.position[axis] = static_cast<float>(static_cast<double>(vertex.position[axis]) * (1.0 - playback->blendWeight) + static_cast<double>(second.position[axis]) * playback->blendWeight);
                        vertex.normal[axis] = vertex.normal[axis] * (1.0f - playback->blendWeight) + second.normal[axis] * playback->blendWeight;
                        lengthSquared += static_cast<double>(vertex.normal[axis]) * vertex.normal[axis];
                    }
                    const double length = sqrt(lengthSquared);
                    if (length > 0.0)
                        for (uint32_t axis = 0; axis < 3; ++axis)
                            vertex.normal[axis] = static_cast<float>(vertex.normal[axis] / length);
                }
            }
            if (other)
                Release(&other->reference);
            if (!success)
            {
                Release(&result->reference);
                return nullptr;
            }
            return result;
        }
    }
    if (success && !sequence)
    {
        for (uint32_t i = 0; success && i < playback->ik.Count(); ++i)
        {
            const auto& command = playback->ik.At(i);
            const auto* chain = playback->ikBones.Data() + command.offset;
            const auto& skeleton = source.animation->source->Skeleton();
            if (command.twoBone)
                success = animation::SolveTwoBoneIk(skeleton, pose, chain[0], chain[1], chain[2], command.target, command.pole, command.weight, pose, error);
            else
                success = animation::SolveFabrikIk(skeleton, pose, chain, command.count, command.target, command.weight, 0.0001f, 64, pose, error);
        }
    }
    if (success)
    {
        const auto* deform = sequence ? primary->source : source.animation->source;
        success = deform->Deform(pose, *result, error);
    }
    if (!success)
    {
        Release(&result->reference);
        return nullptr;
    }
    error.Clear();
    return result;
}
}
