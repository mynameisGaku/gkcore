// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionContactVelocity.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "foundation/Memory.h"

#include <math.h>
#include <stdio.h>

namespace
{
using namespace gk;
using namespace gk::model::animation;

/**
 * 失敗理由を表示してテストを停止する。
 */
bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}

/**
 * 一つの線分鎖と、その前後基準姿勢を用意する。
 */
bool AppendSingleSegment(Array<FSecondaryMotionContactChain>& chains, Array<FSecondaryMotionReferenceSegment>& current, Array<FSecondaryMotionReferenceSegment>& previous, Array<uint64_t>& masks, const FVector3d& currentDirection, double currentLength, const FVector3d& previousDirection, double previousLength, double cosineLimit)
{
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 1u;
    chain.cosineLimit = cosineLimit;
    return chains.Append(chain) && current.Append({ currentDirection, currentLength }) && previous.Append({ previousDirection, previousLength }) && masks.Append(0u);
}

/**
 * 固定root、長さ等式、移動球面の法線、接線保持を独立式で確かめる。
 */
bool TestMovingSphereAndLengthKeepTangentVelocity()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    Array<FModelSecondaryMotionCollisionShape> oldShapes;
    Array<FPreparedSecondaryMotionCollisionShape> prepared;
    String error;
    const FVector3d x = { { 1.0, 0.0, 0.0 } };
    if (!AppendSingleSegment(chains, current, previous, masks, x, 1.0, x, 1.0, -1.0) || !positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append({ { 1.0, 0.0, 0.0 } }) || !velocities.Append({ 0.25f, 7.0f, 0.0f }) || !velocities.Append({ 3.0f, 0.0f, 4.0f }))
    {
        return Fail("moving sphere fixture setup failed");
    }
    const FModelSecondaryMotionCollisionShape oldSphere{ { 1.0f, 1.3f, 0.0f }, { 1.0f, 1.3f, 0.0f }, 0.8f };
    const FModelSecondaryMotionCollisionShape sphere{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, 1.0f };
    if (!oldShapes.Append(oldSphere) || !PrepareSecondaryMotionFreeSegmentShapes(&sphere, 1u, 0.0f, prepared, error))
    {
        return Fail("moving sphere shape preparation failed");
    }
    chains.At(0).shapes = prepared.Data();
    chains.At(0).shapeCount = 1u;
    masks.At(0) = 1u;
    if (ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, {}, {}, oldShapes, &sphere, 1u, 0.1, positions, velocities, error) != ESecondaryMotionContactVelocityResult::Applied || !error.Empty())
    {
        return Fail("moving sphere velocity projection failed");
    }
    // 球中心の-3Y移動に、半径増加による法線方向-2Yを加える。
    if (fabs(velocities.At(0).x - 0.25f) > 1.0e-7 || fabs(velocities.At(0).y - 7.0f) > 1.0e-7 || fabs(velocities.At(1).x - 0.25f) > 1.0e-6 || fabs(velocities.At(1).y + 5.0f) > 1.0e-6 || fabs(velocities.At(1).z - 4.0f) > 1.0e-6)
    {
        return Fail("length equality or moving-sphere normal projection changed the wrong velocity components");
    }
    const double lengthResidual = (velocities.At(1).x - velocities.At(0).x) - (1.0 - 1.0) / 0.1;
    const double normalResidual = -(velocities.At(1).y - (-5.0));
    if (fabs(lengthResidual) > 1.0e-6 || normalResidual < -1.0e-6)
    {
        return Fail("independent length or relative-normal oracle rejected stored velocities");
    }
    return true;
}

/**
 * cosineLimit=1が全軸のアニメーション線分速度を拘束するか確かめる。
 */
