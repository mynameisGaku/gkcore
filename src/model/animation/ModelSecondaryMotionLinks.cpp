// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionLinks.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include <float.h>
#include <math.h>

namespace gk::model
{
namespace
{
// 接続設定の上限。setterでだけ重複を検査する。
const uint32_t MaximumLinks = 1024;

/**
 * 登録済みの自由boneを鎖内の点へ対応付ける。rootと仮想末端は対象外。
 */
bool ResolvePoint(const FModelSecondaryMotionState& state, uint32_t bone, uint32_t& chainIndex, uint32_t& pointIndex)
{
    for (uint32_t chain = 0; chain < state.chains.Count(); ++chain)
    {
        const auto* entry = state.chains.At(chain);
        if (!entry)
        {
            return false;
        }
        for (uint32_t point = 1; point < entry->bones.Count(); ++point)
        {
            if (entry->bones.At(point) == bone)
            {
                chainIndex = chain;
                pointIndex = point;
                return true;
            }
        }
    }
    return false;
}

/**
 * 接続端点を現在の鎖へ解決する。同じ鎖や未登録のboneは失敗する。
 */
bool ResolveLink(const FModelSecondaryMotionState& state, const FModelSecondaryMotionLink& link, uint32_t& firstChain, uint32_t& firstPoint, uint32_t& secondChain, uint32_t& secondPoint, String& error)
{
    if (!isfinite(link.radius) || link.radius < 0.0f || !ResolvePoint(state, link.firstBone, firstChain, firstPoint) || !ResolvePoint(state, link.secondBone, secondChain, secondPoint) || firstChain == secondChain)
    {
        error.Assign("secondary links require free bones in distinct registered chains and a finite nonnegative radius");
        return false;
    }
    return true;
}

/**
 * 接続設定の何番目で失敗したかを診断へ加える。
 */
void LinkFailure(uint32_t index, String& error)
{
    error.Append(" (secondary link: ");
    error.AppendUnsigned(index);
    error.Append(")");
}

/**
 * 倍精度の補正結果を、範囲を失わずfloatへ保存できるか調べる。
 */
bool StorePosition(const FVector3d& input, Vec3& output, String& error)
{
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(input.value[axis]) || fabs(input.value[axis]) > FLT_MAX)
        {
            error.Assign("secondary link position exceeds float range");
            return false;
        }
    }
    output = { static_cast<float>(input.value[0]), static_cast<float>(input.value[1]), static_cast<float>(input.value[2]) };
    return true;
}

/**
 * 候補の全接続線分が、現在の身体形状を避けているか調べる。
 */
bool CheckCandidate(const FModelSecondaryMotionState& state, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, String& error)
{
    for (uint32_t index = 0; index < state.links.Count(); ++index)
    {
        const auto& link = state.links.At(index);
        uint32_t firstChain = 0, firstPoint = 0, secondChain = 0, secondPoint = 0;
        if (!ResolveLink(state, link, firstChain, firstPoint, secondChain, secondPoint, error))
        {
            return false;
        }
        const auto& first = state.chains.At(firstChain)->simulation.points.At(firstPoint).position;
        const auto& second = state.chains.At(secondChain)->simulation.points.At(secondPoint).position;
        if (!animation::CheckSecondaryMotionFreeSegmentContacts(shapes, shapeCount, link.radius, { { first.x, first.y, first.z } }, { { second.x, second.y, second.z } }, error))
        {
            LinkFailure(index, error);
            return false;
        }
    }
    return true;
}
}

bool ValidateSecondaryMotionLinks(const FModelSecondaryMotionState& state, String& error)
{
    error.Clear();
    if (state.links.Count() > MaximumLinks || state.linkIterations == 0 || state.linkIterations > 32)
    {
        error.Assign("secondary link count or iteration limit is invalid");
        return false;
    }
    for (uint32_t index = 0; index < state.links.Count(); ++index)
    {
        const auto& link = state.links.At(index);
        uint32_t firstChain = 0, firstPoint = 0, secondChain = 0, secondPoint = 0;
        if (!ResolveLink(state, link, firstChain, firstPoint, secondChain, secondPoint, error))
        {
            return false;
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier)
        {
            const auto& other = state.links.At(earlier);
            if ((link.firstBone == other.firstBone && link.secondBone == other.secondBone) || (link.firstBone == other.secondBone && link.secondBone == other.firstBone))
            {
                error.Assign("secondary links must not repeat an unordered bone pair");
                return false;
            }
        }
    }
    return true;
}

