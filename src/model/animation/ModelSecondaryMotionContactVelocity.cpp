// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionContactVelocity.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionCollision.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

// 速度射影で同時に扱う最大自由点数。
const uint32_t kMaximumVelocityPoints = 1024u;
// 速度射影で同時に扱う最大制約行数。
const uint32_t kMaximumVelocityRows = 4096u;
// 速度射影で同時に扱う最大疎係数数。
const uint32_t kMaximumVelocityEntries = 65536u;
// PGSで許す最大掃引数。
const uint32_t kMaximumVelocityPasses = 32u;
// float保存後に許す速度制約残差。
const double kVelocityTolerance = 1.0e-7;
// 接触境界でfloat座標丸めを吸収する倍率。
const double kActiveMarginScale = 8.0 * FLT_EPSILON;
// 固定寄与を示す鎖番号。
const uint32_t kFixedChainIndex = UINT32_MAX;

/**
 * 制約行の疎な係数を一つの移動点へ結び付ける。
 */
struct FVelocityEntry
{
    // 全点配列内の係数対象番号。
    uint32_t pointIndex = 0;
    // 速度へ掛ける3成分係数。
    FVector3d coefficient{};
};

/**
 * 速度制約一つの係数範囲と固定寄与を保持する。
 */
struct FVelocityRow
{
    // 疎係数配列内の先頭番号。
    uint32_t firstEntry = 0;
    // この行が持つ疎係数数。
    uint32_t entryCount = 0;
    // 固定root、形状移動、基準姿勢移動による速度寄与。
    double fixedVelocity = 0.0;
    // 等式行ならtrue、外向き不等式ならfalse。
    bool equality = false;
    // 骨長またはzero-angleの基準速度で構造的に満たす行。
    bool structural = false;
};

/**
 * 有限な倍精度3次元値か調べる。
 */
