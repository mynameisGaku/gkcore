// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelPoseCache.h"
#include "model/animation/AModelAnimationSource.h"
#include <math.h>

namespace gk::model
{
namespace
{
/**
 * cacheの配列数と有限な姿勢を検査する。破損した値は再利用しない。
 */
bool ValidPose(const animation::FModelPose& pose, const animation::FModelSkeleton& skeleton)
{
    if (pose.localTransforms.Count() != skeleton.parents.Count() || pose.morphWeights.Count() != skeleton.restMorphWeights.Count())
    {
        return false;
    }
    for (uint32_t bone = 0; bone < pose.localTransforms.Count(); ++bone)
    {
        const auto& transform = pose.localTransforms.At(bone);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(transform.position[axis]) || !isfinite(transform.scale[axis]))
            {
                return false;
            }
        }
        bool nonzero = false;
        for (uint32_t component = 0; component < 4; ++component)
        {
            if (!isfinite(transform.rotation[component]))
            {
                return false;
            }
            nonzero = nonzero || transform.rotation[component] != 0.0f;
        }
        if (!nonzero)
        {
            return false;
        }
    }
    for (uint32_t morph = 0; morph < pose.morphWeights.Count(); ++morph)
    {
        if (!isfinite(pose.morphWeights.At(morph)))
        {
            return false;
        }
    }
    return true;
}

/**
 * 再生条件を値で照合し、命令の配列が置き換わった後の古い姿勢を除外する。
 */
bool Matches(const FModelPoseCache& cache, const detail::ModelResource& source, const FModelPlayback& playback)
{
    if (!source.animation || !source.animation->source || cache.source != source.animation->source || cache.revision != playback.poseRevision || cache.blendWeight != playback.blendWeight || cache.ik.Count() != playback.ik.Count() || cache.ikBones.Count() != playback.ikBones.Count() || !ValidPose(cache.pose, source.animation->source->Skeleton()))
    {
        return false;
    }
    for (uint32_t slot = 0; slot < 2; ++slot)
    {
        const auto& clip = playback.clips[slot];
        if (cache.assets[slot] != clip.asset || cache.clipSources[slot] != (clip.asset ? clip.asset->source : nullptr) || cache.clips[slot] != clip.clip || cache.seconds[slot] != clip.seconds || cache.speeds[slot] != clip.speed || cache.loops[slot] != clip.loop)
        {
            return false;
        }
    }
    for (uint32_t index = 0; index < playback.ik.Count(); ++index)
    {
        const auto& current = playback.ik.At(index);
        const auto& saved = cache.ik.At(index);
        if (current.offset != saved.offset || current.count != saved.count || current.weight != saved.weight || current.twoBone != saved.twoBone)
        {
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (current.target[axis] != saved.target[axis] || current.pole[axis] != saved.pole[axis])
            {
                return false;
            }
        }
    }
    for (uint32_t index = 0; index < playback.ikBones.Count(); ++index)
    {
        if (cache.ikBones.At(index) != playback.ikBones.At(index))
        {
            return false;
        }
    }
    return true;
}
}

void InvalidateModelPoseCache(FModelPlayback& playback)
{
    if (playback.poseRevision == UINT64_MAX)
    {
        delete playback.basePoseCache;
        playback.basePoseCache = nullptr;
        playback.poseRevision = 1;
    }
    else
    {
        ++playback.poseRevision;
    }
}

FModelPoseCache* CreateModelPoseCache(const detail::ModelResource& source, const FModelPlayback& playback, const animation::FModelPose& pose, String& error)
{
    if (!source.animation || !source.animation->source || !ValidPose(pose, source.animation->source->Skeleton()))
    {
        error.Assign("model base pose is invalid for caching");
        return nullptr;
    }
    FModelPoseCache* candidate = nullptr;
    try
    {
        candidate = new FModelPoseCache;
    }
    catch (...)
    {
        error.Assign("model pose cache allocation failed");
        return nullptr;
    }
    if (!candidate->pose.localTransforms.AppendRange(pose.localTransforms.Data(), pose.localTransforms.Count()) || !candidate->pose.morphWeights.AppendRange(pose.morphWeights.Data(), pose.morphWeights.Count()) || !candidate->ik.AppendRange(playback.ik.Data(), playback.ik.Count()) || !candidate->ikBones.AppendRange(playback.ikBones.Data(), playback.ikBones.Count()))
    {
        delete candidate;
        error.Assign("model pose cache allocation failed");
        return nullptr;
    }
    candidate->source = source.animation->source;
    candidate->revision = playback.poseRevision;
    candidate->blendWeight = playback.blendWeight;
    for (uint32_t slot = 0; slot < 2; ++slot)
    {
        const auto& clip = playback.clips[slot];
        candidate->assets[slot] = clip.asset;
        candidate->clipSources[slot] = clip.asset ? clip.asset->source : nullptr;
        candidate->clips[slot] = clip.clip;
        candidate->seconds[slot] = clip.seconds;
        candidate->speeds[slot] = clip.speed;
        candidate->loops[slot] = clip.loop;
    }
    error.Clear();
    return candidate;
}

bool CopyCurrentModelPoseCache(const detail::ModelResource& source, const FModelPlayback& playback, animation::FModelPose& output, bool& hit, String& error)
{
    hit = false;
    if (!playback.basePoseCache || !Matches(*playback.basePoseCache, source, playback))
    {
        return true;
    }
    animation::FModelPose candidate;
    const auto& cached = playback.basePoseCache->pose;
    if (!candidate.localTransforms.AppendRange(cached.localTransforms.Data(), cached.localTransforms.Count()) || !candidate.morphWeights.AppendRange(cached.morphWeights.Data(), cached.morphWeights.Count()))
    {
        error.Assign("model cached pose copy failed");
        return false;
    }
    output.localTransforms.MoveFrom(candidate.localTransforms);
    output.morphWeights.MoveFrom(candidate.morphWeights);
    hit = true;
    return true;
}
}
