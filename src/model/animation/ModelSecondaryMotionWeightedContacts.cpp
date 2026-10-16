// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionWeightedContacts.h"
#include "model/animation/ModelSecondaryMotionContactReach.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionChainContacts.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionDirection.h"
#include "model/animation/ModelSecondaryMotionContactMinimax.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

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
// float保存後も角度制約の許容幅に余裕を残す角度幅。
const double kWeightedContactStoredAngleMargin = 16.0 * FLT_EPSILON;
// 保存後の骨長と角度を呼出し側と同じ幅で検査する。
const double kWeightedContactStoredConstraintTolerance = 128.0 * FLT_EPSILON;
#if defined(GKCORE_TESTING)
// 数値の一致を比較する検査だけで、trialの従来の微分評価を選べる。
thread_local bool useFullWeightedTrialForTesting = false;
#endif

/**
 * 検査の比較時以外は、採用前のtrialで微分を計算しない。
 */
bool CalculateTrialGradients()
{
#if defined(GKCORE_TESTING)
    return useFullWeightedTrialForTesting;
#else
    return false;
#endif
}

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
    return Normalize(reference) && Normalize(direction) && 1.0 + Dot(reference, direction) <= kSecondaryMotionAttachmentAntiparallelThreshold;
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
bool EvaluateAll(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::FVector3d* positions, double& merit, gk::FVector3d* gradients, uint32_t gradientCount, gk::Array<FWeightedAnchorEvaluation>& anchorEvaluations, gk::String& error, FSecondaryMotionContactLinearSystem* linearSystem = nullptr, bool useClearanceMargin = false, bool calculateGradients = true)
{
    merit = 0.0;
    if ((gradientCount > 0 && !gradients) || (!calculateGradients && (gradients || gradientCount > 0u || linearSystem)))
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
        if (!EvaluatePreparedSecondaryMotionChainContactsMerit(chain.shapes, chain.shapeCount, references + chain.firstSegment, chain.segmentCount, chain.cosineLimit, 1.0, chain.attachments, chain.attachmentCount, positions + chain.firstPoint, chain.segmentCount + 1u, chainMerit, calculateGradients ? gradients + chain.firstSegment : nullptr, calculateGradients ? chain.segmentCount : 0u, error, linearSystem, chain.firstSegment, useClearanceMargin))
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
            const bool evaluated = calculateGradients ? EvaluateSecondaryMotionAttachment(anchor.definition, positions[pointIndex], positions[pointIndex + 1u], attachment, error) : EvaluateSecondaryMotionAttachmentPosition(anchor.definition, positions[pointIndex], positions[pointIndex + 1u], attachment.position, error);
            if (!evaluated)
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
            penetration += useClearanceMargin ? contact.shapes[shapeIndex].tolerance * 0.125 : 0.0;
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
            if (!calculateGradients)
            {
                continue;
            }
            // 判定条件は保ち、float保存で境界の内側へ戻らない補正を求める。
            if (linearSystem && !linearSystem->BeginRow(penetration, error))
            {
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
                    if (linearSystem && !linearSystem->AddCoefficient(chain.firstSegment + ancestor, Multiply(normal, references[chain.firstSegment + ancestor].length * anchor.weight), error))
                    {
                        return false;
                    }
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
                if (linearSystem && !linearSystem->AddCoefficient(globalSegment, Multiply(childGradient, references[globalSegment].length * anchor.weight), error))
                {
                    return false;
                }
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
bool EvaluateDirections(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& roots, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& directions, gk::Array<gk::FVector3d>& positions, double& merit, gk::Array<gk::FVector3d>& gradients, gk::Array<FWeightedAnchorEvaluation>& anchorEvaluations, gk::String& error, FSecondaryMotionContactLinearSystem* linearSystem = nullptr, bool useClearanceMargin = false, bool calculateGradients = true)
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
    if (calculateGradients && !gradients.Reserve(expectedGradientCount))
    {
        error.Assign("Secondary motion weighted gradient allocation failed");
        return false;
    }
    for (uint32_t index = 0; calculateGradients && index < expectedGradientCount; ++index)
    {
        if (!gradients.Append(gk::FVector3d{}))
        {
            error.Assign("Secondary motion weighted gradient allocation failed");
            return false;
        }
    }
    if (linearSystem && !linearSystem->Reset(directions.Data(), directions.Count(), error))
    {
        return false;
    }
    if (!BuildPositions(chains, chainCount, references, roots, directions, positions))
    {
        error.Assign("Secondary motion weighted candidate position is outside the float range");
        return false;
    }
    return EvaluateAll(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions.Data(), merit, calculateGradients ? gradients.Data() : nullptr, gradients.Count(), anchorEvaluations, error, linearSystem, useClearanceMargin, calculateGradients);
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
                if (!EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, trialDirections, trialPositions, trialMerit, trialGradients, trialAnchors, error, nullptr, false, CalculateTrialGradients()))
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
                    // 採用した候補だけ次回の方向選択に必要な微分を評価する。
                    if (!CalculateTrialGradients() && !EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, trialDirections, trialPositions, trialMerit, trialGradients, trialAnchors, error))
                    {
                        return false;
                    }
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
 * float保存後に元の骨長・角度・接触条件が成立しているか確認する。
 */
bool IsStoredCandidateClear(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& positions, bool& clear, gk::String& error);

/**
 * 接触行どうしの影響を減衰付き連立方程式で解き、現在姿勢の近くで候補を探す。
 */
