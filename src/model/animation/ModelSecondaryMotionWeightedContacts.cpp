// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionWeightedContacts.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionChainContacts.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionDirection.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

// 一回の方向変更を縮小する回数。
const uint32_t kWeightedContactLineSearchSteps = 14;
// 初期方向が停滞した場合に試す共通傾斜の方位数。
const uint32_t kWeightedContactSeedAzimuths = 16;
// 接触許容幅を既存候補判定と合わせる割合。
const double kWeightedContactToleranceFraction = 0.5;
// 角度境界でfloat保存後の丸めを吸収する幅。
const double kWeightedContactConeBoundary = 64.0 * FLT_EPSILON;

/**
 * 座標が有限であることを確認する。
 */
bool IsFinite(const gk::FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * 座標が有限なfloat位置として保存できるか確認する。
 */
bool IsFloatPosition(const gk::FVector3d& value)
{
    return IsFinite(value) && fabs(value.value[0]) <= FLT_MAX && fabs(value.value[1]) <= FLT_MAX && fabs(value.value[2]) <= FLT_MAX;
}

/**
 * 三次元ベクトルの内積を計算する。
 */
double Dot(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return first.value[0] * second.value[0] + first.value[1] * second.value[1] + first.value[2] * second.value[2];
}

/**
 * 三次元ベクトルを加算する。
 */
gk::FVector3d Add(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return { { first.value[0] + second.value[0], first.value[1] + second.value[1], first.value[2] + second.value[2] } };
}

/**
 * 三次元ベクトルを減算する。
 */
gk::FVector3d Subtract(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return { { first.value[0] - second.value[0], first.value[1] - second.value[1], first.value[2] - second.value[2] } };
}

/**
 * 三次元ベクトルへ係数を掛ける。
 */
gk::FVector3d Multiply(const gk::FVector3d& value, double scale)
{
    return { { value.value[0] * scale, value.value[1] * scale, value.value[2] * scale } };
}

/**
 * 桁あふれを避けて三次元ベクトルの長さを求める。
 */
double Length(const gk::FVector3d& value)
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
 * 三次元ベクトルを有限な単位方向へ変換する。
 */
bool Normalize(gk::FVector3d& value)
{
    const double length = Length(value);
    if (!(length > 0.0) || !isfinite(length))
    {
        return false;
    }
    value = Multiply(value, 1.0 / length);
    return IsFinite(value);
}

/**
 * 反平行な親子方向で最小回転が定義できない候補か調べる。
 */
bool IsSingularAttachmentCandidate(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child)
{
    gk::FVector3d reference = definition.direction;
    gk::FVector3d direction = Subtract(child, parent);
    return Normalize(reference) && Normalize(direction) && 1.0 + Dot(reference, direction) <= 1.0e-12;
}

/**
 * 基準方向から決定的な直交接線を一つ作る。
 */
bool BuildPerpendicular(const gk::FVector3d& direction, gk::FVector3d& tangent)
{
    uint32_t axis = 0;
    if (fabs(direction.value[1]) < fabs(direction.value[axis]))
    {
        axis = 1;
    }
    if (fabs(direction.value[2]) < fabs(direction.value[axis]))
    {
        axis = 2;
    }
    gk::FVector3d basis{};
    basis.value[axis] = 1.0;
    tangent = { { direction.value[1] * basis.value[2] - direction.value[2] * basis.value[1], direction.value[2] * basis.value[0] - direction.value[0] * basis.value[2], direction.value[0] * basis.value[1] - direction.value[1] * basis.value[0] } };
    return Normalize(tangent);
}

/**
 * 勾配を単位球面と方向許容coneの接線へ射影し、大きさを保つ。
 */
bool ProjectGradient(const gk::FVector3d& reference, double cosineLimit, const gk::FVector3d& direction, bool preserveMagnitude, gk::FVector3d& gradient)
{
    gradient = Subtract(gradient, Multiply(direction, Dot(gradient, direction)));
    if (Dot(reference, direction) <= cosineLimit + kWeightedContactConeBoundary)
    {
        const gk::FVector3d coneNormal = Subtract(reference, Multiply(direction, Dot(reference, direction)));
        const double normalSquared = Dot(coneNormal, coneNormal);
        if (normalSquared > DBL_EPSILON * 64.0)
        {
            const double inward = Dot(gradient, coneNormal);
            if (inward < 0.0)
            {
                gradient = Subtract(gradient, Multiply(coneNormal, inward / normalSquared));
            }
        }
    }
    if (!IsFinite(gradient))
    {
        return false;
    }
    if (!preserveMagnitude)
    {
        return Normalize(gradient);
    }
    // 射影後に残った接線勾配の大きさ。
    const double projectedLength = Length(gradient);
    return projectedLength > 0.0 && isfinite(projectedLength);
}

/**
 * 全鎖の方向列から、固定rootと基準骨長を保った位置を構成する。
 */
bool BuildPositions(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& roots, const gk::Array<gk::FVector3d>& directions, gk::Array<gk::FVector3d>& positions)
{
    positions.Clear();
    uint32_t directionOffset = 0;
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        gk::FVector3d parent = roots.At(chainIndex);
        if (!positions.Append(parent))
        {
            return false;
        }
        for (uint32_t segmentIndex = 0; segmentIndex < chain.segmentCount; ++segmentIndex)
        {
            const gk::FVector3d endpoint = Add(parent, Multiply(directions.At(directionOffset + segmentIndex), references[chain.firstSegment + segmentIndex].length));
            if (!IsFloatPosition(endpoint) || !positions.Append(endpoint))
            {
                return false;
            }
            parent = endpoint;
        }
        directionOffset += chain.segmentCount;
    }
    return true;
}

/**
 * 一つのweighted anchorの現在位置と骨方向微分を保持する。
 */
