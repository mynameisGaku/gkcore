// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionContactPoints.h"
#include "model/animation/FModelSecondaryMotionStepShapeCache.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionLinks.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/ModelSecondaryMotionWeightedContacts.h"

#include <float.h>
#include <math.h>

namespace gk::model
{
namespace
{
using animation::FModelSecondaryMotionCollisionShape;
using animation::FPreparedSecondaryMotionCollisionShape;
using animation::FSecondaryMotionChainAttachmentContact;
using animation::FSecondaryMotionContactChain;
using animation::FSecondaryMotionReferenceSegment;
using animation::FSecondaryMotionWeightedContactAnchor;
using animation::FSecondaryMotionWeightedContactPoint;

// 1点へ登録できる骨寄与数の上限。
const uint32_t MaximumAnchorsPerPoint = 64u;
// 1モデルinstanceへ登録できる接触点数の上限。
const uint32_t MaximumWeightedPoints = 1024u;
// 1モデルinstanceへ登録できる骨寄与の総数。
const uint32_t MaximumWeightedAnchors = 16384u;
// 位置と角度のfloat保存誤差を許す倍率。
const double StoredConstraintTolerance = 128.0 * FLT_EPSILON;
// 角度制約の度からradianへの変換係数。
const double RadiansPerDegree = 3.1415926535897932384626433832795 / 180.0;
// 固定寄与に割り当てる鎖番号。
const uint32_t FixedChainIndex = UINT32_MAX;

/**
 * 3成分すべてが有限値かを調べる。
 */
bool IsFinite(const FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * float位置を倍精度の計算値へ変換する。
 */
FVector3d ToDouble(const Vec3& value)
{
    return { { value.x, value.y, value.z } };
}

/**
 * 差分の倍精度位置をfloatへ安全に保存する。
 */
bool StorePosition(const FVector3d& input, Vec3& output, String& error)
{
    if (!IsFinite(input) || fabs(input.value[0]) > FLT_MAX || fabs(input.value[1]) > FLT_MAX || fabs(input.value[2]) > FLT_MAX)
    {
        error.Assign("secondary weighted contact position exceeds float range");
        return false;
    }
    output = { static_cast<float>(input.value[0]), static_cast<float>(input.value[1]), static_cast<float>(input.value[2]) };
    return true;
}

/**
 * 3成分の差を計算する。
 */
FVector3d Subtract(const FVector3d& first, const FVector3d& second)
{
    return { { first.value[0] - second.value[0], first.value[1] - second.value[1], first.value[2] - second.value[2] } };
}

/**
 * 3成分を係数倍する。
 */
FVector3d Multiply(const FVector3d& value, double scale)
{
    return { { value.value[0] * scale, value.value[1] * scale, value.value[2] * scale } };
}

/**
 * 桁あふれを避けて3成分の長さを求める。
 */
double Length(const FVector3d& value)
{
    const double largest = fmax(fabs(value.value[0]), fmax(fabs(value.value[1]), fabs(value.value[2])));
    if (!(largest > 0.0) || !isfinite(largest))
    {
        return largest;
    }
    const double x = value.value[0] / largest;
    const double y = value.value[1] / largest;
    const double z = value.value[2] / largest;
    return largest * sqrt(x * x + y * y + z * z);
}

/**
 * 骨格の親列で祖先関係を調べる。
 */
bool IsAncestor(const animation::FModelSkeleton& skeleton, uint32_t ancestor, uint32_t descendant)
{
    for (int32_t bone = static_cast<int32_t>(descendant); bone >= 0; bone = skeleton.parents.At(static_cast<uint32_t>(bone)))
    {
        if (static_cast<uint32_t>(bone) == ancestor)
        {
            return true;
        }
    }
    return false;
}

/**
 * 骨の行列でローカル点をmodel空間へ変換する。
 */
bool TransformPoint(const Array<float>& matrices, uint32_t bone, const Vec3& local, FVector3d& output, String& error)
{
    const uint64_t offset = static_cast<uint64_t>(bone) * 16u;
    if (offset + 15u >= matrices.Count())
    {
        error.Assign("secondary weighted contact bone matrix is missing");
        return false;
    }
    for (uint32_t element = 0; element < 16u; ++element)
    {
        if (!isfinite(matrices.At(static_cast<uint32_t>(offset + element))))
        {
            error.Assign("secondary weighted contact bone matrix is not finite");
            return false;
        }
    }
    const double x = static_cast<double>(matrices.At(static_cast<uint32_t>(offset))) * local.x + static_cast<double>(matrices.At(static_cast<uint32_t>(offset + 4u))) * local.y + static_cast<double>(matrices.At(static_cast<uint32_t>(offset + 8u))) * local.z;
    const double y = static_cast<double>(matrices.At(static_cast<uint32_t>(offset + 1u))) * local.x + static_cast<double>(matrices.At(static_cast<uint32_t>(offset + 5u))) * local.y + static_cast<double>(matrices.At(static_cast<uint32_t>(offset + 9u))) * local.z;
    const double z = static_cast<double>(matrices.At(static_cast<uint32_t>(offset + 2u))) * local.x + static_cast<double>(matrices.At(static_cast<uint32_t>(offset + 6u))) * local.y + static_cast<double>(matrices.At(static_cast<uint32_t>(offset + 10u))) * local.z;
    const double px = x + matrices.At(static_cast<uint32_t>(offset + 12u));
    const double py = y + matrices.At(static_cast<uint32_t>(offset + 13u));
    const double pz = z + matrices.At(static_cast<uint32_t>(offset + 14u));
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(px) || !isfinite(py) || !isfinite(pz) || fabs(x) > FLT_MAX || fabs(y) > FLT_MAX || fabs(z) > FLT_MAX || fabs(px) > FLT_MAX || fabs(py) > FLT_MAX || fabs(pz) > FLT_MAX)
    {
        error.Assign("secondary weighted contact transformed point exceeds float range");
        return false;
    }
    output = { { px, py, pz } };
    return true;
}

/**
 * 現在のbase FKと基準線分から骨ローカル寄与の回転情報を作る。
 */
bool BuildAttachmentDefinition(const FModelSecondaryMotionChain& chain, uint32_t segment, uint32_t bone, const Vec3& offset, const Array<float>& matrices, animation::FSecondaryMotionAttachmentDefinition& output, String& error)
{
    if (segment >= chain.bones.Count() || segment + 1u >= chain.simulation.previousTargets.Count() || !isfinite(chain.settings.maxAngleDegrees) || chain.settings.maxAngleDegrees < 0.0f || chain.settings.maxAngleDegrees > 180.0f)
    {
        error.Assign("secondary weighted contact chain reference is invalid");
        return false;
    }
    // base姿勢の親子方向。
    const auto& parent = chain.simulation.previousTargets.At(segment);
    const auto& child = chain.simulation.previousTargets.At(segment + 1u);
    const FVector3d direction = Subtract(ToDouble(child), ToDouble(parent));
    const double length = Length(direction);
    if (!(length > 0.0) || !isfinite(length))
    {
        error.Assign("secondary weighted contact base direction is degenerate");
        return false;
    }
    const uint64_t matrixOffset = static_cast<uint64_t>(bone) * 16u;
    if (matrixOffset + 15u >= matrices.Count())
    {
        error.Assign("secondary weighted contact bone matrix is missing");
        return false;
    }
    for (uint32_t element = 0; element < 16u; ++element)
    {
        if (!isfinite(matrices.At(static_cast<uint32_t>(matrixOffset + element))))
        {
            error.Assign("secondary weighted contact bone matrix is not finite");
            return false;
        }
    }
    const double ox = static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset))) * offset.x + static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset + 4u))) * offset.y + static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset + 8u))) * offset.z;
    const double oy = static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset + 1u))) * offset.x + static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset + 5u))) * offset.y + static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset + 9u))) * offset.z;
    const double oz = static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset + 2u))) * offset.x + static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset + 6u))) * offset.y + static_cast<double>(matrices.At(static_cast<uint32_t>(matrixOffset + 10u))) * offset.z;
    const FVector3d modelOffset{ { ox, oy, oz } };
    if (!IsFinite(modelOffset) || fabs(ox) > FLT_MAX || fabs(oy) > FLT_MAX || fabs(oz) > FLT_MAX)
    {
        error.Assign("secondary weighted contact offset exceeds numeric range");
        return false;
    }
    animation::FSecondaryMotionAttachmentDefinition candidate{};
    candidate.direction = Multiply(direction, 1.0 / length);
    candidate.offset = modelOffset;
    candidate.cosineLimit = cos(static_cast<double>(chain.settings.maxAngleDegrees) * RadiansPerDegree);
    if (!isfinite(candidate.cosineLimit))
    {
        error.Assign("secondary weighted contact angle is invalid");
        return false;
    }
    output = candidate;
    return true;
}

