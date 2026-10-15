// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionAttachments.h"
#include "model/animation/ModelSecondaryMotionCollision.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

namespace gk::model::animation
{
namespace
{

// 1回の接触投影で許す形状走査回数。
const uint32_t kAttachmentProjectionPassCount = 64;
// 接触許容幅に対する微小な外向き余白の割合。
const double kAttachmentProjectionGuardFraction = 1.0e-3;
// 外向き余白の絶対下限。
const double kAttachmentProjectionGuardMinimum = 1.0e-7;
#if defined(GKCORE_TESTING)
// 現在の検査threadで、位置だけの評価と骨に固定した接触点の微分評価を区別する回数。
thread_local uint64_t attachmentJacobianCountForTesting = 0u;
#endif
/**
 * 倍精度座標の有限性を調べる。
 */
bool IsFinite(const gk::FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * 3次元ベクトルの内積を計算する。
 */
double Dot(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return first.value[0] * second.value[0] + first.value[1] * second.value[1] + first.value[2] * second.value[2];
}

/**
 * 3次元ベクトルの外積を計算する。
 */
gk::FVector3d Cross(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return { { first.value[1] * second.value[2] - first.value[2] * second.value[1], first.value[2] * second.value[0] - first.value[0] * second.value[2], first.value[0] * second.value[1] - first.value[1] * second.value[0] } };
}

/**
 * 3次元ベクトルの差を計算する。
 */
gk::FVector3d Subtract(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return { { first.value[0] - second.value[0], first.value[1] - second.value[1], first.value[2] - second.value[2] } };
}

/**
 * 3次元ベクトルを係数で拡大する。
 */
gk::FVector3d Multiply(const gk::FVector3d& value, double scale)
{
    return { { value.value[0] * scale, value.value[1] * scale, value.value[2] * scale } };
}

/**
 * 3次元ベクトルを加算する。
 */
gk::FVector3d Add(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return { { first.value[0] + second.value[0], first.value[1] + second.value[1], first.value[2] + second.value[2] } };
}

/**
 * 3次元ベクトルの桁あふれを避けて長さを求める。
 */
double Length(const gk::FVector3d& value)
{
    // 二乗前に使う最大成分。
    const double largest = fmax(fabs(value.value[0]), fmax(fabs(value.value[1]), fabs(value.value[2])));
    if (!(largest > 0.0) || !isfinite(largest))
        return largest;
    // 最大成分で縮小したベクトル。
    const double x = value.value[0] / largest;
    const double y = value.value[1] / largest;
    const double z = value.value[2] / largest;
    return largest * sqrt(x * x + y * y + z * z);
}

/**
 * 有限な3次元ベクトルを単位長へ正規化する。
 */
bool Normalize(gk::FVector3d& value)
{
    // 正規化前の桁あふれを避けた長さ。
    const double length = Length(value);
    if (!(length > 0.0) || !isfinite(length))
        return false;
    value = Multiply(value, 1.0 / length);
    return IsFinite(value);
}

/**
 * 最小回転でoffsetを運び、同時に子位置に対する微分を計算する。
 */
bool EvaluateRotatedOffset(const gk::FVector3d& unitBase, const gk::FVector3d& unitCurrent, double segmentLength, const gk::FVector3d& offset, gk::FVector3d& rotatedOffset, double jacobian[9])
{
    // 最小回転を表す四元数のベクトル部と実部。
    const gk::FVector3d quaternionVector = Cross(unitBase, unitCurrent);
    const double scalar = 1.0 + fmax(-1.0, fmin(1.0, Dot(unitBase, unitCurrent)));
    if (!(scalar > 1.0e-12) || !isfinite(scalar))
        return false;
    // 四元数正規化に使う分母。
    const double denominator = sqrt(2.0 * scalar);
    const gk::FVector3d q = Multiply(quaternionVector, 1.0 / denominator);
    const double w = scalar / denominator;
    // 四元数回転の中間外積。
    const gk::FVector3d firstCross = Cross(q, offset);
    rotatedOffset = Add(offset, Add(Multiply(firstCross, 2.0 * w), Multiply(Cross(q, firstCross), 2.0)));
    if (!IsFinite(rotatedOffset))
        return false;

    if (!jacobian)
    {
        // 単位四元数の微分と積和の項数から、中間式が桁あふれしない範囲だけ微分を省く。
        const double offsetMagnitude = fmax(fabs(offset.value[0]), fmax(fabs(offset.value[1]), fabs(offset.value[2])));
        const double inverseLength = 1.0 / segmentLength;
        const double derivativeScale = offsetMagnitude * inverseLength;
        const double quaternionDerivativeBound = 1.0 / denominator + 2.0 / (denominator * denominator * denominator);
        const double safeDerivativeScale = DBL_MAX / (256.0 * fmax(1.0, quaternionDerivativeBound));
        if (isfinite(inverseLength) && inverseLength <= DBL_MAX * 0.5 && isfinite(derivativeScale) && derivativeScale <= safeDerivativeScale)
        {
            return true;
        }
        // 極小線分では元の数値検査を行い、位置だけ成立する候補で既存の失敗を隠さない。
        double checkedJacobian[9]{};
        return EvaluateRotatedOffset(unitBase, unitCurrent, segmentLength, offset, rotatedOffset, checkedJacobian);
    }

    // 方向軸ごとの四元数微分を出力軸と子軸の計算から分離する。
#if defined(GKCORE_TESTING)
    ++attachmentJacobianCountForTesting;
#endif
    gk::FVector3d derivativeOffsets[3]{};
    for (uint32_t directionAxis = 0; directionAxis < 3; ++directionAxis)
    {
        gk::FVector3d basis = {};
        basis.value[directionAxis] = 1.0;
        const gk::FVector3d derivativeCross = Cross(unitBase, basis);
        const double derivativeScalar = unitBase.value[directionAxis];
        const gk::FVector3d derivativeQ = Subtract(Multiply(derivativeCross, 1.0 / denominator), Multiply(quaternionVector, derivativeScalar / (denominator * denominator * denominator)));
        const double derivativeW = derivativeScalar * scalar / (denominator * denominator * denominator);
        derivativeOffsets[directionAxis] = Add(Multiply(firstCross, 2.0 * derivativeW), Add(Multiply(Cross(derivativeQ, offset), 2.0 * w), Multiply(Add(Cross(derivativeQ, firstCross), Cross(q, Cross(derivativeQ, offset))), 2.0)));
    }
    // 行優先ヤコビアンの各和の順序を保って積算する。
    for (uint32_t outputAxis = 0; outputAxis < 3; ++outputAxis)
    {
        for (uint32_t childAxis = 0; childAxis < 3; ++childAxis)
        {
            double derivative = 0.0;
            for (uint32_t directionAxis = 0; directionAxis < 3; ++directionAxis)
            {
                const double directionDerivative = ((directionAxis == childAxis ? 1.0 : 0.0) - unitCurrent.value[directionAxis] * unitCurrent.value[childAxis]) / segmentLength;
                derivative += derivativeOffsets[directionAxis].value[outputAxis] * directionDerivative;
            }
            jacobian[outputAxis * 3 + childAxis] = derivative;
        }
    }
    for (uint32_t index = 0; index < 9; ++index)
    {
        if (!isfinite(jacobian[index]))
            return false;
    }
    return true;
}

/**
 * 子骨の補正勾配を許容曲げ角の外側へ向けないよう制限する。
 */
bool RestrictGradientToCone(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child, gk::FVector3d& gradient)
{
    if (definition.cosineLimit <= -1.0)
        return true;
    // 基準方向と現在の骨方向。
    gk::FVector3d reference = definition.direction;
    gk::FVector3d current = Subtract(child, parent);
    const double length = Length(current);
    if (!Normalize(reference) || !(length > 0.0) || !Normalize(current))
        return false;
    const double cosine = Dot(reference, current);
    if (cosine > definition.cosineLimit + 64.0 * FLT_EPSILON)
        return true;
    // 曲げ角境界の内側へ進む成分を除く接線勾配。
    const gk::FVector3d boundaryGradient = Subtract(reference, Multiply(current, cosine));
    const double boundaryLengthSquared = Dot(boundaryGradient, boundaryGradient);
    if (!(boundaryLengthSquared > DBL_EPSILON * 64.0))
        return true;
    const double inwardAmount = Dot(gradient, boundaryGradient);
    if (inwardAmount < 0.0)
        gradient = Subtract(gradient, Multiply(boundaryGradient, inwardAmount / boundaryLengthSquared));
    return IsFinite(gradient);
}

/**
 * 子位置を元の長さへ戻し、許容曲げ角の円錐内へ収める。
 */
bool RestoreAttachmentConstraint(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, double originalLength, gk::FVector3d& child)
{
    // 現在の親子方向と基準単位方向。
    gk::FVector3d direction = Subtract(child, parent);
    gk::FVector3d reference = definition.direction;
    if (!(originalLength > 0.0) || !Normalize(direction) || !Normalize(reference))
        return false;
    const double cosine = fmax(-1.0, fmin(1.0, Dot(reference, direction)));
    if (cosine < definition.cosineLimit)
    {
        // 許容円錐の境界へ向ける接線方向。
        gk::FVector3d tangent = Subtract(direction, Multiply(reference, cosine));
        if (!Normalize(tangent))
        {
            tangent = { { 1.0, 0.0, 0.0 } };
            tangent = Subtract(tangent, Multiply(reference, Dot(tangent, reference)));
            if (!Normalize(tangent))
            {
                tangent = { { 0.0, 1.0, 0.0 } };
                tangent = Subtract(tangent, Multiply(reference, Dot(tangent, reference)));
            }
            if (!Normalize(tangent))
                return false;
        }
        direction = Add(Multiply(reference, definition.cosineLimit), Multiply(tangent, sqrt(fmax(0.0, 1.0 - definition.cosineLimit * definition.cosineLimit))));
    }
    child = Add(parent, Multiply(direction, originalLength));
    return IsFinite(child);
}

/**
 * 子位置が元の骨長と許容角を保つか調べる。
 */
bool MatchesAttachmentConstraint(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, double originalLength, const gk::FVector3d& child)
{
    // 補正後の骨方向と長さ。
    const gk::FVector3d segment = Subtract(child, parent);
    const double length = Length(segment);
    gk::FVector3d reference = definition.direction;
    gk::FVector3d direction = segment;
    if (!(length > 0.0) || !isfinite(length) || !Normalize(reference) || !Normalize(direction))
        return false;
    const double lengthTolerance = fmax(1.0e-10, originalLength * 1.0e-10);
    return fabs(length - originalLength) <= lengthTolerance && Dot(reference, direction) + 1.0e-10 >= definition.cosineLimit;
}

/**
 * 接触点の位置と子位置ヤコビアンから法線方向の子勾配を作る。
 */
gk::FVector3d AttachmentGradient(const FSecondaryMotionAttachmentEvaluation& evaluation, const gk::FVector3d& normal, double weight)
{
    // 接触点法線を子位置空間へ写す勾配。
    gk::FVector3d gradient = {};
    for (uint32_t childAxis = 0; childAxis < 3; ++childAxis)
    {
        for (uint32_t pointAxis = 0; pointAxis < 3; ++pointAxis)
            gradient.value[childAxis] += evaluation.childJacobian[pointAxis * 3 + childAxis] * normal.value[pointAxis] * weight;
    }
    return gradient;
}

/**
 * 接触点が有限で、float骨位置へ保存できる範囲か調べる。
 */
bool IsFloatPosition(const gk::FVector3d& value)
{
    return IsFinite(value) && fabs(value.value[0]) <= FLT_MAX && fabs(value.value[1]) <= FLT_MAX && fabs(value.value[2]) <= FLT_MAX;
}

/**
 * 骨の子位置を変えたときの接触点誤差を上から見積もる。
 */
double JacobianFrobeniusNorm(const FSecondaryMotionAttachmentEvaluation& evaluation)
{
    double squared = 0.0;
    for (uint32_t index = 0; index < 9; ++index)
    {
        squared += evaluation.childJacobian[index] * evaluation.childJacobian[index];
    }
    return sqrt(squared);
}

/**
 * 子位置と接触点をfloatへ戻す丸めに備えた、接触の外向き余白を求める。
 */
double AttachmentProjectionGuard(const FPreparedSecondaryMotionCollisionShape& shape, const gk::FVector3d candidate[4], const FSecondaryMotionAttachmentEvaluation& firstEvaluation, const FSecondaryMotionAttachmentEvaluation& secondEvaluation, double spanAmount)
{
    // 親、子、接触端点、身体形状の絶対座標尺度。
    double coordinateScale = 0.0;
    for (uint32_t point = 0; point < 4; ++point)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            coordinateScale = fmax(coordinateScale, fabs(candidate[point].value[axis]));
        }
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        coordinateScale = fmax(coordinateScale, fabs(firstEvaluation.position.value[axis]));
        coordinateScale = fmax(coordinateScale, fabs(secondEvaluation.position.value[axis]));
        coordinateScale = fmax(coordinateScale, fabs(shape.start.value[axis]));
        coordinateScale = fmax(coordinateScale, fabs(shape.end.value[axis]));
    }
    // 線分上の最近点における、二つの子位置丸め誤差の上限。
    const double firstWeight = 1.0 - spanAmount;
    const double secondWeight = spanAmount;
    const double jacobianBound = sqrt(3.0) * FLT_EPSILON * coordinateScale * (firstWeight * JacobianFrobeniusNorm(firstEvaluation) + secondWeight * JacobianFrobeniusNorm(secondEvaluation));
    return fmax(kAttachmentProjectionGuardMinimum, fmax(shape.radius * 1.0e-6, fmax(shape.tolerance * kAttachmentProjectionGuardFraction, fmax(coordinateScale * FLT_EPSILON, jacobianBound))));
}

/**
 * 現在方向に直交する、再現可能な二つの単位接線を作る。
 */
bool BuildAttachmentTangentBasis(const gk::FVector3d& parent, const gk::FVector3d& child, gk::FVector3d& first, gk::FVector3d& second)
{
    // 現在の親子方向。
    gk::FVector3d direction = Subtract(child, parent);
    if (!Normalize(direction))
    {
        return false;
    }
    // 方向との内積が最小になる座標軸を選ぶ。
    uint32_t axis = 0;
    if (fabs(direction.value[1]) < fabs(direction.value[axis]))
    {
        axis = 1;
    }
    if (fabs(direction.value[2]) < fabs(direction.value[axis]))
    {
        axis = 2;
    }
    gk::FVector3d reference = {};
    reference.value[axis] = 1.0;
    first = Cross(direction, reference);
    if (!Normalize(first))
    {
        return false;
    }
    second = Cross(direction, first);
    return Normalize(second);
}

/**
 * 1本の接触shapeで、候補端点の実際の侵入量を評価する。
 */
bool AttachmentPenetration(const FPreparedSecondaryMotionCollisionShape& shape, const FSecondaryMotionAttachmentDefinition& firstDefinition, const FSecondaryMotionAttachmentDefinition& secondDefinition, const gk::FVector3d candidate[4], double& penetration, gk::String& error)
{
    // 現在の二つの骨接触点。
    FSecondaryMotionAttachmentEvaluation firstEvaluation{};
    FSecondaryMotionAttachmentEvaluation secondEvaluation{};
    if (!EvaluateSecondaryMotionAttachment(firstDefinition, candidate[0], candidate[1], firstEvaluation, error) || !EvaluateSecondaryMotionAttachment(secondDefinition, candidate[2], candidate[3], secondEvaluation, error))
    {
        return false;
    }
    double spanAmount = 0.0;
    gk::FVector3d normal{};
    return QueryPreparedSecondaryMotionFreeSegmentContact(shape, firstEvaluation.position, secondEvaluation.position, penetration, spanAmount, normal, error, 0.5);
}

/**
 * 全接触形状に対する最大侵入量と二乗和を評価する。
 */
bool EvaluateAttachmentMerit(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionAttachmentDefinition& firstDefinition, const FSecondaryMotionAttachmentDefinition& secondDefinition, const gk::FVector3d candidate[4], double& maximumPenetration, double& squaredPenetrationSum, gk::String& error)
{
    maximumPenetration = 0.0;
    squaredPenetrationSum = 0.0;
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        double penetration = 0.0;
        if (!AttachmentPenetration(shapes[shapeIndex], firstDefinition, secondDefinition, candidate, penetration, error))
        {
            return false;
        }
        const double positivePenetration = fmax(0.0, penetration);
        maximumPenetration = fmax(maximumPenetration, positivePenetration);
        squaredPenetrationSum += positivePenetration * positivePenetration;
    }
    if (!isfinite(maximumPenetration) || !isfinite(squaredPenetrationSum))
    {
        error.Assign("Secondary motion attachment contact merit is outside the numeric range");
        return false;
    }
    return true;
}