bool IsFinite(const FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * 3次元内積を計算する。
 */
double Dot(const FVector3d& first, const FVector3d& second)
{
    return first.value[0] * second.value[0] + first.value[1] * second.value[1] + first.value[2] * second.value[2];
}

/**
 * 3次元ベクトルの差を計算する。
 */
FVector3d Subtract(const FVector3d& first, const FVector3d& second)
{
    return { { first.value[0] - second.value[0], first.value[1] - second.value[1], first.value[2] - second.value[2] } };
}

/**
 * 3次元ベクトルを係数倍する。
 */
FVector3d Multiply(const FVector3d& value, double scale)
{
    return { { value.value[0] * scale, value.value[1] * scale, value.value[2] * scale } };
}

/**
 * 桁あふれを避けて3次元長を計算する。
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
 * 固定root点かを鎖範囲から調べる。
 */
bool IsRootPoint(const Array<uint8_t>& rootPoints, uint32_t pointIndex)
{
    return pointIndex < rootPoints.Count() && rootPoints.At(pointIndex) != 0u;
}

/**
 * raw shapeの履歴から接触法線方向の面速度を求める。
 */
bool EvaluateSurfaceVelocity(const FModelSecondaryMotionCollisionShape& current, const FModelSecondaryMotionCollisionShape* previous, double delta, const FVector3d& point, const FVector3d& normal, FVector3d& output)
{
    output = {};
    if (!previous || !(delta > 0.0))
    {
        return true;
    }
    const FVector3d axis = { { static_cast<double>(current.end.x) - current.start.x, static_cast<double>(current.end.y) - current.start.y, static_cast<double>(current.end.z) - current.start.z } };
    const FVector3d offset = { { point.value[0] - current.start.x, point.value[1] - current.start.y, point.value[2] - current.start.z } };
    const double axisSquared = Dot(axis, axis);
    const double along = axisSquared > 0.0 ? fmax(0.0, fmin(1.0, Dot(offset, axis) / axisSquared)) : 0.0;
    const double endpointWeights[2] = { 1.0 - along, along };
    const float currentStart[3] = { current.start.x, current.start.y, current.start.z };
    const float currentEnd[3] = { current.end.x, current.end.y, current.end.z };
    const float previousStart[3] = { previous->start.x, previous->start.y, previous->start.z };
    const float previousEnd[3] = { previous->end.x, previous->end.y, previous->end.z };
    for (uint32_t axisIndex = 0; axisIndex < 3u; ++axisIndex)
    {
        output.value[axisIndex] = (endpointWeights[0] * (static_cast<double>(currentStart[axisIndex]) - previousStart[axisIndex]) + endpointWeights[1] * (static_cast<double>(currentEnd[axisIndex]) - previousEnd[axisIndex])) / delta;
    }
    const double radiusRate = (static_cast<double>(current.radius) - previous->radius) / delta;
    for (uint32_t axisIndex = 0; axisIndex < 3u; ++axisIndex)
    {
        output.value[axisIndex] += radiusRate * normal.value[axisIndex];
    }
    return IsFinite(output);
}

/**
 * 接触距離が保存済みfloat位置の誤差幅内か調べる。
 */
bool IsVelocityActive(const FPreparedSecondaryMotionCollisionShape& shape, const FVector3d& point, double penetration)
{
    const double distance = shape.radius - penetration;
    double magnitude = shape.radius;
    for (uint32_t axis = 0; axis < 3u; ++axis)
    {
        magnitude = fmax(magnitude, fabs(point.value[axis]));
        magnitude = fmax(magnitude, fabs(shape.start.value[axis]));
        magnitude = fmax(magnitude, fabs(shape.end.value[axis]));
    }
    return isfinite(distance) && distance <= shape.radius + magnitude * kActiveMarginScale;
}

/**
 * point係数を一時密行へ加算し、root寄与は固定速度へ畳み込む。
 */
bool AddPointCoefficient(const Array<uint8_t>& rootPoints, const Array<Vec3>& sourceVelocities, Array<FVector3d>& coefficients, uint32_t pointIndex, const FVector3d& coefficient, double& fixedVelocity)
{
    if (pointIndex >= coefficients.Count() || !IsFinite(coefficient))
    {
        return false;
    }
    if (IsRootPoint(rootPoints, pointIndex))
    {
        const Vec3& rootVelocity = sourceVelocities.At(pointIndex);
        fixedVelocity += coefficient.value[0] * rootVelocity.x + coefficient.value[1] * rootVelocity.y + coefficient.value[2] * rootVelocity.z;
        return isfinite(fixedVelocity);
    }
    FVector3d& accumulated = coefficients.At(pointIndex);
    for (uint32_t axis = 0; axis < 3u; ++axis)
    {
        accumulated.value[axis] += coefficient.value[axis];
    }
    return IsFinite(accumulated);
}

/**
 * 3次元速度行を係数表へ追加し、自由点のない不成立行を静かに拒否する。
 */
bool AppendRow(Array<FVector3d>& coefficients, double fixedVelocity, bool equality, Array<FVelocityRow>& rows, Array<FVelocityEntry>& entries, bool& budgetExceeded, bool& finiteImpossible, String& error)
{
    if (!isfinite(fixedVelocity))
    {
        error.Assign("secondary contact velocity fixed row is invalid");
        return false;
    }
    FVelocityRow row{};
    row.firstEntry = entries.Count();
    row.fixedVelocity = fixedVelocity;
    row.equality = equality;
    for (uint32_t pointIndex = 0; pointIndex < coefficients.Count(); ++pointIndex)
    {
        const FVector3d& coefficient = coefficients.At(pointIndex);
        const double squared = Dot(coefficient, coefficient);
        if (!isfinite(squared))
        {
            error.Assign("secondary contact velocity coefficient is invalid");
            return false;
        }
        if (!(squared > 0.0))
        {
            continue;
        }
        if (entries.Count() >= kMaximumVelocityEntries)
        {
            budgetExceeded = true;
            return false;
        }
        if (!entries.Append({ pointIndex, coefficient }))
        {
            error.Assign("secondary contact velocity entry allocation failed");
            return false;
        }
        ++row.entryCount;
    }
    if (row.entryCount == 0u)
    {
        if ((equality && fabs(fixedVelocity) > kVelocityTolerance) || (!equality && fixedVelocity < -kVelocityTolerance))
        {
            finiteImpossible = true;
        }
        return true;
    }
    if (rows.Count() >= kMaximumVelocityRows)
    {
        budgetExceeded = true;
        return false;
    }
    if (!rows.Append(row))
    {
        error.Assign("secondary contact velocity row allocation failed");
        return false;
    }
    return true;
}

/**
 * 新しい制約行用にdense scratch係数をゼロへ戻す。
 */
void ResetCoefficients(Array<FVector3d>& coefficients)
{
    for (uint32_t pointIndex = 0; pointIndex < coefficients.Count(); ++pointIndex)
    {
        coefficients.At(pointIndex) = {};
    }
}

/**
 * 接触線分の両端へ、法線と最近点重みから速度係数を加える。
 */
bool AddSpanCoefficients(const Array<uint8_t>& rootPoints, const Array<Vec3>& sourceVelocities, Array<FVector3d>& coefficients, uint32_t firstPoint, uint32_t secondPoint, const FVector3d& normal, double spanAmount, double scale, double& fixedVelocity)
{
    const FVector3d firstCoefficient = Multiply(normal, (1.0 - spanAmount) * scale);
    const FVector3d secondCoefficient = Multiply(normal, spanAmount * scale);
    return AddPointCoefficient(rootPoints, sourceVelocities, coefficients, firstPoint, firstCoefficient, fixedVelocity) && AddPointCoefficient(rootPoints, sourceVelocities, coefficients, secondPoint, secondCoefficient, fixedVelocity);
}

/**
 * weighted anchor位置と、速度Jacobianの親子係数を計算する。
 */
bool EvaluateWeightedPoint(const Array<FSecondaryMotionContactChain>& chains, const Array<FSecondaryMotionReferenceSegment>& references, const Array<FSecondaryMotionWeightedContactAnchor>& anchors, uint32_t firstAnchor, uint32_t anchorCount, const Array<FVector3d>& positions, FVector3d& weightedPoint, String& error)
{
    weightedPoint = {};
    for (uint32_t localIndex = 0; localIndex < anchorCount; ++localIndex)
    {
        const FSecondaryMotionWeightedContactAnchor& anchor = anchors.At(firstAnchor + localIndex);
        FVector3d anchorPosition = anchor.constantPosition;
        if (anchor.chainIndex != kFixedChainIndex)
        {
            if (anchor.chainIndex >= chains.Count())
            {
                error.Assign("secondary contact velocity anchor chain index is invalid");
                return false;
            }
            const FSecondaryMotionContactChain& chain = chains.At(anchor.chainIndex);
            if (anchor.segmentIndex >= chain.segmentCount || chain.firstSegment + anchor.segmentIndex >= references.Count())
            {
                error.Assign("secondary contact velocity anchor segment index is invalid");
                return false;
            }
            FSecondaryMotionAttachmentEvaluation evaluation{};
            const uint32_t parentIndex = chain.firstPoint + anchor.segmentIndex;
            if (!EvaluateSecondaryMotionAttachment(anchor.definition, positions.At(parentIndex), positions.At(parentIndex + 1u), evaluation, error))
            {
                return false;
            }
            anchorPosition = evaluation.position;
        }
        if (!IsFinite(anchorPosition))
        {
            error.Assign("secondary contact velocity anchor position is invalid");
            return false;
        }
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            weightedPoint.value[axis] += anchorPosition.value[axis] * anchor.weight;
        }
    }
    if (!IsFinite(weightedPoint))
    {
        error.Assign("secondary contact velocity weighted point is invalid");
        return false;
    }
    return true;
}

/**
 * weighted anchorの接触法線を、添付位置Jacobianで親子速度へ戻す。
 */
bool AddWeightedAnchorCoefficients(const Array<FSecondaryMotionContactChain>& chains, const Array<uint8_t>& rootPoints, const Array<FSecondaryMotionWeightedContactAnchor>& anchors, const Array<FVector3d>& anchorMotionVelocities, const Array<Vec3>& sourceVelocities, const Array<FVector3d>& positions, uint32_t firstAnchor, uint32_t anchorCount, const FVector3d& normal, Array<FVector3d>& coefficients, double& fixedVelocity, String& error)
{
    for (uint32_t localIndex = 0; localIndex < anchorCount; ++localIndex)
    {
        const uint32_t anchorIndex = firstAnchor + localIndex;
        const FSecondaryMotionWeightedContactAnchor& anchor = anchors.At(anchorIndex);
        const FVector3d& motion = anchorMotionVelocities.At(anchorIndex);
        fixedVelocity += anchor.weight * Dot(normal, motion);
        if (!isfinite(fixedVelocity))
        {
            error.Assign("secondary contact velocity anchor motion is invalid");
            return false;
        }
        if (anchor.chainIndex == kFixedChainIndex)
        {
            continue;
        }
        const FSecondaryMotionContactChain& chain = chains.At(anchor.chainIndex);
        const uint32_t parentIndex = chain.firstPoint + anchor.segmentIndex;
        FSecondaryMotionAttachmentEvaluation evaluation{};
        if (!EvaluateSecondaryMotionAttachment(anchor.definition, positions.At(parentIndex), positions.At(parentIndex + 1u), evaluation, error))
        {
            return false;
        }
        FVector3d childCoefficient{};
        for (uint32_t inputAxis = 0; inputAxis < 3u; ++inputAxis)
        {
            for (uint32_t outputAxis = 0; outputAxis < 3u; ++outputAxis)
            {
                childCoefficient.value[inputAxis] += evaluation.childJacobian[outputAxis * 3u + inputAxis] * normal.value[outputAxis];
            }
        }
        const FVector3d parentCoefficient = Subtract(normal, childCoefficient);
        if (!AddPointCoefficient(rootPoints, sourceVelocities, coefficients, parentIndex, Multiply(parentCoefficient, anchor.weight), fixedVelocity) || !AddPointCoefficient(rootPoints, sourceVelocities, coefficients, parentIndex + 1u, Multiply(childCoefficient, anchor.weight), fixedVelocity))
        {
            error.Assign("secondary contact velocity anchor coefficient is invalid");
            return false;
        }
    }
    return true;
}