/**
 * ContactPointsの範囲と寄与weightを骨格に依存せず検査する。
 */
bool ValidatePointRanges(const FModelSecondaryMotionState& state, String& error)
{
    if (state.contactPoints.Count() > MaximumWeightedPoints || state.contactAnchors.Count() > MaximumWeightedAnchors || ((state.contactPoints.Count() == 0u) != (state.contactAnchors.Count() == 0u)))
    {
        error.Assign("secondary weighted contact count or anchor storage is invalid");
        return false;
    }
    Array<uint8_t> usedAnchors;
    if (!usedAnchors.Reserve(state.contactAnchors.Count()))
    {
        error.Assign("secondary weighted contact validation allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < state.contactAnchors.Count(); ++index)
    {
        if (!usedAnchors.Append(0u))
        {
            error.Assign("secondary weighted contact validation allocation failed");
            return false;
        }
    }
    for (uint32_t pointIndex = 0; pointIndex < state.contactPoints.Count(); ++pointIndex)
    {
        const auto& point = state.contactPoints.At(pointIndex);
        const uint64_t end = static_cast<uint64_t>(point.firstAnchor) + point.anchorCount;
        if (point.anchorCount == 0u || point.anchorCount > MaximumAnchorsPerPoint || end > state.contactAnchors.Count() || !isfinite(point.radius) || point.radius < 0.0f)
        {
            error.Assign("secondary weighted contact point range or radius is invalid");
            return false;
        }
        double weightSum = 0.0;
        for (uint32_t anchorOffset = 0; anchorOffset < point.anchorCount; ++anchorOffset)
        {
            const uint32_t anchorIndex = point.firstAnchor + anchorOffset;
            const auto& anchor = state.contactAnchors.At(anchorIndex);
            if (!isfinite(anchor.offset.x) || !isfinite(anchor.offset.y) || !isfinite(anchor.offset.z) || !isfinite(anchor.weight) || !(anchor.weight > 0.0f) || usedAnchors.At(anchorIndex) != 0u)
            {
                error.Assign("secondary weighted contact anchor is invalid or repeated");
                return false;
            }
            usedAnchors.At(anchorIndex) = 1u;
            weightSum += anchor.weight;
        }
        if (!isfinite(weightSum) || fabs(weightSum - 1.0) > static_cast<double>(64.0f * FLT_EPSILON))
        {
            error.Assign("secondary weighted contact weights must sum to one");
            return false;
        }
    }
    for (uint32_t index = 0; index < usedAnchors.Count(); ++index)
    {
        if (usedAnchors.At(index) == 0u)
        {
            error.Assign("secondary weighted contact anchor is not owned by a point");
            return false;
        }
    }
    return true;
}

/**
 * 候補鎖とbone所属、混在禁止設定を検査する。
 */
bool ValidateChains(const FModelSecondaryMotionState& state, const animation::FModelSkeleton& skeleton, String& error)
{
    if (state.links.Count() > 0u || state.distanceConstraints.Count() > 0u)
    {
        error.Assign("secondary weighted contacts cannot be combined with origin links or distance constraints");
        return false;
    }
    if (state.linkIterations == 0u || state.linkIterations > 32u || skeleton.parents.Count() == 0u)
    {
        error.Assign("secondary weighted contact iteration limit or skeleton is invalid");
        return false;
    }
    for (uint32_t bone = 0; bone < skeleton.parents.Count(); ++bone)
    {
        const int32_t parent = skeleton.parents.At(bone);
        if (parent < -1 || (parent >= 0 && static_cast<uint32_t>(parent) >= bone))
        {
            error.Assign("secondary weighted contact skeleton hierarchy is invalid");
            return false;
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < state.chains.Count(); ++chainIndex)
    {
        const auto* chain = state.chains.At(chainIndex);
        if (!chain || chain->bones.Count() == 0u || chain->simulation.points.Count() != chain->bones.Count() + 1u || chain->simulation.previousTargets.Count() != chain->bones.Count() + 1u || !isfinite(chain->settings.maxAngleDegrees) || chain->settings.maxAngleDegrees < 0.0f || chain->settings.maxAngleDegrees > 180.0f)
        {
            error.Assign("secondary weighted contact chain state is invalid");
            return false;
        }
        for (uint32_t boneIndex = 0; boneIndex < chain->bones.Count(); ++boneIndex)
        {
            const uint32_t bone = chain->bones.At(boneIndex);
            if (bone >= skeleton.parents.Count() || (boneIndex > 0u && skeleton.parents.At(bone) != static_cast<int32_t>(chain->bones.At(boneIndex - 1u))))
            {
                error.Assign("secondary weighted contact chain bone sequence is invalid");
                return false;
            }
            for (uint32_t earlierChain = 0; earlierChain <= chainIndex; ++earlierChain)
            {
                const auto* other = state.chains.At(earlierChain);
                const uint32_t limit = earlierChain == chainIndex ? boneIndex : (other ? other->bones.Count() : 0u);
                for (uint32_t earlierBone = 0; earlierBone < limit; ++earlierBone)
                {
                    if (other->bones.At(earlierBone) == bone)
                    {
                        error.Assign("secondary weighted contact chains overlap");
                        return false;
                    }
                }
            }
        }
        for (uint32_t earlierChain = 0; earlierChain < chainIndex; ++earlierChain)
        {
            const auto* earlier = state.chains.At(earlierChain);
            if (IsAncestor(skeleton, earlier->bones.At(0u), chain->bones.At(0u)) || IsAncestor(skeleton, chain->bones.At(0u), earlier->bones.At(0u)))
            {
                error.Assign("secondary weighted contact chains have an ancestor relationship");
                return false;
            }
        }
        for (uint32_t point = 0; point < chain->simulation.points.Count(); ++point)
        {
            const auto& position = chain->simulation.points.At(point).position;
            const auto& target = chain->simulation.previousTargets.At(point);
            // 時間0やteleportでも、不正な速度を候補へ持ち越さない。
            const auto& velocity = chain->simulation.points.At(point).velocity;
            if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z) || !isfinite(target.x) || !isfinite(target.y) || !isfinite(target.z) || !isfinite(velocity.x) || !isfinite(velocity.y) || !isfinite(velocity.z))
            {
                error.Assign("secondary weighted contact chain has a non-finite point or velocity");
                return false;
            }
        }
    }
    for (uint32_t pointIndex = 0; pointIndex < state.contactPoints.Count(); ++pointIndex)
    {
        const auto& point = state.contactPoints.At(pointIndex);
        for (uint32_t anchorOffset = 0; anchorOffset < point.anchorCount; ++anchorOffset)
        {
            const uint32_t bone = state.contactAnchors.At(point.firstAnchor + anchorOffset).bone;
            if (bone >= skeleton.parents.Count())
            {
                error.Assign("secondary weighted contact anchor bone is outside the skeleton");
                return false;
            }
            bool registered = false;
            bool controlledDescendant = false;
            for (uint32_t chainIndex = 0; chainIndex < state.chains.Count(); ++chainIndex)
            {
                const auto* chain = state.chains.At(chainIndex);
                if (IsAncestor(skeleton, chain->bones.At(0), bone))
                {
                    controlledDescendant = true;
                }
                for (uint32_t boneIndex = 0; boneIndex < chain->bones.Count(); ++boneIndex)
                {
                    registered = registered || chain->bones.At(boneIndex) == bone;
                }
            }
            if (controlledDescendant && !registered)
            {
                error.Assign("secondary weighted contact anchor is an unregistered secondary descendant");
                return false;
            }
        }
    }
    if (state.contactLinks.Count() > 0u)
    {
        if (!ValidateSecondaryMotionContactLinks(state, error))
        {
            return false;
        }
        for (uint32_t index = 0; index < state.contactLinks.Count(); ++index)
        {
            const auto& link = state.contactLinks.At(index);
            if (link.firstBone != link.secondBone)
            {
                error.Assign("weighted contacts only support same-bone legacy contact links");
                return false;
            }
        }
    }
    return true;
}

/**
 * 1つの旧contact linkの端点情報を鎖のbone-local定義へ変換する。
 */
bool BuildLegacyAttachment(const FModelSecondaryMotionState& state, const Array<float>& matrices, const gk::FModelSecondaryMotionContactLink& link, uint32_t chainIndex, uint32_t segment, animation::FSecondaryMotionChainAttachmentContact& output, String& error)
{
    const auto* chain = state.chains.At(chainIndex);
    if (!chain || segment == 0u || segment >= chain->bones.Count())
    {
        error.Assign("legacy weighted contact link requires a registered free bone");
        return false;
    }
    animation::FSecondaryMotionChainAttachmentContact candidate{};
    candidate.segmentIndex = segment;
    if (!BuildAttachmentDefinition(*chain, segment, link.firstBone, link.firstOffset, matrices, candidate.firstDefinition, error) || !BuildAttachmentDefinition(*chain, segment, link.secondBone, link.secondOffset, matrices, candidate.secondDefinition, error))
    {
        return false;
    }
    output = candidate;
    return true;
}

/**
 * float保存後も鎖の骨長と許容曲げ角が成立するか調べる。
 */
bool CheckStoredChain(const FModelSecondaryMotionChain& chain, const Vec3* stored, String& error)
{
    for (uint32_t segment = 0; segment < chain.bones.Count(); ++segment)
    {
        const FVector3d parent = ToDouble(stored[segment]);
        const FVector3d child = ToDouble(stored[segment + 1u]);
        const auto& refParent = chain.simulation.previousTargets.At(segment);
        const auto& refChild = chain.simulation.previousTargets.At(segment + 1u);
        const FVector3d reference = Subtract(ToDouble(refChild), ToDouble(refParent));
        const FVector3d current = Subtract(child, parent);
        const double targetLength = Length(reference);
        const double length = Length(current);
        if (!(targetLength > 0.0) || !(length > 0.0) || !isfinite(targetLength) || !isfinite(length) || fabs(length - targetLength) > targetLength * StoredConstraintTolerance)
        {
            error.Assign("secondary weighted contact changed a stored bone length");
            return false;
        }
        const double referenceLength = Length(reference);
        const double currentLength = Length(current);
        const double cosine = (reference.value[0] * current.value[0] + reference.value[1] * current.value[1] + reference.value[2] * current.value[2]) / (referenceLength * currentLength);
        const double angle = static_cast<double>(chain.settings.maxAngleDegrees) * RadiansPerDegree;
        const double cosineLimit = cos(fmin(3.1415926535897932384626433832795, angle + StoredConstraintTolerance));
        if (!isfinite(cosine) || cosine < cosineLimit)
        {
            error.Assign("secondary weighted contact changed a stored bend angle");
            return false;
        }
    }
    return true;
}

/**
 * 倍精度chain位置へbone-local接触寄与を評価する。
 */
bool EvaluateAnchor(const animation::FSecondaryMotionWeightedContactAnchor& anchor, const FVector3d* positions, uint32_t positionCount, const animation::FSecondaryMotionContactChain* chains, uint32_t chainCount, FVector3d& output, String& error)
{
    if (anchor.chainIndex == FixedChainIndex)
    {
        output = anchor.constantPosition;
        if (!IsFinite(output))
        {
            error.Assign("secondary weighted contact fixed anchor is not finite");
            return false;
        }
        return true;
    }
    if (anchor.chainIndex >= chainCount)
    {
        error.Assign("secondary weighted contact anchor chain index is invalid");
        return false;
    }
    const auto& chain = chains[anchor.chainIndex];
    if (anchor.segmentIndex >= chain.segmentCount || chain.firstPoint > UINT32_MAX - anchor.segmentIndex - 1u || chain.firstPoint + anchor.segmentIndex + 1u >= positionCount)
    {
        error.Assign("secondary weighted contact anchor segment index is invalid");
        return false;
    }
    const FVector3d& parent = positions[chain.firstPoint + anchor.segmentIndex];
    const FVector3d& child = positions[chain.firstPoint + anchor.segmentIndex + 1u];
    animation::FSecondaryMotionAttachmentEvaluation evaluation{};
    if (!animation::EvaluateSecondaryMotionAttachment(anchor.definition, parent, child, evaluation, error))
    {
        return false;
    }
    output = evaluation.position;
    return true;
}

/**
 * 描画FK行列でbone-local点を変換し、finiteなmodel座標を返す。
 */
bool EvaluateRenderedAnchor(const FModelSecondaryMotionContactAnchor& anchor, const Array<float>& matrices, FVector3d& output, String& error)
{
    return TransformPoint(matrices, anchor.bone, anchor.offset, output, error);
}

/**
 * 前回から現在の目標へ物理予測し、時間0では確定方向と現在骨長を再試行に使う。
 */
bool BuildPreviousDirectionSeed(const FModelSecondaryMotionState& candidate, const FModelSecondaryMotionState& previous, const Array<FVector3d>& currentPositions, const Array<FSecondaryMotionContactChain>& chains, const Array<FSecondaryMotionReferenceSegment>& references, double delta, Array<FVector3d>& seed, bool& eligible, String& error)
{
    eligible = false;
    if (delta >= 0.25 || previous.chains.Count() == 0u || previous.chains.Count() != candidate.chains.Count())
    {
        return true;
    }

    eligible = true;
    for (uint32_t chainIndex = 0; chainIndex < candidate.chains.Count(); ++chainIndex)
    {
        const auto* currentChain = candidate.chains.At(chainIndex);
        const auto* previousChain = previous.chains.At(chainIndex);
        if (!currentChain || !previousChain || currentChain->bones.Count() != previousChain->bones.Count() || currentChain->simulation.points.Count() != previousChain->simulation.points.Count() || currentChain->simulation.points.Count() != currentChain->bones.Count() + 1u || previousChain->simulation.previousTargets.Count() != previousChain->simulation.points.Count())
        {
            eligible = false;
            return true;
        }
        for (uint32_t boneIndex = 0; boneIndex < currentChain->bones.Count(); ++boneIndex)
        {
            if (currentChain->bones.At(boneIndex) != previousChain->bones.At(boneIndex))
            {
                eligible = false;
                return true;
            }
        }
        if (!isfinite(currentChain->settings.teleportDistance) || currentChain->settings.teleportDistance < 0.0f)
        {
            error.Assign("secondary weighted contact teleport distance is invalid");
            return false;
        }
        for (uint32_t pointIndex = 0; pointIndex < previousChain->simulation.points.Count(); ++pointIndex)
        {
            const auto& oldPoint = previousChain->simulation.points.At(pointIndex);
            const auto& oldTarget = previousChain->simulation.previousTargets.At(pointIndex);
            if (!isfinite(oldPoint.position.x) || !isfinite(oldPoint.position.y) || !isfinite(oldPoint.position.z) || !isfinite(oldPoint.velocity.x) || !isfinite(oldPoint.velocity.y) || !isfinite(oldPoint.velocity.z) || !isfinite(oldTarget.x) || !isfinite(oldTarget.y) || !isfinite(oldTarget.z))
            {
                error.Assign("secondary weighted contact previous direction state is not finite");
                return false;
            }
        }
        const FVector3d rootDelta = Subtract(ToDouble(currentChain->simulation.previousTargets.At(0u)), ToDouble(previousChain->simulation.previousTargets.At(0u)));
        const double rootDistance = Length(rootDelta);
        if (!isfinite(rootDistance))
        {
            error.Assign("secondary weighted contact previous root distance is invalid");
            return false;
        }
        if (rootDistance > currentChain->settings.teleportDistance)
        {
            eligible = false;
            return true;
        }
    }

    if (!seed.Reserve(currentPositions.Count()))
    {
        error.Assign("secondary weighted contact warm-start allocation failed");
        return false;
    }
    if (delta > 0.0)
    {
        for (uint32_t chainIndex = 0; chainIndex < candidate.chains.Count(); ++chainIndex)
        {
            const auto& currentChain = *candidate.chains.At(chainIndex);
            const auto& previousChain = *previous.chains.At(chainIndex);
            animation::FModelSecondaryMotionChainState prediction;
            // 接触の順序による姿勢跳びを持ち込まず、既存のばね・減衰・骨制約で予測する。
            if (!prediction.points.AppendRange(previousChain.simulation.points.Data(), previousChain.simulation.points.Count()) || !prediction.previousTargets.AppendRange(previousChain.simulation.previousTargets.Data(), previousChain.simulation.previousTargets.Count()))
            {
                error.Assign("secondary weighted contact prediction state allocation failed");
                return false;
            }
            if (!animation::StepSecondaryMotionChain(currentChain.simulation.previousTargets.Data(), currentChain.simulation.previousTargets.Count(), currentChain.settings, delta, prediction, error))
            {
                return false;
            }
            for (uint32_t point = 0; point < prediction.points.Count(); ++point)
            {
                if (!seed.Append(ToDouble(prediction.points.At(point).position)))
                {
                    error.Assign("secondary weighted contact prediction point allocation failed");
                    return false;
                }
            }
        }
        if (seed.Count() != currentPositions.Count())
        {
            error.Assign("secondary weighted contact prediction point count is invalid");
            return false;
        }
        return true;
    }
    for (uint32_t chainIndex = 0; chainIndex < candidate.chains.Count(); ++chainIndex)
    {
        const auto& previousChain = *previous.chains.At(chainIndex);
        const uint32_t firstPoint = chains.At(chainIndex).firstPoint;
        const uint32_t firstReference = chains.At(chainIndex).firstSegment;
        const uint32_t segmentCount = chains.At(chainIndex).segmentCount;
        FVector3d parent = currentPositions.At(firstPoint);
        if (!seed.Append(parent))
        {
            error.Assign("secondary weighted contact warm-start position allocation failed");
            return false;
        }
        for (uint32_t segment = 0; segment < segmentCount; ++segment)
        {
            const auto& oldParent = previousChain.simulation.points.At(segment).position;
            const auto& oldChild = previousChain.simulation.points.At(segment + 1u).position;
            FVector3d direction = Subtract(ToDouble(oldChild), ToDouble(oldParent));
            const double oldLength = Length(direction);
            if (!(oldLength > 0.0) || !isfinite(oldLength))
            {
                error.Assign("secondary weighted contact previous bone direction is degenerate");
                return false;
            }
            direction = Multiply(direction, 1.0 / oldLength);
            const double currentLength = references.At(firstReference + segment).length;
            FVector3d child{ { parent.value[0] + direction.value[0] * currentLength, parent.value[1] + direction.value[1] * currentLength, parent.value[2] + direction.value[2] * currentLength } };
            Vec3 checkedPosition{};
            if (!isfinite(currentLength) || !(currentLength > 0.0) || !StorePosition(child, checkedPosition, error) || !seed.Append(child))
            {
                if (error.Empty())
                {
                    error.Assign("secondary weighted contact warm-start bone length is invalid");
                }
                return false;
            }
            parent = child;
        }
    }
    if (seed.Count() != currentPositions.Count())
    {
        error.Assign("secondary weighted contact warm-start point count is invalid");
        return false;
    }
    return true;
}

}