/**
 * 最大侵入量を優先し、同値なら侵入の二乗和が小さい候補を選ぶ。
 */
bool AttachmentMeritImproves(double candidateMaximum, double candidateSum, double referenceMaximum, double referenceSum)
{
    return candidateMaximum < referenceMaximum || (candidateMaximum == referenceMaximum && candidateSum < referenceSum);
}

/**
 * 接触補正を縮小率だけ適用し、二本の骨長と角度を復元する。
 */
bool BuildRestoredAttachmentTrial(const FSecondaryMotionAttachmentDefinition& firstDefinition, const FSecondaryMotionAttachmentDefinition& secondDefinition, const gk::FVector3d candidate[4], const gk::FVector3d& firstDelta, const gk::FVector3d& secondDelta, double firstLength, double secondLength, double scale, bool sharedChild, gk::FVector3d trial[4])
{
    for (uint32_t point = 0; point < 4; ++point)
    {
        trial[point] = candidate[point];
    }
    trial[1] = Add(trial[1], Multiply(firstDelta, scale));
    if (sharedChild)
    {
        trial[3] = trial[1];
    }
    else
    {
        trial[3] = Add(trial[3], Multiply(secondDelta, scale));
    }
    if (!RestoreAttachmentConstraint(firstDefinition, trial[0], firstLength, trial[1]))
    {
        return false;
    }
    if (sharedChild)
    {
        trial[3] = trial[1];
        if (!RestoreAttachmentConstraint(secondDefinition, trial[2], secondLength, trial[3]))
        {
            return false;
        }
        trial[1] = trial[3];
    }
    else if (!RestoreAttachmentConstraint(secondDefinition, trial[2], secondLength, trial[3]))
    {
        return false;
    }
    return IsFloatPosition(trial[1]) && IsFloatPosition(trial[3]) && MatchesAttachmentConstraint(firstDefinition, trial[0], firstLength, trial[1]) && MatchesAttachmentConstraint(secondDefinition, trial[2], secondLength, trial[3]);
}