struct FWeightedAnchorEvaluation
{
    // 基準姿勢から回転したoffset位置、または固定位置。
    gk::FVector3d position{};
    // 動く寄与が属する鎖番号。固定寄与ではUINT32_MAX。
    uint32_t chainIndex = 0xFFFFFFFFu;
    // 動く寄与が属する鎖内segment番号。
    uint32_t segmentIndex = 0;
    // 子位置変化からanchor位置変化への微分。
    double childJacobian[9]{};
};

/**
 * 各鎖の身体接触と重み付き点接触を評価し、方向勾配を集める。
 */
bool EvaluateAll(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::FVector3d* positions, double& merit, gk::FVector3d* gradients, uint32_t gradientCount, gk::Array<FWeightedAnchorEvaluation>& anchorEvaluations, gk::String& error)
{
    merit = 0.0;
    if (gradientCount > 0 && !gradients)
    {
        error.Assign("Secondary motion weighted contact gradients are missing");
        return false;
    }
    for (uint32_t index = 0; index < gradientCount; ++index)
    {
        gradients[index] = {};
    }
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        double chainMerit = 0.0;
        if (!EvaluatePreparedSecondaryMotionChainContactsMerit(chain.shapes, chain.shapeCount, references + chain.firstSegment, chain.segmentCount, chain.cosineLimit, 1.0, chain.attachments, chain.attachmentCount, positions + chain.firstPoint, chain.segmentCount + 1u, chainMerit, gradients + chain.firstSegment, chain.segmentCount, error))
        {
            return false;
        }
        if (chainMerit == DBL_MAX)
        {
            merit = DBL_MAX;
            for (uint32_t index = 0; index < gradientCount; ++index)
            {
                gradients[index] = {};
            }
            return true;
        }
        merit += chainMerit;
    }
    if (!isfinite(merit))
    {
        error.Assign("Secondary motion weighted contact merit is outside the numeric range");
        return false;
    }
    anchorEvaluations.Clear();
    if (!anchorEvaluations.Reserve(anchorCount))
    {
        error.Assign("Secondary motion weighted anchor allocation failed");
        return false;
    }
    for (uint32_t anchorIndex = 0; anchorIndex < anchorCount; ++anchorIndex)
    {
        const FSecondaryMotionWeightedContactAnchor& anchor = anchors[anchorIndex];
        FWeightedAnchorEvaluation evaluation{};
        evaluation.chainIndex = anchor.chainIndex;
        evaluation.segmentIndex = anchor.segmentIndex;
        if (anchor.chainIndex == 0xFFFFFFFFu)
        {
            evaluation.position = anchor.constantPosition;
        }
        else
        {
            const FSecondaryMotionContactChain& chain = chains[anchor.chainIndex];
            const uint32_t pointIndex = chain.firstPoint + anchor.segmentIndex;
            if (IsSingularAttachmentCandidate(anchor.definition, positions[pointIndex], positions[pointIndex + 1u]))
            {
                merit = DBL_MAX;
                for (uint32_t index = 0; index < gradientCount; ++index)
                {
                    gradients[index] = {};
                }
                return true;
            }
            FSecondaryMotionAttachmentEvaluation attachment{};
            if (!EvaluateSecondaryMotionAttachment(anchor.definition, positions[pointIndex], positions[pointIndex + 1u], attachment, error))
            {
                return false;
            }
            evaluation.position = attachment.position;
            for (uint32_t matrixIndex = 0; matrixIndex < 9; ++matrixIndex)
            {
                evaluation.childJacobian[matrixIndex] = attachment.childJacobian[matrixIndex];
            }
        }
        if (!IsFloatPosition(evaluation.position) || !anchorEvaluations.Append(evaluation))
        {
            error.Assign("Secondary motion weighted anchor position is invalid");
            return false;
        }
    }
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        const FSecondaryMotionWeightedContactPoint& contact = contacts[contactIndex];
        gk::FVector3d center{};
        for (uint32_t localAnchor = 0; localAnchor < contact.anchorCount; ++localAnchor)
        {
            const FSecondaryMotionWeightedContactAnchor& anchor = anchors[contact.firstAnchor + localAnchor];
            center = Add(center, Multiply(anchorEvaluations.At(contact.firstAnchor + localAnchor).position, anchor.weight));
        }
        if (!IsFloatPosition(center))
        {
            error.Assign("Secondary motion weighted contact center is outside the float range");
            return false;
        }
        for (uint32_t shapeIndex = 0; shapeIndex < contact.shapeCount; ++shapeIndex)
        {
            double penetration = 0.0;
            double spanAmount = 0.0;
            gk::FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(contact.shapes[shapeIndex], center, center, penetration, spanAmount, normal, error, kWeightedContactToleranceFraction))
            {
                return false;
            }
            if (!(penetration > 0.0))
            {
                continue;
            }
            merit += penetration * penetration;
            if (!isfinite(merit))
            {
                error.Assign("Secondary motion weighted contact merit is outside the numeric range");
                return false;
            }
            for (uint32_t localAnchor = 0; localAnchor < contact.anchorCount; ++localAnchor)
            {
                const uint32_t anchorIndex = contact.firstAnchor + localAnchor;
                const FSecondaryMotionWeightedContactAnchor& anchor = anchors[anchorIndex];
                if (anchor.chainIndex == 0xFFFFFFFFu)
                {
                    continue;
                }
                const FSecondaryMotionContactChain& chain = chains[anchor.chainIndex];
                const uint32_t globalSegment = chain.firstSegment + anchor.segmentIndex;
                const double weightedPenetration = anchor.weight * penetration;
                for (uint32_t ancestor = 0; ancestor < anchor.segmentIndex; ++ancestor)
                {
                    const double scale = references[chain.firstSegment + ancestor].length * weightedPenetration;
                    gradients[chain.firstSegment + ancestor] = Add(gradients[chain.firstSegment + ancestor], Multiply(normal, scale));
                }
                gk::FVector3d childGradient{};
                for (uint32_t childAxis = 0; childAxis < 3; ++childAxis)
                {
                    for (uint32_t pointAxis = 0; pointAxis < 3; ++pointAxis)
                    {
                        childGradient.value[childAxis] += anchorEvaluations.At(anchorIndex).childJacobian[pointAxis * 3u + childAxis] * normal.value[pointAxis];
                    }
                }
                const double currentScale = references[globalSegment].length * weightedPenetration;
                gradients[globalSegment] = Add(gradients[globalSegment], Multiply(childGradient, currentScale));
            }
        }
    }
    for (uint32_t index = 0; index < gradientCount; ++index)
    {
        if (!IsFinite(gradients[index]))
        {
            error.Assign("Secondary motion weighted contact gradient is invalid");
            return false;
        }
    }
    return true;
}