bool ConstrainSecondaryMotionLinks(FModelSecondaryMotionState& candidate, const FModelSecondaryMotionState& previous, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, double delta, String& error)
{
    if (candidate.links.Count() == 0 || shapeCount == 0)
    {
        error.Clear();
        return true;
    }
    // 前の点との差ではなく、既存Stepへ加えた接触補正だけを速度へ戻す。
    Array<Vec3> beforePositions;
    Array<uint32_t> offsets;
    Array<uint8_t> affected;
    for (uint32_t chain = 0; chain < candidate.chains.Count(); ++chain)
    {
        const auto* entry = candidate.chains.At(chain);
        if (!entry || entry->simulation.points.Count() != entry->bones.Count() + 1 || entry->simulation.previousTargets.Count() != entry->simulation.points.Count() || !offsets.Append(beforePositions.Count()) || !affected.Append(0))
        {
            error.Assign("secondary link chain state is invalid or allocation failed");
            return false;
        }
        for (uint32_t point = 0; point < entry->simulation.points.Count(); ++point)
        {
            if (!beforePositions.Append(entry->simulation.points.At(point).position))
            {
                error.Assign("secondary link correction allocation failed");
                return false;
            }
        }
    }
    for (uint32_t pass = 0; pass < candidate.linkIterations; ++pass)
    {
        for (uint32_t index = 0; index < candidate.links.Count(); ++index)
        {
            const auto& link = candidate.links.At(index);
            uint32_t firstChain = 0, firstPoint = 0, secondChain = 0, secondPoint = 0;
            if (!ResolveLink(candidate, link, firstChain, firstPoint, secondChain, secondPoint, error))
            {
                return false;
            }
            auto& firstState = candidate.chains.At(firstChain)->simulation;
            auto& secondState = candidate.chains.At(secondChain)->simulation;
            auto& first = firstState.points.At(firstPoint).position;
            auto& second = secondState.points.At(secondPoint).position;
            const auto& referenceFirst = firstState.previousTargets.At(firstPoint);
            const auto& referenceSecond = secondState.previousTargets.At(secondPoint);
            FVector3d projectedFirst{ { first.x, first.y, first.z } }, projectedSecond{ { second.x, second.y, second.z } };
            if (!animation::ProjectSecondaryMotionFreeSegmentContacts(shapes, shapeCount, link.radius, { { referenceFirst.x, referenceFirst.y, referenceFirst.z } }, { { referenceSecond.x, referenceSecond.y, referenceSecond.z } }, projectedFirst, projectedSecond, error) || !StorePosition(projectedFirst, first, error) || !StorePosition(projectedSecond, second, error))
            {
                LinkFailure(index, error);
                return false;
            }
            affected.At(firstChain) = 1;
            affected.At(secondChain) = 1;
        }
        // 接続の押し出しで変わった鎖だけ、同じ目標へ長さと角度を戻す。
        for (uint32_t chain = 0; chain < candidate.chains.Count(); ++chain)
        {
            if (!affected.At(chain))
            {
                continue;
            }
            auto& entry = *candidate.chains.At(chain);
            if (!animation::StepSecondaryMotionChainWithCollisions(entry.simulation.previousTargets.Data(), entry.simulation.previousTargets.Count(), entry.settings, 0.0, shapes, shapes, shapeCount, entry.simulation, error, entry.collisionRadius))
            {
                return false;
            }
        }
        if (CheckCandidate(candidate, shapes, shapeCount, error))
        {
            // 既存の最後の小刻み更新と同じ秒数で、接触補正分を速度へ足す。
            const double step = delta > 0.0 && delta < 0.25 ? delta / ceil(delta * 120.0) : 0.0;
            for (uint32_t chain = 0; chain < candidate.chains.Count(); ++chain)
            {
                if (!affected.At(chain) || !(step > 0.0))
                {
                    continue;
                }
                auto& entry = *candidate.chains.At(chain);
                const auto* old = previous.chains.At(chain);
                if (!old || old->simulation.previousTargets.Count() == 0)
                {
                    continue;
                }
                const auto& oldRoot = old->simulation.previousTargets.At(0);
                const auto& root = entry.simulation.previousTargets.At(0);
                const double dx = static_cast<double>(root.x) - oldRoot.x, dy = static_cast<double>(root.y) - oldRoot.y, dz = static_cast<double>(root.z) - oldRoot.z;
                if (sqrt(dx * dx + dy * dy + dz * dz) > entry.settings.teleportDistance)
                {
                    continue;
                }
                for (uint32_t point = 1; point < entry.simulation.points.Count(); ++point)
                {
                    auto& current = entry.simulation.points.At(point);
                    const auto& before = beforePositions.At(offsets.At(chain) + point);
                    const FVector3d velocity{ { current.velocity.x + (static_cast<double>(current.position.x) - before.x) / step, current.velocity.y + (static_cast<double>(current.position.y) - before.y) / step, current.velocity.z + (static_cast<double>(current.position.z) - before.z) / step } };
                    if (!StorePosition(velocity, current.velocity, error))
                    {
                        return false;
                    }
                }
            }
            error.Clear();
            return true;
        }
    }
    error.Assign("secondary links could not preserve body contact and chain constraints together");
    return false;
}

bool CheckRenderedSecondaryMotionLinks(const FModelSecondaryMotionState& state, const Array<float>& matrices, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, String& error)
{
    for (uint32_t index = 0; index < state.links.Count(); ++index)
    {
        const auto& link = state.links.At(index);
        const uint64_t first = static_cast<uint64_t>(link.firstBone) * 16u;
        const uint64_t second = static_cast<uint64_t>(link.secondBone) * 16u;
        if (first + 15u >= matrices.Count() || second + 15u >= matrices.Count())
        {
            error.Assign("secondary link rendered bone is outside pose matrices");
            return false;
        }
        const FVector3d firstPosition{ { matrices.At(static_cast<uint32_t>(first + 12u)), matrices.At(static_cast<uint32_t>(first + 13u)), matrices.At(static_cast<uint32_t>(first + 14u)) } };
        const FVector3d secondPosition{ { matrices.At(static_cast<uint32_t>(second + 12u)), matrices.At(static_cast<uint32_t>(second + 13u)), matrices.At(static_cast<uint32_t>(second + 14u)) } };
        if (!animation::CheckSecondaryMotionFreeSegmentContacts(shapes, shapeCount, link.radius, firstPosition, secondPosition, error))
        {
            LinkFailure(index, error);
            return false;
        }
    }
    return true;
}
}