/**
 * 一次勾配が消えた接触を、制約を保つ小さな向き変更で脱出する。
 */
bool TryAttachmentGeometricEscape(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionAttachmentDefinition& firstDefinition, const FSecondaryMotionAttachmentDefinition& secondDefinition, const gk::FVector3d candidate[4], double currentMaximum, double currentSum, bool sharedChild, gk::FVector3d escaped[4], gk::String& error)
{
    // 現在の親子方向に直交する二つの決定的な接線。
    gk::FVector3d firstTangent0{}, firstTangent1{};
    if (!BuildAttachmentTangentBasis(candidate[0], candidate[1], firstTangent0, firstTangent1))
    {
        error.Assign("Secondary motion attachment escape direction is invalid");
        return false;
    }
    gk::FVector3d secondTangent0{}, secondTangent1{};
    if (!sharedChild && !BuildAttachmentTangentBasis(candidate[2], candidate[3], secondTangent0, secondTangent1))
    {
        error.Assign("Secondary motion attachment escape direction is invalid");
        return false;
    }
    // 軸方向と対角方向を含む接線候補を固定順で用意する。
    gk::FVector3d firstDirections[8] = { firstTangent0, Multiply(firstTangent0, -1.0), firstTangent1, Multiply(firstTangent1, -1.0), Add(firstTangent0, firstTangent1), Subtract(firstTangent0, firstTangent1), Add(Multiply(firstTangent0, -1.0), firstTangent1), Multiply(Add(firstTangent0, firstTangent1), -1.0) };
    gk::FVector3d secondDirections[8]{};
    if (!sharedChild)
    {
        secondDirections[0] = secondTangent0;
        secondDirections[1] = Multiply(secondTangent0, -1.0);
        secondDirections[2] = secondTangent1;
        secondDirections[3] = Multiply(secondTangent1, -1.0);
        secondDirections[4] = Add(secondTangent0, secondTangent1);
        secondDirections[5] = Subtract(secondTangent0, secondTangent1);
        secondDirections[6] = Add(Multiply(secondTangent0, -1.0), secondTangent1);
        secondDirections[7] = Multiply(Add(secondTangent0, secondTangent1), -1.0);
    }
    for (uint32_t direction = 4; direction < 8; ++direction)
    {
        if (!Normalize(firstDirections[direction]) || (!sharedChild && !Normalize(secondDirections[direction])))
        {
            error.Assign("Secondary motion attachment escape direction is invalid");
            return false;
        }
    }
    // 小さい変更から広い向きまで試し、制約復元後の改善だけ採用する。
    const double escapeAngles[4] = { 0.025, 0.075, 0.2, 0.45 };
    double bestMaximum = currentMaximum;
    double bestSum = currentSum;
    bool found = false;
    const uint32_t firstOptionCount = 9;
    const uint32_t secondOptionCount = sharedChild ? 1u : 9u;
    for (uint32_t angleIndex = 0; angleIndex < 4; ++angleIndex)
    {
        const double angle = escapeAngles[angleIndex];
        for (uint32_t firstOption = 0; firstOption < firstOptionCount; ++firstOption)
        {
            for (uint32_t secondOption = 0; secondOption < secondOptionCount; ++secondOption)
            {
                // 元の4点から独立に候補を作る。
                gk::FVector3d trial[4] = { candidate[0], candidate[1], candidate[2], candidate[3] };
                if (sharedChild)
                {
                    if (firstOption > 0)
                    {
                        gk::FVector3d direction = Subtract(candidate[1], candidate[0]);
                        const double length = Length(direction);
                        if (!Normalize(direction))
                        {
                            continue;
                        }
                        direction = Add(Multiply(direction, cos(angle)), Multiply(firstDirections[firstOption - 1u], sin(angle)));
                        trial[1] = Add(candidate[0], Multiply(direction, length));
                        if (!RestoreAttachmentConstraint(firstDefinition, trial[0], length, trial[1]))
                        {
                            continue;
                        }
                        trial[3] = trial[1];
                        if (!RestoreAttachmentConstraint(secondDefinition, trial[2], Length(Subtract(candidate[3], candidate[2])), trial[3]))
                        {
                            continue;
                        }
                        trial[1] = trial[3];
                    }
                }
                else
                {
                    if (firstOption > 0)
                    {
                        gk::FVector3d direction = Subtract(candidate[1], candidate[0]);
                        const double length = Length(direction);
                        if (!Normalize(direction))
                        {
                            continue;
                        }
                        direction = Add(Multiply(direction, cos(angle)), Multiply(firstDirections[firstOption - 1u], sin(angle)));
                        trial[1] = Add(candidate[0], Multiply(direction, length));
                        if (!RestoreAttachmentConstraint(firstDefinition, trial[0], length, trial[1]))
                        {
                            continue;
                        }
                    }
                    if (secondOption > 0)
                    {
                        gk::FVector3d direction = Subtract(candidate[3], candidate[2]);
                        const double length = Length(direction);
                        if (!Normalize(direction))
                        {
                            continue;
                        }
                        direction = Add(Multiply(direction, cos(angle)), Multiply(secondDirections[secondOption - 1u], sin(angle)));
                        trial[3] = Add(candidate[2], Multiply(direction, length));
                        if (!RestoreAttachmentConstraint(secondDefinition, trial[2], length, trial[3]))
                        {
                            continue;
                        }
                    }
                }
                if (!IsFloatPosition(trial[1]) || !IsFloatPosition(trial[3]) || !MatchesAttachmentConstraint(firstDefinition, trial[0], Length(Subtract(candidate[1], candidate[0])), trial[1]) || !MatchesAttachmentConstraint(secondDefinition, trial[2], Length(Subtract(candidate[3], candidate[2])), trial[3]))
                {
                    continue;
                }
                double trialMaximum = 0.0;
                double trialSum = 0.0;
                if (!EvaluateAttachmentMerit(shapes, shapeCount, firstDefinition, secondDefinition, trial, trialMaximum, trialSum, error))
                {
                    return false;
                }
                if (AttachmentMeritImproves(trialMaximum, trialSum, bestMaximum, bestSum))
                {
                    bestMaximum = trialMaximum;
                    bestSum = trialSum;
                    for (uint32_t point = 0; point < 4; ++point)
                    {
                        escaped[point] = trial[point];
                    }
                    found = true;
                }
            }
        }
    }
    if (!found)
    {
        error.Assign("Secondary motion attachment contact has no improving geometric escape");
        return false;
    }
    error.Clear();
    return true;
}