/**
 * 現在方向に勾配を適用し、各鎖の角度制約を保つ候補を作る。
 */
bool MakeTrialDirections(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& current, const gk::Array<gk::FVector3d>& gradients, bool preserveMagnitude, double maximumGradientLength, double angle, gk::Array<gk::FVector3d>& trial)
{
    trial.Clear();
    uint32_t segmentOffset = 0;
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        for (uint32_t localSegment = 0; localSegment < chain.segmentCount; ++localSegment)
        {
            const uint32_t index = segmentOffset + localSegment;
            gk::FVector3d tangent = gradients.At(index);
            gk::FVector3d candidate = current.At(index);
            // この自由度に割り当てられた勾配の大きさ。
            const double tangentLength = Length(tangent);
            if (tangentLength > 0.0)
            {
                if (preserveMagnitude)
                {
                    // 最大勾配に対するこの自由度の移動比率。
                    const double relativeStep = tan(angle) * (tangentLength / maximumGradientLength);
                    // 各自由度で方向だけを取り出した接線勾配。
                    const gk::FVector3d tangentDirection = { { tangent.value[0] / tangentLength, tangent.value[1] / tangentLength, tangent.value[2] / tangentLength } };
                    candidate = Add(current.At(index), Multiply(tangentDirection, relativeStep));
                }
                else
                {
                    // 従来どおり、各自由度を同じ角度だけ動かす。
                    if (!Normalize(tangent))
                    {
                        return false;
                    }
                    candidate = Add(Multiply(current.At(index), cos(angle)), Multiply(tangent, sin(angle)));
                }
                if (!NormalizeAndLimitSecondaryMotionDirection(references[chain.firstSegment + localSegment].direction, chain.cosineLimit, candidate))
                {
                    return false;
                }
            }
            if (!trial.Append(candidate))
            {
                return false;
            }
        }
        segmentOffset += chain.segmentCount;
    }
    return true;
}

/**
 * 初期または試行方向から全鎖位置を作り、全接触を評価する。
 */