bool RefineCoupledSeed(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& roots, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, uint32_t maximumIterations, gk::Array<gk::FVector3d>& directions, gk::Array<gk::FVector3d>& positions, gk::Array<gk::FVector3d>& gradients, double& merit, bool& clear, gk::Array<FWeightedAnchorEvaluation>& anchorEvaluations, bool& progressed, gk::String& error)
{
    // 関係する自由度を同時に更新し、単独勾配では進みにくい接触を扱う。
    FSecondaryMotionContactLinearSystem linearSystem;
    double relativeDamping = 1.0e-3;
    clear = false;
    progressed = false;
    for (uint32_t iteration = 0; iteration < maximumIterations; ++iteration)
    {
        if (!EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, directions, positions, merit, gradients, anchorEvaluations, error, &linearSystem, true))
        {
            // 上限で評価を中断した姿勢は、途中までの侵入量で次の候補へ引き継がない。
            progressed = false;
            merit = DBL_MAX;
            // 計算量の上限では従来探索へ戻し、入力や確保の失敗は呼出し側へ伝える。
            return error.Empty();
        }
        if (merit == DBL_MAX)
        {
            return true;
        }
        // 探索の余白をすべて消し切る必要はなく、元の保存条件が成立した時点で採用へ進む。
        if (!IsStoredCandidateClear(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions, clear, error))
        {
            return false;
        }
        if (clear)
        {
            merit = 0.0;
            return true;
        }
        bool improved = false;
        for (uint32_t attempt = 0; attempt < 16u && !improved; ++attempt)
        {
            gk::Array<gk::FVector3d> stepDirections;
            if (!linearSystem.Solve(relativeDamping, stepDirections, error))
            {
                if (!error.Empty())
                {
                    return false;
                }
                relativeDamping *= 10.0;
                continue;
            }
            double maximumStep = 0.0;
            for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
            {
                const auto& chain = chains[chainIndex];
                for (uint32_t segment = 0; segment < chain.segmentCount; ++segment)
                {
                    const uint32_t index = chain.firstSegment + segment;
                    ProjectGradient(references[index].direction, chain.cosineLimit, directions.At(index), true, stepDirections.At(index));
                    const double stepLength = Length(stepDirections.At(index));
                    if (!isfinite(stepLength))
                    {
                        error.Assign("Secondary motion coupled contact step is invalid");
                        return false;
                    }
                    maximumStep = fmax(maximumStep, stepLength);
                }
            }
            if (!(maximumStep > 0.0))
            {
                return true;
            }
            // 長すぎる連立補正は全体を縮めず、減衰を増やして近傍の解を求め直す。
            const double maximumTangentStep = tan(0.35);
            if (maximumStep > maximumTangentStep)
            {
                relativeDamping *= fmax(2.0, fmin(1.0e6, maximumStep / maximumTangentStep));
                continue;
            }
            double angle = atan(maximumStep);
            for (uint32_t search = 0; search < kWeightedContactLineSearchSteps; ++search)
            {
                gk::Array<gk::FVector3d> trialDirections;
                gk::Array<gk::FVector3d> trialPositions;
                gk::Array<gk::FVector3d> trialGradients;
                gk::Array<FWeightedAnchorEvaluation> trialAnchors;
                if (!trialDirections.Reserve(directions.Count()) || !trialPositions.Reserve(positions.Count()) || !trialGradients.Reserve(gradients.Count()) || !trialAnchors.Reserve(anchorCount))
                {
                    error.Assign("Secondary motion coupled contact workspace allocation failed");
                    return false;
                }
                if (!MakeTrialDirections(chains, chainCount, references, directions, stepDirections, true, maximumStep, angle, trialDirections))
                {
                    angle *= 0.5;
                    continue;
                }
                double trialMerit = 0.0;
                if (!EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, trialDirections, trialPositions, trialMerit, trialGradients, trialAnchors, error, nullptr, true, CalculateTrialGradients()))
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
                    // 次の連立反復の先頭で微分を再評価するため、ここでは位置だけを採用する。
                    directions.MoveFrom(trialDirections);
                    positions.MoveFrom(trialPositions);
                    gradients.MoveFrom(trialGradients);
                    anchorEvaluations.MoveFrom(trialAnchors);
                    merit = trialMerit;
                    clear = merit == 0.0;
                    improved = true;
                    progressed = true;
                    relativeDamping = fmax(1.0e-12, relativeDamping * (search == 0u ? 0.3 : 2.0));
                    break;
                }
                angle *= 0.5;
            }
            if (!improved)
            {
                relativeDamping *= 10.0;
            }
        }
        if (!improved || clear)
        {
            return true;
        }
    }
    return IsStoredCandidateClear(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions, clear, error);
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
            gk::FVector3d current = currentDirections.At(directionIndex);
            if (!Normalize(current))
            {
                return false;
            }
            gk::FVector3d tangent = Subtract(worldTangent, Multiply(current, Dot(worldTangent, current)));
            if (!Normalize(tangent) && !BuildPerpendicular(current, tangent))
            {
                return false;
            }
            gk::FVector3d candidate = Add(Multiply(current, cos(angle)), Multiply(tangent, sin(angle)));
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
 * floatへ保存した候補が骨長と許容角を保つか確認する。
 */
bool IsStoredPoseWithinChainConstraints(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& positions)
{
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const FSecondaryMotionContactChain& chain = chains[chainIndex];
        for (uint32_t segment = 0; segment < chain.segmentCount; ++segment)
        {
            const uint32_t parentIndex = chain.firstPoint + segment;
            const uint32_t childIndex = parentIndex + 1u;
            gk::FVector3d parent{};
            gk::FVector3d child{};
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                parent.value[axis] = static_cast<float>(positions.At(parentIndex).value[axis]);
                child.value[axis] = static_cast<float>(positions.At(childIndex).value[axis]);
            }
            const gk::FVector3d current = Subtract(child, parent);
            const double length = Length(current);
            const FSecondaryMotionReferenceSegment& reference = references[chain.firstSegment + segment];
            const double angleLimit = acos(chain.cosineLimit);
            const double storedCosineLimit = cos(fmin(3.1415926535897932384626433832795, angleLimit + kWeightedContactStoredConstraintTolerance));
            if (!(length > 0.0) || !isfinite(length) || fabs(length - reference.length) > reference.length * kWeightedContactStoredConstraintTolerance)
            {
                return false;
            }
            const double cosine = Dot(current, reference.direction) / length;
            if (!isfinite(cosine) || cosine < storedCosineLimit)
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * float丸め後も骨制約と全接触を満たすか確認する。
 */
bool IsStoredCandidateClear(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& positions, bool& clear, gk::String& error)
{
    clear = false;
    if (!IsStoredPoseWithinChainConstraints(chains, chainCount, references, positions))
    {
        return true;
    }
    gk::Array<gk::FVector3d> storedPositions;
    gk::Array<gk::FVector3d> gradients;
    gk::Array<FWeightedAnchorEvaluation> anchorEvaluations;
    uint32_t segmentCount = 0;
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        segmentCount += chains[chainIndex].segmentCount;
    }
    if (!storedPositions.Reserve(positions.Count()) || !gradients.Reserve(segmentCount) || !anchorEvaluations.Reserve(anchorCount))
    {
        error.Assign("Secondary motion weighted stored-pose workspace allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < positions.Count(); ++index)
    {
        gk::FVector3d stored{};
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            stored.value[axis] = static_cast<float>(positions.At(index).value[axis]);
        }
        if (!storedPositions.Append(stored))
        {
            error.Assign("Secondary motion weighted stored-pose workspace allocation failed");
            return false;
        }
    }
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        if (!gradients.Append(gk::FVector3d{}))
        {
            error.Assign("Secondary motion weighted stored-pose workspace allocation failed");
            return false;
        }
    }
    double merit = 0.0;
    if (!EvaluateAll(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, storedPositions.Data(), merit, gradients.Data(), gradients.Count(), anchorEvaluations, error))
    {
        return false;
    }
    clear = merit == 0.0;
    return true;
}

/**
 * 入力位置から候補位置までの二乗変位を合計する。
 */
double PositionDisplacementSquared(const gk::Array<gk::FVector3d>& initialPositions, const gk::Array<gk::FVector3d>& candidatePositions)
{
    double displacement = 0.0;
    for (uint32_t index = 0; index < initialPositions.Count(); ++index)
    {
        const gk::FVector3d delta = Subtract(candidatePositions.At(index), initialPositions.At(index));
        displacement += Dot(delta, delta);
    }
    return displacement;
}