bool TestStraightChainTracksAllReferenceVelocityAxes()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    String error;
    const FVector3d currentDirection = { { 0.0, 1.0, 0.0 } };
    const FVector3d previousDirection = { { 1.0, 0.0, 0.0 } };
    if (!AppendSingleSegment(chains, current, previous, masks, currentDirection, 2.0, previousDirection, 1.0, 1.0) || !positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append({ { 0.0, 2.0, 0.0 } }) || !velocities.Append({ 0.25f, -0.5f, 0.75f }) || !velocities.Append({ 9.0f, 8.0f, 7.0f }))
    {
        return Fail("straight-chain fixture setup failed");
    }
    if (ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, {}, {}, {}, nullptr, 0u, 0.1, positions, velocities, error) != ESecondaryMotionContactVelocityResult::Applied || !error.Empty())
    {
        return Fail("straight-chain velocity projection failed");
    }
    // 基準線分差分は毎秒(-10,20,0)となる。
    if (fabs(velocities.At(1).x + 9.75f) > 1.0e-6 || fabs(velocities.At(1).y - 19.5f) > 1.0e-6 || fabs(velocities.At(1).z - 0.75f) > 1.0e-6)
    {
        return Fail("straight-chain equality did not impose the full reference-segment velocity");
    }
    return true;
}

/**
 * 曲げ境界にある節が移動基準方向の変化にも従うか独立微分で確かめる。
 */
bool TestActiveBendBoundaryTracksMovingReference()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    String error;
    const double sine = sqrt(0.75);
    const double previousAngle = 0.1;
    const FVector3d x = { { 1.0, 0.0, 0.0 } };
    const FVector3d previousDirection = { { cos(previousAngle), sin(previousAngle), 0.0 } };
    if (!AppendSingleSegment(chains, current, previous, masks, x, 1.0, previousDirection, 1.0, 0.5) || !positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append({ { 0.5, sine, 0.0 } }) || !velocities.Append({ 0.0f, 0.0f, 0.0f }) || !velocities.Append({ 0.0f, 0.0f, 0.0f }))
    {
        return Fail("moving-reference bend fixture setup failed");
    }
    if (ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, {}, {}, {}, nullptr, 0u, 0.1, positions, velocities, error) != ESecondaryMotionContactVelocityResult::Applied || !error.Empty())
    {
        return Fail("active bend boundary projection failed");
    }
    const FVector3d actualDirection = { { 0.5, sine, 0.0 } };
    const FVector3d tangent = { { 0.75, -0.5 * sine, 0.0 } };
    const FVector3d referenceRate = { { 0.0, -sin(previousAngle) / 0.1, 0.0 } };
    const double bendRate = tangent.value[0] * velocities.At(1).x + tangent.value[1] * velocities.At(1).y + actualDirection.value[0] * referenceRate.value[0] + actualDirection.value[1] * referenceRate.value[1];
    const double lengthRate = actualDirection.value[0] * velocities.At(1).x + actualDirection.value[1] * velocities.At(1).y;
    if (bendRate < -1.0e-6 || fabs(lengthRate) > 1.0e-6 || fabs(velocities.At(1).x) < 0.1)
    {
        return Fail("active bend boundary omitted reference motion or violated length equality");
    }
    return true;
}

/**
 * 動的anchorのdefinition移動と固定anchor移動が同じ法線速度項へ入るか確かめる。
 */
