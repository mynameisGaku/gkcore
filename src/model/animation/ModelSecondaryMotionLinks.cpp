// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionLinks.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionDistances.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/FModelSecondaryMotionStepShapeCache.h"
#include <float.h>
#include <math.h>
#if defined(GKCORE_TESTING) || defined(GKCORE_TEST_FRAME_CAPTURE)
#include <stdio.h>
#include <stdlib.h>
#endif
#include <string.h>

namespace gk::model
{
namespace
{
// 接続設定の上限。setterでだけ重複を検査する。
const uint32_t MaximumLinks = 1024;
// 距離制約の上限。
const uint32_t MaximumDistanceConstraints = 1024;
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
 * 1回の更新中だけ保持する距離制約の鎖内端点。
 */
struct FResolvedSecondaryMotionDistance
{
    // 始点を所有する鎖の番号。
    uint32_t firstChain = 0;
    // 始点の鎖内の自由節番号。
    uint32_t firstPoint = 0;
    // 終点を所有する鎖の番号。
    uint32_t secondChain = 0;
    // 終点の鎖内の自由節番号。
    uint32_t secondPoint = 0;
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
 * 距離制約を別鎖に属する二つの自由節へ対応付ける。
 */
bool ResolveDistance(const FModelSecondaryMotionState& state, const FModelSecondaryMotionDistanceConstraint& constraint, uint32_t& firstChain, uint32_t& firstPoint, uint32_t& secondChain, uint32_t& secondPoint, String& error)
{
    if (!isfinite(constraint.restDistance) || !(constraint.restDistance > 0.0f) || !isfinite(constraint.compliance) || constraint.compliance < 0.0f || !ResolvePoint(state, constraint.firstBone, firstChain, firstPoint) || !ResolvePoint(state, constraint.secondBone, secondChain, secondPoint) || firstChain == secondChain)
    {
        error.Assign("secondary distances require separate registered free bones and finite positive distance with nonnegative compliance");
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
 * 距離制約の何番目で失敗したかを診断へ加える。
 */
void DistanceFailure(uint32_t index, String& error)
{
    error.Append(" (secondary distance: ");
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
 * 接続端点の現在親位置と、登録済み基準方向・曲げ角をanchorへまとめる。
 */
bool BuildFreeSegmentAnchor(const animation::FModelSecondaryMotionChainState& simulation, uint32_t pointIndex, float maxAngleDegrees, animation::FSecondaryMotionFreeSegmentAnchor& anchor, String& error)
{
    if (pointIndex == 0 || pointIndex >= simulation.points.Count() || pointIndex >= simulation.previousTargets.Count() || !isfinite(maxAngleDegrees) || maxAngleDegrees < 0.0f || maxAngleDegrees > 180.0f)
    {
        error.Assign("secondary link anchor index or angle is invalid");
        return false;
    }
    // 投影直前の親節位置。
    const auto& parent = simulation.points.At(pointIndex - 1u).position;
    // 登録時基準姿勢の隣接2点。
    const auto& referenceParent = simulation.previousTargets.At(pointIndex - 1u);
    const auto& referenceChild = simulation.previousTargets.At(pointIndex);
    // 基準の親から子への方向成分。
    const double dx = static_cast<double>(referenceChild.x) - referenceParent.x;
    const double dy = static_cast<double>(referenceChild.y) - referenceParent.y;
    const double dz = static_cast<double>(referenceChild.z) - referenceParent.z;
    // 基準方向を単位長にする距離。
    const double length = sqrt(dx * dx + dy * dy + dz * dz);
    if (!(length > 0.0) || !isfinite(length))
    {
        error.Assign("secondary link anchor reference direction is degenerate");
        return false;
    }
    // 許容曲げ角をradianへ変換した値。
    const double radians = static_cast<double>(maxAngleDegrees) * (3.1415926535897932384626433832795 / 180.0);
    anchor.parentPosition = { { parent.x, parent.y, parent.z } };
    anchor.referenceDirection = { { dx / length, dy / length, dz / length } };
    anchor.cosineLimit = cos(radians);
    return isfinite(anchor.cosineLimit);
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
bool CheckCandidate(const FModelSecondaryMotionState& state, const Array<FResolvedSecondaryMotionLink>& links, const Array<FResolvedSecondaryMotionDistance>& distances, const Array<animation::FPreparedSecondaryMotionCollisionShape>& shapes, uint32_t shapeCount, Array<uint8_t>& restorePending, String& error)
{
    // 最初の失敗をerrorに保ち、残りの制約も次の反復へ必要な鎖を集める。
    bool clear = true;
    String remainingError;
    for (uint32_t index = 0; index < distances.Count(); ++index)
    {
        const auto& distance = distances.At(index);
        const auto& constraint = state.distanceConstraints.At(index);
        const auto& first = state.chains.At(distance.firstChain)->simulation.points.At(distance.firstPoint).position;
        const auto& second = state.chains.At(distance.secondChain)->simulation.points.At(distance.secondPoint).position;
        String& diagnostic = clear ? error : remainingError;
        if (!animation::CheckSecondaryMotionDistance(constraint.restDistance, constraint.compliance, state.distanceStepSeconds, state.distanceLambdas.At(index), { { first.x, first.y, first.z } }, { { second.x, second.y, second.z } }, diagnostic, CandidateContactToleranceFraction))
        {
            restorePending.At(distance.firstChain) = 1;
            restorePending.At(distance.secondChain) = 1;
            if (clear)
                DistanceFailure(index, error);
            clear = false;
        }
    }
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
    if (clear)
        error.Clear();
    return clear;
}

#if defined(GKCORE_TESTING) || defined(GKCORE_TEST_FRAME_CAPTURE)
/**
 * 1段階の制約残差と形状別最大侵入を、失敗時出力用に保持する。
 */
struct FSecondaryMotionConstraintTraceStage
{
    // この記録が属する反復番号。
    uint32_t pass = 0;
    // 距離制約全体で最大の絶対残差。
    double maxDistanceResidual = 0.0;
    // 最大残差を持つ距離制約番号。
    uint32_t distanceIndex = 0;
    // 残差計算がすべて有限値だったか。
    bool distanceValid = false;
    // 身体形状ごとに見た接続線分の最大侵入深さ。
    double maxPenetration[64]{};
    // 形状ごとの最大侵入を生じた接続線分番号。
    uint32_t maxPenetrationLink[64]{};
    // 形状ごとの侵入計算がすべて成功したか。
    uint8_t shapeValid[64]{};
};

/**
 * 診断環境変数が有効なときだけ数値採取を行う。
 */
bool IsSecondaryMotionConstraintTraceEnabled()
{
    const char* setting = getenv("GKCORE_TRACE_SECONDARY_CONSTRAINTS");
    return setting && setting[0] == '1' && setting[1] == '\0';
}

/**
 * 現在の候補から距離残差と各形状の最大侵入を計算する。
 */
void CaptureSecondaryMotionConstraintStage(uint32_t pass, const FModelSecondaryMotionState& state, const Array<FResolvedSecondaryMotionLink>& links, const Array<FResolvedSecondaryMotionDistance>& distances, const Array<animation::FPreparedSecondaryMotionCollisionShape>& shapes, uint32_t shapeCount, FSecondaryMotionConstraintTraceStage& output)
{
    output = {};
    output.pass = pass;
    output.maxDistanceResidual = -1.0;
    output.distanceIndex = UINT32_MAX;
    output.distanceValid = true;
    for (uint32_t shape = 0; shape < shapeCount; ++shape)
    {
        output.maxPenetration[shape] = -DBL_MAX;
        output.maxPenetrationLink[shape] = UINT32_MAX;
        output.shapeValid[shape] = 1;
    }
    for (uint32_t index = 0; index < distances.Count(); ++index)
    {
        const auto& resolved = distances.At(index);
        const auto& constraint = state.distanceConstraints.At(index);
        const auto& first = state.chains.At(resolved.firstChain)->simulation.points.At(resolved.firstPoint).position;
        const auto& second = state.chains.At(resolved.secondChain)->simulation.points.At(resolved.secondPoint).position;
        const double dx = static_cast<double>(second.x) - first.x;
        const double dy = static_cast<double>(second.y) - first.y;
        const double dz = static_cast<double>(second.z) - first.z;
        const double length = sqrt(dx * dx + dy * dy + dz * dz);
        const double alpha = constraint.compliance == 0.0f ? 0.0 : static_cast<double>(constraint.compliance) / (state.distanceStepSeconds * state.distanceStepSeconds);
        const double residual = length - constraint.restDistance - alpha * state.distanceLambdas.At(index);
        if (!isfinite(residual))
        {
            output.distanceValid = false;
            continue;
        }
        const double absoluteResidual = fabs(residual);
        if (absoluteResidual > output.maxDistanceResidual)
        {
            output.maxDistanceResidual = absoluteResidual;
            output.distanceIndex = index;
        }
    }
    if (distances.Count() == 0)
    {
        output.maxDistanceResidual = 0.0;
        output.distanceIndex = UINT32_MAX;
    }
    String measurementError;
    for (uint32_t linkIndex = 0; linkIndex < links.Count(); ++linkIndex)
    {
        const auto& link = links.At(linkIndex);
        const auto& first = state.chains.At(link.firstChain)->simulation.points.At(link.firstPoint).position;
        const auto& second = state.chains.At(link.secondChain)->simulation.points.At(link.secondPoint).position;
        const FVector3d firstDouble{ { first.x, first.y, first.z } };
        const FVector3d secondDouble{ { second.x, second.y, second.z } };
        for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
        {
            const auto& shape = shapes.At(link.shapeOffset + shapeIndex);
            double penetration = 0.0;
            if (!animation::MeasurePreparedSecondaryMotionFreeSegmentPenetration(shape, firstDouble, secondDouble, penetration, measurementError))
            {
                output.shapeValid[shapeIndex] = 0;
                continue;
            }
            if (penetration > output.maxPenetration[shapeIndex])
            {
                output.maxPenetration[shapeIndex] = penetration;
                output.maxPenetrationLink[shapeIndex] = linkIndex;
            }
        }
    }
    for (uint32_t shape = 0; shape < shapeCount; ++shape)
    {
        if (output.maxPenetrationLink[shape] == UINT32_MAX)
        {
            output.maxPenetration[shape] = 0.0;
        }
    }
}

/**
 * 非収束時に最後の2反復を3段階ずつJSON行として出力する。
 */
void WriteSecondaryMotionConstraintTrace(const FSecondaryMotionConstraintTraceStage stages[2][3], uint32_t iterations, uint32_t shapeCount, double delta, double stepSeconds)
{
    static const char* stageNames[] = { "after_distance", "after_links", "after_restore" };
    const uint32_t firstPass = iterations > 1u ? iterations - 2u : 0u;
    for (uint32_t pass = firstPass; pass < iterations; ++pass)
    {
        const uint32_t slot = pass & 1u;
        for (uint32_t stage = 0; stage < 3; ++stage)
        {
            const auto& sample = stages[slot][stage];
            fprintf(stderr, "{\"type\":\"gkcore_secondary_constraint_trace\",\"delta\":%.17g,\"h\":%.17g,\"iterations\":%u,\"pass\":%u,\"stage\":\"%s\",\"distance\":{\"index\":", delta, stepSeconds, iterations, sample.pass, stageNames[stage]);
            if (sample.distanceValid && sample.distanceIndex != UINT32_MAX)
                fprintf(stderr, "%u,\"absResidual\":%.17g},\"shapes\":[", sample.distanceIndex, sample.maxDistanceResidual);
            else
                fprintf(stderr, "null,\"absResidual\":null},\"shapes\":[");
            for (uint32_t shape = 0; shape < shapeCount; ++shape)
            {
                if (shape > 0)
                    fprintf(stderr, ",");
                if (sample.shapeValid[shape] && sample.maxPenetrationLink[shape] != UINT32_MAX)
                    fprintf(stderr, "{\"shape\":%u,\"link\":%u,\"maxPenetration\":%.17g}", shape, sample.maxPenetrationLink[shape], sample.maxPenetration[shape]);
                else if (sample.shapeValid[shape])
                    fprintf(stderr, "{\"shape\":%u,\"link\":null,\"maxPenetration\":0}", shape);
                else
                    fprintf(stderr, "{\"shape\":%u,\"link\":null,\"maxPenetration\":null}", shape);
            }
            fprintf(stderr, "]}\n");
        }
    }
}
#endif
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

bool ValidateSecondaryMotionDistanceConstraints(const FModelSecondaryMotionState& state, String& error)
{
    error.Clear();
    if (state.distanceConstraints.Count() == 0u)
    {
        if (state.distanceLambdas.Count() != 0u)
        {
            error.Assign("secondary distance lambda state has no matching constraints");
            return false;
        }
        return true;
    }
    if (state.distanceConstraints.Count() > MaximumDistanceConstraints || state.distanceLambdas.Count() != state.distanceConstraints.Count() || state.linkIterations == 0 || state.linkIterations > 32 || !isfinite(state.distanceStepSeconds) || !(state.distanceStepSeconds > 0.0))
    {
        error.Assign("secondary distance count, lambda state, time step, or iteration limit is invalid");
        return false;
    }
    for (uint32_t index = 0; index < state.distanceConstraints.Count(); ++index)
    {
        const auto& constraint = state.distanceConstraints.At(index);
        uint32_t firstChain = 0;
        uint32_t firstPoint = 0;
        uint32_t secondChain = 0;
        uint32_t secondPoint = 0;
        if (!ResolveDistance(state, constraint, firstChain, firstPoint, secondChain, secondPoint, error) || !isfinite(state.distanceLambdas.At(index)))
        {
            if (error.Empty())
                error.Assign("secondary distance lambda is non-finite");
            DistanceFailure(index, error);
            return false;
        }
        for (uint32_t earlier = 0; earlier < index; ++earlier)
        {
            const auto& other = state.distanceConstraints.At(earlier);
            if ((constraint.firstBone == other.firstBone && constraint.secondBone == other.secondBone) || (constraint.firstBone == other.secondBone && constraint.secondBone == other.firstBone))
            {
                error.Assign("secondary distances must not repeat an unordered bone pair");
                DistanceFailure(index, error);
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
    const bool activeLinks = candidate.links.Count() > 0 && shapeCount > 0;
    const bool activeDistances = candidate.distanceConstraints.Count() > 0;
    if (!activeLinks && !activeDistances)
    {
        error.Clear();
        return true;
    }
    if (activeDistances)
    {
        candidate.distanceStepSeconds = delta > 0.0 && delta < 0.25 ? delta : 1.0 / 120.0;
        if (!ValidateSecondaryMotionDistanceConstraints(candidate, error))
            return false;
    }
    // 更新中は鎖や身体設定を変えないため、端点と半径別形状を再利用する。
    Array<FResolvedSecondaryMotionLink> resolvedLinks;
    Array<animation::FPreparedSecondaryMotionCollisionShape> preparedShapes;
    if (activeLinks && !PrepareLinks(candidate, shapes, shapeCount, resolvedLinks, preparedShapes, error))
    {
        return false;
    }
    // 距離制約の端点も更新中に一度だけ鎖内位置へ解決する。
    Array<FResolvedSecondaryMotionDistance> resolvedDistances;
    if (!resolvedDistances.Reserve(candidate.distanceConstraints.Count()))
    {
        error.Assign("secondary distance endpoint allocation failed");
        return false;
    }
    Array<uint8_t> distanceChains;
    if (!distanceChains.Reserve(candidate.chains.Count()))
    {
        error.Assign("secondary distance chain allocation failed");
        return false;
    }
    for (uint32_t chain = 0; chain < candidate.chains.Count(); ++chain)
    {
        if (!distanceChains.Append(0))
        {
            error.Assign("secondary distance chain allocation failed");
            return false;
        }
    }
    for (uint32_t index = 0; index < candidate.distanceConstraints.Count(); ++index)
    {
        const auto& constraint = candidate.distanceConstraints.At(index);
        FResolvedSecondaryMotionDistance resolved{};
        if (!ResolveDistance(candidate, constraint, resolved.firstChain, resolved.firstPoint, resolved.secondChain, resolved.secondPoint, error) || !resolvedDistances.Append(resolved))
        {
            if (error.Empty())
                error.Assign("secondary distance endpoint allocation failed");
            DistanceFailure(index, error);
            return false;
        }
        distanceChains.At(resolved.firstChain) = 1;
        distanceChains.At(resolved.secondChain) = 1;
        // XPBD lambdaは前回フレームから持ち越さず、このframe-level solveで蓄積する。
        candidate.distanceLambdas.At(index) = 0.0;
    }
    // 身体形状は更新中に変わらないため、鎖の節半径ごとに前処理を共有する。
    animation::FModelSecondaryMotionStepShapeCache chainShapeCache(shapes, shapeCount);
    // このUpdateで複数回呼ぶ鎖solverのscratch容量を共有する。
    animation::FModelSecondaryMotionSolverWorkspace solverWorkspace;
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
#if defined(GKCORE_TESTING) || defined(GKCORE_TEST_FRAME_CAPTURE)
    const bool traceSecondaryConstraints = IsSecondaryMotionConstraintTraceEnabled();
    FSecondaryMotionConstraintTraceStage constraintTrace[2][3]{};
#endif
    for (uint32_t pass = 0; pass < candidate.linkIterations; ++pass)
    {
#if defined(GKCORE_TESTING) || defined(GKCORE_TEST_FRAME_CAPTURE)
        const bool captureConstraintPass = traceSecondaryConstraints && pass + 2u >= candidate.linkIterations;
#endif
        for (uint32_t index = 0; index < resolvedDistances.Count(); ++index)
        {
            const auto& distance = resolvedDistances.At(index);
            const auto& constraint = candidate.distanceConstraints.At(index);
            auto& firstState = candidate.chains.At(distance.firstChain)->simulation;
            auto& secondState = candidate.chains.At(distance.secondChain)->simulation;
            auto& first = firstState.points.At(distance.firstPoint).position;
            auto& second = secondState.points.At(distance.secondPoint).position;
            const auto& referenceFirst = firstState.previousTargets.At(distance.firstPoint);
            const auto& referenceSecond = secondState.previousTargets.At(distance.secondPoint);
            const Vec3 firstBefore = first;
            const Vec3 secondBefore = second;
            FVector3d projectedFirst{ { first.x, first.y, first.z } };
            FVector3d projectedSecond{ { second.x, second.y, second.z } };
            if (!animation::ProjectSecondaryMotionDistance({ { referenceFirst.x, referenceFirst.y, referenceFirst.z } }, { { referenceSecond.x, referenceSecond.y, referenceSecond.z } }, constraint.restDistance, constraint.compliance, candidate.distanceStepSeconds, candidate.distanceLambdas.At(index), projectedFirst, projectedSecond, error))
            {
                DistanceFailure(index, error);
                return false;
            }
            Vec3 storedFirst = first;
            Vec3 storedSecond = second;
            if (!StorePosition(projectedFirst, storedFirst, error) || !StorePosition(projectedSecond, storedSecond, error))
            {
                DistanceFailure(index, error);
                return false;
            }
            first = storedFirst;
            second = storedSecond;
            affected.At(distance.firstChain) = 1;
            affected.At(distance.secondChain) = 1;
            if (memcmp(&firstBefore, &first, sizeof(first)) != 0)
                restorePending.At(distance.firstChain) = 1;
            if (memcmp(&secondBefore, &second, sizeof(second)) != 0)
                restorePending.At(distance.secondChain) = 1;
        }
#if defined(GKCORE_TESTING) || defined(GKCORE_TEST_FRAME_CAPTURE)
        if (captureConstraintPass)
        {
            CaptureSecondaryMotionConstraintStage(pass, candidate, resolvedLinks, resolvedDistances, preparedShapes, activeLinks ? shapeCount : 0u, constraintTrace[pass & 1u][0]);
        }
#endif
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
            bool projected = false;
            if (pass >= (candidate.linkIterations + 1u) / 2u)
            {
                animation::FSecondaryMotionFreeSegmentAnchor firstAnchor;
                animation::FSecondaryMotionFreeSegmentAnchor secondAnchor;
                if (!BuildFreeSegmentAnchor(firstState, link.firstPoint, candidate.chains.At(link.firstChain)->settings.maxAngleDegrees, firstAnchor, error) || !BuildFreeSegmentAnchor(secondState, link.secondPoint, candidate.chains.At(link.secondChain)->settings.maxAngleDegrees, secondAnchor, error))
                {
                    LinkFailure(index, error);
                    error.Append(" (link anchor)");
                    return false;
                }
                projected = animation::ProjectPreparedSecondaryMotionFreeSegmentContactsWithAnchors(preparedShapes.Data() + link.shapeOffset, shapeCount, { { referenceFirst.x, referenceFirst.y, referenceFirst.z } }, { { referenceSecond.x, referenceSecond.y, referenceSecond.z } }, firstAnchor, secondAnchor, projectedFirst, projectedSecond, error);
            }
            else
            {
                projected = animation::ProjectPreparedSecondaryMotionFreeSegmentContacts(preparedShapes.Data() + link.shapeOffset, shapeCount, { { referenceFirst.x, referenceFirst.y, referenceFirst.z } }, { { referenceSecond.x, referenceSecond.y, referenceSecond.z } }, projectedFirst, projectedSecond, error);
            }
            if (!projected || !StorePosition(projectedFirst, first, error) || !StorePosition(projectedSecond, second, error))
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
#if defined(GKCORE_TESTING) || defined(GKCORE_TEST_FRAME_CAPTURE)
        if (captureConstraintPass)
        {
            CaptureSecondaryMotionConstraintStage(pass, candidate, resolvedLinks, resolvedDistances, preparedShapes, activeLinks ? shapeCount : 0u, constraintTrace[pass & 1u][1]);
        }
#endif
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
            const bool restored = distanceChains.At(chain) ? animation::RestoreSecondaryMotionChainWithPreparedCollisions(entry.simulation.previousTargets.Data(), entry.simulation.previousTargets.Count(), entry.settings, chainShapes, solverWorkspace, entry.simulation, error) : animation::StepSecondaryMotionChainWithPreparedCollisions(entry.simulation.previousTargets.Data(), entry.simulation.previousTargets.Count(), entry.settings, chainShapes, solverWorkspace, entry.simulation, error);
            if (!restored)
            {
                return false;
            }
            restorePending.At(chain) = 0;
        }
#if defined(GKCORE_TESTING) || defined(GKCORE_TEST_FRAME_CAPTURE)
        if (captureConstraintPass)
        {
            CaptureSecondaryMotionConstraintStage(pass, candidate, resolvedLinks, resolvedDistances, preparedShapes, activeLinks ? shapeCount : 0u, constraintTrace[pass & 1u][2]);
        }
#endif
        if (CheckCandidate(candidate, resolvedLinks, resolvedDistances, preparedShapes, activeLinks ? shapeCount : 0u, restorePending, error))
        {
            // 距離制約はframe-level solve、それ以外は既存substepの秒数で速度へ補正を戻す。
            const double step = activeDistances ? (delta > 0.0 && delta < 0.25 ? delta : 0.0) : (delta > 0.0 && delta < 0.25 ? delta / ceil(delta * 120.0) : 0.0);
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
    if (error.Empty())
        error.Assign(activeDistances ? "secondary links and distances could not preserve body contact and chain constraints together" : "secondary links could not preserve body contact and chain constraints together");
    else if (activeDistances)
        error.Append(" (secondary constraints did not converge)");
#if defined(GKCORE_TESTING) || defined(GKCORE_TEST_FRAME_CAPTURE)
    if (traceSecondaryConstraints)
    {
        const double traceStep = activeDistances ? candidate.distanceStepSeconds : (delta > 0.0 && delta < 0.25 ? delta / ceil(delta * 120.0) : 0.0);
        WriteSecondaryMotionConstraintTrace(constraintTrace, candidate.linkIterations, activeLinks ? shapeCount : 0u, delta, traceStep);
    }
#endif
    return false;
}

bool CheckRenderedSecondaryMotionLinks(const FModelSecondaryMotionState& state, const Array<float>& matrices, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, String& error)
{
    if (state.distanceConstraints.Count() > 0 && !ValidateSecondaryMotionDistanceConstraints(state, error))
        return false;
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
    for (uint32_t index = 0; index < state.distanceConstraints.Count(); ++index)
    {
        const auto& constraint = state.distanceConstraints.At(index);
        const uint64_t firstOffset = static_cast<uint64_t>(constraint.firstBone) * 16u;
        const uint64_t secondOffset = static_cast<uint64_t>(constraint.secondBone) * 16u;
        if (firstOffset + 15u >= matrices.Count() || secondOffset + 15u >= matrices.Count())
        {
            error.Assign("secondary distance rendered bone is outside pose matrices");
            DistanceFailure(index, error);
            return false;
        }
        const FVector3d firstPosition{ { matrices.At(static_cast<uint32_t>(firstOffset + 12u)), matrices.At(static_cast<uint32_t>(firstOffset + 13u)), matrices.At(static_cast<uint32_t>(firstOffset + 14u)) } };
        const FVector3d secondPosition{ { matrices.At(static_cast<uint32_t>(secondOffset + 12u)), matrices.At(static_cast<uint32_t>(secondOffset + 13u)), matrices.At(static_cast<uint32_t>(secondOffset + 14u)) } };
        if (!animation::CheckSecondaryMotionDistance(constraint.restDistance, constraint.compliance, state.distanceStepSeconds, state.distanceLambdas.At(index), firstPosition, secondPosition, error))
        {
            DistanceFailure(index, error);
            return false;
        }
    }
    return true;
}
}