/**
 * clear姿勢を接触制約を保ったまま入力方向へ近づける。
 */
bool ContractClearCandidate(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& roots, const gk::Array<gk::FVector3d>& initialDirections, const gk::Array<gk::FVector3d>& initialPositions, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& candidateDirections, gk::Array<gk::FVector3d>& candidatePositions, gk::Array<gk::FVector3d>& candidateGradients, gk::Array<FWeightedAnchorEvaluation>& candidateAnchors, bool& preservedClearance, gk::String& error)
{
    if (!IsStoredCandidateClear(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, candidatePositions, preservedClearance, error))
    {
        return false;
    }
    double lowerFraction = 0.0;
    double upperFraction = 1.0;
    // 従来の成立姿勢を残し、その手前で見逃した成立区間を最大4回だけ調べる。
    double scanLowerFraction = 0.0;
    double scanUpperFraction = 1.0;
    double bestDisplacementSquared = 0.0;
    bool foundEarlierInterval = false;
    for (uint32_t probe = 0; probe < 16u; ++probe)
    {
        const bool earlierIntervalProbe = probe >= 12u;
        if (probe == 12u)
        {
            scanUpperFraction = upperFraction;
            bestDisplacementSquared = PositionDisplacementSquared(initialPositions, candidatePositions);
        }
        if (probe == 15u && !foundEarlierInterval)
        {
            break;
        }
        // 接触の有無は方向の補間率に対して単調ではないため、手前から区切って確認する。
        const double fraction = !earlierIntervalProbe ? (lowerFraction + upperFraction) * 0.5 : foundEarlierInterval ? (scanLowerFraction + scanUpperFraction) * 0.5 : scanUpperFraction * static_cast<double>(probe - 11u) * 0.25;
        double& rejectedLowerFraction = earlierIntervalProbe ? scanLowerFraction : lowerFraction;
        double& acceptedUpperFraction = earlierIntervalProbe ? scanUpperFraction : upperFraction;
        gk::Array<gk::FVector3d> trialDirections;
        gk::Array<gk::FVector3d> trialPositions;
        gk::Array<gk::FVector3d> trialGradients;
        gk::Array<FWeightedAnchorEvaluation> trialAnchors;
        if (!trialDirections.Reserve(candidateDirections.Count()) || !trialPositions.Reserve(candidatePositions.Count()) || !trialGradients.Reserve(candidateGradients.Count()) || !trialAnchors.Reserve(anchorCount))
        {
            error.Assign("Secondary motion weighted proximity workspace allocation failed");
            return false;
        }
        uint32_t segmentOffset = 0;
        bool valid = true;
        for (uint32_t chainIndex = 0; chainIndex < chainCount && valid; ++chainIndex)
        {
            const FSecondaryMotionContactChain& chain = chains[chainIndex];
            const double angleLimit = acos(chain.cosineLimit);
            const double safeCosineLimit = cos(fmax(0.0, angleLimit - kWeightedContactStoredAngleMargin));
            for (uint32_t localSegment = 0; localSegment < chain.segmentCount; ++localSegment)
            {
                const uint32_t index = segmentOffset + localSegment;
                gk::FVector3d direction = Add(Multiply(initialDirections.At(index), 1.0 - fraction), Multiply(candidateDirections.At(index), fraction));
                if (!NormalizeAndLimitSecondaryMotionDirection(references[chain.firstSegment + localSegment].direction, safeCosineLimit, direction) || !trialDirections.Append(direction))
                {
                    valid = false;
                    break;
                }
            }
            segmentOffset += chain.segmentCount;
        }
        if (!valid)
        {
            rejectedLowerFraction = fraction;
            continue;
        }
        for (uint32_t index = 0; index < candidateGradients.Count(); ++index)
        {
            if (!trialGradients.Append(gk::FVector3d{}))
            {
                error.Assign("Secondary motion weighted proximity workspace allocation failed");
                return false;
            }
        }
        double trialMerit = 0.0;
        if (!EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, trialDirections, trialPositions, trialMerit, trialGradients, trialAnchors, error, nullptr, false, CalculateTrialGradients()))
        {
            if (!error.Empty())
            {
                return false;
            }
            rejectedLowerFraction = fraction;
            continue;
        }
        bool storedClear = false;
        if (trialMerit == 0.0 && !IsStoredCandidateClear(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, trialPositions, storedClear, error))
        {
            return false;
        }
        if (trialMerit == 0.0 && storedClear)
        {
            acceptedUpperFraction = fraction;
            foundEarlierInterval = foundEarlierInterval || earlierIntervalProbe;
            const double displacementSquared = earlierIntervalProbe ? PositionDisplacementSquared(initialPositions, trialPositions) : 0.0;
            // 追加探索では、全制約を満たし入力からの変位が小さい姿勢だけを採用する。
            if (!earlierIntervalProbe || !preservedClearance || displacementSquared < bestDisplacementSquared)
            {
                preservedClearance = true;
                bestDisplacementSquared = displacementSquared;
                candidatePositions.MoveFrom(trialPositions);
                candidateGradients.MoveFrom(trialGradients);
                candidateAnchors.MoveFrom(trialAnchors);
            }
        }
        else
        {
            rejectedLowerFraction = fraction;
        }
    }
    return true;
}

/**
 * 一つのowner鎖の祖先segmentだけを30度傾け、他の方向は保存bitを保つ。入力不正・数値不正・確保失敗はerrorへ返す。
 */
bool MakeOwnerPrefixSeed(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const gk::Array<gk::FVector3d>& currentDirections, uint32_t ownerChain, uint32_t lastSegment, const gk::FVector3d& worldTangent, gk::Array<gk::FVector3d>& output, gk::String& error)
{
    if (ownerChain >= chainCount || lastSegment >= chains[ownerChain].segmentCount)
    {
        error.Assign("Secondary motion weighted owner seed range is invalid");
        return false;
    }
    // 対向する接触勾配の相殺を抜けるため、合成勾配と独立な固定角を使う。
    const double angle = 3.1415926535897932384626433832795 / 6.0;
    output.Clear();
    for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
    {
        const auto& chain = chains[chainIndex];
        for (uint32_t segment = 0; segment < chain.segmentCount; ++segment)
        {
            const uint32_t index = chain.firstSegment + segment;
            gk::FVector3d candidate = currentDirections.At(index);
            if (chainIndex == ownerChain && segment <= lastSegment)
            {
                if (!Normalize(candidate))
                {
                    error.Assign("Secondary motion weighted owner seed direction is invalid");
                    return false;
                }
                gk::FVector3d tangent = Subtract(worldTangent, Multiply(candidate, Dot(worldTangent, candidate)));
                if (!Normalize(tangent) && !BuildPerpendicular(candidate, tangent))
                {
                    error.Assign("Secondary motion weighted owner seed tangent is invalid");
                    return false;
                }
                candidate = Add(Multiply(candidate, cos(angle)), Multiply(tangent, sin(angle)));
                if (!NormalizeAndLimitSecondaryMotionDirection(references[index].direction, chain.cosineLimit, candidate))
                {
                    error.Assign("Secondary motion weighted owner seed direction is invalid");
                    return false;
                }
            }
            if (!output.Append(candidate))
            {
                error.Assign("Secondary motion weighted owner seed allocation failed");
                return false;
            }
        }
    }
    return true;
}