bool EvaluateDirections(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& roots, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& directions, gk::Array<gk::FVector3d>& positions, double& merit, gk::Array<gk::FVector3d>& gradients, gk::Array<FWeightedAnchorEvaluation>& anchorEvaluations, gk::String& error)
{
    uint32_t expectedGradientCount = 0;
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        if (chains[chainIndex].segmentCount > UINT32_MAX - expectedGradientCount)
        {
            error.Assign("Secondary motion weighted direction range overflowed");
            return false;
        }
        expectedGradientCount += chains[chainIndex].segmentCount;
    }
    if (directions.Count() != expectedGradientCount)
    {
        error.Assign("Secondary motion weighted direction count is invalid");
        return false;
    }
    gradients.Clear();
    if (!gradients.Reserve(expectedGradientCount))
    {
        error.Assign("Secondary motion weighted gradient allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < expectedGradientCount; ++index)
    {
        if (!gradients.Append(gk::FVector3d{}))
        {
            error.Assign("Secondary motion weighted gradient allocation failed");
            return false;
        }
    }
    if (!BuildPositions(chains, chainCount, references, roots, directions, positions))
    {
        error.Assign("Secondary motion weighted candidate position is outside the float range");
        return false;
    }
    return EvaluateAll(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions.Data(), merit, gradients.Data(), gradients.Count(), anchorEvaluations, error);
}

/**
 * 有限反復と単調line searchで一つの方向seedを改善する。
 */
bool RefineSeed(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& roots, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, uint32_t maximumIterations, bool preserveMagnitude, bool useConjugate, gk::Array<gk::FVector3d>& directions, gk::Array<gk::FVector3d>& positions, gk::Array<gk::FVector3d>& gradients, double& merit, bool& clear, gk::Array<FWeightedAnchorEvaluation>& anchorEvaluations, gk::String& error)
{
    clear = merit == 0.0;
    if (clear)
    {
        return true;
    }
    gk::Array<gk::FVector3d> previousGradients;
    gk::Array<gk::FVector3d> previousSearchDirections;
    if (!previousGradients.Reserve(gradients.Count()) || !previousSearchDirections.Reserve(gradients.Count()))
    {
        error.Assign("Secondary motion weighted conjugate workspace allocation failed");
        return false;
    }
    bool hasPreviousDirection = false;
    for (uint32_t iteration = 0; iteration < maximumIterations; ++iteration)
    {
        gk::Array<gk::FVector3d> rawTangentGradients;
        gk::Array<gk::FVector3d> tangentGradients;
        if (!rawTangentGradients.Reserve(gradients.Count()) || !tangentGradients.Reserve(gradients.Count()))
        {
            error.Assign("Secondary motion weighted tangent allocation failed");
            return false;
        }
        bool hasTangent = false;
        double maximumGradientLength = 0.0;
        uint32_t segmentOffset = 0;
        for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
        {
            const FSecondaryMotionContactChain& chain = chains[chainIndex];
            for (uint32_t localSegment = 0; localSegment < chain.segmentCount; ++localSegment)
            {
                const uint32_t index = segmentOffset + localSegment;
                gk::FVector3d tangent = gradients.At(index);
                const bool hasProjectedTangent = ProjectGradient(references[chain.firstSegment + localSegment].direction, chain.cosineLimit, directions.At(index), true, tangent);
                const double tangentLength = Length(tangent);
                if (!IsFinite(tangent) || !isfinite(tangentLength))
                {
                    error.Assign("Secondary motion weighted contact gradient is invalid");
                    return false;
                }
                gk::FVector3d searchDirection = tangent;
                if (hasProjectedTangent)
                {
                    hasTangent = true;
                    maximumGradientLength = fmax(maximumGradientLength, tangentLength);
                    if (!preserveMagnitude && !Normalize(searchDirection))
                    {
                        error.Assign("Secondary motion weighted unit search direction is invalid");
                        return false;
                    }
                }
                if (!rawTangentGradients.Append(tangent) || !tangentGradients.Append(searchDirection))
                {
                    error.Assign("Secondary motion weighted tangent allocation failed");
                    return false;
                }
            }
            segmentOffset += chain.segmentCount;
        }
        if (!hasTangent)
        {
            return true;
        }
        if (useConjugate && hasPreviousDirection)
        {
            double numerator = 0.0;
            double denominator = 0.0;
            segmentOffset = 0;
            for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
            {
                const FSecondaryMotionContactChain& chain = chains[chainIndex];
                for (uint32_t localSegment = 0; localSegment < chain.segmentCount; ++localSegment)
                {
                    const uint32_t index = segmentOffset + localSegment;
                    gk::FVector3d previousGradient = previousGradients.At(index);
                    if (!ProjectGradient(references[chain.firstSegment + localSegment].direction, chain.cosineLimit, directions.At(index), true, previousGradient))
                    {
                        previousGradient = {};
                    }
                    const gk::FVector3d currentGradient = rawTangentGradients.At(index);
                    numerator += Dot(currentGradient, Subtract(currentGradient, previousGradient));
                    denominator += Dot(previousGradient, previousGradient);
                }
                segmentOffset += chain.segmentCount;
            }
            if (!isfinite(numerator) || !isfinite(denominator))
            {
                error.Assign("Secondary motion weighted conjugate coefficient is invalid");
                return false;
            }
            const double beta = denominator > 0.0 ? fmax(0.0, numerator / denominator) : 0.0;
            if (!isfinite(beta))
            {
                error.Assign("Secondary motion weighted conjugate coefficient is invalid");
                return false;
            }
            double descent = 0.0;
            segmentOffset = 0;
            for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
            {
                const FSecondaryMotionContactChain& chain = chains[chainIndex];
                for (uint32_t localSegment = 0; localSegment < chain.segmentCount; ++localSegment)
                {
                    const uint32_t index = segmentOffset + localSegment;
                    gk::FVector3d previousDirection = previousSearchDirections.At(index);
                    if (!ProjectGradient(references[chain.firstSegment + localSegment].direction, chain.cosineLimit, directions.At(index), true, previousDirection))
                    {
                        previousDirection = {};
                    }
                    gk::FVector3d searchDirection = Add(rawTangentGradients.At(index), Multiply(previousDirection, beta));
                    if (!ProjectGradient(references[chain.firstSegment + localSegment].direction, chain.cosineLimit, directions.At(index), true, searchDirection))
                    {
                        searchDirection = rawTangentGradients.At(index);
                    }
                    tangentGradients.At(index) = searchDirection;
                    descent += Dot(rawTangentGradients.At(index), searchDirection);
                }
                segmentOffset += chain.segmentCount;
            }
            if (!isfinite(descent))
            {
                error.Assign("Secondary motion weighted conjugate direction is invalid");
                return false;
            }
            if (!(descent > 0.0))
            {
                for (uint32_t index = 0; index < tangentGradients.Count(); ++index)
                {
                    tangentGradients.At(index) = rawTangentGradients.At(index);
                }
            }
        }
        maximumGradientLength = 0.0;
        for (uint32_t index = 0; index < tangentGradients.Count(); ++index)
        {
            const double tangentLength = Length(tangentGradients.At(index));
            if (!isfinite(tangentLength))
            {
                error.Assign("Secondary motion weighted conjugate direction length is invalid");
                return false;
            }
            maximumGradientLength = fmax(maximumGradientLength, tangentLength);
        }
        if (!(maximumGradientLength > 0.0))
        {
            return true;
        }
        double descent = 0.0;
        for (uint32_t index = 0; index < rawTangentGradients.Count(); ++index)
        {
            descent += Dot(rawTangentGradients.At(index), tangentGradients.At(index));
        }
        if (!isfinite(descent) || descent < 0.0)
        {
            error.Assign("Secondary motion weighted line-search descent is invalid");
            return false;
        }
        if (!(descent > 0.0))
        {
            return true;
        }
        const double maximumLineAngle = 0.35;
        const double minimumLineAngle = maximumLineAngle / 8192.0;
        const double estimatedAngle = 1.5 * atan(maximumGradientLength * merit / descent);
        if (!isfinite(estimatedAngle))
        {
            error.Assign("Secondary motion weighted line-search angle is invalid");
            return false;
        }
        double initialLineAngle = fmax(minimumLineAngle, fmin(maximumLineAngle, estimatedAngle));
        bool improved = false;
        for (uint32_t directionMode = 0; directionMode < (useConjugate ? 2u : 1u) && !improved; ++directionMode)
        {
            if (directionMode == 1u)
            {
                maximumGradientLength = 0.0;
                for (uint32_t index = 0; index < rawTangentGradients.Count(); ++index)
                {
                    tangentGradients.At(index) = rawTangentGradients.At(index);
                    maximumGradientLength = fmax(maximumGradientLength, Length(rawTangentGradients.At(index)));
                }
                if (!(maximumGradientLength > 0.0) || !isfinite(maximumGradientLength))
                {
                    break;
                }
                descent = 0.0;
                for (uint32_t index = 0; index < rawTangentGradients.Count(); ++index)
                {
                    descent += Dot(rawTangentGradients.At(index), tangentGradients.At(index));
                }
                if (!isfinite(descent) || descent < 0.0)
                {
                    error.Assign("Secondary motion weighted restart descent is invalid");
                    return false;
                }
                if (!(descent > 0.0))
                {
                    break;
                }
                const double restartAngle = 1.5 * atan(maximumGradientLength * merit / descent);
                if (!isfinite(restartAngle))
                {
                    error.Assign("Secondary motion weighted restart angle is invalid");
                    return false;
                }
                initialLineAngle = fmax(minimumLineAngle, fmin(maximumLineAngle, restartAngle));
            }
            double angle = initialLineAngle;
            for (uint32_t step = 0; step < kWeightedContactLineSearchSteps; ++step)
            {
                gk::Array<gk::FVector3d> trialDirections;
                gk::Array<gk::FVector3d> trialPositions;
                gk::Array<gk::FVector3d> trialGradients;
                gk::Array<FWeightedAnchorEvaluation> trialAnchors;
                if (!trialDirections.Reserve(directions.Count()) || !trialPositions.Reserve(positions.Count()) || !trialGradients.Reserve(gradients.Count()) || !trialAnchors.Reserve(anchorCount))
                {
                    error.Assign("Secondary motion weighted candidate allocation failed");
                    return false;
                }
                if (!MakeTrialDirections(chains, chainCount, references, directions, tangentGradients, preserveMagnitude, maximumGradientLength, angle, trialDirections))
                {
                    angle *= 0.5;
                    continue;
                }
                double trialMerit = 0.0;
                if (!EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, trialDirections, trialPositions, trialMerit, trialGradients, trialAnchors, error))
                {
                    if (!error.Empty())
                    {
                        return false;
                    }
                    angle *= 0.5;
                    continue;
                }
                if (trialMerit < merit)
                {
                    if (useConjugate)
                    {
                        previousGradients.Clear();
                        previousSearchDirections.Clear();
                        for (uint32_t index = 0; index < rawTangentGradients.Count(); ++index)
                        {
                            if (!previousGradients.Append(rawTangentGradients.At(index)) || !previousSearchDirections.Append(tangentGradients.At(index)))
                            {
                                error.Assign("Secondary motion weighted conjugate history allocation failed");
                                return false;
                            }
                        }
                        hasPreviousDirection = true;
                    }
                    directions.MoveFrom(trialDirections);
                    positions.MoveFrom(trialPositions);
                    gradients.MoveFrom(trialGradients);
                    anchorEvaluations.MoveFrom(trialAnchors);
                    merit = trialMerit;
                    clear = merit == 0.0;
                    improved = true;
                    break;
                }
                angle *= 0.5;
            }
        }
        if (!improved || clear)
        {
            return true;
        }
    }
    return true;
}