bool ValidateSecondaryMotionContactPoints(const FModelSecondaryMotionState& state, const animation::FModelSkeleton& skeleton, String& error)
{
    error.Clear();
    if (!ValidatePointRanges(state, error))
    {
        return false;
    }
    if (state.contactPoints.Count() == 0u)
    {
        return true;
    }
    return ValidateChains(state, skeleton, error);
}

bool ConstrainSecondaryMotionContactPoints(FModelSecondaryMotionState& candidate, const FModelSecondaryMotionState& previous, const animation::FModelSkeleton& skeleton, const Array<float>& baseMatrices, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, double delta, String& error)
{
    error.Clear();
    if (!ValidateSecondaryMotionContactPoints(candidate, skeleton, error))
    {
        return false;
    }
    if (candidate.contactPoints.Count() == 0u)
    {
        return true;
    }
    if (!isfinite(delta) || delta < 0.0 || shapeCount > 64u || (shapeCount > 0u && !shapes) || baseMatrices.Count() % 16u != 0u || !animation::ValidateSecondaryMotionCollisionShapes(shapes, shapeCount, error))
    {
        if (error.Empty())
        {
            error.Assign("secondary weighted contact update input is invalid");
        }
        return false;
    }
    if (skeleton.parents.Count() > UINT32_MAX / 16u || baseMatrices.Count() != skeleton.parents.Count() * 16u)
    {
        error.Assign("secondary weighted contact base pose matrix count does not match the skeleton");
        return false;
    }
    if (shapeCount == 0u)
    {
        return true;
    }
    uint64_t totalPositions = 0;
    uint64_t totalSegments = 0;
    for (uint32_t index = 0; index < candidate.chains.Count(); ++index)
    {
        const auto* chain = candidate.chains.At(index);
        totalPositions += chain->simulation.points.Count();
        totalSegments += chain->bones.Count();
    }
    if (totalPositions > UINT32_MAX || totalSegments > UINT32_MAX || totalPositions != 0u && candidate.chains.Count() == 0u)
    {
        error.Assign("secondary weighted contact chain ranges exceed the supported size");
        return false;
    }

    Array<FVector3d> positions;
    Array<FVector3d> beforePositions;
    Array<FVector3d> storedDoublePositions;
    Array<Vec3> storedPositions;
    Array<Vec3> stagedVelocities;
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> references;
    Array<FSecondaryMotionWeightedContactAnchor> anchors;
    Array<FSecondaryMotionWeightedContactPoint> contacts;
    Array<FModelSecondaryMotionCollisionShape> chainExpandedShapes;
    Array<FPreparedSecondaryMotionCollisionShape> chainPreparedShapes;
    Array<FPreparedSecondaryMotionCollisionShape> pointPreparedShapes;
    Array<FPreparedSecondaryMotionCollisionShape> attachmentPreparedShapes;
    Array<uint32_t> chainPointOffsets;
    Array<uint32_t> chainReferenceOffsets;
    Array<uint32_t> chainShapeOffsets;
    Array<uint32_t> chainShapeCounts;
    Array<uint32_t> pointShapeOffsets;
    Array<uint32_t> attachmentCounts;
    Array<uint32_t> attachmentOffsets;
    Array<uint32_t> boneChain;
    Array<uint32_t> boneSegment;
    if (previous.chains.Count() != 0u && previous.chains.Count() != candidate.chains.Count())
    {
        error.Assign("secondary weighted contact previous chain count does not match");
        return false;
    }
    if (!positions.Reserve(static_cast<uint32_t>(totalPositions)) || !beforePositions.Reserve(static_cast<uint32_t>(totalPositions)) || !storedDoublePositions.Reserve(static_cast<uint32_t>(totalPositions)) || !storedPositions.Reserve(static_cast<uint32_t>(totalPositions)) || !stagedVelocities.Reserve(static_cast<uint32_t>(totalPositions)) || !chains.Reserve(candidate.chains.Count()) || !references.Reserve(static_cast<uint32_t>(totalSegments)) || !anchors.Reserve(candidate.contactAnchors.Count()) || !contacts.Reserve(candidate.contactPoints.Count()) || !chainPointOffsets.Reserve(candidate.chains.Count()) || !chainReferenceOffsets.Reserve(candidate.chains.Count()) || !chainShapeOffsets.Reserve(candidate.chains.Count()) || !chainShapeCounts.Reserve(candidate.chains.Count()) || !attachmentCounts.Reserve(candidate.chains.Count()) || !attachmentOffsets.Reserve(candidate.chains.Count()) || !boneChain.Reserve(skeleton.parents.Count()) || !boneSegment.Reserve(skeleton.parents.Count()))
    {
        error.Assign("secondary weighted contact workspace allocation failed");
        return false;
    }
    for (uint32_t bone = 0; bone < skeleton.parents.Count(); ++bone)
    {
        if (!boneChain.Append(FixedChainIndex) || !boneSegment.Append(FixedChainIndex))
        {
            error.Assign("secondary weighted contact bone map allocation failed");
            return false;
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < candidate.chains.Count(); ++chainIndex)
    {
        auto& chain = *candidate.chains.At(chainIndex);
        const auto* previousChain = chainIndex < previous.chains.Count() ? previous.chains.At(chainIndex) : nullptr;
        bool sameBones = previousChain == nullptr || previousChain->bones.Count() == chain.bones.Count();
        for (uint32_t boneIndex = 0; previousChain && sameBones && boneIndex < chain.bones.Count(); ++boneIndex)
        {
            sameBones = previousChain->bones.At(boneIndex) == chain.bones.At(boneIndex);
        }
        if (!sameBones || (previousChain && previousChain->simulation.points.Count() != chain.simulation.points.Count()))
        {
            error.Assign("secondary weighted contact previous chain does not match the candidate");
            return false;
        }
        if (!chainPointOffsets.Append(positions.Count()) || !chainReferenceOffsets.Append(references.Count()) || !chainShapeOffsets.Append(0u) || !chainShapeCounts.Append(0u) || !attachmentCounts.Append(0u) || !attachmentOffsets.Append(0u))
        {
            error.Assign("secondary weighted contact range allocation failed");
            return false;
        }
        const uint32_t firstPoint = positions.Count();
        const uint32_t firstSegment = references.Count();
        for (uint32_t point = 0; point < chain.simulation.points.Count(); ++point)
        {
            const auto& statePoint = chain.simulation.points.At(point);
            const FVector3d position = ToDouble(statePoint.position);
            if (!positions.Append(position) || !beforePositions.Append(position) || !storedPositions.Append(statePoint.position) || !stagedVelocities.Append(statePoint.velocity))
            {
                error.Assign("secondary weighted contact position allocation failed");
                return false;
            }
        }
        for (uint32_t segment = 0; segment < chain.bones.Count(); ++segment)
        {
            const uint32_t bone = chain.bones.At(segment);
            if (boneChain.At(bone) != FixedChainIndex)
            {
                error.Assign("secondary weighted contact chain bone is registered more than once");
                return false;
            }
            boneChain.At(bone) = chainIndex;
            boneSegment.At(bone) = segment;
            const FVector3d direction = Subtract(ToDouble(chain.simulation.previousTargets.At(segment + 1u)), ToDouble(chain.simulation.previousTargets.At(segment)));
            const double length = Length(direction);
            if (!(length > 0.0) || !isfinite(length))
            {
                error.Assign("secondary weighted contact reference segment is degenerate");
                return false;
            }
            animation::FSecondaryMotionReferenceSegment reference{};
            reference.direction = Multiply(direction, 1.0 / length);
            reference.length = length;
            if (!references.Append(reference))
            {
                error.Assign("secondary weighted contact reference allocation failed");
                return false;
            }
        }
        FSecondaryMotionContactChain chainDescription{};
        chainDescription.firstPoint = firstPoint;
        chainDescription.firstSegment = firstSegment;
        chainDescription.segmentCount = chain.bones.Count();
        chainDescription.cosineLimit = cos(static_cast<double>(chain.settings.maxAngleDegrees) * RadiansPerDegree);
        if (!isfinite(chainDescription.cosineLimit) || !chains.Append(chainDescription))
        {
            error.Assign("secondary weighted contact chain descriptor is invalid");
            return false;
        }
    }

    // cache借用shapeは次のGetより前に、更新ローカル配列へ複写する。
    animation::FModelSecondaryMotionStepShapeCache bodyCache(shapes, shapeCount);
    for (uint32_t chainIndex = 0; chainIndex < candidate.chains.Count(); ++chainIndex)
    {
        const auto& chain = *candidate.chains.At(chainIndex);
        animation::FModelSecondaryMotionStepShapes view{};
        if (!bodyCache.Get(chain.collisionRadius, chain.colliderMask, view, error))
        {
            return false;
        }
        chainShapeOffsets.At(chainIndex) = chainPreparedShapes.Count();
        chainShapeCounts.At(chainIndex) = view.count;
        if (view.count > 0u && (!chainExpandedShapes.AppendRange(view.expanded, view.count) || !chainPreparedShapes.AppendRange(view.prepared, view.count)))
        {
            error.Assign("secondary weighted contact chain shape copy failed");
            return false;
        }
    }

    // 各weighted pointへ全身体形状を半径込みで準備する。
    for (uint32_t pointIndex = 0; pointIndex < candidate.contactPoints.Count(); ++pointIndex)
    {
        const auto& point = candidate.contactPoints.At(pointIndex);
        Array<FPreparedSecondaryMotionCollisionShape> prepared;
        if (!animation::PrepareSecondaryMotionFreeSegmentShapes(shapes, shapeCount, point.radius, prepared, error))
        {
            return false;
        }
        if (!pointShapeOffsets.Append(pointPreparedShapes.Count()) || !pointPreparedShapes.AppendRange(prepared.Data(), prepared.Count()))
        {
            error.Assign("secondary weighted contact shape allocation failed");
            return false;
        }
        FSecondaryMotionWeightedContactPoint contact{};
        contact.firstAnchor = anchors.Count();
        contact.anchorCount = point.anchorCount;
        contact.shapeCount = shapeCount;
        for (uint32_t anchorOffset = 0; anchorOffset < point.anchorCount; ++anchorOffset)
        {
            const auto& source = candidate.contactAnchors.At(point.firstAnchor + anchorOffset);
            FSecondaryMotionWeightedContactAnchor anchor{};
            anchor.weight = source.weight;
            if (boneChain.At(source.bone) != FixedChainIndex)
            {
                const uint32_t chainIndex = boneChain.At(source.bone);
                const uint32_t segment = boneSegment.At(source.bone);
                anchor.chainIndex = chainIndex;
                anchor.segmentIndex = segment;
                if (!BuildAttachmentDefinition(*candidate.chains.At(chainIndex), segment, source.bone, source.offset, baseMatrices, anchor.definition, error))
                {
                    return false;
                }
            }
            else
            {
                anchor.chainIndex = FixedChainIndex;
                if (!TransformPoint(baseMatrices, source.bone, source.offset, anchor.constantPosition, error))
                {
                    return false;
                }
            }
            if (!anchors.Append(anchor))
            {
                error.Assign("secondary weighted contact anchor allocation failed");
                return false;
            }
        }
        if (!contacts.Append(contact))
        {
            error.Assign("secondary weighted contact descriptor allocation failed");
            return false;
        }
    }

    // 既存same-bone contact linkも、同じ鎖solverへattachmentとして渡す。
    Array<animation::FSecondaryMotionChainAttachmentContact> resolvedAttachments;
    Array<uint32_t> resolvedAttachmentChains;
    Array<uint32_t> resolvedAttachmentShapeOffsets;
    Array<uint32_t> resolvedAttachmentShapeCounts;
    if (candidate.contactLinks.Count() > 0u)
    {
        if (!resolvedAttachments.Reserve(candidate.contactLinks.Count()) || !resolvedAttachmentChains.Reserve(candidate.contactLinks.Count()) || !resolvedAttachmentShapeOffsets.Reserve(candidate.contactLinks.Count()) || !resolvedAttachmentShapeCounts.Reserve(candidate.contactLinks.Count()))
        {
            error.Assign("secondary weighted contact attachment allocation failed");
            return false;
        }
        for (uint32_t index = 0; index < candidate.contactLinks.Count(); ++index)
        {
            const auto& link = candidate.contactLinks.At(index);
            const uint32_t chainIndex = boneChain.At(link.firstBone);
            const uint32_t segment = boneSegment.At(link.firstBone);
            animation::FSecondaryMotionChainAttachmentContact attachment{};
            if (chainIndex == FixedChainIndex || !BuildLegacyAttachment(candidate, baseMatrices, link, chainIndex, segment, attachment, error))
            {
                if (error.Empty())
                {
                    error.Assign("legacy weighted contact link has no registered chain");
                }
                return false;
            }
            Array<FPreparedSecondaryMotionCollisionShape> prepared;
            if (!animation::PrepareSecondaryMotionFreeSegmentShapes(shapes, shapeCount, link.radius, prepared, error))
            {
                return false;
            }
            const uint32_t offset = attachmentPreparedShapes.Count();
            if (!attachmentPreparedShapes.AppendRange(prepared.Data(), prepared.Count()) || !resolvedAttachments.Append(attachment) || !resolvedAttachmentChains.Append(chainIndex) || !resolvedAttachmentShapeOffsets.Append(offset) || !resolvedAttachmentShapeCounts.Append(prepared.Count()))
            {
                error.Assign("secondary weighted contact attachment shape allocation failed");
                return false;
            }
        }
    }

    // 全てのshape配列が確定してから、借用pointerをdescriptorへ結び付ける。
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        chains.At(chainIndex).shapes = chainShapeCounts.At(chainIndex) > 0u ? chainPreparedShapes.Data() + chainShapeOffsets.At(chainIndex) : nullptr;
        chains.At(chainIndex).shapeCount = chainShapeCounts.At(chainIndex);
        attachmentOffsets.At(chainIndex) = 0u;
        attachmentCounts.At(chainIndex) = 0u;
        for (uint32_t index = 0; index < resolvedAttachments.Count(); ++index)
        {
            if (resolvedAttachmentChains.At(index) == chainIndex)
            {
                ++attachmentCounts.At(chainIndex);
            }
        }
    }
    Array<animation::FSecondaryMotionChainAttachmentContact> groupedAttachments;
    if (!groupedAttachments.Reserve(resolvedAttachments.Count()))
    {
        error.Assign("secondary weighted contact attachment group allocation failed");
        return false;
    }
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        attachmentOffsets.At(chainIndex) = groupedAttachments.Count();
        for (uint32_t index = 0; index < resolvedAttachments.Count(); ++index)
        {
            if (resolvedAttachmentChains.At(index) != chainIndex)
            {
                continue;
            }
            auto attachment = resolvedAttachments.At(index);
            const uint32_t shapeOffset = resolvedAttachmentShapeOffsets.At(index);
            const uint32_t shapeCountForLink = resolvedAttachmentShapeCounts.At(index);
            attachment.shapes = shapeCountForLink > 0u ? attachmentPreparedShapes.Data() + shapeOffset : nullptr;
            attachment.shapeCount = shapeCountForLink;
            if (!groupedAttachments.Append(attachment))
            {
                error.Assign("secondary weighted contact attachment group append failed");
                return false;
            }
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        chains.At(chainIndex).attachments = attachmentCounts.At(chainIndex) > 0u ? groupedAttachments.Data() + attachmentOffsets.At(chainIndex) : nullptr;
        chains.At(chainIndex).attachmentCount = attachmentCounts.At(chainIndex);
    }
    for (uint32_t pointIndex = 0; pointIndex < contacts.Count(); ++pointIndex)
    {
        const uint32_t offset = pointShapeOffsets.At(pointIndex);
        contacts.At(pointIndex).shapes = shapeCount > 0u ? pointPreparedShapes.Data() + offset : nullptr;
    }

    Array<FVector3d> warmStart;
    bool eligible = false;
    bool solved = false;
    if (delta > 0.0 && !BuildPreviousDirectionSeed(candidate, previous, beforePositions, chains, references, delta, warmStart, eligible, error))
    {
        return false;
    }
    if (eligible)
    {
        solved = animation::ResolvePreparedSecondaryMotionWeightedContacts(chains.Data(), chains.Count(), references.Data(), references.Count(), anchors.Data(), anchors.Count(), contacts.Data(), contacts.Count(), warmStart, error, candidate.linkIterations);
        if (solved)
        {
            positions.MoveFrom(warmStart);
        }
        else if (!error.Empty())
        {
            return false;
        }
    }
    if (!solved)
    {
        solved = animation::ResolvePreparedSecondaryMotionWeightedContacts(chains.Data(), chains.Count(), references.Data(), references.Count(), anchors.Data(), anchors.Count(), contacts.Data(), contacts.Count(), positions, error, candidate.linkIterations);
    }
    if (!solved && error.Empty() && delta == 0.0)
    {
        if (!BuildPreviousDirectionSeed(candidate, previous, beforePositions, chains, references, delta, warmStart, eligible, error))
        {
            return false;
        }
        if (eligible)
        {
            error.Clear();
            solved = animation::ResolvePreparedSecondaryMotionWeightedContacts(chains.Data(), chains.Count(), references.Data(), references.Count(), anchors.Data(), anchors.Count(), contacts.Data(), contacts.Count(), warmStart, error, candidate.linkIterations);
            if (solved)
            {
                positions.MoveFrom(warmStart);
            }
        }
    }
    if (!solved)
    {
        if (error.Empty())
        {
            error.Assign("secondary weighted contacts could not preserve body contact and chain constraints together");
        }
        return false;
    }
    if (positions.Count() != totalPositions)
    {
        error.Assign("secondary weighted contact solver changed the position count");
        return false;
    }
    for (uint32_t point = 0; point < positions.Count(); ++point)
    {
        if (!StorePosition(positions.At(point), storedPositions.At(point), error))
        {
            return false;
        }
        if (!storedDoublePositions.Append(ToDouble(storedPositions.At(point))))
        {
            error.Assign("secondary weighted contact stored position allocation failed");
            return false;
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        const auto& chain = *candidate.chains.At(chainIndex);
        const uint32_t first = chains.At(chainIndex).firstPoint;
        const Vec3& originalRoot = chain.simulation.points.At(0u).position;
        const Vec3& storedRoot = storedPositions.At(first);
        if (storedRoot.x != originalRoot.x || storedRoot.y != originalRoot.y || storedRoot.z != originalRoot.z || !CheckStoredChain(chain, storedPositions.Data() + first, error))
        {
            if (error.Empty())
            {
                error.Assign("secondary weighted contact changed a fixed chain root");
            }
            return false;
        }
        if (chainShapeCounts.At(chainIndex) > 0u && !animation::CheckSecondaryMotionContacts(chainExpandedShapes.Data() + chainShapeOffsets.At(chainIndex), chainShapeCounts.At(chainIndex), storedDoublePositions.Data() + first, chain.simulation.points.Count(), error, 1.0))
        {
            return false;
        }
    }

    // floatへ戻したbone寄与をskin weightのまま混ぜ、半分の許容幅で再検査する。
    for (uint32_t pointIndex = 0; pointIndex < contacts.Count(); ++pointIndex)
    {
        const auto& contact = contacts.At(pointIndex);
        FVector3d weightedPosition{};
        for (uint32_t anchorOffset = 0; anchorOffset < contact.anchorCount; ++anchorOffset)
        {
            FVector3d anchorPosition{};
            if (!EvaluateAnchor(anchors.At(contact.firstAnchor + anchorOffset), storedDoublePositions.Data(), storedDoublePositions.Count(), chains.Data(), chains.Count(), anchorPosition, error))
            {
                return false;
            }
            const double weight = anchors.At(contact.firstAnchor + anchorOffset).weight;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                weightedPosition.value[axis] += anchorPosition.value[axis] * weight;
            }
        }
        if (!IsFinite(weightedPosition))
        {
            error.Assign("secondary weighted contact point is not finite after float storage");
            return false;
        }
        if (!animation::CheckPreparedSecondaryMotionFreeSegmentContacts(contact.shapes, contact.shapeCount, weightedPosition, weightedPosition, error, 0.5))
        {
            return false;
        }
    }
    for (uint32_t index = 0; index < groupedAttachments.Count(); ++index)
    {
        const auto& attachment = groupedAttachments.At(index);
        uint32_t ownerChain = FixedChainIndex;
        for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
        {
            const uint32_t firstAttachment = attachmentOffsets.At(chainIndex);
            if (index >= firstAttachment && index - firstAttachment < attachmentCounts.At(chainIndex))
            {
                ownerChain = chainIndex;
                break;
            }
        }
        if (ownerChain == FixedChainIndex)
        {
            error.Assign("secondary weighted contact attachment owner is invalid");
            return false;
        }
        const auto& chainDescriptor = chains.At(ownerChain);
        const uint32_t parentIndex = chainDescriptor.firstPoint + attachment.segmentIndex;
        animation::FSecondaryMotionAttachmentEvaluation firstEvaluation{};
        animation::FSecondaryMotionAttachmentEvaluation secondEvaluation{};
        if (!animation::EvaluateSecondaryMotionAttachment(attachment.firstDefinition, storedDoublePositions.At(parentIndex), storedDoublePositions.At(parentIndex + 1u), firstEvaluation, error) || !animation::EvaluateSecondaryMotionAttachment(attachment.secondDefinition, storedDoublePositions.At(parentIndex), storedDoublePositions.At(parentIndex + 1u), secondEvaluation, error) || !animation::CheckPreparedSecondaryMotionFreeSegmentContacts(attachment.shapes, attachment.shapeCount, firstEvaluation.position, secondEvaluation.position, error, 0.5))
        {
            return false;
        }
    }

    // 位置・速度の両方を検証し終えてから候補鎖へ一括反映する。
    const bool projectVelocity = delta > 0.0 && delta < 0.25;
    if (projectVelocity)
    {
        for (uint32_t chainIndex = 0; chainIndex < candidate.chains.Count(); ++chainIndex)
        {
            const auto& chain = *candidate.chains.At(chainIndex);
            const auto* old = chainIndex < previous.chains.Count() ? previous.chains.At(chainIndex) : nullptr;
            if (!isfinite(chain.settings.teleportDistance) || chain.settings.teleportDistance < 0.0f)
            {
                error.Assign("secondary weighted contact teleport distance is invalid");
                return false;
            }
            if (!old || old->simulation.previousTargets.Count() == 0u)
            {
                continue;
            }
            if (!isfinite(old->simulation.previousTargets.At(0u).x) || !isfinite(old->simulation.previousTargets.At(0u).y) || !isfinite(old->simulation.previousTargets.At(0u).z))
            {
                error.Assign("secondary weighted contact teleport reference is invalid");
                return false;
            }
            const auto& oldRoot = old->simulation.previousTargets.At(0);
            const auto& root = chain.simulation.previousTargets.At(0);
            const FVector3d rootDelta = Subtract(ToDouble(root), ToDouble(oldRoot));
            if (Length(rootDelta) > chain.settings.teleportDistance)
            {
                continue;
            }
            const uint32_t first = chains.At(chainIndex).firstPoint;
            for (uint32_t point = 1; point < chain.simulation.points.Count(); ++point)
            {
                const FVector3d correction = Subtract(ToDouble(storedPositions.At(first + point)), beforePositions.At(first + point));
                const double correctionLength = Length(correction);
                FVector3d velocity = ToDouble(stagedVelocities.At(first + point));
                if (correctionLength > 0.0)
                {
                    // 幾何補正を勢いへ変換せず、補正に逆行する予測速度の成分だけを取り除く。
                    const FVector3d correctionDirection = Multiply(correction, 1.0 / correctionLength);
                    const double inwardVelocity = velocity.value[0] * correctionDirection.value[0] + velocity.value[1] * correctionDirection.value[1] + velocity.value[2] * correctionDirection.value[2];
                    if (inwardVelocity < 0.0)
                    {
                        velocity = Subtract(velocity, Multiply(correctionDirection, inwardVelocity));
                    }
                }
                if (!StorePosition(velocity, stagedVelocities.At(first + point), error))
                {
                    return false;
                }
            }
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < candidate.chains.Count(); ++chainIndex)
    {
        auto& chain = *candidate.chains.At(chainIndex);
        const uint32_t first = chains.At(chainIndex).firstPoint;
        for (uint32_t point = 0; point < chain.simulation.points.Count(); ++point)
        {
            chain.simulation.points.At(point).position = storedPositions.At(first + point);
            chain.simulation.points.At(point).velocity = stagedVelocities.At(first + point);
        }
    }
    error.Clear();
    return true;
}

bool CheckRenderedSecondaryMotionContactPoints(const FModelSecondaryMotionState& state, const Array<float>& renderedMatrices, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, String& error)
{
    error.Clear();
    if (!ValidatePointRanges(state, error))
    {
        return false;
    }
    if (state.contactPoints.Count() == 0u)
    {
        return true;
    }
    if (shapeCount > 64u || (shapeCount > 0u && !shapes) || renderedMatrices.Count() % 16u != 0u || !animation::ValidateSecondaryMotionCollisionShapes(shapes, shapeCount, error))
    {
        if (error.Empty())
        {
            error.Assign("secondary weighted rendered contact input is invalid");
        }
        return false;
    }
    if (shapeCount == 0u)
    {
        return true;
    }
    for (uint32_t pointIndex = 0; pointIndex < state.contactPoints.Count(); ++pointIndex)
    {
        const auto& point = state.contactPoints.At(pointIndex);
        Array<FPreparedSecondaryMotionCollisionShape> prepared;
        if (!animation::PrepareSecondaryMotionFreeSegmentShapes(shapes, shapeCount, point.radius, prepared, error))
        {
            return false;
        }
        FVector3d weightedPosition{};
        if (!EvaluateRenderedSecondaryMotionContactPoint(state, renderedMatrices, pointIndex, weightedPosition, error) || !animation::CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), weightedPosition, weightedPosition, error, 1.0))
        {
            if (error.Empty())
            {
                error.Assign("secondary weighted rendered contact point is invalid");
            }
            return false;
        }
    }
    error.Clear();
    return true;
}