/**
 * 最大weighted侵入点から合算weight上位2鎖と必要な祖先範囲を選ぶ。同率は鎖番号順とし、動的寄与がなければ0件を返す。
 */
bool SelectWorstContactOwners(const FSecondaryMotionWeightedContactAnchor* anchors, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<FWeightedAnchorEvaluation>& evaluations, uint32_t chainCount, uint32_t ownerIndices[2], uint32_t ownerLastSegments[2], uint32_t& ownerCount, gk::String& error)
{
    ownerCount = 0u;
    uint32_t selectedContact = UINT32_MAX;
    double maximumPenetration = 0.0;
    for (uint32_t contactIndex = 0; contactIndex < contactCount; ++contactIndex)
    {
        const auto& contact = contacts[contactIndex];
        gk::FVector3d center{};
        for (uint32_t local = 0; local < contact.anchorCount; ++local)
        {
            const uint32_t index = contact.firstAnchor + local;
            center = Add(center, Multiply(evaluations.At(index).position, anchors[index].weight));
        }
        for (uint32_t shapeIndex = 0; shapeIndex < contact.shapeCount; ++shapeIndex)
        {
            double penetration = 0.0;
            double span = 0.0;
            gk::FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(contact.shapes[shapeIndex], center, center, penetration, span, normal, error, kWeightedContactToleranceFraction))
            {
                return false;
            }
            if (penetration > maximumPenetration)
            {
                maximumPenetration = penetration;
                selectedContact = contactIndex;
            }
        }
    }
    if (selectedContact == UINT32_MAX)
    {
        return true;
    }
    // 既存の鎖数上限に収まる局所集計。接触点が使う動的寄与だけを加える。
    double ownerWeights[1024] = {};
    uint32_t lastSegments[1024] = {};
    const auto& contact = contacts[selectedContact];
    for (uint32_t local = 0; local < contact.anchorCount; ++local)
    {
        const auto& anchor = anchors[contact.firstAnchor + local];
        if (anchor.chainIndex == UINT32_MAX)
        {
            continue;
        }
        ownerWeights[anchor.chainIndex] += anchor.weight;
        if (!isfinite(ownerWeights[anchor.chainIndex]))
        {
            error.Assign("Secondary motion weighted owner weight is outside the numeric range");
            return false;
        }
        if (anchor.segmentIndex > lastSegments[anchor.chainIndex])
        {
            lastSegments[anchor.chainIndex] = anchor.segmentIndex;
        }
    }
    for (uint32_t rank = 0; rank < 2u; ++rank)
    {
        uint32_t selectedOwner = UINT32_MAX;
        double selectedWeight = 0.0;
        for (uint32_t chainIndex = 0; chainIndex < chainCount; ++chainIndex)
        {
            if ((ownerCount == 0u || chainIndex != ownerIndices[0]) && ownerWeights[chainIndex] > selectedWeight)
            {
                selectedOwner = chainIndex;
                selectedWeight = ownerWeights[chainIndex];
            }
        }
        if (selectedOwner == UINT32_MAX)
        {
            break;
        }
        ownerIndices[ownerCount] = selectedOwner;
        ownerLastSegments[ownerCount] = lastSegments[selectedOwner];
        ++ownerCount;
    }
    return true;
}

/**
 * 通常探索の失敗後に、owner個別の祖先方向を最大3回・36候補だけ試す。全保存条件を満たす位置だけを候補へ保存する。
 */