bool TestWeightedAnchorMotionIsAppliedOncePerWeight()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    Array<FSecondaryMotionWeightedContactAnchor> anchors;
    Array<FVector3d> anchorMotion;
    Array<FSecondaryMotionWeightedContactPoint> contacts;
    Array<FPreparedSecondaryMotionCollisionShape> prepared;
    String error;
    const FVector3d x = { { 1.0, 0.0, 0.0 } };
    if (!AppendSingleSegment(chains, current, previous, masks, x, 1.0, x, 1.0, -1.0))
    {
        return Fail("weighted-anchor chain fixture setup failed");
    }
    // weighted接触点を鎖の途中へ置くため、2本目のsegmentを追加する。
    chains.At(0).segmentCount = 2u;
    current.At(0).length = 1.0;
    previous.At(0).length = 1.0;
    if (!current.Append({ x, 1.0 }) || !previous.Append({ x, 1.0 }) || !positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append({ { 1.0, 0.0, 0.0 } }) || !positions.Append({ { 2.0, 0.0, 0.0 } }) || !velocities.Append({ 0.0f, 0.0f, 0.0f }) || !velocities.Append({ 0.0f, 0.0f, 0.0f }) || !velocities.Append({ 0.0f, 0.0f, 0.0f }))
    {
        return Fail("weighted-anchor point fixture setup failed");
    }
    FModelSecondaryMotionCollisionShape sphere{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, 1.0f };
    if (!PrepareSecondaryMotionFreeSegmentShapes(&sphere, 1u, 0.0f, prepared, error))
    {
        return Fail("weighted-anchor shape preparation failed");
    }
    FSecondaryMotionWeightedContactAnchor fixed{};
    fixed.chainIndex = UINT32_MAX;
    fixed.constantPosition = { { 1.0, 0.0, 0.0 } };
    fixed.weight = 0.5;
    FSecondaryMotionWeightedContactAnchor dynamic{};
    dynamic.chainIndex = 0u;
    dynamic.segmentIndex = 1u;
    dynamic.definition.direction = x;
    dynamic.weight = 0.5;
    if (!anchors.Append(fixed) || !anchors.Append(dynamic) || !anchorMotion.Append({ { 0.0, 2.0, 0.0 } }) || !anchorMotion.Append({ { 0.0, 2.0, 0.0 } }))
    {
        return Fail("weighted-anchor motion fixture setup failed");
    }
    FSecondaryMotionWeightedContactPoint contact{};
    contact.firstAnchor = 0u;
    contact.anchorCount = 2u;
    contact.shapes = prepared.Data();
    contact.shapeCount = 1u;
    if (!contacts.Append(contact) || ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, anchors, anchorMotion, {}, contacts, {}, &sphere, 1u, 0.1, positions, velocities, error) != ESecondaryMotionContactVelocityResult::Applied || !error.Empty())
    {
        return Fail("weighted-anchor velocity projection failed");
    }
    // 両anchorは毎秒+2Yで動くためweighted点も追従し、法線係数は子のY速度へ作用する。
    if (fabs(velocities.At(1).y + 4.0f) > 1.0e-6 || fabs(velocities.At(1).x) > 1.0e-6 || fabs(velocities.At(1).z) > 1.0e-6)
    {
        return Fail("fixed or dynamic weighted-anchor motion was omitted or double-counted");
    }
    const double weightedNormalResidual = -0.5 * velocities.At(1).y - 2.0;
    if (weightedNormalResidual < -1.0e-6)
    {
        return Fail("independent weighted-anchor normal oracle rejected stored velocity");
    }
    return true;
}

/**
 * 同じ骨へ付けた接触線分の両端移動項が接触点速度へ反映されるか確かめる。
 */