/**
 * 局所escapeが停滞した場合に、許容coneの決定的な方向格子を調べる。
 */
bool TryAttachmentConeGridEscape(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionAttachmentDefinition& firstDefinition, const FSecondaryMotionAttachmentDefinition& secondDefinition, const gk::FVector3d candidate[4], double currentMaximum, double currentSum, bool sharedChild, gk::FVector3d escaped[4], gk::String& error)
{
    // 基準骨方向と、その周囲を走査する直交基底。
    gk::FVector3d firstBase = firstDefinition.direction;
    gk::FVector3d firstTangent0{}, firstTangent1{};
    if (!Normalize(firstBase) || !BuildAttachmentTangentBasis(gk::FVector3d{}, firstBase, firstTangent0, firstTangent1))
    {
        error.Assign("Secondary motion attachment cone search direction is invalid");
        return false;
    }
    gk::FVector3d secondBase = secondDefinition.direction;
    gk::FVector3d secondTangent0{}, secondTangent1{};
    if (!sharedChild && (!Normalize(secondBase) || !BuildAttachmentTangentBasis(gk::FVector3d{}, secondBase, secondTangent0, secondTangent1)))
    {
        error.Assign("Secondary motion attachment cone search direction is invalid");
        return false;
    }
    // Cone中心を含め、半径方向と境界方向を16方位ずつ走査する。
    gk::FVector3d firstDirections[34]{};
    gk::FVector3d secondDirections[34]{};
    firstDirections[0] = Subtract(candidate[1], candidate[0]);
    Normalize(firstDirections[0]);
    firstDirections[1] = firstBase;
    if (!sharedChild)
    {
        secondDirections[0] = Subtract(candidate[3], candidate[2]);
        Normalize(secondDirections[0]);
        secondDirections[1] = secondBase;
    }
    const double firstLimit = acos(firstDefinition.cosineLimit);
    const double secondLimit = sharedChild ? 0.0 : acos(secondDefinition.cosineLimit);
    const double ringFractions[2] = { 0.5, 1.0 };
    uint32_t directionCount = 2;
    for (uint32_t ring = 0; ring < 2; ++ring)
    {
        const double angle = firstLimit * ringFractions[ring];
        for (uint32_t azimuth = 0; azimuth < 16; ++azimuth)
        {
            const double phase = (6.283185307179586476925286766559 * azimuth) / 16.0;
            const gk::FVector3d tangent = Add(Multiply(firstTangent0, cos(phase)), Multiply(firstTangent1, sin(phase)));
            firstDirections[directionCount] = Add(Multiply(firstBase, cos(angle)), Multiply(tangent, sin(angle)));
            if (!Normalize(firstDirections[directionCount]))
            {
                error.Assign("Secondary motion attachment cone search direction is invalid");
                return false;
            }
            ++directionCount;
        }
    }
    if (!sharedChild)
    {
        directionCount = 2;
        for (uint32_t ring = 0; ring < 2; ++ring)
        {
            const double angle = secondLimit * ringFractions[ring];
            for (uint32_t azimuth = 0; azimuth < 16; ++azimuth)
            {
                const double phase = (6.283185307179586476925286766559 * azimuth) / 16.0;
                const gk::FVector3d tangent = Add(Multiply(secondTangent0, cos(phase)), Multiply(secondTangent1, sin(phase)));
                secondDirections[directionCount] = Add(Multiply(secondBase, cos(angle)), Multiply(tangent, sin(angle)));
                if (!Normalize(secondDirections[directionCount]))
                {
                    error.Assign("Secondary motion attachment cone search direction is invalid");
                    return false;
                }
                ++directionCount;
            }
        }
    }
    // 現候補を基準にし、clear候補同士なら移動量の小さい方を選ぶ。
    double bestMaximum = currentMaximum;
    double bestSum = currentSum;
    double bestMovement = 0.0;
    bool found = false;
    const uint32_t firstOptionCount = 34u;
    const uint32_t secondOptionCount = sharedChild ? 1u : 34u;
    const double firstLength = Length(Subtract(candidate[1], candidate[0]));
    const double secondLength = Length(Subtract(candidate[3], candidate[2]));
    for (uint32_t firstOption = 0; firstOption < firstOptionCount; ++firstOption)
    {
        for (uint32_t secondOption = 0; secondOption < secondOptionCount; ++secondOption)
        {
            gk::FVector3d trial[4] = { candidate[0], candidate[1], candidate[2], candidate[3] };
            trial[1] = Add(trial[0], Multiply(firstDirections[firstOption], firstLength));
            if (!RestoreAttachmentConstraint(firstDefinition, trial[0], firstLength, trial[1]))
            {
                continue;
            }
            if (sharedChild)
            {
                trial[3] = trial[1];
                if (!RestoreAttachmentConstraint(secondDefinition, trial[2], secondLength, trial[3]))
                {
                    continue;
                }
                trial[1] = trial[3];
            }
            else
            {
                trial[3] = Add(trial[2], Multiply(secondDirections[secondOption], secondLength));
                if (!RestoreAttachmentConstraint(secondDefinition, trial[2], secondLength, trial[3]))
                {
                    continue;
                }
            }
            if (!IsFloatPosition(trial[1]) || !IsFloatPosition(trial[3]) || !MatchesAttachmentConstraint(firstDefinition, trial[0], firstLength, trial[1]) || !MatchesAttachmentConstraint(secondDefinition, trial[2], secondLength, trial[3]))
            {
                continue;
            }
            double trialMaximum = 0.0;
            double trialSum = 0.0;
            // 180度近傍の回転特異候補は格子探索から除外する。
            gk::String trialError;
            if (!EvaluateAttachmentMerit(shapes, shapeCount, firstDefinition, secondDefinition, trial, trialMaximum, trialSum, trialError))
            {
                continue;
            }
            const gk::FVector3d firstMove = Subtract(trial[1], candidate[1]);
            const gk::FVector3d secondMove = sharedChild ? gk::FVector3d{} : Subtract(trial[3], candidate[3]);
            const double movement = Dot(firstMove, firstMove) + Dot(secondMove, secondMove);
            const bool improves = AttachmentMeritImproves(trialMaximum, trialSum, bestMaximum, bestSum);
            const bool sameClearanceSmallerMove = trialMaximum == 0.0 && bestMaximum == 0.0 && trialSum == bestSum && movement < bestMovement;
            if (improves || sameClearanceSmallerMove)
            {
                bestMaximum = trialMaximum;
                bestSum = trialSum;
                bestMovement = movement;
                for (uint32_t point = 0; point < 4; ++point)
                {
                    escaped[point] = trial[point];
                }
                found = true;
            }
        }
    }
    if (!found)
    {
        error.Assign("Secondary motion attachment contact has no improving cone direction");
        return false;
    }
    error.Clear();
    return true;
}

}