bool RefineOwnerPrefixFallback(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, uint32_t referenceCount, const gk::Array<gk::FVector3d>& roots, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, uint32_t maximumIterations, const gk::Array<gk::FVector3d>& initialDirections, const gk::Array<gk::FVector3d>& closestAttemptDirections, const gk::Array<gk::FVector3d>& closestCoupledDirections, const gk::Array<gk::FVector3d>& initialPositions, gk::Array<gk::FVector3d>& bestPositions, double& bestDisplacementSquared, bool& foundClear, gk::String& error)
{
    // 余白の有無が異なるmeritは比較せず、利用できる候補の優先順位だけを使う。
    const gk::Array<gk::FVector3d>* baseSource = &initialDirections;
    if (referenceCount > 0u && closestCoupledDirections.Count() == referenceCount)
    {
        baseSource = &closestCoupledDirections;
    }
    else if (referenceCount > 0u && closestAttemptDirections.Count() == referenceCount)
    {
        baseSource = &closestAttemptDirections;
    }
    gk::Array<gk::FVector3d> baseDirections;
    if (!baseDirections.AppendRange(baseSource->Data(), baseSource->Count()))
    {
        error.Assign("Secondary motion weighted owner base allocation failed");
        return false;
    }
    // 各ownerを別々に動かし、同じ回転で相殺し続けるweighted offsetを分離する。
    const gk::FVector3d worldTangents[6] = { { { 1.0, 0.0, 0.0 } }, { { -1.0, 0.0, 0.0 } }, { { 0.0, 1.0, 0.0 } }, { { 0.0, -1.0, 0.0 } }, { { 0.0, 0.0, 1.0 } }, { { 0.0, 0.0, -1.0 } } };
    for (uint32_t round = 0; round < 3u; ++round)
    {
        gk::Array<gk::FVector3d> basePositions;
        gk::Array<gk::FVector3d> baseGradients;
        gk::Array<FWeightedAnchorEvaluation> baseAnchors;
        double baseMerit = 0.0;
        if (!EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, baseDirections, basePositions, baseMerit, baseGradients, baseAnchors, error, nullptr, true))
        {
            return false;
        }
        if (baseMerit == DBL_MAX)
        {
            break;
        }
        uint32_t ownerIndices[2] = {};
        uint32_t ownerLastSegments[2] = {};
        uint32_t ownerCount = 0u;
        if (!SelectWorstContactOwners(anchors, contacts, contactCount, baseAnchors, chainCount, ownerIndices, ownerLastSegments, ownerCount, error))
        {
            return false;
        }
        gk::Array<gk::FVector3d> roundBestDirections;
        double roundBestMerit = baseMerit;
        for (uint32_t owner = 0; owner < ownerCount; ++owner)
        {
            for (uint32_t axis = 0; axis < 6u; ++axis)
            {
                gk::Array<gk::FVector3d> trialDirections;
                gk::Array<gk::FVector3d> trialPositions;
                gk::Array<gk::FVector3d> trialGradients;
                gk::Array<FWeightedAnchorEvaluation> trialAnchors;
                if (!MakeOwnerPrefixSeed(chains, chainCount, references, baseDirections, ownerIndices[owner], ownerLastSegments[owner], worldTangents[axis], trialDirections, error))
                {
                    return false;
                }
                double trialMerit = 0.0;
                if (!EvaluateDirections(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, trialDirections, trialPositions, trialMerit, trialGradients, trialAnchors, error))
                {
                    return false;
                }
                bool clear = false;
                bool progressed = false;
                if (!RefineCoupledSeed(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, maximumIterations, trialDirections, trialPositions, trialGradients, trialMerit, clear, trialAnchors, progressed, error))
                {
                    return false;
                }
                bool preservedClearance = false;
                if (clear && !ContractClearCandidate(chains, chainCount, references, roots, initialDirections, initialPositions, anchors, anchorCount, contacts, contactCount, trialDirections, trialPositions, trialGradients, trialAnchors, preservedClearance, error))
                {
                    return false;
                }
                bool storedClear = false;
                if (!IsStoredCandidateClear(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, trialPositions, storedClear, error))
                {
                    return false;
                }
                // 同じ余白込み評価の厳密な改善だけを次のroundへ引き継ぐ。
                if (isfinite(trialMerit) && trialMerit >= 0.0 && trialMerit < roundBestMerit)
                {
                    roundBestDirections.Clear();
                    if (!roundBestDirections.AppendRange(trialDirections.Data(), trialDirections.Count()))
                    {
                        error.Assign("Secondary motion weighted owner handoff allocation failed");
                        return false;
                    }
                    roundBestMerit = trialMerit;
                }
                if (clear && preservedClearance && storedClear)
                {
                    const double displacement = PositionDisplacementSquared(initialPositions, trialPositions);
                    if (!isfinite(displacement))
                    {
                        error.Assign("Secondary motion weighted candidate displacement is invalid");
                        return false;
                    }
                    if (displacement < bestDisplacementSquared)
                    {
                        bestPositions.Clear();
                        if (!bestPositions.AppendRange(trialPositions.Data(), trialPositions.Count()))
                        {
                            error.Assign("Secondary motion weighted result allocation failed");
                            return false;
                        }
                        bestDisplacementSquared = displacement;
                    }
                    foundClear = true;
                }
            }
        }
        if (foundClear || roundBestDirections.Count() != referenceCount || ownerCount == 0u)
        {
            break;
        }
        baseDirections.MoveFrom(roundBestDirections);
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

bool ContractPreparedSecondaryMotionWeightedCandidate(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, uint32_t referenceCount, const gk::Array<gk::FVector3d>& roots, const gk::Array<gk::FVector3d>& initialDirections, const gk::Array<gk::FVector3d>& candidateDirections, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& initialPositions, gk::Array<gk::FVector3d>& candidatePositions, bool& preservedClearance, gk::String& error)
{
    preservedClearance = false;
    error.Clear();
    // 呼出し側で接触定義は検証済みでも、所有配列の対応はこの入口で確認する。
    if (chainCount > 1024u || referenceCount > 16384u || anchorCount > 16384u || contactCount > 1024u || (chainCount > 0u && !chains) || (referenceCount > 0u && !references) || (anchorCount > 0u && !anchors) || (contactCount > 0u && !contacts) || roots.Count() != chainCount || initialDirections.Count() != referenceCount || candidateDirections.Count() != referenceCount || initialPositions.Count() != candidatePositions.Count())
    {
        error.Assign("Secondary motion proximity input arrays are invalid");
        return false;
    }
    for (uint32_t index = 0u; index < initialPositions.Count(); ++index)
    {
        if (!IsFloatPosition(initialPositions.At(index)))
        {
            error.Assign("Secondary motion proximity initial position is invalid");
            return false;
        }
    }
    for (uint32_t index = 0u; index < roots.Count(); ++index)
    {
        if (!IsFloatPosition(roots.At(index)))
        {
            error.Assign("Secondary motion proximity root is invalid");
            return false;
        }
    }
    uint32_t pointCount = 0u;
    if (!ValidateChains(chains, chainCount, references, referenceCount, candidatePositions, pointCount, error))
    {
        return false;
    }
    for (uint32_t index = 0u; index < referenceCount; ++index)
    {
        if (!IsFloatPosition(initialDirections.At(index)) || !IsFloatPosition(candidateDirections.At(index)))
        {
            error.Assign("Secondary motion proximity direction is invalid");
            return false;
        }
    }
    // 出力候補とは別に所有し、途中の確保失敗で呼出し元の位置を変更しない。
    gk::Array<gk::FVector3d> workingPositions;
    gk::Array<gk::FVector3d> gradients;
    gk::Array<FWeightedAnchorEvaluation> anchorEvaluations;
    if (!workingPositions.AppendRange(candidatePositions.Data(), candidatePositions.Count()) || !gradients.Reserve(referenceCount) || !anchorEvaluations.Reserve(anchorCount))
    {
        error.Assign("Secondary motion proximity result allocation failed");
        return false;
    }
    for (uint32_t index = 0u; index < referenceCount; ++index)
    {
        if (!gradients.Append(gk::FVector3d{}))
        {
            error.Assign("Secondary motion proximity gradient allocation failed");
            return false;
        }
    }
    bool clear = false;
    if (!ContractClearCandidate(chains, chainCount, references, roots, initialDirections, initialPositions, anchors, anchorCount, contacts, contactCount, candidateDirections, workingPositions, gradients, anchorEvaluations, clear, error))
    {
        return false;
    }
    if (clear)
    {
        candidatePositions.MoveFrom(workingPositions);
        preservedClearance = true;
    }
    return true;
}

/**
 * 検証済みの接触定義を同じ判定式で評価し、clear行を含めて二配列へ原子的に保存する。
 */
bool EvaluateSecondaryMotionContactSignedRows(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::FVector3d* positions, uint32_t positionCount, gk::Array<double>& signedValues, gk::Array<double>& tolerances, gk::String& error)
{
    error.Clear();
    if (chainCount > 1024u || anchorCount > 16384u || contactCount > 1024u || (chainCount > 0u && !chains) || (anchorCount > 0u && !anchors) || (contactCount > 0u && !contacts) || (positionCount > 0u && !positions) || &signedValues == &tolerances)
    {
        error.Assign("Secondary motion signed contact input is invalid");
        return false;
    }
    // 行数は確保前に数え、補助探索の作業量を制限する。
    uint64_t rowCount = 0u;
    for (uint32_t index = 0u; index < chainCount; ++index)
    {
        const auto& chain = chains[index];
        if (chain.firstPoint >= positionCount || chain.segmentCount >= positionCount - chain.firstPoint || chain.shapeCount > 64u || (chain.shapeCount > 0u && !chain.shapes) || chain.attachmentCount > 1024u || (chain.attachmentCount > 0u && !chain.attachments))
        {
            error.Assign("Secondary motion signed chain range is invalid");
            return false;
        }
        rowCount += static_cast<uint64_t>(chain.shapeCount) * chain.segmentCount * 2u;
        for (uint32_t local = 0u; local < chain.attachmentCount; ++local)
        {
            const auto& attachment = chain.attachments[local];
            if (attachment.segmentIndex >= chain.segmentCount || attachment.shapeCount > 64u || (attachment.shapeCount > 0u && !attachment.shapes))
            {
                error.Assign("Secondary motion signed attachment range is invalid");
                return false;
            }
            rowCount += attachment.shapeCount;
        }
    }
    for (uint32_t index = 0u; index < contactCount; ++index)
    {
        const auto& contact = contacts[index];
        if (contact.firstAnchor > anchorCount || contact.anchorCount > anchorCount - contact.firstAnchor || contact.anchorCount == 0u || contact.shapeCount > 64u || (contact.shapeCount > 0u && !contact.shapes))
        {
            error.Assign("Secondary motion signed weighted range is invalid");
            return false;
        }
        rowCount += contact.shapeCount;
    }
    if (rowCount > 65536u)
    {
        return false;
    }
    for (uint32_t index = 0u; index < positionCount; ++index)
    {
        if (!IsFloatPosition(positions[index]))
        {
            error.Assign("Secondary motion signed position is invalid");
            return false;
        }
    }
    gk::Array<double> values;
    gk::Array<double> widths;
    gk::Array<gk::FVector3d> anchorPositions;
    if (!values.Reserve(static_cast<uint32_t>(rowCount)) || !widths.Reserve(static_cast<uint32_t>(rowCount)) || !anchorPositions.Reserve(anchorCount))
    {
        error.Assign("Secondary motion signed contact allocation failed");
        return false;
    }
    // この評価中だけ位置を共有し、重み付き行ごとの同じ回転計算を避ける。
    for (uint32_t index = 0u; index < anchorCount; ++index)
    {
        const auto& anchor = anchors[index];
        gk::FVector3d point = anchor.constantPosition;
        if (!isfinite(anchor.weight) || !(anchor.weight > 0.0))
        {
            error.Assign("Secondary motion signed anchor weight is invalid");
            return false;
        }
        if (anchor.chainIndex != UINT32_MAX)
        {
            if (anchor.chainIndex >= chainCount || anchor.segmentIndex >= chains[anchor.chainIndex].segmentCount)
            {
                error.Assign("Secondary motion signed anchor range is invalid");
                return false;
            }
            const uint32_t parent = chains[anchor.chainIndex].firstPoint + anchor.segmentIndex;
            // 反対向きの特異姿勢は入力エラーにせず、今回の候補だけを捨てる。
            if (IsSingularAttachmentCandidate(anchor.definition, positions[parent], positions[parent + 1u]))
            {
                return false;
            }
            if (!EvaluateSecondaryMotionAttachmentPosition(anchor.definition, positions[parent], positions[parent + 1u], point, error))
            {
                return false;
            }
        }
        if (!IsFloatPosition(point) || !anchorPositions.Append(point))
        {
            error.Assign("Secondary motion signed anchor position is invalid");
            return false;
        }
    }
    // 一方の配列だけを書き換えず、値と保存余白を同じ行番号にそろえる。
    const auto Append = [&](double value, double tolerance)
    {
        if (!isfinite(value) || !isfinite(tolerance) || tolerance < 0.0)
        {
            error.Assign("Secondary motion signed contact value is invalid");
            return false;
        }
        if (!values.Append(value) || !widths.Append(tolerance))
        {
            error.Assign("Secondary motion signed contact row allocation failed");
            return false;
        }
        return true;
    };
    for (uint32_t index = 0u; index < contactCount; ++index)
    {
        const auto& contact = contacts[index];
        gk::FVector3d center{};
        for (uint32_t local = 0u; local < contact.anchorCount; ++local)
        {
            const uint32_t anchor = contact.firstAnchor + local;
            center = Add(center, Multiply(anchorPositions.At(anchor), anchors[anchor].weight));
        }
        for (uint32_t shapeIndex = 0u; shapeIndex < contact.shapeCount; ++shapeIndex)
        {
            double penetration = 0.0;
            double span = 0.0;
            gk::FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(contact.shapes[shapeIndex], center, center, penetration, span, normal, error, kWeightedContactToleranceFraction) || !Append(penetration, contact.shapes[shapeIndex].tolerance))
            {
                return false;
            }
        }
    }
    for (uint32_t index = 0u; index < chainCount; ++index)
    {
        const auto& chain = chains[index];
        for (uint32_t shapeIndex = 0u; shapeIndex < chain.shapeCount; ++shapeIndex)
        {
            const auto& shape = chain.shapes[shapeIndex];
            const double rootThreshold = shape.hasRootContactOverride ? GetPreparedSecondaryMotionRootContactThreshold(shape) : 0.0;
            if (shape.hasRootContactOverride && (!isfinite(rootThreshold) || rootThreshold < 0.0 || rootThreshold > shape.radius))
            {
                error.Assign("Secondary motion signed root threshold is invalid");
                return false;
            }
            for (uint32_t segment = 0u; segment < chain.segmentCount; ++segment)
            {
                const auto& first = positions[chain.firstPoint + segment];
                const auto& second = positions[chain.firstPoint + segment + 1u];
                double penetration = 0.0;
                double span = 0.0;
                gk::FVector3d normal{};
                if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, second, second, penetration, span, normal, error, 1.0) || !Append(penetration, shape.tolerance))
                {
                    return false;
                }
                bool skip = false;
                double rootDistance = 0.0;
                if (segment == 0u)
                {
                    if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, first, first, penetration, span, normal, error, 0.0))
                    {
                        return false;
                    }
                    rootDistance = shape.radius - penetration;
                    skip = shape.hasRootContactOverride ? penetration > shape.radius - rootThreshold : penetration > shape.tolerance;
                }
                if (skip)
                {
                    if (!Append(-1.0, 0.0))
                    {
                        return false;
                    }
                    continue;
                }
                const bool override = segment == 0u && shape.hasRootContactOverride;
                if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, first, second, penetration, span, normal, error, override ? 0.0 : 1.0))
                {
                    return false;
                }
                double tolerance = shape.tolerance;
                if (override)
                {
                    const double threshold = GetPreparedSecondaryMotionRootSpanThreshold(shape, rootDistance, 1.0);
                    penetration += threshold - shape.radius;
                    // .125倍すると、従来のroot側余白の上限と同じ値になる。
                    tolerance = fmin(tolerance, fmax(0.0, rootDistance - threshold) * 4.0);
                }
                if (!Append(penetration, tolerance))
                {
                    return false;
                }
            }
        }
        for (uint32_t local = 0u; local < chain.attachmentCount; ++local)
        {
            const auto& contact = chain.attachments[local];
            if (contact.shapeCount == 0u)
            {
                continue;
            }
            const uint32_t parent = chain.firstPoint + contact.segmentIndex;
            gk::FVector3d first{};
            gk::FVector3d second{};
            if (IsSingularAttachmentCandidate(contact.firstDefinition, positions[parent], positions[parent + 1u]) || IsSingularAttachmentCandidate(contact.secondDefinition, positions[parent], positions[parent + 1u]))
            {
                return false;
            }
            if (!EvaluateSecondaryMotionAttachmentPosition(contact.firstDefinition, positions[parent], positions[parent + 1u], first, error) || !EvaluateSecondaryMotionAttachmentPosition(contact.secondDefinition, positions[parent], positions[parent + 1u], second, error))
            {
                return false;
            }
            for (uint32_t shapeIndex = 0u; shapeIndex < contact.shapeCount; ++shapeIndex)
            {
                double penetration = 0.0;
                double span = 0.0;
                gk::FVector3d normal{};
                if (!QueryPreparedSecondaryMotionFreeSegmentContact(contact.shapes[shapeIndex], first, second, penetration, span, normal, error, 0.5) || !Append(penetration, contact.shapes[shapeIndex].tolerance))
                {
                    return false;
                }
            }
        }
    }
    signedValues.MoveFrom(values);
    tolerances.MoveFrom(widths);
    return true;
}