bool TestSameBoneAttachmentMotionMovesContactBoundary()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    Array<FVector3d> attachmentMotion;
    Array<FPreparedSecondaryMotionCollisionShape> prepared;
    String error;
    const FVector3d x = { { 1.0, 0.0, 0.0 } };
    if (!AppendSingleSegment(chains, current, previous, masks, x, 1.0, x, 1.0, -1.0))
    {
        return Fail("same-bone attachment chain fixture setup failed");
    }
    chains.At(0).segmentCount = 2u;
    if (!current.Append({ x, 1.0 }) || !previous.Append({ x, 1.0 }) || !positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append({ { 1.0, 0.0, 0.0 } }) || !positions.Append({ { 2.0, 0.0, 0.0 } }) || !velocities.Append({ 0.0f, 0.0f, 0.0f }) || !velocities.Append({ 0.0f, 0.0f, 0.0f }) || !velocities.Append({ 0.0f, 0.0f, 0.0f }))
    {
        return Fail("same-bone attachment point fixture setup failed");
    }
    const FModelSecondaryMotionCollisionShape sphere{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, 1.0f };
    if (!PrepareSecondaryMotionFreeSegmentShapes(&sphere, 1u, 0.0f, prepared, error))
    {
        return Fail("same-bone attachment shape preparation failed");
    }
    FSecondaryMotionChainAttachmentContact attachment{};
    attachment.segmentIndex = 1u;
    attachment.firstDefinition.direction = x;
    attachment.secondDefinition.direction = x;
    attachment.shapes = prepared.Data();
    attachment.shapeCount = 1u;
    chains.At(0).attachments = &attachment;
    chains.At(0).attachmentCount = 1u;
    if (!attachmentMotion.Append({ { 0.0, 2.0, 0.0 } }) || !attachmentMotion.Append({ { 0.0, 2.0, 0.0 } }) || ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, attachmentMotion, {}, {}, &sphere, 1u, 0.1, positions, velocities, error) != ESecondaryMotionContactVelocityResult::Applied || !error.Empty())
    {
        return Fail("same-bone attachment velocity projection failed");
    }
    // 両端が毎秒+2Yで動き法線は-Yなので、骨側は毎秒-2Y以上で離れる必要がある。
    if (fabs(velocities.At(1).y + 2.0f) > 1.0e-6 || fabs(velocities.At(1).x) > 1.0e-6 || fabs(velocities.At(1).z) > 1.0e-6)
    {
        return Fail("same-bone attachment motion was omitted from its surface-relative contact row");
    }
    return true;
}

/**
 * 自由度のない不可能行をquiet失敗とし、部分結果を保存しないか調べる。
 */
bool TestFixedOnlyImpossibleContactFailsQuietlyAndAtomically()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    Array<FSecondaryMotionWeightedContactAnchor> anchors;
    Array<FVector3d> anchorMotion;
    Array<FSecondaryMotionWeightedContactPoint> contacts;
    Array<FPreparedSecondaryMotionCollisionShape> prepared;
    String error;
    const FModelSecondaryMotionCollisionShape sphere{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, 1.0f };
    const FVector3d x = { { 1.0, 0.0, 0.0 } };
    if (!AppendSingleSegment(chains, current, previous, masks, x, 1.0, x, 1.0, -1.0) || !positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append({ { 1.0, 0.0, 0.0 } }) || !velocities.Append({ 3.0f, 4.0f, 5.0f }) || !velocities.Append({ 6.0f, 7.0f, 8.0f }) || !PrepareSecondaryMotionFreeSegmentShapes(&sphere, 1u, 0.0f, prepared, error))
    {
        return Fail("fixed-only shape preparation failed");
    }
    FSecondaryMotionWeightedContactAnchor fixed{};
    fixed.chainIndex = UINT32_MAX;
    fixed.constantPosition = { { 1.0, 0.0, 0.0 } };
    fixed.weight = 1.0;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 1u;
    contact.shapes = prepared.Data();
    contact.shapeCount = 1u;
    if (!anchors.Append(fixed) || !anchorMotion.Append({ { 0.0, 1.0, 0.0 } }) || !contacts.Append(contact))
    {
        return Fail("fixed-only fixture setup failed");
    }
    const Vec3 beforeRoot = velocities.At(0);
    const Vec3 beforeChild = velocities.At(1);
    const ESecondaryMotionContactVelocityResult result = ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, anchors, anchorMotion, {}, contacts, {}, &sphere, 1u, 0.1, positions, velocities, error);
    if (result != ESecondaryMotionContactVelocityResult::Unresolved || !error.Empty())
    {
        return Fail("fixed-only inward surface motion was not quietly rejected");
    }
    if (velocities.Count() != 2u || velocities.At(0).x != beforeRoot.x || velocities.At(0).y != beforeRoot.y || velocities.At(0).z != beforeRoot.z || velocities.At(1).x != beforeChild.x || velocities.At(1).y != beforeChild.y || velocities.At(1).z != beforeChild.z)
    {
        return Fail("quiet infeasibility partially changed stored velocities");
    }
    return true;
}

