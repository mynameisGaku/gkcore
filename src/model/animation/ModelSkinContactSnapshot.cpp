// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSkinContactSnapshot.h"
#include "model/animation/ModelSkinPointContacts.h"
#include "model/animation/ModelSkinPoints.h"
#include "model/animation/ModelSecondaryMotionColliders.h"
#include "model/animation/ModelPose.h"
#include <float.h>
#include <math.h>
#if defined(_WIN32) && defined(GKCORE_TEST_FRAME_CAPTURE)
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#endif

/**
 * 予約した描画だけが所有する頂点接触の補正値を作る処理。
 */
namespace gk::model
{
bool SelectModelSkinContactPositions(const AModelAnimationSource& source, const FModelSecondaryMotionState& state, Array<uint32_t>& output, String& error)
{
    const auto* geometry = source.GpuSkinningGeometry();
    const auto& skeleton = source.Skeleton();
    if (!geometry || source.Format() != EModelAnimationFormat::Fbx)
    {
        error.Assign("vertex contact selection requires a supported FBX source");
        return false;
    }
    // 揺れboneの枝だけを選び、身体boneにしかweightがない点は保つ。
    Array<uint8_t> affected;
    for (uint32_t node = 0; node < skeleton.parents.Count(); ++node)
    {
        bool selected = false;
        for (uint32_t chain = 0; chain < state.chains.Count(); ++chain)
        {
            const auto* current = state.chains.At(chain);
            if (!current || current->bones.Count() == 0 || current->bones.At(0) >= skeleton.parents.Count())
            {
                error.Assign("vertex contact chain is invalid");
                return false;
            }
            int32_t ancestor = static_cast<int32_t>(node);
            for (uint32_t steps = 0; ancestor >= 0; ++steps)
            {
                if (steps >= skeleton.parents.Count() || static_cast<uint32_t>(ancestor) >= skeleton.parents.Count())
                {
                    error.Assign("vertex contact skeleton hierarchy is invalid");
                    return false;
                }
                if (static_cast<uint32_t>(ancestor) == current->bones.At(0))
                {
                    selected = true;
                    break;
                }
                ancestor = skeleton.parents.At(static_cast<uint32_t>(ancestor));
            }
        }
        if (!affected.Append(selected ? 1 : 0))
        {
            error.Assign("vertex contact bone selection allocation failed");
            return false;
        }
    }
    if (geometry->influenceRanges.Count() != geometry->positions.Count())
    {
        error.Assign("vertex contact influence counts are invalid");
        return false;
    }
    Array<uint32_t> candidate;
    for (uint32_t position = 0; position < geometry->positions.Count(); ++position)
    {
        const auto& range = geometry->influenceRanges.At(position);
        if (range.firstInfluence > geometry->influences.Count() || range.influenceCount > geometry->influences.Count() - range.firstInfluence)
        {
            error.Assign("vertex contact influence range is invalid");
            return false;
        }
        bool selected = false;
        for (uint32_t offset = 0; offset < range.influenceCount; ++offset)
        {
            const auto& influence = geometry->influences.At(range.firstInfluence + offset);
            if (influence.clusterIndex >= geometry->clusters.Count() || !isfinite(influence.weight) || influence.weight < 0.0)
            {
                error.Assign("vertex contact influence is invalid");
                return false;
            }
            const uint32_t node = geometry->clusters.At(influence.clusterIndex).nodeIndex;
            if (node >= affected.Count())
            {
                error.Assign("vertex contact cluster bone is invalid");
                return false;
            }
            selected = selected || (influence.weight > 0.0 && affected.At(node) != 0);
        }
        if (selected && !candidate.Append(position))
        {
            error.Assign("vertex contact selection allocation failed");
            return false;
        }
    }
    output.MoveFrom(candidate);
    error.Clear();
    return true;
}

bool BuildModelSkinContactSnapshot(const AModelAnimationSource& source, const FModelSecondaryMotionState* state, const animation::FModelPose& pose, const Array<animation::FModelGpuSkinningGeometry::FMatrix>& matrices, Array<animation::FModelSkinPointCorrection>& output, String& error)
{
    if (!state || !state->skinPointContactsEnabled || state->colliders.Count() == 0 || state->chains.Count() == 0)
    {
        output.Clear();
        error.Clear();
        return true;
    }
    // 補正と法線の両方を同じFBX skin cacheから評価する。
    const auto* geometry = source.GpuSkinningGeometry();
    if (!geometry || source.Format() != EModelAnimationFormat::Fbx || !source.SupportsGpuSkinningPose(pose))
    {
        error.Assign("vertex contacts require the supported zero-morph FBX skinning path");
        return false;
    }
#if defined(_WIN32) && defined(GKCORE_TEST_FRAME_CAPTURE)
    // 検証用DLLだけで区間時間を記録し、通常SDKへ計測処理を含めない。
    const char* profileSetting = getenv("GKCORE_PROFILE_SKIN_CONTACTS");
    const bool profile = profileSetting && profileSetting[0] == '1' && profileSetting[1] == '\0';
    LARGE_INTEGER frequency{}, begin{}, shapeEnd{}, skinBegin{}, skinEnd{}, contactEnd{}, finish{};
    int64_t skinTicks = 0;
    int64_t contactTicks = 0;
    if (profile && (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&begin)))
    {
        error.Assign("skin contact profile clock failed");
        return false;
    }
#endif
    // 現在のposeから身体形状を作り、前回更新の形状へ依存しない。
    Array<float> boneMatrices;
    Array<animation::FModelSecondaryMotionCollisionShape> shapes;
    const auto& skeleton = source.Skeleton();
    if (!animation::EvaluateModelPose(skeleton, pose, boneMatrices, error) || !EvaluateSecondaryMotionColliders(skeleton, pose, boneMatrices, state->colliders.Data(), state->colliders.Count(), state->colliderMargin, shapes, error))
    {
        return false;
    }
    if (state->skinContactGeometry != geometry)
    {
        error.Assign("vertex contact selection does not match the model geometry");
        return false;
    }
#if defined(_WIN32) && defined(GKCORE_TEST_FRAME_CAPTURE)
    if (profile)
    {
        QueryPerformanceCounter(&shapeEnd);
    }
#endif
    const auto& ids = state->skinContactPositionIds;
    Array<animation::FModelSkinPointCorrection> candidate;
    uint32_t first = 0;
    while (first < ids.Count())
    {
        const uint32_t count = ids.Count() - first > 8192 ? 8192 : ids.Count() - first;
        Array<FVector3d> points;
        Array<FVector3d> corrected;
#if defined(_WIN32) && defined(GKCORE_TEST_FRAME_CAPTURE)
        if (profile)
        {
            QueryPerformanceCounter(&skinBegin);
        }
#endif
        if (!animation::EvaluateModelSkinPoints(*geometry, matrices, ids.Data() + first, count, points, error))
        {
            return false;
        }
#if defined(_WIN32) && defined(GKCORE_TEST_FRAME_CAPTURE)
        if (profile)
        {
            QueryPerformanceCounter(&skinEnd);
        }
#endif
        if (!animation::ProjectModelSkinPoints(points.Data(), points.Count(), shapes.Data(), shapes.Count(), 64, corrected, error))
        {
            return false;
        }
#if defined(_WIN32) && defined(GKCORE_TEST_FRAME_CAPTURE)
        if (profile)
        {
            QueryPerformanceCounter(&contactEnd);
            skinTicks += skinEnd.QuadPart - skinBegin.QuadPart;
            contactTicks += contactEnd.QuadPart - skinEnd.QuadPart;
        }
#endif
        for (uint32_t item = 0; item < count; ++item)
        {
            const auto& value = corrected.At(item);
            const auto& previous = points.At(item);
            if (value.value[0] == previous.value[0] && value.value[1] == previous.value[1] && value.value[2] == previous.value[2])
            {
                continue;
            }
            animation::FModelSkinPointCorrection correction{};
            correction.positionId = ids.At(first + item);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!isfinite(value.value[axis]) || fabs(value.value[axis]) > FLT_MAX)
                {
                    error.Assign("vertex contact position exceeds the float range");
                    return false;
                }
                correction.position[axis] = value.value[axis];
            }
            if (!candidate.Append(correction))
            {
                error.Assign("vertex contact correction allocation failed");
                return false;
            }
        }
        first += count;
    }
#if defined(_WIN32) && defined(GKCORE_TEST_FRAME_CAPTURE)
    if (profile)
    {
        QueryPerformanceCounter(&finish);
        const double milliseconds = 1000.0 / static_cast<double>(frequency.QuadPart);
        fprintf(stderr, "{\"type\":\"gkcore_skin_contact_profile\",\"selectedPoints\":%u,\"changedPoints\":%u,\"shapeMs\":%.6f,\"skinMs\":%.6f,\"projectMs\":%.6f,\"totalMs\":%.6f}\n", ids.Count(), candidate.Count(), (shapeEnd.QuadPart - begin.QuadPart) * milliseconds, skinTicks * milliseconds, contactTicks * milliseconds, (finish.QuadPart - begin.QuadPart) * milliseconds);
    }
#endif
    output.MoveFrom(candidate);
    error.Clear();
    return true;
}
}