bool CheckSecondaryMotionWeightedStoredCandidate(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& positions, bool& clear, gk::String& error)
{
    error.Clear();
    clear = false;
    if (chainCount > 1024u || (chainCount > 0u && (!chains || !references)))
    {
        error.Assign("Secondary motion stored candidate input is invalid");
        return false;
    }
    uint64_t referenceCount = 0u;
    for (uint32_t index = 0u; index < chainCount; ++index)
    {
        referenceCount += chains[index].segmentCount;
    }
    if (referenceCount > 16384u)
    {
        error.Assign("Secondary motion stored candidate references exceed the limit");
        return false;
    }
    uint32_t pointCount = 0u;
    if (!ValidateChains(chains, chainCount, references, static_cast<uint32_t>(referenceCount), positions, pointCount, error))
    {
        return false;
    }
    // 配列範囲と数値を検査してから、既存の保存後検査へ渡す。
    gk::Array<double> signedValues;
    gk::Array<double> tolerances;
    if (!EvaluateSecondaryMotionContactSignedRows(chains, chainCount, anchors, anchorCount, contacts, contactCount, positions.Data(), positions.Count(), signedValues, tolerances, error))
    {
        return error.Empty();
    }
    return IsStoredCandidateClear(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions, clear, error);
}

#if defined(GKCORE_TESTING)
/**
 * 専用検査で同じsolverの従来trialと位置専用trialを比較する。
 */