/**
 * 共通の入力検査と回転計算を使い、必要な場合だけ位置微分も計算する。
 */
static bool EvaluateAttachment(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child, FSecondaryMotionAttachmentEvaluation& evaluation, gk::String& error, bool calculateJacobian)
{
    error.Clear();
    if (!IsFloatPosition(parent) || !IsFloatPosition(child) || !IsFinite(definition.direction) || !IsFinite(definition.offset) || !isfinite(definition.cosineLimit) || definition.cosineLimit < -1.0 || definition.cosineLimit > 1.0)
    {
        error.Assign("Secondary motion attachment input is invalid");
        return false;
    }
    // 基準方向と現在の親子方向。
    gk::FVector3d baseDirection = definition.direction;
    const gk::FVector3d segment = Subtract(child, parent);
    const double segmentLength = Length(segment);
    gk::FVector3d currentDirection = segment;
    if (!(segmentLength > 0.0) || !isfinite(segmentLength) || !Normalize(baseDirection) || !Normalize(currentDirection))
    {
        error.Assign("Secondary motion attachment has a zero-length direction");
        return false;
    }
    // 回転済み局所offsetと行優先微分行列。
    gk::FVector3d rotatedOffset{};
    FSecondaryMotionAttachmentEvaluation candidate{};
    if (!EvaluateRotatedOffset(baseDirection, currentDirection, segmentLength, definition.offset, rotatedOffset, calculateJacobian ? candidate.childJacobian : nullptr))
    {
        error.Assign("Secondary motion attachment rotation is singular");
        return false;
    }
    candidate.position = Add(parent, rotatedOffset);
    if (!IsFloatPosition(candidate.position))
    {
        error.Assign("Secondary motion attachment position is outside the float range");
        return false;
    }
    evaluation = candidate;
    error.Clear();
    return true;
}