/**
 * 基準方向を共通の世界接線へ傾ける決定的seedを作る。
 */
bool MakeCoherentSeed(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& currentDirections, const bool* activeChains, const gk::FVector3d& worldTangent, double angleFraction, gk::Array<gk::FVector3d>& directions)
{
    directions.Clear();
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        const double maximumAngle = acos(chain.cosineLimit);
        const double angle = maximumAngle * angleFraction;
        for (uint32_t localSegment = 0; localSegment < chain.segmentCount; ++localSegment)
        {
            // 現在方向を保つ鎖では、seedによる姿勢変化を避ける。
            const uint32_t directionIndex = chain.firstSegment + localSegment;
            if (!activeChains[chainIndex])
            {
                if (!directions.Append(currentDirections.At(directionIndex)))
                {
                    return false;
                }
                continue;
            }
            const FSecondaryMotionReferenceSegment& referenceSegment = references[directionIndex];
            gk::FVector3d reference = referenceSegment.direction;
            if (!Normalize(reference))
            {
                return false;
            }
            gk::FVector3d tangent = Subtract(worldTangent, Multiply(reference, Dot(worldTangent, reference)));
            if (!Normalize(tangent) && !BuildPerpendicular(reference, tangent))
            {
                return false;
            }
            gk::FVector3d candidate = Add(Multiply(reference, cos(angle)), Multiply(tangent, sin(angle)));
            if (!NormalizeAndLimitSecondaryMotionDirection(referenceSegment.direction, chain.cosineLimit, candidate) || !directions.Append(candidate))
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * 初期の接触勾配を使い、侵入鎖だけを小さく動かすseedを作る。
 */
bool MakeTargetedGradientSeed(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& currentDirections, const gk::Array<gk::FVector3d>& gradients, const bool* targetChains, double angleFraction, gk::Array<gk::FVector3d>& directions)
{
    directions.Clear();
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        const double angle = acos(chain.cosineLimit) * angleFraction;
        for (uint32_t localSegment = 0; localSegment < chain.segmentCount; ++localSegment)
        {
            const uint32_t directionIndex = chain.firstSegment + localSegment;
            gk::FVector3d candidate = currentDirections.At(directionIndex);
            if (targetChains[chainIndex])
            {
                gk::FVector3d tangent = gradients.At(directionIndex);
                if (!IsFinite(tangent))
                {
                    return false;
                }
                if (ProjectGradient(references[directionIndex].direction, chain.cosineLimit, candidate, true, tangent))
                {
                    if (!Normalize(tangent))
                    {
                        return false;
                    }
                    candidate = Add(Multiply(candidate, cos(angle)), Multiply(tangent, sin(angle)));
                    if (!NormalizeAndLimitSecondaryMotionDirection(references[directionIndex].direction, chain.cosineLimit, candidate))
                    {
                        return false;
                    }
                }
            }
            if (!directions.Append(candidate))
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * 初期侵入に関わる鎖だけを追加seedの対象として記録する。
 */
bool BuildTargetContactChains(const FSecondaryMotionWeightedContactAnchor* anchors, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<FWeightedAnchorEvaluation>& anchorEvaluations, uint32_t chainCount, bool* targetChains, gk::String& error)
{
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        targetChains[chainIndex] = false;
    }
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        const FSecondaryMotionWeightedContactPoint& contact = contacts[contactIndex];
        gk::FVector3d center{};
        for (uint32_t localAnchor = 0; localAnchor < contact.anchorCount; ++localAnchor)
        {
            const uint32_t anchorIndex = contact.firstAnchor + localAnchor;
            center = Add(center, Multiply(anchorEvaluations.At(anchorIndex).position, anchors[anchorIndex].weight));
        }
        bool penetrates = false;
        for (uint32_t shapeIndex = 0; shapeIndex < contact.shapeCount; ++shapeIndex)
        {
            double penetration = 0.0;
            double spanAmount = 0.0;
            gk::FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(contact.shapes[shapeIndex], center, center, penetration, spanAmount, normal, error, kWeightedContactToleranceFraction))
            {
                return false;
            }
            penetrates = penetrates || penetration > 0.0;
        }
        if (!penetrates)
        {
            continue;
        }
        for (uint32_t localAnchor = 0; localAnchor < contact.anchorCount; ++localAnchor)
        {
            const uint32_t anchorIndex = contact.firstAnchor + localAnchor;
            const uint32_t chainIndex = anchors[anchorIndex].chainIndex;
            if (chainIndex != 0xFFFFFFFFu)
            {
                targetChains[chainIndex] = true;
            }
        }
    }
    return true;
}

/**
 * 接触鎖の配列範囲と基準姿勢の整合性を検証する。
 */
bool ValidateChains(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, uint32_t referenceCount, const gk::Array<gk::FVector3d>& positions, uint32_t& totalPoints, gk::String& error)
{
    totalPoints = 0;
    uint32_t expectedSegment = 0;
    uint32_t expectedPoint = 0;
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        if (chain.segmentCount == 0 || chain.firstPoint != expectedPoint || chain.firstSegment != expectedSegment || chain.shapeCount > 64 || chain.attachmentCount > 1024 || (chain.shapeCount > 0 && !chain.shapes) || (chain.attachmentCount > 0 && !chain.attachments) || !isfinite(chain.cosineLimit) || chain.cosineLimit < -1.0 || chain.cosineLimit > 1.0 || expectedSegment > referenceCount || chain.segmentCount > referenceCount - expectedSegment || chain.segmentCount > 16384u - expectedSegment || chain.segmentCount + 1u > UINT32_MAX - expectedPoint || expectedPoint > positions.Count() || chain.segmentCount + 1u > positions.Count() - expectedPoint)
        {
            error.Assign("Secondary motion weighted chain range is invalid");
            return false;
        }
        for (uint32_t localSegment = 0; localSegment < chain.segmentCount; ++localSegment)
        {
            const FSecondaryMotionReferenceSegment& reference = references[expectedSegment + localSegment];
            gk::FVector3d direction = reference.direction;
            if (!IsFloatPosition(reference.direction) || !isfinite(reference.length) || !(reference.length > 0.0) || reference.length > FLT_MAX || !Normalize(direction) || fabs(Length(reference.direction) - 1.0) > 1.0e-8)
            {
                error.Assign("Secondary motion weighted reference segment is invalid");
                return false;
            }
        }
        expectedSegment += chain.segmentCount;
        expectedPoint += chain.segmentCount + 1u;
    }
    if (expectedSegment != referenceCount || expectedPoint != positions.Count())
    {
        error.Assign("Secondary motion weighted chain arrays do not match");
        return false;
    }
    totalPoints = expectedPoint;
    return true;
}

}