/**
 * 行の速度残差を作業倍精度速度から計算する。
 */
double EvaluateRow(const FVelocityRow& row, const Array<FVelocityEntry>& entries, const Array<FVector3d>& velocities)
{
    double value = row.fixedVelocity;
    for (uint32_t localEntry = 0; localEntry < row.entryCount; ++localEntry)
    {
        const FVelocityEntry& entry = entries.At(row.firstEntry + localEntry);
        value += Dot(entry.coefficient, velocities.At(entry.pointIndex));
    }
    return value;
}

/**
 * float保存候補を使って行残差を再計算する。
 */
double EvaluateStoredRow(const FVelocityRow& row, const Array<FVelocityEntry>& entries, const Array<Vec3>& velocities)
{
    double value = row.fixedVelocity;
    for (uint32_t localEntry = 0; localEntry < row.entryCount; ++localEntry)
    {
        const FVelocityEntry& entry = entries.At(row.firstEntry + localEntry);
        const Vec3& velocity = velocities.At(entry.pointIndex);
        value += entry.coefficient.value[0] * velocity.x + entry.coefficient.value[1] * velocity.y + entry.coefficient.value[2] * velocity.z;
    }
    return value;
}

}

ESecondaryMotionContactVelocityResult ProjectSecondaryMotionContactVelocities(const Array<FSecondaryMotionContactChain>& chains, const Array<FSecondaryMotionReferenceSegment>& references, const Array<FSecondaryMotionReferenceSegment>& previousReferences, const Array<uint64_t>& colliderMasks, const Array<FSecondaryMotionWeightedContactAnchor>& anchors, const Array<FVector3d>& anchorMotionVelocities, const Array<FVector3d>& attachmentMotionVelocities, const Array<FSecondaryMotionWeightedContactPoint>& contacts, const Array<FModelSecondaryMotionCollisionShape>& previousShapes, const FModelSecondaryMotionCollisionShape* currentShapes, uint32_t shapeCount, double delta, const Array<FVector3d>& positions, Array<Vec3>& velocities, String& error)
{
    error.Clear();
    if (chains.Count() != colliderMasks.Count() || references.Count() != previousReferences.Count() || references.Count() > 16384u || anchors.Count() != anchorMotionVelocities.Count() || (shapeCount > 0u && !currentShapes) || shapeCount > 64u || (previousShapes.Count() > 0u && previousShapes.Count() != shapeCount) || !isfinite(delta) || !(delta > 0.0) || delta >= 0.25 || positions.Count() != velocities.Count())
    {
        error.Assign("secondary contact velocity input ranges are invalid");
        return ESecondaryMotionContactVelocityResult::Failed;
    }
    uint64_t expectedPoints = 0;
    uint64_t expectedReferences = 0;
    uint64_t expectedAttachments = 0;
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains.At(chainIndex);
        if (chain.segmentCount == 0u || chain.firstPoint != expectedPoints || chain.firstSegment != expectedReferences || chain.shapeCount > 64u || chain.attachmentCount > 1024u || (chain.shapeCount > 0u && !chain.shapes) || (chain.attachmentCount > 0u && !chain.attachments) || !isfinite(chain.cosineLimit) || chain.cosineLimit < -1.0 || chain.cosineLimit > 1.0)
        {
            error.Assign("secondary contact velocity chain descriptor is invalid");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
        expectedPoints += static_cast<uint64_t>(chain.segmentCount) + 1u;
        expectedReferences += chain.segmentCount;
        expectedAttachments += static_cast<uint64_t>(chain.attachmentCount) * 2u;
        if (expectedPoints > UINT32_MAX || expectedReferences > UINT32_MAX || expectedAttachments > UINT32_MAX)
        {
            error.Assign("secondary contact velocity chain range overflowed");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    if (expectedPoints != positions.Count() || expectedReferences != references.Count() || expectedAttachments != attachmentMotionVelocities.Count())
    {
        error.Assign("secondary contact velocity chain arrays do not match");
        return ESecondaryMotionContactVelocityResult::Failed;
    }
    uint64_t expectedAnchor = 0;
    for (uint32_t contactIndex = 0; contactIndex < contacts.Count(); ++contactIndex)
    {
        const FSecondaryMotionWeightedContactPoint& contact = contacts.At(contactIndex);
        if (contact.firstAnchor != expectedAnchor || contact.anchorCount == 0u || contact.anchorCount > 64u || contact.anchorCount > anchors.Count() - expectedAnchor || contact.shapeCount != shapeCount || (contact.shapeCount > 0u && !contact.shapes))
        {
            error.Assign("secondary contact velocity weighted point range is invalid");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
        expectedAnchor += contact.anchorCount;
    }
    if (expectedAnchor != anchors.Count() || anchors.Count() > 16384u || contacts.Count() > 1024u)
    {
        error.Assign("secondary contact velocity anchor ranges do not match");
        return ESecondaryMotionContactVelocityResult::Failed;
    }
    for (uint32_t index = 0; index < positions.Count(); ++index)
    {
        if (!IsFinite(positions.At(index)) || fabs(positions.At(index).value[0]) > FLT_MAX || fabs(positions.At(index).value[1]) > FLT_MAX || fabs(positions.At(index).value[2]) > FLT_MAX || !isfinite(velocities.At(index).x) || !isfinite(velocities.At(index).y) || !isfinite(velocities.At(index).z))
        {
            error.Assign("secondary contact velocity point input is not finite");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    for (uint32_t index = 0; index < references.Count(); ++index)
    {
        const FSecondaryMotionReferenceSegment& current = references.At(index);
        const FSecondaryMotionReferenceSegment& previous = previousReferences.At(index);
        if (!IsFinite(current.direction) || !IsFinite(previous.direction) || fabs(Length(current.direction) - 1.0) > 1.0e-8 || fabs(Length(previous.direction) - 1.0) > 1.0e-8 || !isfinite(current.length) || !isfinite(previous.length) || !(current.length > 0.0) || !(previous.length > 0.0))
        {
            error.Assign("secondary contact velocity reference segment is invalid");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    for (uint32_t index = 0; index < anchorMotionVelocities.Count(); ++index)
    {
        if (!IsFinite(anchorMotionVelocities.At(index)))
        {
            error.Assign("secondary contact velocity anchor motion is invalid");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    for (uint32_t index = 0; index < attachmentMotionVelocities.Count(); ++index)
    {
        if (!IsFinite(attachmentMotionVelocities.At(index)))
        {
            error.Assign("secondary contact velocity attachment motion is invalid");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    if (shapeCount > 0u && !ValidateSecondaryMotionCollisionShapes(currentShapes, shapeCount, error))
    {
        return ESecondaryMotionContactVelocityResult::Failed;
    }
    if (previousShapes.Count() > 0u && !ValidateSecondaryMotionCollisionShapes(previousShapes.Data(), previousShapes.Count(), error))
    {
        return ESecondaryMotionContactVelocityResult::Failed;
    }
    if (positions.Count() > kMaximumVelocityPoints)
    {
        return ESecondaryMotionContactVelocityResult::Unresolved;
    }

    Array<FVector3d> coefficients;
    Array<uint8_t> rootPoints;
    Array<FVelocityRow> originalRows;
    Array<FVelocityEntry> originalEntries;
    Array<FVelocityRow> solveRows;
    Array<FVelocityEntry> solveEntries;
    Array<FVector3d> baselineVelocities;
    Array<FVector3d> workingDegreesOfFreedom;
    Array<uint32_t> pointChainIndices;
    Array<Vec3> candidateVelocities;
    if (!coefficients.Reserve(positions.Count()) || !rootPoints.Reserve(positions.Count()) || !baselineVelocities.Reserve(positions.Count()) || !workingDegreesOfFreedom.Reserve(positions.Count()) || !pointChainIndices.Reserve(positions.Count()) || !candidateVelocities.AppendRange(velocities.Data(), velocities.Count()))
    {
        error.Assign("secondary contact velocity scratch allocation failed");
        return ESecondaryMotionContactVelocityResult::Failed;
    }
    for (uint32_t pointIndex = 0; pointIndex < positions.Count(); ++pointIndex)
    {
        if (!coefficients.Append({}) || !rootPoints.Append(0u) || !baselineVelocities.Append({}) || !workingDegreesOfFreedom.Append({}) || !pointChainIndices.Append(UINT32_MAX))
        {
            error.Assign("secondary contact velocity scratch allocation failed");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains.At(chainIndex);
        rootPoints.At(chain.firstPoint) = 1u;
        pointChainIndices.At(chain.firstPoint) = chainIndex;
        const Vec3& rootVelocity = velocities.At(chain.firstPoint);
        baselineVelocities.At(chain.firstPoint) = { { rootVelocity.x, rootVelocity.y, rootVelocity.z } };
        for (uint32_t segmentIndex = 0; segmentIndex < chain.segmentCount; ++segmentIndex)
        {
            const uint32_t parentIndex = chain.firstPoint + segmentIndex;
            const uint32_t childIndex = parentIndex + 1u;
            const uint32_t referenceIndex = chain.firstSegment + segmentIndex;
            const FVector3d currentSegment = Subtract(positions.At(childIndex), positions.At(parentIndex));
            const double currentLength = Length(currentSegment);
            if (!(currentLength > 0.0) || !isfinite(currentLength))
            {
                error.Assign("secondary contact velocity baseline segment is invalid");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            const FVector3d direction = Multiply(currentSegment, 1.0 / currentLength);
            const FSecondaryMotionReferenceSegment& currentReference = references.At(referenceIndex);
            const FSecondaryMotionReferenceSegment& previousReference = previousReferences.At(referenceIndex);
            FVector3d relativeVelocity{};
            if (chain.cosineLimit == 1.0)
            {
                const FVector3d currentAnimatedSegment = Multiply(currentReference.direction, currentReference.length);
                const FVector3d previousAnimatedSegment = Multiply(previousReference.direction, previousReference.length);
                relativeVelocity = Multiply(Subtract(currentAnimatedSegment, previousAnimatedSegment), 1.0 / delta);
            }
            else
            {
                const Vec3& parentVelocity = velocities.At(parentIndex);
                const Vec3& childVelocity = velocities.At(childIndex);
                relativeVelocity = { { static_cast<double>(childVelocity.x) - parentVelocity.x, static_cast<double>(childVelocity.y) - parentVelocity.y, static_cast<double>(childVelocity.z) - parentVelocity.z } };
                const double lengthRate = (currentReference.length - previousReference.length) / delta;
                const double radialRate = Dot(relativeVelocity, direction);
                relativeVelocity = { { relativeVelocity.value[0] + direction.value[0] * (lengthRate - radialRate), relativeVelocity.value[1] + direction.value[1] * (lengthRate - radialRate), relativeVelocity.value[2] + direction.value[2] * (lengthRate - radialRate) } };
            }
            const FVector3d& parentBaseline = baselineVelocities.At(parentIndex);
            baselineVelocities.At(childIndex) = { { parentBaseline.value[0] + relativeVelocity.value[0], parentBaseline.value[1] + relativeVelocity.value[1], parentBaseline.value[2] + relativeVelocity.value[2] } };
            pointChainIndices.At(childIndex) = chainIndex;
            if (!IsFinite(baselineVelocities.At(childIndex)) || fabs(baselineVelocities.At(childIndex).value[0]) > FLT_MAX || fabs(baselineVelocities.At(childIndex).value[1]) > FLT_MAX || fabs(baselineVelocities.At(childIndex).value[2]) > FLT_MAX)
            {
                error.Assign("secondary contact velocity baseline is outside the numeric range");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
        }
    }
    bool budgetExceeded = false;
    bool finiteImpossible = false;
    double fixedVelocity = 0.0;

    // 鎖長とアニメーション基準の曲げ制約を疎な速度行へ変換する。
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains.At(chainIndex);
        for (uint32_t segmentIndex = 0; segmentIndex < chain.segmentCount; ++segmentIndex)
        {
            const uint32_t parentIndex = chain.firstPoint + segmentIndex;
            const uint32_t childIndex = parentIndex + 1u;
            const uint32_t referenceIndex = chain.firstSegment + segmentIndex;
            const FVector3d currentSegment = Subtract(positions.At(childIndex), positions.At(parentIndex));
            const double currentLength = Length(currentSegment);
            if (!(currentLength > 0.0) || !isfinite(currentLength))
            {
                error.Assign("secondary contact velocity current segment is invalid");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            const FVector3d direction = Multiply(currentSegment, 1.0 / currentLength);
            const FSecondaryMotionReferenceSegment& currentReference = references.At(referenceIndex);
            const FSecondaryMotionReferenceSegment& previousReference = previousReferences.At(referenceIndex);
            const FVector3d currentAnimatedSegment = Multiply(currentReference.direction, currentReference.length);
            const FVector3d previousAnimatedSegment = Multiply(previousReference.direction, previousReference.length);

            const bool zeroAngle = chain.cosineLimit == 1.0;
            if (!zeroAngle)
            {
                ResetCoefficients(coefficients);
                fixedVelocity = -(currentReference.length - previousReference.length) / delta;
                if (!AddPointCoefficient(rootPoints, velocities, coefficients, parentIndex, Multiply(direction, -1.0), fixedVelocity) || !AddPointCoefficient(rootPoints, velocities, coefficients, childIndex, direction, fixedVelocity) || !AppendRow(coefficients, fixedVelocity, true, originalRows, originalEntries, budgetExceeded, finiteImpossible, error))
                {
                    return budgetExceeded ? ESecondaryMotionContactVelocityResult::Unresolved : ESecondaryMotionContactVelocityResult::Failed;
                }
                originalRows.At(originalRows.Count() - 1u).structural = true;
            }
            if (zeroAngle)
            {
                const FVector3d targetVelocity = Multiply(Subtract(currentAnimatedSegment, previousAnimatedSegment), 1.0 / delta);
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    ResetCoefficients(coefficients);
                    FVector3d axisDirection{};
                    axisDirection.value[axis] = 1.0;
                    fixedVelocity = -targetVelocity.value[axis];
                    if (!AddPointCoefficient(rootPoints, velocities, coefficients, parentIndex, Multiply(axisDirection, -1.0), fixedVelocity) || !AddPointCoefficient(rootPoints, velocities, coefficients, childIndex, axisDirection, fixedVelocity) || !AppendRow(coefficients, fixedVelocity, true, originalRows, originalEntries, budgetExceeded, finiteImpossible, error))
                    {
                        return budgetExceeded ? ESecondaryMotionContactVelocityResult::Unresolved : ESecondaryMotionContactVelocityResult::Failed;
                    }
                    originalRows.At(originalRows.Count() - 1u).structural = true;
                }
            }
            else if (chain.cosineLimit > -1.0)
            {
                FVector3d referenceDirection = currentReference.direction;
                const double cosine = Dot(direction, referenceDirection);
                if (cosine <= chain.cosineLimit + 128.0 * FLT_EPSILON)
                {
                    const FVector3d tangent = Subtract(referenceDirection, Multiply(direction, cosine));
                    FVector3d referenceChange = Subtract(currentAnimatedSegment, previousAnimatedSegment);
                    const double radialRate = Dot(referenceChange, referenceDirection);
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        referenceChange.value[axis] = (referenceChange.value[axis] - referenceDirection.value[axis] * radialRate) / (currentReference.length * delta);
                    }
                    ResetCoefficients(coefficients);
                    fixedVelocity = Dot(direction, referenceChange);
                    if (!AddPointCoefficient(rootPoints, velocities, coefficients, parentIndex, Multiply(tangent, -1.0 / currentLength), fixedVelocity) || !AddPointCoefficient(rootPoints, velocities, coefficients, childIndex, Multiply(tangent, 1.0 / currentLength), fixedVelocity) || !AppendRow(coefficients, fixedVelocity, false, originalRows, originalEntries, budgetExceeded, finiteImpossible, error))
                    {
                        return budgetExceeded ? ESecondaryMotionContactVelocityResult::Unresolved : ESecondaryMotionContactVelocityResult::Failed;
                    }
                }
            }
        }
    }
    if (budgetExceeded || finiteImpossible)
    {
        error.Clear();
        return ESecondaryMotionContactVelocityResult::Unresolved;
    }

    // 鎖の身体形状へ節球と自由線分の接触速度行を作る。
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains.At(chainIndex);
        uint32_t localShapeIndex = 0;
        for (uint32_t sourceShapeIndex = 0; sourceShapeIndex < shapeCount; ++sourceShapeIndex)
        {
            if ((colliderMasks.At(chainIndex) & (static_cast<uint64_t>(1) << sourceShapeIndex)) == 0u)
            {
                continue;
            }
            if (localShapeIndex >= chain.shapeCount)
            {
                error.Assign("secondary contact velocity masked shape range is invalid");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            const FPreparedSecondaryMotionCollisionShape& shape = chain.shapes[localShapeIndex++];
            const FModelSecondaryMotionCollisionShape* oldShape = previousShapes.Count() == shapeCount ? &previousShapes.At(sourceShapeIndex) : nullptr;
            for (uint32_t segmentIndex = 0; segmentIndex < chain.segmentCount; ++segmentIndex)
            {
                const uint32_t parentIndex = chain.firstPoint + segmentIndex;
                const uint32_t childIndex = parentIndex + 1u;
                double penetration = 0.0;
                double spanAmount = 0.0;
                FVector3d normal{};
                if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, positions.At(childIndex), positions.At(childIndex), penetration, spanAmount, normal, error, 0.0))
                {
                    return ESecondaryMotionContactVelocityResult::Failed;
                }
                if (IsVelocityActive(shape, positions.At(childIndex), penetration))
                {
                    FVector3d surfaceVelocity{};
                    if (!EvaluateSurfaceVelocity(currentShapes[sourceShapeIndex], oldShape, delta, positions.At(childIndex), normal, surfaceVelocity))
                    {
                        error.Assign("secondary contact velocity body surface rate is invalid");
                        return ESecondaryMotionContactVelocityResult::Failed;
                    }
                    ResetCoefficients(coefficients);
                    fixedVelocity = -Dot(normal, surfaceVelocity);
                    if (!AddPointCoefficient(rootPoints, velocities, coefficients, childIndex, normal, fixedVelocity) || !AppendRow(coefficients, fixedVelocity, false, originalRows, originalEntries, budgetExceeded, finiteImpossible, error))
                    {
                        return budgetExceeded ? ESecondaryMotionContactVelocityResult::Unresolved : ESecondaryMotionContactVelocityResult::Failed;
                    }
                }
                bool skipRootSpan = false;
                double rootSpanThreshold = shape.radius;
                if (segmentIndex == 0u)
                {
                    if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, positions.At(parentIndex), positions.At(parentIndex), penetration, spanAmount, normal, error, 0.0))
                    {
                        return ESecondaryMotionContactVelocityResult::Failed;
                    }
                    const double rootDistance = shape.radius - penetration;
                    skipRootSpan = shape.hasRootContactOverride ? penetration > shape.radius - GetPreparedSecondaryMotionRootContactThreshold(shape) : penetration > shape.tolerance;
                    if (shape.hasRootContactOverride)
                    {
                        rootSpanThreshold = GetPreparedSecondaryMotionRootSpanThreshold(shape, rootDistance, 0.0);
                    }
                }
                if (!skipRootSpan)
                {
                    if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, positions.At(parentIndex), positions.At(childIndex), penetration, spanAmount, normal, error, 0.0))
                    {
                        return ESecondaryMotionContactVelocityResult::Failed;
                    }
                    const FVector3d midpoint = { { positions.At(parentIndex).value[0] * (1.0 - spanAmount) + positions.At(childIndex).value[0] * spanAmount, positions.At(parentIndex).value[1] * (1.0 - spanAmount) + positions.At(childIndex).value[1] * spanAmount, positions.At(parentIndex).value[2] * (1.0 - spanAmount) + positions.At(childIndex).value[2] * spanAmount } };
                    double activeRadius = shape.radius;
                    if (segmentIndex == 0u && shape.hasRootContactOverride)
                    {
                        activeRadius = rootSpanThreshold;
                    }
                    const double distance = shape.radius - penetration;
                    double coordinateMagnitude = activeRadius;
                    for (uint32_t axis = 0; axis < 3u; ++axis)
                    {
                        coordinateMagnitude = fmax(coordinateMagnitude, fabs(midpoint.value[axis]));
                        coordinateMagnitude = fmax(coordinateMagnitude, fabs(shape.start.value[axis]));
                        coordinateMagnitude = fmax(coordinateMagnitude, fabs(shape.end.value[axis]));
                    }
                    if (isfinite(distance) && distance <= activeRadius + coordinateMagnitude * kActiveMarginScale)
                    {
                        FVector3d surfaceVelocity{};
                        if (!EvaluateSurfaceVelocity(currentShapes[sourceShapeIndex], oldShape, delta, midpoint, normal, surfaceVelocity))
                        {
                            error.Assign("secondary contact velocity body surface rate is invalid");
                            return ESecondaryMotionContactVelocityResult::Failed;
                        }
                        ResetCoefficients(coefficients);
                        fixedVelocity = -Dot(normal, surfaceVelocity);
                        if (!AddSpanCoefficients(rootPoints, velocities, coefficients, parentIndex, childIndex, normal, spanAmount, 1.0, fixedVelocity) || !AppendRow(coefficients, fixedVelocity, false, originalRows, originalEntries, budgetExceeded, finiteImpossible, error))
                        {
                            return budgetExceeded ? ESecondaryMotionContactVelocityResult::Unresolved : ESecondaryMotionContactVelocityResult::Failed;
                        }
                    }
                }
            }
        }
        if (localShapeIndex != chain.shapeCount)
        {
            error.Assign("secondary contact velocity masked shape count is invalid");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    if (budgetExceeded || finiteImpossible)
    {
        error.Clear();
        return ESecondaryMotionContactVelocityResult::Unresolved;
    }

    // weighted contact pointへ接触法線とskin weightの速度行を作る。
    for (uint32_t contactIndex = 0; contactIndex < contacts.Count(); ++contactIndex)
    {
        const FSecondaryMotionWeightedContactPoint& contact = contacts.At(contactIndex);
        FVector3d weightedPoint{};
        if (!EvaluateWeightedPoint(chains, references, anchors, contact.firstAnchor, contact.anchorCount, positions, weightedPoint, error))
        {
            return ESecondaryMotionContactVelocityResult::Failed;
        }
        for (uint32_t shapeIndex = 0; shapeIndex < contact.shapeCount; ++shapeIndex)
        {
            const FPreparedSecondaryMotionCollisionShape& shape = contact.shapes[shapeIndex];
            double penetration = 0.0;
            double spanAmount = 0.0;
            FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, weightedPoint, weightedPoint, penetration, spanAmount, normal, error, 0.0))
            {
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            if (!IsVelocityActive(shape, weightedPoint, penetration))
            {
                continue;
            }
            FVector3d surfaceVelocity{};
            const FModelSecondaryMotionCollisionShape* oldShape = previousShapes.Count() == shapeCount ? &previousShapes.At(shapeIndex) : nullptr;
            if (!EvaluateSurfaceVelocity(currentShapes[shapeIndex], oldShape, delta, weightedPoint, normal, surfaceVelocity))
            {
                error.Assign("secondary contact velocity weighted surface rate is invalid");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            ResetCoefficients(coefficients);
            fixedVelocity = -Dot(normal, surfaceVelocity);
            if (!AddWeightedAnchorCoefficients(chains, rootPoints, anchors, anchorMotionVelocities, velocities, positions, contact.firstAnchor, contact.anchorCount, normal, coefficients, fixedVelocity, error) || !AppendRow(coefficients, fixedVelocity, false, originalRows, originalEntries, budgetExceeded, finiteImpossible, error))
            {
                return budgetExceeded ? ESecondaryMotionContactVelocityResult::Unresolved : ESecondaryMotionContactVelocityResult::Failed;
            }
        }
    }
    if (budgetExceeded || finiteImpossible)
    {
        error.Clear();
        return ESecondaryMotionContactVelocityResult::Unresolved;
    }

    // same-bone接触線分も端点attachment Jacobianとspan重みで速度行へ加える。
    uint32_t attachmentMotionOffset = 0;
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains.At(chainIndex);
        for (uint32_t attachmentIndex = 0; attachmentIndex < chain.attachmentCount; ++attachmentIndex)
        {
            const FSecondaryMotionChainAttachmentContact& attachment = chain.attachments[attachmentIndex];
            if (attachment.segmentIndex >= chain.segmentCount || attachment.shapeCount != shapeCount || (attachment.shapeCount > 0u && !attachment.shapes))
            {
                error.Assign("secondary contact velocity attachment shape range is invalid");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            const uint32_t parentIndex = chain.firstPoint + attachment.segmentIndex;
            const uint32_t childIndex = parentIndex + 1u;
            FSecondaryMotionAttachmentEvaluation firstEvaluation{};
            FSecondaryMotionAttachmentEvaluation secondEvaluation{};
            if (!EvaluateSecondaryMotionAttachment(attachment.firstDefinition, positions.At(parentIndex), positions.At(childIndex), firstEvaluation, error) || !EvaluateSecondaryMotionAttachment(attachment.secondDefinition, positions.At(parentIndex), positions.At(childIndex), secondEvaluation, error))
            {
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            const FVector3d& firstMotion = attachmentMotionVelocities.At(attachmentMotionOffset + attachmentIndex * 2u);
            const FVector3d& secondMotion = attachmentMotionVelocities.At(attachmentMotionOffset + attachmentIndex * 2u + 1u);
            for (uint32_t shapeIndex = 0; shapeIndex < attachment.shapeCount; ++shapeIndex)
            {
                const FPreparedSecondaryMotionCollisionShape& shape = attachment.shapes[shapeIndex];
                double penetration = 0.0;
                double spanAmount = 0.0;
                FVector3d normal{};
                if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, firstEvaluation.position, secondEvaluation.position, penetration, spanAmount, normal, error, 0.0))
                {
                    return ESecondaryMotionContactVelocityResult::Failed;
                }
                const FVector3d contactPoint = { { firstEvaluation.position.value[0] * (1.0 - spanAmount) + secondEvaluation.position.value[0] * spanAmount, firstEvaluation.position.value[1] * (1.0 - spanAmount) + secondEvaluation.position.value[1] * spanAmount, firstEvaluation.position.value[2] * (1.0 - spanAmount) + secondEvaluation.position.value[2] * spanAmount } };
                if (!IsVelocityActive(shape, contactPoint, penetration))
                {
                    continue;
                }
                FVector3d surfaceVelocity{};
                const FModelSecondaryMotionCollisionShape* oldShape = previousShapes.Count() == shapeCount ? &previousShapes.At(shapeIndex) : nullptr;
                if (!EvaluateSurfaceVelocity(currentShapes[shapeIndex], oldShape, delta, contactPoint, normal, surfaceVelocity))
                {
                    error.Assign("secondary contact velocity attachment surface rate is invalid");
                    return ESecondaryMotionContactVelocityResult::Failed;
                }
                ResetCoefficients(coefficients);
                fixedVelocity = -Dot(normal, surfaceVelocity) + (1.0 - spanAmount) * Dot(normal, firstMotion) + spanAmount * Dot(normal, secondMotion);
                const FVector3d endpointWeights[2] = { Multiply(normal, 1.0 - spanAmount), Multiply(normal, spanAmount) };
                const FSecondaryMotionAttachmentEvaluation* evaluations[2] = { &firstEvaluation, &secondEvaluation };
                for (uint32_t endpoint = 0; endpoint < 2u; ++endpoint)
                {
                    FVector3d childCoefficient{};
                    for (uint32_t inputAxis = 0; inputAxis < 3u; ++inputAxis)
                    {
                        for (uint32_t outputAxis = 0; outputAxis < 3u; ++outputAxis)
                        {
                            childCoefficient.value[inputAxis] += evaluations[endpoint]->childJacobian[outputAxis * 3u + inputAxis] * endpointWeights[endpoint].value[outputAxis];
                        }
                    }
                    const FVector3d parentCoefficient = Subtract(endpointWeights[endpoint], childCoefficient);
                    if (!AddPointCoefficient(rootPoints, velocities, coefficients, parentIndex, parentCoefficient, fixedVelocity) || !AddPointCoefficient(rootPoints, velocities, coefficients, childIndex, childCoefficient, fixedVelocity))
                    {
                        error.Assign("secondary contact velocity attachment coefficient is invalid");
                        return ESecondaryMotionContactVelocityResult::Failed;
                    }
                }
                if (!AppendRow(coefficients, fixedVelocity, false, originalRows, originalEntries, budgetExceeded, finiteImpossible, error))
                {
                    return budgetExceeded ? ESecondaryMotionContactVelocityResult::Unresolved : ESecondaryMotionContactVelocityResult::Failed;
                }
            }
        }
        attachmentMotionOffset += chain.attachmentCount * 2u;
    }
    if (budgetExceeded || finiteImpossible)
    {
        error.Clear();
        return ESecondaryMotionContactVelocityResult::Unresolved;
    }

    // point速度行を、鎖の各節に割り当てた接線自由度の行へ変換する。
    for (uint32_t rowIndex = 0; rowIndex < originalRows.Count(); ++rowIndex)
    {
        const FVelocityRow& originalRow = originalRows.At(rowIndex);
        if (originalRow.structural)
        {
            continue;
        }
        ResetCoefficients(coefficients);
        fixedVelocity = originalRow.fixedVelocity;
        for (uint32_t localEntry = 0; localEntry < originalRow.entryCount; ++localEntry)
        {
            const FVelocityEntry& originalEntry = originalEntries.At(originalRow.firstEntry + localEntry);
            const uint32_t pointIndex = originalEntry.pointIndex;
            const uint32_t chainIndex = pointChainIndices.At(pointIndex);
            if (chainIndex >= chains.Count() || IsRootPoint(rootPoints, pointIndex))
            {
                error.Assign("secondary contact velocity point-to-chain mapping is invalid");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            fixedVelocity += Dot(originalEntry.coefficient, baselineVelocities.At(pointIndex));
            if (!isfinite(fixedVelocity))
            {
                error.Assign("secondary contact velocity baseline row is invalid");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            const FSecondaryMotionContactChain& chain = chains.At(chainIndex);
            if (chain.cosineLimit == 1.0)
            {
                continue;
            }
            const uint32_t localPoint = pointIndex - chain.firstPoint;
            for (uint32_t segmentIndex = 1u; segmentIndex <= localPoint; ++segmentIndex)
            {
                const uint32_t parentIndex = chain.firstPoint + segmentIndex - 1u;
                const uint32_t childIndex = parentIndex + 1u;
                FVector3d segmentDirection = Subtract(positions.At(childIndex), positions.At(parentIndex));
                const double segmentLength = Length(segmentDirection);
                if (!(segmentLength > 0.0) || !isfinite(segmentLength))
                {
                    error.Assign("secondary contact velocity tangent segment is invalid");
                    return ESecondaryMotionContactVelocityResult::Failed;
                }
                segmentDirection = Multiply(segmentDirection, 1.0 / segmentLength);
                const FVector3d tangentCoefficient = Subtract(originalEntry.coefficient, Multiply(segmentDirection, Dot(originalEntry.coefficient, segmentDirection)));
                FVector3d& dofCoefficient = coefficients.At(childIndex);
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    dofCoefficient.value[axis] += tangentCoefficient.value[axis];
                }
                if (!IsFinite(dofCoefficient))
                {
                    error.Assign("secondary contact velocity tangent coefficient is invalid");
                    return ESecondaryMotionContactVelocityResult::Failed;
                }
            }
        }
        if (!AppendRow(coefficients, fixedVelocity, originalRow.equality, solveRows, solveEntries, budgetExceeded, finiteImpossible, error))
        {
            return budgetExceeded ? ESecondaryMotionContactVelocityResult::Unresolved : ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    if (budgetExceeded || finiteImpossible)
    {
        error.Clear();
        return ESecondaryMotionContactVelocityResult::Unresolved;
    }

    // 32回を上限に接線自由度の等式と外向き不等式を同じ疎行列で投影する。
    for (uint32_t pass = 0; pass < kMaximumVelocityPasses; ++pass)
    {
        bool changed = false;
        for (uint32_t rowIndex = 0; rowIndex < solveRows.Count(); ++rowIndex)
        {
            const FVelocityRow& row = solveRows.At(rowIndex);
            const double residual = EvaluateRow(row, solveEntries, workingDegreesOfFreedom);
            if (!isfinite(residual))
            {
                error.Assign("secondary contact velocity solve is outside the numeric range");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            if ((row.equality && fabs(residual) <= 1.0e-12) || (!row.equality && residual >= 0.0))
            {
                continue;
            }
            double denominator = 0.0;
            for (uint32_t localEntry = 0; localEntry < row.entryCount; ++localEntry)
            {
                const FVelocityEntry& entry = solveEntries.At(row.firstEntry + localEntry);
                denominator += Dot(entry.coefficient, entry.coefficient);
            }
            if (!(denominator > 0.0) || !isfinite(denominator))
            {
                error.Clear();
                return ESecondaryMotionContactVelocityResult::Unresolved;
            }
            const double scale = -residual / denominator;
            if (!isfinite(scale))
            {
                error.Assign("secondary contact velocity correction is outside the numeric range");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            for (uint32_t localEntry = 0; localEntry < row.entryCount; ++localEntry)
            {
                const FVelocityEntry& entry = solveEntries.At(row.firstEntry + localEntry);
                FVector3d& velocity = workingDegreesOfFreedom.At(entry.pointIndex);
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    velocity.value[axis] += entry.coefficient.value[axis] * scale;
                }
                if (!IsFinite(velocity) || fabs(velocity.value[0]) > FLT_MAX || fabs(velocity.value[1]) > FLT_MAX || fabs(velocity.value[2]) > FLT_MAX)
                {
                    error.Assign("secondary contact velocity candidate exceeds float range");
                    return ESecondaryMotionContactVelocityResult::Failed;
                }
            }
            changed = true;
        }
        if (!changed)
        {
            break;
        }
    }

    // 接線自由度を鎖順に加えて、物理点速度へ戻す。
    Array<FVector3d> reconstructedVelocities;
    if (!reconstructedVelocities.Reserve(positions.Count()))
    {
        error.Assign("secondary contact velocity reconstruction allocation failed");
        return ESecondaryMotionContactVelocityResult::Failed;
    }
    for (uint32_t pointIndex = 0; pointIndex < positions.Count(); ++pointIndex)
    {
        if (!reconstructedVelocities.Append({}))
        {
            error.Assign("secondary contact velocity reconstruction allocation failed");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < chains.Count(); ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains.At(chainIndex);
        const uint32_t firstPoint = chain.firstPoint;
        const Vec3& rootVelocity = velocities.At(firstPoint);
        reconstructedVelocities.At(firstPoint) = { { rootVelocity.x, rootVelocity.y, rootVelocity.z } };
        for (uint32_t segmentIndex = 0; segmentIndex < chain.segmentCount; ++segmentIndex)
        {
            const uint32_t parentIndex = firstPoint + segmentIndex;
            const uint32_t childIndex = parentIndex + 1u;
            const FVector3d relativeBaseline = Subtract(baselineVelocities.At(childIndex), baselineVelocities.At(parentIndex));
            const FVector3d& degreeOfFreedom = workingDegreesOfFreedom.At(childIndex);
            reconstructedVelocities.At(childIndex) = { { reconstructedVelocities.At(parentIndex).value[0] + relativeBaseline.value[0] + degreeOfFreedom.value[0], reconstructedVelocities.At(parentIndex).value[1] + relativeBaseline.value[1] + degreeOfFreedom.value[1], reconstructedVelocities.At(parentIndex).value[2] + relativeBaseline.value[2] + degreeOfFreedom.value[2] } };
            const FVector3d& value = reconstructedVelocities.At(childIndex);
            if (!IsFinite(value) || fabs(value.value[0]) > FLT_MAX || fabs(value.value[1]) > FLT_MAX || fabs(value.value[2]) > FLT_MAX)
            {
                error.Assign("secondary contact velocity reconstruction exceeds float range");
                return ESecondaryMotionContactVelocityResult::Failed;
            }
            candidateVelocities.At(childIndex) = { static_cast<float>(value.value[0]), static_cast<float>(value.value[1]), static_cast<float>(value.value[2]) };
        }
    }
    for (uint32_t rowIndex = 0; rowIndex < originalRows.Count(); ++rowIndex)
    {
        const FVelocityRow& row = originalRows.At(rowIndex);
        const double residual = EvaluateStoredRow(row, originalEntries, candidateVelocities);
        if (!isfinite(residual))
        {
            error.Assign("secondary contact velocity stored validation is outside the numeric range");
            return ESecondaryMotionContactVelocityResult::Failed;
        }
        if ((row.equality && fabs(residual) > kVelocityTolerance) || (!row.equality && residual < -kVelocityTolerance))
        {
            error.Clear();
            return ESecondaryMotionContactVelocityResult::Unresolved;
        }
    }
    velocities.MoveFrom(candidateVelocities);
    return ESecondaryMotionContactVelocityResult::Applied;
}

}
