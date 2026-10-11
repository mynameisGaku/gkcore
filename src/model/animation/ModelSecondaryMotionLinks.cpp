// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionLinks.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/FModelSecondaryMotionStepShapeCache.h"
#include <float.h>
#include <math.h>
#include <string.h>

namespace gk::model
{
namespace
{
// 接続設定の上限。setterでだけ重複を検査する。
const uint32_t MaximumLinks = 1024;
// 候補の接触誤差に使う割合。残り半分は姿勢をFKへ戻す丸めの余白に残す。
const double CandidateContactToleranceFraction = 0.5;

/**
 * 1回の更新中だけ保持する接続端点と形状配列の参照位置。
 */
struct FResolvedSecondaryMotionLink
{
    // 始点を所有する鎖の番号。
    uint32_t firstChain = 0;
    // 始点の鎖内の自由節番号。
    uint32_t firstPoint = 0;
    // 終点を所有する別の鎖の番号。
    uint32_t secondChain = 0;
    // 終点の鎖内の自由節番号。
    uint32_t secondPoint = 0;
    // 同じ半径向けに準備した身体形状群の開始位置。
    uint32_t shapeOffset = 0;
};

/**
 * 同じ線分半径に対する身体形状群の対応を保持する。
 */
struct FLinkShapeGroup
{
    // この形状群へ加えた線分半径。
    float radius = 0.0f;
    // 全準備形状の中で、この群が始まる位置。
    uint32_t offset = 0;
};

#if defined(GKCORE_TESTING)
// 接続solver内で解決した端点組と、準備した形状の数。
uint64_t secondaryMotionLinkResolutionCount = 0;
uint64_t secondaryMotionLinkShapePreparationCount = 0;
// 接続投影後に鎖の長さ・角度・接触を復元した呼び出し数。
uint64_t secondaryMotionLinkChainConstraintCount = 0;
#endif

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
 * 接続端点と半径ごとの身体形状を準備する。不正設定や確保失敗は候補を破棄する。
 */
bool PrepareLinks(const FModelSecondaryMotionState& state, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, Array<FResolvedSecondaryMotionLink>& links, Array<animation::FPreparedSecondaryMotionCollisionShape>& preparedShapes, String& error)
{
    // 上限の積は65536で、形状群の開始位置をuint32_tへ収められる。
    if (state.links.Count() > MaximumLinks || shapeCount > 64 || !links.Reserve(state.links.Count()))
    {
        error.Assign("secondary link preparation count or allocation is invalid");
        return false;
    }
    Array<FLinkShapeGroup> groups;
    for (uint32_t index = 0; index < state.links.Count(); ++index)
    {
        const auto& link = state.links.At(index);
        FResolvedSecondaryMotionLink resolved;
        if (!ResolveLink(state, link, resolved.firstChain, resolved.firstPoint, resolved.secondChain, resolved.secondPoint, error))
        {
            return false;
        }
        // 同じfloat半径は従来の拡張処理も同じ結果になるため共有する。
        bool found = false;
        for (uint32_t group = 0; group < groups.Count(); ++group)
        {
            if (groups.At(group).radius == link.radius)
            {
                resolved.shapeOffset = groups.At(group).offset;
                found = true;
                break;
            }
        }
        if (!found)
        {
            Array<animation::FPreparedSecondaryMotionCollisionShape> preparedGroup;
            if (!animation::PrepareSecondaryMotionFreeSegmentShapes(shapes, shapeCount, link.radius, preparedGroup, error))
            {
                return false;
            }
            resolved.shapeOffset = preparedShapes.Count();
            if (!preparedShapes.AppendRange(preparedGroup.Data(), preparedGroup.Count()) || !groups.Append(FLinkShapeGroup{ link.radius, resolved.shapeOffset }))
            {
                error.Assign("secondary link prepared shapes allocation failed");
                return false;
            }
#if defined(GKCORE_TESTING)
            secondaryMotionLinkShapePreparationCount += preparedGroup.Count();
#endif
        }
        if (!links.Append(resolved))
        {
            error.Assign("secondary link endpoint allocation failed");
            return false;
        }
#if defined(GKCORE_TESTING)
        ++secondaryMotionLinkResolutionCount;
#endif
    }
    return true;
}

/**
 * 全線分を検査し、不成立線分の両端鎖を次の復元対象へ予約する。
 */
bool CheckCandidate(const FModelSecondaryMotionState& state, const Array<FResolvedSecondaryMotionLink>& links, const Array<animation::FPreparedSecondaryMotionCollisionShape>& shapes, uint32_t shapeCount, Array<uint8_t>& restorePending, String& error)
{
    // 最初の失敗をerrorに保ち、残りの線分も次の反復へ必要な鎖を集める。
    bool clear = true;
    String remainingError;
    for (uint32_t index = 0; index < links.Count(); ++index)
    {
        const auto& link = links.At(index);
        const auto& first = state.chains.At(link.firstChain)->simulation.points.At(link.firstPoint).position;
        const auto& second = state.chains.At(link.secondChain)->simulation.points.At(link.secondPoint).position;
        // 接触許容幅の半分をFKの丸め用に残し、鎖の長さを戻す交互補正も有限回で確定する。
        String& diagnostic = clear ? error : remainingError;
        if (!animation::CheckPreparedSecondaryMotionFreeSegmentContacts(shapes.Data() + link.shapeOffset, shapeCount, { { first.x, first.y, first.z } }, { { second.x, second.y, second.z } }, diagnostic, CandidateContactToleranceFraction))
        {
            restorePending.At(link.firstChain) = 1;
            restorePending.At(link.secondChain) = 1;
            if (clear)
            {
                LinkFailure(index, error);
            }
            clear = false;
        }
    }
    return clear;
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

#if defined(GKCORE_TESTING)
void ResetSecondaryMotionLinkPreparationCounts()
{
    secondaryMotionLinkResolutionCount = 0;
    secondaryMotionLinkShapePreparationCount = 0;
    secondaryMotionLinkChainConstraintCount = 0;
}
uint64_t GetSecondaryMotionLinkResolutionCount()
{
    return secondaryMotionLinkResolutionCount;
}
uint64_t GetSecondaryMotionLinkShapePreparationCount()
{
    return secondaryMotionLinkShapePreparationCount;
}
uint64_t GetSecondaryMotionLinkChainConstraintCount()
{
    return secondaryMotionLinkChainConstraintCount;
}
#endif

bool ConstrainSecondaryMotionLinks(FModelSecondaryMotionState& candidate, const FModelSecondaryMotionState& previous, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, double delta, String& error)
{
    if (candidate.links.Count() == 0 || shapeCount == 0)
    {
        error.Clear();
        return true;
    }
    // 更新中は鎖や身体設定を変えないため、端点と半径別形状を再利用する。
    Array<FResolvedSecondaryMotionLink> resolvedLinks;
    Array<animation::FPreparedSecondaryMotionCollisionShape> preparedShapes;
    if (!PrepareLinks(candidate, shapes, shapeCount, resolvedLinks, preparedShapes, error))
    {
        return false;
    }
    // 身体形状は更新中に変わらないため、鎖の節半径ごとに前処理を共有する。
    animation::FModelSecondaryMotionStepShapeCache chainShapeCache(shapes, shapeCount);
    // 前の点との差ではなく、既存Stepへ加えた接触補正だけを速度へ戻す。
    Array<Vec3> beforePositions;
    Array<uint32_t> offsets;
    Array<uint8_t> affected;
    // 投影で変わった鎖と、前回の不成立線分が要求する鎖の復元予約。
    Array<uint8_t> restorePending;
    for (uint32_t chain = 0; chain < candidate.chains.Count(); ++chain)
    {
        const auto* entry = candidate.chains.At(chain);
        if (!entry || entry->simulation.points.Count() != entry->bones.Count() + 1 || entry->simulation.previousTargets.Count() != entry->simulation.points.Count() || !offsets.Append(beforePositions.Count()) || !affected.Append(0) || !restorePending.Append(0))
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
        for (uint32_t index = 0; index < resolvedLinks.Count(); ++index)
        {
            const auto& link = resolvedLinks.At(index);
            auto& firstState = candidate.chains.At(link.firstChain)->simulation;
            auto& secondState = candidate.chains.At(link.secondChain)->simulation;
            auto& first = firstState.points.At(link.firstPoint).position;
            auto& second = secondState.points.At(link.secondPoint).position;
            const auto& referenceFirst = firstState.previousTargets.At(link.firstPoint);
            const auto& referenceSecond = secondState.previousTargets.At(link.secondPoint);
            // float保存後の位置を比べ、符号付きゼロを含む変化も復元対象にする。
            const Vec3 firstBefore = first, secondBefore = second;
            FVector3d projectedFirst{ { first.x, first.y, first.z } }, projectedSecond{ { second.x, second.y, second.z } };
            if (!animation::ProjectPreparedSecondaryMotionFreeSegmentContacts(preparedShapes.Data() + link.shapeOffset, shapeCount, { { referenceFirst.x, referenceFirst.y, referenceFirst.z } }, { { referenceSecond.x, referenceSecond.y, referenceSecond.z } }, projectedFirst, projectedSecond, error) || !StorePosition(projectedFirst, first, error) || !StorePosition(projectedSecond, second, error))
            {
                LinkFailure(index, error);
                error.Append(" (link projection)");
                return false;
            }
            affected.At(link.firstChain) = 1;
            affected.At(link.secondChain) = 1;
            if (memcmp(&firstBefore, &first, sizeof(first)) != 0)
            {
                restorePending.At(link.firstChain) = 1;
            }
            if (memcmp(&secondBefore, &second, sizeof(second)) != 0)
            {
                restorePending.At(link.secondChain) = 1;
            }
        }
        // 初回は接続鎖をすべて検証し、以後は変更または不成立が残る鎖だけ復元する。
        for (uint32_t chain = 0; chain < candidate.chains.Count(); ++chain)
        {
            if (!affected.At(chain) || (pass > 0 && !restorePending.At(chain)))
            {
                continue;
            }
            auto& entry = *candidate.chains.At(chain);
            // viewは次のcache取得までに使い切り、鎖や次の更新へ保持しない。
            animation::FModelSecondaryMotionStepShapes chainShapes;
            if (!chainShapeCache.Get(entry.collisionRadius, chainShapes, error))
            {
                return false;
            }
#if defined(GKCORE_TESTING)
            ++secondaryMotionLinkChainConstraintCount;
#endif
            if (!animation::StepSecondaryMotionChainWithPreparedCollisions(entry.simulation.previousTargets.Data(), entry.simulation.previousTargets.Count(), entry.settings, chainShapes, entry.simulation, error))
            {
                return false;
            }
            restorePending.At(chain) = 0;
        }
        if (CheckCandidate(candidate, resolvedLinks, preparedShapes, shapeCount, restorePending, error))
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
            error.Append(" (rendered link)");
            return false;
        }
    }
    return true;
}
}