bool ResolvePreparedSecondaryMotionWeightedContacts(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, uint32_t referenceCount, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, gk::Array<gk::FVector3d>& positions, gk::String& error, uint32_t maximumIterations)
{
    error.Clear();
    if (chainCount > 1024 || (chainCount > 0 && !chains) || referenceCount > 16384 || (referenceCount > 0 && !references) || anchorCount > 16384 || (anchorCount > 0 && !anchors) || contactCount > 1024 || (contactCount > 0 && !contacts) || maximumIterations == 0 || maximumIterations > 32)
    {
        error.Assign("Secondary motion weighted contact input is invalid");
        return false;
    }
    uint32_t totalPoints = 0;
    if (!ValidateChains(chains, chainCount, references, referenceCount, positions, totalPoints, error))
    {
        return false;
    }
    for (uint32_t index = 0; index < positions.Count(); ++index)
    {
        if (!IsFloatPosition(positions.At(index)))
        {
            error.Assign("Secondary motion weighted input position is invalid");
            return false;
        }
    }
    uint32_t expectedAnchor = 0;
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        const FSecondaryMotionWeightedContactPoint& contact = contacts[contactIndex];
        if (contact.firstAnchor != expectedAnchor || contact.anchorCount == 0 || contact.anchorCount > 64 || contact.anchorCount > anchorCount - expectedAnchor || contact.shapeCount > 64 || (contact.shapeCount > 0 && !contact.shapes))
        {
            error.Assign("Secondary motion weighted contact range is invalid");
            return false;
        }
        double weightSum = 0.0;
        for (uint32_t localAnchor = 0; localAnchor < contact.anchorCount; ++localAnchor)
        {
            const FSecondaryMotionWeightedContactAnchor& anchor = anchors[expectedAnchor + localAnchor];
            if (!isfinite(anchor.weight) || !(anchor.weight > 0.0))
            {
                error.Assign("Secondary motion weighted anchor weight is invalid");
                return false;
            }
            weightSum += anchor.weight;
            if (!isfinite(weightSum))
            {
                error.Assign("Secondary motion weighted anchor weights overflow");
                return false;
            }
            if (anchor.chainIndex == 0xFFFFFFFFu)
            {
                if (!IsFloatPosition(anchor.constantPosition))
                {
                    error.Assign("Secondary motion fixed contact anchor is invalid");
                    return false;
                }
            }
            else
            {
                if (anchor.chainIndex >= chainCount || anchor.segmentIndex >= chains[anchor.chainIndex].segmentCount)
                {
                    error.Assign("Secondary motion moving contact anchor range is invalid");
                    return false;
                }
                const FSecondaryMotionContactChain& chain = chains[anchor.chainIndex];
                if (!IsFloatPosition(anchor.definition.direction) || !IsFloatPosition(anchor.definition.offset) || !isfinite(anchor.definition.cosineLimit) || anchor.definition.cosineLimit < -1.0 || anchor.definition.cosineLimit > 1.0)
                {
                    error.Assign("Secondary motion moving contact anchor definition is invalid");
                    return false;
                }
                gk::FVector3d anchorDirection = anchor.definition.direction;
                gk::FVector3d referenceDirection = references[chain.firstSegment + anchor.segmentIndex].direction;
                if (!Normalize(anchorDirection) || !Normalize(referenceDirection) || Dot(anchorDirection, referenceDirection) < 1.0 - 1.0e-12 || fabs(anchor.definition.cosineLimit - chain.cosineLimit) > 1.0e-12)
                {
                    error.Assign("Secondary motion moving contact anchor does not match its chain");
                    return false;
                }
            }
        }
        if (!(weightSum > 0.0))
        {
            error.Assign("Secondary motion weighted contact has no positive weight");
            return false;
        }
        expectedAnchor += contact.anchorCount;
    }
    if (expectedAnchor != anchorCount)
    {
        error.Assign("Secondary motion weighted anchor ranges do not cover the input");
        return false;
    }
    if (chainCount == 0 && positions.Count() != 0)
    {
        error.Assign("Secondary motion fixed-only contact has unexpected positions");
        return false;
    }
    bool hasContactShapes = false;
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        hasContactShapes = hasContactShapes || chains[chainIndex].shapeCount > 0;
        for (uint32_t attachmentIndex = 0; attachmentIndex < chains[chainIndex].attachmentCount; ++attachmentIndex)
        {
            hasContactShapes = hasContactShapes || chains[chainIndex].attachments[attachmentIndex].shapeCount > 0;
        }
    }
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        hasContactShapes = hasContactShapes || contacts[contactIndex].shapeCount > 0;
    }
    if (!hasContactShapes)
    {
        return true;
    }
    gk::Array<gk::FVector3d> roots;
    gk::Array<gk::FVector3d> directions;
    gk::Array<gk::FVector3d> candidatePositions;
    gk::Array<gk::FVector3d> gradients;
    gk::Array<FWeightedAnchorEvaluation> anchorEvaluations;
    if (!roots.Reserve(chainCount) || !directions.Reserve(referenceCount) || !candidatePositions.Reserve(totalPoints) || !gradients.Reserve(referenceCount) || !anchorEvaluations.Reserve(anchorCount))
    {
        error.Assign("Secondary motion weighted contact workspace allocation failed");
        return false;
    }
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        if (!roots.Append(positions.At(chain.firstPoint)))
        {
            error.Assign("Secondary motion weighted root allocation failed");
            return false;
        }
    }
    uint32_t currentChain = 0;
    for (uint32_t index = 0; index < referenceCount; ++index)
    {
        const FSecondaryMotionReferenceSegment& reference = references[index];
        while (currentChain + 1u < chainCount && index >= chains[currentChain].firstSegment + chains[currentChain].segmentCount)
        {
            ++currentChain;
        }
        const FSecondaryMotionContactChain& chain = chains[currentChain];
        gk::FVector3d direction = Subtract(positions.At(chain.firstPoint + index - chain.firstSegment + 1u), positions.At(chain.firstPoint + index - chain.firstSegment));
        if (!Normalize(direction))
        {
            direction = reference.direction;
        }
        if (!NormalizeAndLimitSecondaryMotionDirection(reference.direction, chain.cosineLimit, direction) || !directions.Append(direction) || !gradients.Append(gk::FVector3d{}))
        {
            error.Assign("Secondary motion weighted initial direction is invalid");
            return false;
        }
    }
    bool targetChains[1024] = {};
    double initialMerit = 0.0;
    if (!EvaluateAll(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions.Data(), initialMerit, gradients.Data(), referenceCount, anchorEvaluations, error))
    {
        return false;
    }
    if (initialMerit != DBL_MAX && !BuildTargetContactChains(anchors, contacts, contactCount, anchorEvaluations, chainCount, targetChains, error))
    {
        return false;
    }
    // coherent seedで動かす鎖をweighted anchorと初期侵入から選ぶ。
    bool coherentSeedChains[1024] = {};
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        const FSecondaryMotionWeightedContactPoint& contact = contacts[contactIndex];
        if (contact.shapeCount == 0)
        {
            continue;
        }
        for (uint32_t localAnchor = 0; localAnchor < contact.anchorCount; ++localAnchor)
        {
            const uint32_t anchorIndex = contact.firstAnchor + localAnchor;
            const uint32_t chainIndex = anchors[anchorIndex].chainIndex;
            if (chainIndex != 0xFFFFFFFFu)
            {
                coherentSeedChains[chainIndex] = true;
            }
        }
    }
    // 各鎖の初期接触merit評価へ渡す方向勾配領域。
    gk::Array<gk::FVector3d> chainGradientScratch;
    if (!chainGradientScratch.Reserve(referenceCount))
    {
        error.Assign("Secondary motion weighted chain merit workspace allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < referenceCount; ++index)
    {
        if (!chainGradientScratch.Append(gk::FVector3d{}))
        {
            error.Assign("Secondary motion weighted chain merit workspace allocation failed");
            return false;
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        // 鎖単位の初期侵入量は全勾配が0でもactive判定へ反映する。
        double chainMerit = 0.0;
        if (!EvaluatePreparedSecondaryMotionChainContactsMerit(chain.shapes, chain.shapeCount, references + chain.firstSegment, chain.segmentCount, chain.cosineLimit, 1.0, chain.attachments, chain.attachmentCount, positions.Data() + chain.firstPoint, chain.segmentCount + 1u, chainMerit, chainGradientScratch.Data() + chain.firstSegment, chain.segmentCount, error))
        {
            return false;
        }
        if (chainMerit > 0.0)
        {
            coherentSeedChains[chainIndex] = true;
        }
    }
    double bestMerit = DBL_MAX;
    bool foundClear = false;
    gk::Array<gk::FVector3d> bestPositions;
    if (!bestPositions.Reserve(totalPoints))
    {
        error.Assign("Secondary motion weighted result allocation failed");
        return false;
    }
    gk::Array<gk::FVector3d> seedDirections;
    if (!seedDirections.Reserve(referenceCount))
    {
        error.Assign("Secondary motion weighted seed allocation failed");
        return false;
    }
    for (uint32_t strategyIndex = 0; strategyIndex < 3u && !foundClear; ++strategyIndex)
    {
        // 単位勾配と従来の相対量方式を保ち、最後に共役勾配を試す。
        const bool preserveMagnitude = strategyIndex != 0u;
        const bool useConjugate = strategyIndex == 2u;
        bool tryInitial = true;
        const uint32_t maximumSeedIndex = strategyIndex == 2u ? kWeightedContactSeedAzimuths + 3u : kWeightedContactSeedAzimuths;
        for (uint32_t seedIndex = 0; seedIndex <= maximumSeedIndex && !foundClear; ++seedIndex)
        {
            if (tryInitial)
            {
                seedDirections.Clear();
                for (uint32_t index = 0; index < directions.Count(); ++index)
                {
                    if (!seedDirections.Append(directions.At(index)))
                    {
                        error.Assign("Secondary motion weighted seed allocation failed");
                        return false;
                    }
                }
                tryInitial = false;
            }
            else if (seedIndex <= kWeightedContactSeedAzimuths)
            {
                const uint32_t directionIndex = seedIndex - 1u;
                const double phase = 6.2831853071795864769 * static_cast<double>(directionIndex) / static_cast<double>(kWeightedContactSeedAzimuths);
                const gk::FVector3d worldTangent = { { cos(phase), sin(phase), 0.5 } };
                if (!MakeCoherentSeed(chains, chainCount, references, directions, coherentSeedChains, worldTangent, 0.5, seedDirections))
                {
                    error.Assign("Secondary motion weighted coherent seed is invalid");
                    return false;
                }
            }
            else
            {
                // 停滞した姿勢の近くから探索を再開する角度を選ぶ。
                const uint32_t localSeedIndex = seedIndex - kWeightedContactSeedAzimuths - 1u;
                const double angleFractions[3] = { 0.0222222222222222, 0.0555555555555556, 0.1111111111111111 };
                if (!MakeTargetedGradientSeed(chains, chainCount, references, directions, gradients, targetChains, angleFractions[localSeedIndex], seedDirections))
                {
                    error.Assign("Secondary motion weighted gradient seed is invalid");
                    return false;
                }
            }
            gk::Array<gk::FVector3d> seedPositions;
            gk::Array<gk::FVector3d> seedGradients;
            gk::Array<FWeightedAnchorEvaluation> seedAnchors;
            if (!seedPositions.Reserve(totalPoints) || !seedGradients.Reserve(referenceCount) || !seedAnchors.Reserve(anchorCount))
            {
                error.Assign("Secondary motion weighted seed workspace allocation failed");
                return false;
            }
            for (uint32_t index = 0; index < referenceCount; ++index)
            {
                if (!seedGradients.Append(gk::FVector3d{}))
                {
                    error.Assign("Secondary motion weighted seed workspace allocation failed");
                    return false;
                }
            }
            double seedMerit = 0.0;
            if (!EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, seedDirections, seedPositions, seedMerit, seedGradients, seedAnchors, error))
            {
                if (!error.Empty())
                {
                    return false;
                }
                continue;
            }
            gk::Array<gk::FVector3d> trialDirections;
            if (!trialDirections.Reserve(referenceCount))
            {
                error.Assign("Secondary motion weighted trial allocation failed");
                return false;
            }
            for (uint32_t index = 0; index < seedDirections.Count(); ++index)
            {
                if (!trialDirections.Append(seedDirections.At(index)))
                {
                    error.Assign("Secondary motion weighted trial allocation failed");
                    return false;
                }
            }
            bool clear = seedMerit == 0.0;
            if (!RefineSeed(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, maximumIterations, preserveMagnitude, useConjugate, trialDirections, seedPositions, seedGradients, seedMerit, clear, seedAnchors, error))
            {
                return false;
            }
            if (seedMerit < bestMerit)
            {
                bestMerit = seedMerit;
                bestPositions.Clear();
                for (uint32_t index = 0; index < seedPositions.Count(); ++index)
                {
                    if (!bestPositions.Append(seedPositions.At(index)))
                    {
                        error.Assign("Secondary motion weighted result allocation failed");
                        return false;
                    }
                }
            }
            if (clear)
            {
                bestPositions.Clear();
                for (uint32_t index = 0; index < seedPositions.Count(); ++index)
                {
                    if (!bestPositions.Append(seedPositions.At(index)))
                    {
                        error.Assign("Secondary motion weighted result allocation failed");
                        return false;
                    }
                }
                foundClear = true;
            }
        }
    }
    if (!foundClear)
    {
        error.Clear();
        return false;
    }
    for (uint32_t index = 0; index < totalPoints; ++index)
    {
        positions.At(index) = bestPositions.At(index);
    }
    error.Clear();
    return true;
}

}