/**
 * 不正数値と確保失敗が理由付きのatomic failureになるか調べる。
 */
bool TestNumericAndAllocationFailuresAreAtomic()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    String error;
    const FVector3d x = { { 1.0, 0.0, 0.0 } };
    if (!AppendSingleSegment(chains, current, previous, masks, x, 1.0, x, 1.0, -1.0) || !positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append({ { 1.0, 0.0, 0.0 } }) || !velocities.Append({ 1.0f, 2.0f, 3.0f }) || !velocities.Append({ 4.0f, 5.0f, 6.0f }))
    {
        return Fail("atomic-failure fixture setup failed");
    }
    const Vec3 beforeRoot = velocities.At(0);
    const Vec3 beforeChild = velocities.At(1);
    current.At(0).length = NAN;
    if (ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, {}, {}, {}, nullptr, 0u, 0.1, positions, velocities, error) != ESecondaryMotionContactVelocityResult::Failed || error.Empty() || velocities.At(0).x != beforeRoot.x || velocities.At(1).z != beforeChild.z)
    {
        return Fail("invalid reference did not fail with an unchanged velocity array");
    }
    current.At(0).length = 1.0;
    error.Clear();
    SetAllocationFailureAfterForTesting(0);
    const ESecondaryMotionContactVelocityResult resultWithReservedError = ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, {}, {}, {}, nullptr, 0u, 0.1, positions, velocities, error);
    ResetAllocationFailureForTesting();
    if (resultWithReservedError != ESecondaryMotionContactVelocityResult::Failed || error.Empty() || velocities.At(0).x != beforeRoot.x || velocities.At(0).y != beforeRoot.y || velocities.At(0).z != beforeRoot.z || velocities.At(1).x != beforeChild.x || velocities.At(1).y != beforeChild.y || velocities.At(1).z != beforeChild.z)
    {
        return Fail("allocation failure did not preserve velocities and report its cause");
    }
    String freshAllocationError;
    SetAllocationFailureAfterForTesting(0);
    const ESecondaryMotionContactVelocityResult resultWithoutErrorStorage = ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, {}, {}, {}, nullptr, 0u, 0.1, positions, velocities, freshAllocationError);
    ResetAllocationFailureForTesting();
    if (resultWithoutErrorStorage != ESecondaryMotionContactVelocityResult::Failed || !freshAllocationError.Empty() || velocities.At(0).x != beforeRoot.x || velocities.At(0).y != beforeRoot.y || velocities.At(0).z != beforeRoot.z || velocities.At(1).x != beforeChild.x || velocities.At(1).y != beforeChild.y || velocities.At(1).z != beforeChild.z)
    {
        return Fail("allocation failure depended on allocating its diagnostic or changed velocities");
    }
    return true;
}
/**
 * 長い直線鎖で先端接触と各骨の長さ速度を同時に満たせるか調べる。
 */