bool EvaluateRenderedSecondaryMotionContactPoint(const FModelSecondaryMotionState& state, const Array<float>& renderedMatrices, uint32_t pointIndex, FVector3d& output, String& error)
{
    error.Clear();
    if (state.contactPoints.Count() > MaximumWeightedPoints || state.contactAnchors.Count() > MaximumWeightedAnchors || renderedMatrices.Count() % 16u != 0u || pointIndex >= state.contactPoints.Count())
    {
        error.Assign("secondary weighted rendered contact point index or matrix range is invalid");
        return false;
    }
    const auto& point = state.contactPoints.At(pointIndex);
    const uint64_t end = static_cast<uint64_t>(point.firstAnchor) + point.anchorCount;
    if (point.anchorCount == 0u || point.anchorCount > MaximumAnchorsPerPoint || end > state.contactAnchors.Count() || !isfinite(point.radius) || point.radius < 0.0f)
    {
        error.Assign("secondary weighted rendered contact point definition is invalid");
        return false;
    }
    FVector3d candidate{};
    double weightSum = 0.0;
    for (uint32_t anchorOffset = 0; anchorOffset < point.anchorCount; ++anchorOffset)
    {
        const auto& anchor = state.contactAnchors.At(point.firstAnchor + anchorOffset);
        if (!isfinite(anchor.weight) || !(anchor.weight > 0.0f) || !isfinite(anchor.offset.x) || !isfinite(anchor.offset.y) || !isfinite(anchor.offset.z))
        {
            error.Assign("secondary weighted rendered contact anchor is invalid");
            return false;
        }
        FVector3d position{};
        if (!EvaluateRenderedAnchor(anchor, renderedMatrices, position, error))
        {
            return false;
        }
        const double weight = anchor.weight;
        weightSum += weight;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            candidate.value[axis] += position.value[axis] * weight;
        }
    }
    if (!isfinite(weightSum) || fabs(weightSum - 1.0) > static_cast<double>(64.0f * FLT_EPSILON))
    {
        error.Assign("secondary weighted rendered contact weights must sum to one");
        return false;
    }
    if (!IsFinite(candidate) || fabs(candidate.value[0]) > FLT_MAX || fabs(candidate.value[1]) > FLT_MAX || fabs(candidate.value[2]) > FLT_MAX)
    {
        error.Assign("secondary weighted rendered contact point exceeds float range");
        return false;
    }
    output = candidate;
    error.Clear();
    return true;
}

}