bool EvaluateSecondaryMotionAttachment(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child, FSecondaryMotionAttachmentEvaluation& evaluation, gk::String& error)
{
    return EvaluateAttachment(definition, parent, child, evaluation, error, true);
}

bool EvaluateSecondaryMotionAttachmentPosition(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child, gk::FVector3d& position, gk::String& error)
{
    FSecondaryMotionAttachmentEvaluation evaluation{};
    if (!EvaluateAttachment(definition, parent, child, evaluation, error, false))
    {
        return false;
    }
    position = evaluation.position;
    return true;
}

#if defined(GKCORE_TESTING)
void ResetSecondaryMotionAttachmentJacobianCountForTesting()
{
    attachmentJacobianCountForTesting = 0u;
}

uint64_t GetSecondaryMotionAttachmentJacobianCountForTesting()
{
    return attachmentJacobianCountForTesting;
}
#endif

bool ProjectSecondaryMotionAttachmentContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionAttachmentDefinition& firstDefinition, const FSecondaryMotionAttachmentDefinition& secondDefinition, gk::FVector3d* const points[4], gk::String& error)
{
    error.Clear();
    if (shapeCount > 64 || (shapeCount > 0 && !shapes) || !points)
    {
        error.Assign("Secondary motion attachment projection input is invalid");
        return false;
    }
    if (!points[0] || !points[1] || !points[2] || !points[3])
    {
        error.Assign("Secondary motion attachment point is missing");
        return false;
    }
    // 全位置を候補へ複写し、全shape成功後にだけ書き戻す。
    gk::FVector3d candidate[4] = { *points[0], *points[1], *points[2], *points[3] };
    for (uint32_t index = 0; index < 4; ++index)
    {
        if (!IsFloatPosition(candidate[index]))
        {
            error.Assign("Secondary motion attachment point is outside the float range");
            return false;
        }
    }
    if (!isfinite(firstDefinition.cosineLimit) || firstDefinition.cosineLimit < -1.0 || firstDefinition.cosineLimit > 1.0 || !isfinite(secondDefinition.cosineLimit) || secondDefinition.cosineLimit < -1.0 || secondDefinition.cosineLimit > 1.0 || !IsFinite(firstDefinition.direction) || !IsFinite(secondDefinition.direction) || !IsFinite(firstDefinition.offset) || !IsFinite(secondDefinition.offset))
    {
        error.Assign("Secondary motion attachment definition is invalid");
        return false;
    }
    // 各骨長は投影中に一定に保つ。
    const double firstLength = Length(Subtract(candidate[1], candidate[0]));
    const double secondLength = Length(Subtract(candidate[3], candidate[2]));
    if (!(firstLength > 0.0) || !(secondLength > 0.0) || !isfinite(firstLength) || !isfinite(secondLength))
    {
        error.Assign("Secondary motion attachment has a zero-length bone");
        return false;
    }
    // 同じ子位置を指す場合は一つの自由変数として補正する。
    const bool sharedChild = points[1] == points[3];
    if ((points[1] == points[0]) || (points[1] == points[2]) || (points[3] == points[0]) || (points[3] == points[2]))
    {
        error.Assign("Secondary motion attachment mutable point aliases a fixed pivot");
        return false;
    }
    if (sharedChild)
        candidate[3] = candidate[1];
    if (shapeCount == 0)
    {
        error.Clear();
        return true;
    }

    for (uint32_t pass = 0; pass < kAttachmentProjectionPassCount; ++pass)
    {
        bool allClear = true;
        for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
        {
            // 現在の二つの骨接触点とその子位置微分。
            FSecondaryMotionAttachmentEvaluation firstEvaluation{};
            FSecondaryMotionAttachmentEvaluation secondEvaluation{};
            if (!EvaluateSecondaryMotionAttachment(firstDefinition, candidate[0], candidate[1], firstEvaluation, error) || !EvaluateSecondaryMotionAttachment(secondDefinition, candidate[2], candidate[3], secondEvaluation, error))
                return false;
            double penetration = 0.0;
            double spanAmount = 0.0;
            gk::FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(shapes[shapeIndex], firstEvaluation.position, secondEvaluation.position, penetration, spanAmount, normal, error, 0.5))
                return false;
            if (!(penetration > 0.0))
                continue;
            allClear = false;
            // 線分最近位置における二接触点の寄与率。
            const double firstWeight = 1.0 - spanAmount;
            const double secondWeight = spanAmount;
            gk::FVector3d firstGradient = AttachmentGradient(firstEvaluation, normal, firstWeight);
            gk::FVector3d secondGradient = AttachmentGradient(secondEvaluation, normal, secondWeight);
            if (!RestrictGradientToCone(firstDefinition, candidate[0], candidate[1], firstGradient) || !RestrictGradientToCone(secondDefinition, candidate[2], candidate[3], secondGradient))
            {
                error.Assign("Secondary motion attachment cone gradient is invalid");
                return false;
            }
            if (sharedChild)
            {
                firstGradient = Add(firstGradient, secondGradient);
                secondGradient = firstGradient;
            }
            const double denominator = Dot(firstGradient, firstGradient) + (sharedChild ? 0.0 : Dot(secondGradient, secondGradient));
            if (!isfinite(denominator))
            {
                error.Assign("Secondary motion attachment contact gradient is invalid");
                return false;
            }
            double currentMaximum = 0.0;
            double currentSum = 0.0;
            if (!EvaluateAttachmentMerit(shapes, shapeCount, firstDefinition, secondDefinition, candidate, currentMaximum, currentSum, error))
            {
                return false;
            }
            bool accepted = false;
            if (denominator > DBL_EPSILON * 64.0)
            {
                // float保存と、骨回転を経た接触点移動に対する外向き余白。
                const double guard = AttachmentProjectionGuard(shapes[shapeIndex], candidate, firstEvaluation, secondEvaluation, spanAmount);
                const double lambda = (penetration + guard) / denominator;
                if (!isfinite(guard) || !isfinite(lambda))
                {
                    error.Assign("Secondary motion attachment correction is outside the numeric range");
                    return false;
                }
                const gk::FVector3d firstDelta = Multiply(firstGradient, lambda);
                const gk::FVector3d secondDelta = sharedChild ? gk::FVector3d{} : Multiply(secondGradient, lambda);
                // 長さと角度を復元した後、全形状の侵入量が減る縮小率だけ採用する。
                double scale = 1.0;
                for (uint32_t attempt = 0; attempt < 13; ++attempt)
                {
                    gk::FVector3d trial[4]{};
                    if (BuildRestoredAttachmentTrial(firstDefinition, secondDefinition, candidate, firstDelta, secondDelta, firstLength, secondLength, scale, sharedChild, trial))
                    {
                        double trialMaximum = 0.0;
                        double trialSum = 0.0;
                        if (!EvaluateAttachmentMerit(shapes, shapeCount, firstDefinition, secondDefinition, trial, trialMaximum, trialSum, error))
                        {
                            return false;
                        }
                        if (AttachmentMeritImproves(trialMaximum, trialSum, currentMaximum, currentSum))
                        {
                            candidate[1] = trial[1];
                            candidate[3] = trial[3];
                            accepted = true;
                            break;
                        }
                    }
                    scale *= 0.5;
                }
            }
            if (!accepted)
            {
                // 一次補正が停滞した場合も、全形状を改善する向きだけ試す。
                gk::FVector3d escaped[4]{};
                gk::String escapeError;
                if (!TryAttachmentGeometricEscape(shapes, shapeCount, firstDefinition, secondDefinition, candidate, currentMaximum, currentSum, sharedChild, escaped, escapeError) && !TryAttachmentConeGridEscape(shapes, shapeCount, firstDefinition, secondDefinition, candidate, currentMaximum, currentSum, sharedChild, escaped, error))
                {
                    return false;
                }
                candidate[1] = escaped[1];
                candidate[3] = escaped[3];
            }
        }
        if (allClear)
        {
            *points[1] = candidate[1];
            if (!sharedChild)
                *points[3] = candidate[3];
            error.Clear();
            return true;
        }
        bool finalClear = true;
        for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
        {
            FSecondaryMotionAttachmentEvaluation firstEvaluation{};
            FSecondaryMotionAttachmentEvaluation secondEvaluation{};
            if (!EvaluateSecondaryMotionAttachment(firstDefinition, candidate[0], candidate[1], firstEvaluation, error) || !EvaluateSecondaryMotionAttachment(secondDefinition, candidate[2], candidate[3], secondEvaluation, error))
                return false;
            double penetration = 0.0;
            double spanAmount = 0.0;
            gk::FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(shapes[shapeIndex], firstEvaluation.position, secondEvaluation.position, penetration, spanAmount, normal, error, 0.5))
                return false;
            if (penetration > 0.0)
                finalClear = false;
        }
        if (finalClear)
        {
            *points[1] = candidate[1];
            if (!sharedChild)
                *points[3] = candidate[3];
            error.Clear();
            return true;
        }
    }
    error.Assign("Secondary motion attachment contact projection did not converge");
    return false;
}

}