bool TestLongChainTipContactAndEveryLengthConstraint()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    Array<FSecondaryMotionWeightedContactAnchor> anchors;
    Array<FVector3d> anchorMotion;
    Array<FSecondaryMotionWeightedContactPoint> contacts;
    Array<FPreparedSecondaryMotionCollisionShape> prepared;
    String error;
    const FVector3d x = { { 1.0, 0.0, 0.0 } };
    const double segmentLength = 0.1;
    const double sphereRadius = 0.2;
    const double diagonal = sphereRadius / sqrt(2.0);
    FSecondaryMotionContactChain chain{};
    chain.segmentCount = 8u;
    chain.cosineLimit = -1.0;
    if (!chains.Append(chain) || !masks.Append(0u))
    {
        return Fail("long-chain fixture could not append its chain");
    }
    for (uint32_t pointIndex = 0; pointIndex <= 8u; ++pointIndex)
    {
        const double xPosition = static_cast<double>(pointIndex) * segmentLength;
        if (!positions.Append({ { xPosition, 0.0, 0.0 } }))
        {
            return Fail("long-chain fixture could not append a point");
        }
        if (pointIndex < 8u && (!current.Append({ x, segmentLength }) || !previous.Append({ x, segmentLength })))
        {
            return Fail("long-chain fixture could not append a reference segment");
        }
        const Vec3 velocity = pointIndex == 8u ? Vec3{ 0.0f, -1.0f, 0.0f } : Vec3{ 0.0f, 0.0f, 0.0f };
        if (!velocities.Append(velocity))
        {
            return Fail("long-chain fixture could not append a velocity");
        }
    }
    const FVector3d tip = positions.At(8u);
    const FModelSecondaryMotionCollisionShape sphere{ { static_cast<float>(tip.value[0] - diagonal), static_cast<float>(tip.value[1] - diagonal), 0.0f }, { static_cast<float>(tip.value[0] - diagonal), static_cast<float>(tip.value[1] - diagonal), 0.0f }, static_cast<float>(sphereRadius) };
    if (!PrepareSecondaryMotionFreeSegmentShapes(&sphere, 1u, 0.0f, prepared, error))
    {
        return Fail("long-chain fixture shape preparation failed");
    }
    FSecondaryMotionWeightedContactAnchor anchor{};
    anchor.chainIndex = 0u;
    anchor.segmentIndex = 7u;
    anchor.definition.direction = x;
    anchor.definition.offset = { { segmentLength, 0.0, 0.0 } };
    anchor.weight = 1.0;
    FSecondaryMotionWeightedContactPoint contact{};
    contact.anchorCount = 1u;
    contact.shapes = prepared.Data();
    contact.shapeCount = 1u;
    if (!anchors.Append(anchor) || !anchorMotion.Append({ { 0.0, 0.0, 0.0 } }) || !contacts.Append(contact))
    {
        return Fail("long-chain fixture could not append its weighted contact");
    }
    const ESecondaryMotionContactVelocityResult result = ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, anchors, anchorMotion, {}, contacts, {}, &sphere, 1u, 1.0 / 60.0, positions, velocities, error);
    if (result != ESecondaryMotionContactVelocityResult::Applied || !error.Empty())
    {
        fprintf(stderr, "long-chain projection result=%u error=%s\n", static_cast<unsigned>(result), error.CStr());
        return Fail("long-chain tip contact and length constraints did not converge");
    }
    if (fabs(velocities.At(0).x) > 1.0e-7 || fabs(velocities.At(0).y) > 1.0e-7 || fabs(velocities.At(0).z) > 1.0e-7)
    {
        return Fail("long-chain projection moved the fixed root");
    }
    for (uint32_t segmentIndex = 0; segmentIndex < 8u; ++segmentIndex)
    {
        const Vec3 parentVelocity = velocities.At(segmentIndex);
        const Vec3 childVelocity = velocities.At(segmentIndex + 1u);
        const double radialRate = static_cast<double>(childVelocity.x) - parentVelocity.x;
        if (!isfinite(parentVelocity.x) || !isfinite(parentVelocity.y) || !isfinite(parentVelocity.z) || !isfinite(childVelocity.x) || !isfinite(childVelocity.y) || !isfinite(childVelocity.z) || fabs(radialRate) > 1.0e-7)
        {
            return Fail("long-chain projection violated a finite segment length rate");
        }
    }
    const double normalX = tip.value[0] - sphere.start.x;
    const double normalY = tip.value[1] - sphere.start.y;
    const double normalLength = sqrt(normalX * normalX + normalY * normalY);
    if (!isfinite(normalLength) || !(normalLength > 0.0))
    {
        return Fail("long-chain fixture has an invalid sphere normal");
    }
    const double tipNormalRate = (normalX * velocities.At(8u).x + normalY * velocities.At(8u).y) / normalLength;
    if (!isfinite(tipNormalRate) || tipNormalRate < -1.0e-7)
    {
        return Fail("long-chain projection left the weighted tip moving into its active sphere");
    }
    return true;
}

