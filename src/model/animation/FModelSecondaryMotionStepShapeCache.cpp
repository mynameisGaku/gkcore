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
    error.Clear();
    if (!isfinite(pointRadius) || pointRadius < 0.0f || sourceShapeCount > 64u || (sourceShapeCount > 0u && !sourceShapes) || !ValidateSecondaryMotionCollisionShapes(sourceShapes, sourceShapeCount, error))
    {
        if (error.Empty())
            error.Assign("Secondary motion step shape cache input is invalid");
        return false;
    }
    if (sourceShapeCount == 0u)
    {
        // 空入力では参照先をすべてnullにする。
        FModelSecondaryMotionStepShapes empty{};
        output = empty;
        return true;
    }
    for (uint32_t groupIndex = 0; groupIndex < groups.Count(); ++groupIndex)
    {
        if (groups.At(groupIndex).pointRadius == pointRadius)
        {
            // 半径一致時は連結配列内の既存範囲を返す。
            const uint32_t offset = groups.At(groupIndex).offset;
            FModelSecondaryMotionStepShapes cached{};
            cached.expanded = expandedShapes.Data() + offset;
            cached.guarded = guardedShapes.Data() + offset;
            cached.prepared = preparedShapes.Data() + offset;
            cached.count = sourceShapeCount;
            output = cached;
            return true;
        }
    }

    // 節半径を足した元形状を一時配列へ作る。
    gk::Array<FModelSecondaryMotionCollisionShape> expandedCandidate;
    if (pointRadius == 0.0f)
    {
        if (!expandedCandidate.Reserve(sourceShapeCount) || !expandedCandidate.AppendRange(sourceShapes, sourceShapeCount))
        {
            error.Assign("Secondary motion step shape copy failed");
            return false;
        }
    }
    else if (!ExpandSecondaryMotionCollisionShapes(sourceShapes, sourceShapeCount, pointRadius, expandedCandidate, error))
    {
        return false;
    }

    // 接触許容幅を相殺するguard形状を一時配列へ作る。
    gk::Array<FModelSecondaryMotionCollisionShape> guardedCandidate;
    if (!guardedCandidate.Reserve(sourceShapeCount))
    {
        error.Assign("Secondary motion guarded shape allocation failed");
        return false;
    }
    for (uint32_t shapeIndex = 0; shapeIndex < sourceShapeCount; ++shapeIndex)
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
    if (!PrepareSecondaryMotionCollisionShapes(guardedCandidate.Data(), sourceShapeCount, preparedCandidate, error))
    {
        return false;
    }

    // 既存配列末尾へ追加できる範囲とrollback位置を記録する。
    const uint32_t oldExpandedCount = expandedShapes.Count();
    const uint32_t oldGuardedCount = guardedShapes.Count();
    const uint32_t oldPreparedCount = preparedShapes.Count();
    const uint32_t oldGroupCount = groups.Count();
    if (sourceShapeCount > UINT32_MAX - oldExpandedCount || sourceShapeCount > UINT32_MAX - oldGuardedCount || sourceShapeCount > UINT32_MAX - oldPreparedCount || oldGroupCount == UINT32_MAX)
    {
        error.Assign("Secondary motion step shape cache exceeds its capacity range");
        return false;
    }
    if (!expandedShapes.Reserve(oldExpandedCount + sourceShapeCount) || !guardedShapes.Reserve(oldGuardedCount + sourceShapeCount) || !preparedShapes.Reserve(oldPreparedCount + sourceShapeCount) || !groups.Reserve(oldGroupCount + 1u))
    {
        error.Assign("Secondary motion step shape cache allocation failed");
        return false;
    }
    // 形状範囲を全配列へ追加し、最後にgroupを公開する。
    FGroup group{};
    group.pointRadius = pointRadius;
    group.offset = oldExpandedCount;
    if (!expandedShapes.AppendRange(expandedCandidate.Data(), sourceShapeCount) || !guardedShapes.AppendRange(guardedCandidate.Data(), sourceShapeCount) || !preparedShapes.AppendRange(preparedCandidate.Data(), sourceShapeCount) || !groups.Append(group))
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
    cached.count = sourceShapeCount;
    output = cached;
    return true;
}

}