void SetSecondaryMotionWeightedFullTrialForTesting(bool enabled)
{
    useFullWeightedTrialForTesting = enabled;
}

/**
 * 専用検査で同じ候補の侵入量を微分の有無だけ変えて比較する。
 */
bool EvaluateSecondaryMotionWeightedMeritForTesting(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& positions, bool calculateGradients, bool useClearanceMargin, double& merit, gk::String& error)
{
    gk::Array<gk::FVector3d> gradients;
    gk::Array<FWeightedAnchorEvaluation> evaluations;
    uint32_t segmentCount = 0u;
    for (uint32_t index = 0; index < chainCount; ++index)
    {
        segmentCount += chains[index].segmentCount;
    }
    for (uint32_t index = 0; calculateGradients && index < segmentCount; ++index)
    {
        if (!gradients.Append({}))
        {
            return false;
        }
    }
    return EvaluateAll(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions.Data(), merit, calculateGradients ? gradients.Data() : nullptr, gradients.Count(), evaluations, error, nullptr, useClearanceMargin, calculateGradients);
}
#endif

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
    bool storedInputClear = false;
    if (initialMerit == 0.0 && !IsStoredCandidateClear(chains, chainCount, references, anchors, anchorCount, contacts, contactCount, positions, storedInputClear, error))
    {
        return false;
    }
    if (initialMerit == 0.0 && storedInputClear)
    {
        return true;
    }
    // 既存の入力・数値・初期確保の検査後、探索前にだけ到達不能を区別する。
    uint32_t unreachablePoint = 0u;
    uint32_t unreachableShape = 0u;
    if (initialMerit != DBL_MAX && FindUnreachableSecondaryMotionWeightedContact(chains, references, anchors, contacts, contactCount, positions, unreachablePoint, unreachableShape))
    {
        // shapeはこの接触点に適用する身体形状配列の0始まり番号。
        char message[128]{};
        snprintf(message, sizeof(message), "Secondary motion weighted contact is unreachable: point %u, shape %u", unreachablePoint, unreachableShape);
        error.Assign(message);
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
    bool foundClear = false;
    double bestDisplacementSquared = DBL_MAX;
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
    // 接触の解消へ最も近づいた途中姿勢を、後続の連立探索へ引き継ぐ。
    gk::Array<gk::FVector3d> closestAttemptDirections;
    // 余白なしの接触評価で最も小さかった侵入量。
    double closestAttemptMerit = DBL_MAX;
    // 保存時の余白を含む連立探索の途中姿勢は、元条件の候補と分けて比較する。
    gk::Array<gk::FVector3d> closestCoupledDirections;
    // 保存時の余白も含めた接触評価で最も小さかった侵入量。
    double closestCoupledMerit = DBL_MAX;
    for (uint32_t attemptIndex = 0; attemptIndex < 6u; ++attemptIndex)
    {
        // 現在の方向から一度だけ連立補正し、近い成立姿勢を広いseed探索より先に使う。
        const bool localCoupledOnly = attemptIndex == 0u;
        // 局所探索が失敗した後は、従来の三方式、途中姿勢の引き継ぎ、最後の継続の順を保つ。
        const uint32_t strategyIndex = localCoupledOnly ? 3u : attemptIndex - 1u;
        const bool preserveMagnitude = strategyIndex != 0u;
        const bool useCoupled = strategyIndex >= 3u;
        // 最後の候補では最良の連立姿勢だけを再開する。
        const bool continueCoupled = strategyIndex == 4u;
        if (continueCoupled && closestCoupledDirections.Count() == 0u)
        {
            continue;
        }
        const bool useConjugate = strategyIndex == 2u;
        // 引き継ぎ候補を先頭へ加え、元の初期方向と既存seedも残す。
        const auto& previousAttemptDirections = continueCoupled ? closestCoupledDirections : closestAttemptDirections;
        const uint32_t additionalSeedCount = !localCoupledOnly && useCoupled && referenceCount > 0u && previousAttemptDirections.Count() == referenceCount ? 1u : 0u;
        // 全seedが失敗した後の継続は、最も改善した連立候補から一度だけ行う。
        const uint32_t maximumSeedIndex = localCoupledOnly || continueCoupled ? 0u : (strategyIndex == 2u ? kWeightedContactSeedAzimuths + 6u : 3u) + additionalSeedCount;
        uint32_t firstClearSeedIndex = UINT32_MAX;
        for (uint32_t seedIndex = 0; seedIndex <= maximumSeedIndex; ++seedIndex)
        {
            if (firstClearSeedIndex != UINT32_MAX)
            {
                break;
            }
            if (seedIndex == 0u && additionalSeedCount != 0u)
            {
                seedDirections.Clear();
                if (!seedDirections.AppendRange(previousAttemptDirections.Data(), previousAttemptDirections.Count()))
                {
                    error.Assign("Secondary motion weighted handoff seed allocation failed");
                    return false;
                }
            }
            else if (seedIndex == additionalSeedCount)
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
            }
            else if (seedIndex <= 3u + additionalSeedCount)
            {
                const uint32_t localSeedIndex = seedIndex - additionalSeedCount - 1u;
                const uint32_t directionIndex = localSeedIndex * 5u;
                const double phase = 6.2831853071795864769 * static_cast<double>(directionIndex) / static_cast<double>(kWeightedContactSeedAzimuths);
                const gk::FVector3d worldTangent = { { cos(phase), sin(phase), 0.5 } };
                const double angleFractions[3] = { 0.0222222222222222, 0.0555555555555556, 0.1111111111111111 };
                const double angleFraction = angleFractions[localSeedIndex];
                if (!MakeCoherentSeed(chains, chainCount, references, directions, coherentSeedChains, worldTangent, angleFraction, seedDirections))
                {
                    error.Assign("Secondary motion weighted coherent seed is invalid");
                    return false;
                }
            }
            else if (seedIndex <= kWeightedContactSeedAzimuths + 3u + additionalSeedCount)
            {
                const uint32_t directionIndex = seedIndex - additionalSeedCount - 4u;
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
                const uint32_t localSeedIndex = seedIndex - additionalSeedCount - kWeightedContactSeedAzimuths - 4u;
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
            // 上限で探索を省いた場合は、変化していない方向を継続候補へ保存しない。
            bool coupledProgressed = false;
            if (useCoupled)
            {
                if (seedMerit != DBL_MAX && !RefineCoupledSeed(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, maximumIterations, trialDirections, seedPositions, seedGradients, seedMerit, clear, seedAnchors, coupledProgressed, error))
                {
                    return false;
                }
            }
            else if (!RefineSeed(chains, chainCount, references, roots, anchors, anchorCount, contacts, contactCount, maximumIterations, preserveMagnitude, useConjugate, trialDirections, seedPositions, seedGradients, seedMerit, clear, seedAnchors, error))
            {
                return false;
            }
            if (!clear && (!useCoupled || coupledProgressed) && isfinite(seedMerit) && seedMerit >= 0.0 && seedMerit < DBL_MAX)
            {
                // 余白あり・なしの侵入量を混ぜず、同じ評価方法どうしで最良姿勢を選ぶ。
                auto& closestDirections = useCoupled ? closestCoupledDirections : closestAttemptDirections;
                double& closestMerit = useCoupled ? closestCoupledMerit : closestAttemptMerit;
                if (seedMerit < closestMerit)
                {
                    // コピーを完成させてから保存し、確保に失敗した場合は入力位置を保つ。
                    gk::Array<gk::FVector3d> savedDirections;
                    if (!savedDirections.AppendRange(trialDirections.Data(), trialDirections.Count()))
                    {
                        error.Assign("Secondary motion weighted handoff direction allocation failed");
                        return false;
                    }
                    closestDirections.MoveFrom(savedDirections);
                    closestMerit = seedMerit;
                }
            }
            if (clear)
            {
                bool preservedClearance = false;
                if (!ContractClearCandidate(chains, chainCount, references, roots, directions, positions, anchors, anchorCount, contacts, contactCount, trialDirections, seedPositions, seedGradients, seedAnchors, preservedClearance, error))
                {
                    return false;
                }
                if (!preservedClearance)
                {
                    continue;
                }
                const double candidateDisplacementSquared = PositionDisplacementSquared(positions, seedPositions);
                if (!isfinite(candidateDisplacementSquared))
                {
                    error.Assign("Secondary motion weighted candidate displacement is invalid");
                    return false;
                }
                if (candidateDisplacementSquared < bestDisplacementSquared)
                {
                    bestDisplacementSquared = candidateDisplacementSquared;
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
                foundClear = true;
                if (firstClearSeedIndex == UINT32_MAX)
                {
                    firstClearSeedIndex = seedIndex;
                }
            }
        }
        if (foundClear)
        {
            break;
        }
    }
    if (!foundClear && !RefineOwnerPrefixFallback(chains, chainCount, references, referenceCount, roots, anchors, anchorCount, contacts, contactCount, maximumIterations, directions, closestAttemptDirections, closestCoupledDirections, positions, bestPositions, bestDisplacementSquared, foundClear, error))
    {
        return false;
    }
    if (!foundClear)
    {
        // 等式の侵入二乗和で停滞した場合だけ、clear行も保つ片側制約で補助探索する。
        const gk::Array<gk::FVector3d>* base = &directions;
        if (referenceCount > 0u && closestCoupledDirections.Count() == referenceCount)
        {
            base = &closestCoupledDirections;
        }
        else if (referenceCount > 0u && closestAttemptDirections.Count() == referenceCount)
        {
            base = &closestAttemptDirections;
        }
        gk::Array<gk::FVector3d> minimaxPositions;
        bool minimaxClear = false;
        if (!RefineSecondaryMotionContactMinimax(chains, chainCount, references, referenceCount, anchors, anchorCount, contacts, contactCount, roots, *base, minimaxPositions, minimaxClear, error, maximumIterations))
        {
            return false;
        }
        if (minimaxClear)
        {
            gk::Array<gk::FVector3d> minimaxDirections;
            for (uint32_t chainIndex = 0u; chainIndex < chainCount; ++chainIndex)
            {
                const auto& chain = chains[chainIndex];
                for (uint32_t segment = 0u; segment < chain.segmentCount; ++segment)
                {
                    const uint32_t parent = chain.firstPoint + segment;
                    auto direction = Subtract(minimaxPositions.At(parent + 1u), minimaxPositions.At(parent));
                    if (!Normalize(direction) || !minimaxDirections.Append(direction))
                    {
                        error.Assign("Secondary motion minimax direction allocation or value is invalid");
                        return false;
                    }
                }
            }
            gk::Array<gk::FVector3d> minimaxGradients;
            gk::Array<FWeightedAnchorEvaluation> minimaxAnchors;
            bool preserved = false;
            if (!ContractClearCandidate(chains, chainCount, references, roots, directions, positions, anchors, anchorCount, contacts, contactCount, minimaxDirections, minimaxPositions, minimaxGradients, minimaxAnchors, preserved, error))
            {
                return false;
            }
            if (preserved)
            {
                bestPositions.MoveFrom(minimaxPositions);
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