/**
 * 斜め骨の接線速度で、double解の成立とfloat保存だけで生じる残差を独立式で分けて検査する。
 */
bool TestDiagonalTangentAcceptsOnlyMeasuredCastError()
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    String error;
    // 3:4の骨へ十分に大きい速度を与え、float丸めの残差を固定許容幅より大きくする。
    const FVector3d direction{ { 0.6, 0.8, 0.0 } };
    const Vec3 input{ 123.456f, -95.123f, 17.1f };
    if (!AppendSingleSegment(chains, current, previous, masks, direction, 1.0, direction, 1.0, -1.0) || !positions.Append({ { 0.0, 0.0, 0.0 } }) || !positions.Append(direction) || !velocities.Append({}) || !velocities.Append(input))
    {
        return Fail("diagonal tangent fixture setup failed");
    }
    // 骨長速度0の直交射影をsolverとは独立に求め、double解とfloat変換の誤差上界を計算する。
    const double radial = direction.value[0] * input.x + direction.value[1] * input.y;
    const double expected[3] = { input.x - direction.value[0] * radial, input.y - direction.value[1] * radial, input.z };
    const float rounded[3] = { static_cast<float>(expected[0]), static_cast<float>(expected[1]), static_cast<float>(expected[2]) };
    const double doubleResidual = direction.value[0] * expected[0] + direction.value[1] * expected[1];
    const double storedResidual = direction.value[0] * rounded[0] + direction.value[1] * rounded[1];
    const double castBound = fabs(direction.value[0]) * fabs(rounded[0] - expected[0]) + fabs(direction.value[1]) * fabs(rounded[1] - expected[1]);
    if (fabs(doubleResidual) > 1.0e-7 || fabs(storedResidual) <= 1.0e-7 || fabs(storedResidual) > 1.0e-7 + castBound)
    {
        return Fail("diagonal tangent fixture does not isolate a float-only constraint residual");
    }
    const ESecondaryMotionContactVelocityResult result = ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, {}, {}, {}, nullptr, 0u, 0.1, positions, velocities, error);
    if (result != ESecondaryMotionContactVelocityResult::Applied || !error.Empty() || velocities.At(0).x != 0.0f || velocities.At(0).y != 0.0f || velocities.At(0).z != 0.0f || velocities.At(1).x != rounded[0] || velocities.At(1).y != rounded[1] || velocities.At(1).z != rounded[2])
    {
        fprintf(stderr, "diagonal tangent result=%u doubleResidual=%.17g storedResidual=%.17g castBound=%.17g error=%s\n", static_cast<unsigned>(result), doubleResidual, storedResidual, castBound, error.CStr());
        return Fail("a valid diagonal tangent velocity was rejected solely by its measured float cast error");
    }
    fprintf(stdout, "diagonal tangent doubleResidual=%.17g storedResidual=%.17g castBound=%.17g\n", doubleResidual, storedResidual, castBound);
    return true;
}

/**
 * 自由点に相反する面速度を要求し、double段階の不成立を保存丸めの許容で隠さないことを確かめる。
 */
