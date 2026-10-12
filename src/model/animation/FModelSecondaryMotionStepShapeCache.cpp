// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FModelSecondaryMotionStepShapeCache.h"
#include "model/animation/ModelSecondaryMotionCollision.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{

FModelSecondaryMotionStepShapeCache::FModelSecondaryMotionStepShapeCache(const FModelSecondaryMotionCollisionShape* shapes, uint32_t count) : sourceShapes(shapes), sourceShapeCount(count)
{
}

bool FModelSecondaryMotionStepShapeCache::Get(float pointRadius, FModelSecondaryMotionStepShapes& output, gk::String& error)
{
    return Get(pointRadius, ~static_cast<uint64_t>(0), output, error);
}

bool FModelSecondaryMotionStepShapeCache::Get(float pointRadius, uint64_t colliderMask, FModelSecondaryMotionStepShapes& output, gk::String& error)
{
    error.Clear();
    if (!isfinite(pointRadius) || pointRadius < 0.0f || sourceShapeCount > 64u || (sourceShapeCount > 0u && !sourceShapes) || !ValidateSecondaryMotionCollisionShapes(sourceShapes, sourceShapeCount, error))
    {
        if (error.Empty())
            error.Assign("Secondary motion step shape cache input is invalid");
        return false;
    }
    // source countに存在するbitだけを残し、64個の場合はshiftを行わない。
    const uint64_t validMask = sourceShapeCount == 64u ? ~static_cast<uint64_t>(0) : (sourceShapeCount == 0u ? 0u : (static_cast<uint64_t>(1) << sourceShapeCount) - 1u);
    const uint64_t effectiveMask = colliderMask & validMask;
    uint32_t selectedCount = 0;
    for (uint32_t sourceIndex = 0; sourceIndex < sourceShapeCount; ++sourceIndex)
    {
        const uint64_t sourceBit = static_cast<uint64_t>(1) << sourceIndex;
        if ((effectiveMask & sourceBit) != 0u)
        {
            ++selectedCount;
        }
    }
    if (selectedCount == 0u)
    {
        // 全shapeを検証した後、空の選択結果を返す。
        FModelSecondaryMotionStepShapes empty{};
        output = empty;
        return true;
    }
    for (uint32_t groupIndex = 0; groupIndex < groups.Count(); ++groupIndex)
    {
        const FGroup& group = groups.At(groupIndex);
        if (group.pointRadius == pointRadius && group.colliderMask == effectiveMask && group.count == selectedCount)
        {
            // 同じ半径と選択集合なら連結配列内の既存範囲を返す。
            FModelSecondaryMotionStepShapes cached{};
            cached.expanded = expandedShapes.Data() + group.offset;
            cached.guarded = guardedShapes.Data() + group.offset;
            cached.prepared = preparedShapes.Data() + group.offset;
            cached.count = selectedCount;
            output = cached;
            return true;
        }
    }

    // 全shape選択では一時配列を作らず、部分選択だけsource順に詰め直す。
    const FModelSecondaryMotionCollisionShape* selectedSourceShapes = sourceShapes;
    gk::Array<FModelSecondaryMotionCollisionShape> selectedSourceCandidate;
    if (selectedCount != sourceShapeCount)
    {
        if (!selectedSourceCandidate.Reserve(selectedCount))
        {
            error.Assign("Secondary motion selected shape allocation failed");
            return false;
        }
        for (uint32_t sourceIndex = 0; sourceIndex < sourceShapeCount; ++sourceIndex)
        {
            const uint64_t sourceBit = static_cast<uint64_t>(1) << sourceIndex;
            if ((effectiveMask & sourceBit) != 0u && !selectedSourceCandidate.Append(sourceShapes[sourceIndex]))
            {
                error.Assign("Secondary motion selected shape copy failed");
                return false;
            }
        }
        selectedSourceShapes = selectedSourceCandidate.Data();
    }

    // 節半径を足した元形状を一時配列へ作る。
    gk::Array<FModelSecondaryMotionCollisionShape> expandedCandidate;
    if (pointRadius == 0.0f)
    {
        if (!expandedCandidate.Reserve(selectedCount) || !expandedCandidate.AppendRange(selectedSourceShapes, selectedCount))
        {
            error.Assign("Secondary motion step shape copy failed");
            return false;
        }
    }
    else if (!ExpandSecondaryMotionCollisionShapes(selectedSourceShapes, selectedCount, pointRadius, expandedCandidate, error))
    {
        return false;
    }

    // 接触許容幅を相殺するguard形状を一時配列へ作る。
    gk::Array<FModelSecondaryMotionCollisionShape> guardedCandidate;
    if (!guardedCandidate.Reserve(selectedCount))
    {
        error.Assign("Secondary motion guarded shape allocation failed");
        return false;
    }
    for (uint32_t shapeIndex = 0; shapeIndex < selectedCount; ++shapeIndex)
    {
        // delta0のCoreと同じ補間式を使い、符号付きゼロの丸めも揃える。
        // guard形状の元になる拡張済みshape。
        const FModelSecondaryMotionCollisionShape& source = expandedCandidate.At(shapeIndex);
        // delta0のCoreと一致させる補間係数。
        const double weight = 1.0;
        // Coreと同じ順序で各軸を補間する。
        const double startX = static_cast<double>(source.start.x) + (static_cast<double>(source.start.x) - static_cast<double>(source.start.x)) * weight;
        const double startY = static_cast<double>(source.start.y) + (static_cast<double>(source.start.y) - static_cast<double>(source.start.y)) * weight;
        const double startZ = static_cast<double>(source.start.z) + (static_cast<double>(source.start.z) - static_cast<double>(source.start.z)) * weight;
        const double endX = static_cast<double>(source.end.x) + (static_cast<double>(source.end.x) - static_cast<double>(source.end.x)) * weight;
        const double endY = static_cast<double>(source.end.y) + (static_cast<double>(source.end.y) - static_cast<double>(source.end.y)) * weight;
        const double endZ = static_cast<double>(source.end.z) + (static_cast<double>(source.end.z) - static_cast<double>(source.end.z)) * weight;
        // 半径にもCoreと同じguard係数を適用する。
        const double interpolatedRadius = static_cast<double>(source.radius) + (static_cast<double>(source.radius) - source.radius) * weight;
        const double guardedRadius = interpolatedRadius * 1.001;
        if (!isfinite(guardedRadius) || guardedRadius > FLT_MAX)
        {
            error.Assign("Secondary motion collision radius exceeds the float range");
            return false;
        }
        // floatへ戻した形状をCoreへ渡す。
        FModelSecondaryMotionCollisionShape guarded{};
        guarded.start = { static_cast<float>(startX), static_cast<float>(startY), static_cast<float>(startZ) };
        guarded.end = { static_cast<float>(endX), static_cast<float>(endY), static_cast<float>(endZ) };
        guarded.radius = static_cast<float>(guardedRadius);
        if (!isfinite(guarded.start.x) || !isfinite(guarded.start.y) || !isfinite(guarded.start.z) || !isfinite(guarded.end.x) || !isfinite(guarded.end.y) || !isfinite(guarded.end.z) || !isfinite(guarded.radius) || !(guarded.radius > 0.0f) || !guardedCandidate.Append(guarded))
        {
            error.Assign("Secondary motion guarded shape is invalid");
            return false;
        }
    }

    // 接触計算用の準備値を候補配列へ作る。
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedCandidate;
    if (!PrepareSecondaryMotionCollisionShapes(guardedCandidate.Data(), selectedCount, preparedCandidate, error))
    {
        return false;
    }
    for (uint32_t shapeIndex = 0; shapeIndex < selectedCount; ++shapeIndex)
    {
        const double rootRadius = expandedCandidate.At(shapeIndex).radius;
        const double rootTolerance = fmax(rootRadius * 1.0e-3, rootRadius * DBL_EPSILON * 64.0);
        preparedCandidate.At(shapeIndex).hasRootContactOverride = true;
        preparedCandidate.At(shapeIndex).rootContactThreshold = rootRadius - rootTolerance;
    }

    // 既存配列末尾へ追加できる範囲とrollback位置を記録する。
    const uint32_t oldExpandedCount = expandedShapes.Count();
    const uint32_t oldGuardedCount = guardedShapes.Count();
    const uint32_t oldPreparedCount = preparedShapes.Count();
    const uint32_t oldGroupCount = groups.Count();
    if (selectedCount > UINT32_MAX - oldExpandedCount || selectedCount > UINT32_MAX - oldGuardedCount || selectedCount > UINT32_MAX - oldPreparedCount || oldGroupCount == UINT32_MAX)
    {
        error.Assign("Secondary motion step shape cache exceeds its capacity range");
        return false;
    }
    if (!expandedShapes.Reserve(oldExpandedCount + selectedCount) || !guardedShapes.Reserve(oldGuardedCount + selectedCount) || !preparedShapes.Reserve(oldPreparedCount + selectedCount) || !groups.Reserve(oldGroupCount + 1u))
    {
        error.Assign("Secondary motion step shape cache allocation failed");
        return false;
    }
    // 形状範囲を全配列へ追加し、最後にgroupを公開する。
    FGroup group{};
    group.pointRadius = pointRadius;
    group.colliderMask = effectiveMask;
    group.count = selectedCount;
    group.offset = oldExpandedCount;
    if (!expandedShapes.AppendRange(expandedCandidate.Data(), selectedCount) || !guardedShapes.AppendRange(guardedCandidate.Data(), selectedCount) || !preparedShapes.AppendRange(preparedCandidate.Data(), selectedCount) || !groups.Append(group))
    {
        while (expandedShapes.Count() > oldExpandedCount)
        {
            expandedShapes.RemoveAt(expandedShapes.Count() - 1u);
        }
        while (guardedShapes.Count() > oldGuardedCount)
        {
            guardedShapes.RemoveAt(guardedShapes.Count() - 1u);
        }
        while (preparedShapes.Count() > oldPreparedCount)
        {
            preparedShapes.RemoveAt(preparedShapes.Count() - 1u);
        }
        while (groups.Count() > oldGroupCount)
        {
            groups.RemoveAt(groups.Count() - 1u);
        }
        error.Assign("Secondary motion step shape cache append failed");
        return false;
    }
    // 全配列への追加後にだけviewを出力へ渡す。
    FModelSecondaryMotionStepShapes cached{};
    cached.expanded = expandedShapes.Data() + oldExpandedCount;
    cached.guarded = guardedShapes.Data() + oldGuardedCount;
    cached.prepared = preparedShapes.Data() + oldPreparedCount;
    cached.count = selectedCount;
    output = cached;
    return true;
}

}