bool TestIncompatibleMovingSurfacesRejectDoubleResidualAtomically(bool diagonal)
{
    Array<FSecondaryMotionContactChain> chains;
    Array<FSecondaryMotionReferenceSegment> current;
    Array<FSecondaryMotionReferenceSegment> previous;
    Array<uint64_t> masks;
    Array<FVector3d> positions;
    Array<Vec3> velocities;
    Array<FModelSecondaryMotionCollisionShape> oldShapes;
    Array<FPreparedSecondaryMotionCollisionShape> prepared;
    String error;
    const FVector3d direction{ { 1.0, 0.0, 0.0 } };
    // 斜めの面では大きい接線速度も与え、cast許容量より小さい真のdouble残差を許さない。
    const float y = diagonal ? 0.6f : 1.0f;
    const float z = diagonal ? 0.8f : 0.0f;
    const float oldY = diagonal ? nextafterf(y, INFINITY) : 1.1f;
    const float oldZ = diagonal ? nextafterf(z, INFINITY) : 0.0f;
    const FModelSecondaryMotionCollisionShape shapes[2] = { { { 1.0f, y, z }, { 1.0f, y, z }, 1.0f }, { { 1.0f, -y, -z }, { 1.0f, -y, -z }, 1.0f } };
    const FModelSecondaryMotionCollisionShape old[2] = { { { 1.0f, oldY, oldZ }, { 1.0f, oldY, oldZ }, 1.0f }, { { 1.0f, -oldY, -oldZ }, { 1.0f, -oldY, -oldZ }, 1.0f } };
    const Vec3 input = diagonal ? Vec3{ 0.0f, 12345678.0f, 4567890.0f } : Vec3{ 0.0f, 0.0f, 12345.678f };
    // 両不等式の和は負で固定され、どのdouble速度でも少なくともこの幅を違反する。
    const double normalLength = sqrt(static_cast<double>(y) * y + static_cast<double>(z) * z);
    const double minimumViolation = (y * (static_cast<double>(oldY) - y) + z * (static_cast<double>(oldZ) - z)) / (normalLength * 0.1);
    if (!(minimumViolation > 1.0e-7))
    {
        return Fail("incompatible moving-surface fixture does not exceed the double constraint tolerance");
    }
    if (!AppendSingleSegment(chains, current, previous, masks, direction, 1.0, direction, 1.0, -1.0) || !positions.Append({}) || !positions.Append(direction) || !velocities.Append({}) || !velocities.Append(input) || !oldShapes.AppendRange(old, 2u) || !PrepareSecondaryMotionFreeSegmentShapes(shapes, 2u, 0.0f, prepared, error))
    {
        return Fail("incompatible moving-surface fixture setup failed");
    }
    chains.At(0u).shapes = prepared.Data();
    chains.At(0u).shapeCount = 2u;
    masks.At(0u) = 3u;
    const ESecondaryMotionContactVelocityResult result = ProjectSecondaryMotionContactVelocities(chains, current, previous, masks, {}, {}, {}, {}, oldShapes, shapes, 2u, 0.1, positions, velocities, error);
    if (result != ESecondaryMotionContactVelocityResult::Unresolved || !error.Empty() || velocities.Count() != 2u || velocities.At(0u).x != 0.0f || velocities.At(0u).y != 0.0f || velocities.At(0u).z != 0.0f || velocities.At(1u).x != input.x || velocities.At(1u).y != input.y || velocities.At(1u).z != input.z)
    {
        fprintf(stderr, "incompatible moving surfaces diagonal=%u result=%u minimumDoubleViolation=%.17g\n", diagonal ? 1u : 0u, static_cast<unsigned>(result), minimumViolation);
        return Fail("a genuinely incompatible double velocity constraint was accepted or changed stored velocities");
    }
    return true;
}
}

int main()
{
    return TestMovingSphereAndLengthKeepTangentVelocity() && TestStraightChainTracksAllReferenceVelocityAxes() && TestActiveBendBoundaryTracksMovingReference() && TestWeightedAnchorMotionIsAppliedOncePerWeight() && TestSameBoneAttachmentMotionMovesContactBoundary() && TestLongChainTipContactAndEveryLengthConstraint() && TestFixedOnlyImpossibleContactFailsQuietlyAndAtomically() && TestNumericAndAllocationFailuresAreAtomic() && TestIncompatibleMovingSurfacesRejectDoubleResidualAtomically(false) && TestIncompatibleMovingSurfacesRejectDoubleResidualAtomically(true) && TestDiagonalTangentAcceptsOnlyMeasuredCastError() ? 0 : 1;
}
