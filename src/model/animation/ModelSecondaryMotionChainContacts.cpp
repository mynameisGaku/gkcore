// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionChainContacts.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionDirection.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

// 同じ方向勾配で続ける上限反復数。
const uint32_t kMaximumChainContactIterations = 64;
// 接線方向の一歩を半減する回数。
const uint32_t kChainContactLineSearchSteps = 12;
// 局所探索後に試すcoherent tiltの方位数。
const uint32_t kChainContactSeedAzimuths = 8;
// float方向保存後も角度境界付近を接線として扱う幅。
const double kChainConeBoundaryFraction = 64.0 * FLT_EPSILON;

/**
 * 三次元座標が有限か調べる。
 */
bool IsFinite(const gk::FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * 三次元座標が有限floatの保存範囲に収まるか調べる。
 */
bool IsFloatPosition(const gk::FVector3d& value)
{
    return IsFinite(value) && fabs(value.value[0]) <= FLT_MAX && fabs(value.value[1]) <= FLT_MAX && fabs(value.value[2]) <= FLT_MAX;
}

/**
 * 3次元ベクトルの内積を計算する。
 */
double Dot(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return first.value[0] * second.value[0] + first.value[1] * second.value[1] + first.value[2] * second.value[2];
}

/**
 * 3次元ベクトルを加算する。
 */
gk::FVector3d Add(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return { { first.value[0] + second.value[0], first.value[1] + second.value[1], first.value[2] + second.value[2] } };
}

/**
 * 3次元ベクトルを減算する。
 */
gk::FVector3d Subtract(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return { { first.value[0] - second.value[0], first.value[1] - second.value[1], first.value[2] - second.value[2] } };
}

/**
 * 3次元ベクトルに係数を掛ける。
 */
gk::FVector3d Multiply(const gk::FVector3d& value, double scale)
{
    return { { value.value[0] * scale, value.value[1] * scale, value.value[2] * scale } };
}

/**
 * 最大成分で縮小して桁あふれを避けた長さを計算する。
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
 * ベクトルを単位長へ変換する。
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
 * 方向に直交する決定的な接線を作る。
 */
bool BuildPerpendicular(const gk::FVector3d& direction, gk::FVector3d& perpendicular)
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
    if (axis == 0)
    {
        perpendicular = { { 0.0, direction.value[2], -direction.value[1] } };
    }
    else if (axis == 1)
    {
        perpendicular = { { -direction.value[2], 0.0, direction.value[0] } };
    }
    else
    {
        perpendicular = { { direction.value[1], -direction.value[0], 0.0 } };
    }
    return Normalize(perpendicular);
}

/**
 * 単位方向から安定した接線基底を作る。
 */
bool BuildTangentBasis(const gk::FVector3d& direction, gk::FVector3d& first, gk::FVector3d& second)
{
    if (!BuildPerpendicular(direction, first))
    {
        return false;
    }
    second = { { direction.value[1] * first.value[2] - direction.value[2] * first.value[1], direction.value[2] * first.value[0] - direction.value[0] * first.value[2], direction.value[0] * first.value[1] - direction.value[1] * first.value[0] } };
    return Normalize(second);
}

/**
 * 接線勾配を単位球面と許容coneの接線へ射影する。
 */
bool ProjectGradientToFeasibleTangent(const gk::FVector3d& reference, double cosineLimit, const gk::FVector3d& direction, gk::FVector3d& gradient)
{
    gradient = Subtract(gradient, Multiply(direction, Dot(gradient, direction)));
    if (Dot(reference, direction) <= cosineLimit + kChainConeBoundaryFraction)
    {
        const gk::FVector3d coneNormal = Subtract(reference, Multiply(direction, Dot(reference, direction)));
        const double coneNormalSquared = Dot(coneNormal, coneNormal);
        if (coneNormalSquared > DBL_EPSILON * 64.0)
        {
            const double outward = Dot(gradient, coneNormal);
            if (outward < 0.0)
            {
                gradient = Subtract(gradient, Multiply(coneNormal, outward / coneNormalSquared));
            }
        }
    }
    return Normalize(gradient);
}

/**
 * 準備済み方向配列から固定rootと骨長を保つ位置列を作る。
 */
bool BuildPositions(const gk::FVector3d& root, const FSecondaryMotionReferenceSegment* references, uint32_t segmentCount, const gk::Array<gk::FVector3d>& directions, gk::Array<gk::FVector3d>& output)
{
    output.Clear();
    if (!output.Append(root))
    {
        return false;
    }
    gk::FVector3d parent = root;
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        const gk::FVector3d endpoint = Add(parent, Multiply(directions.At(index), references[index].length));
        if (!IsFloatPosition(endpoint) || !output.Append(endpoint))
        {
            return false;
        }
        parent = endpoint;
    }
    return true;
}

/**
 * 正の侵入量の二乗和と対応する節方向勾配を加える。
 */
bool AddPenetration(double penetration, const gk::FVector3d& normal, double directionScale, double& merit, gk::Array<gk::FVector3d>& gradients, uint32_t segmentIndex, const FSecondaryMotionReferenceSegment* references, bool includeCurrent)
{
    if (!(penetration > 0.0))
    {
        return true;
    }
    merit += penetration * penetration;
    if (!isfinite(merit))
    {
        return false;
    }
    const uint32_t fullSegments = includeCurrent ? segmentIndex + 1u : segmentIndex;
    for (uint32_t index = 0; index < fullSegments; ++index)
    {
        const double scale = references[index].length * penetration;
        gradients.At(index) = Add(gradients.At(index), Multiply(normal, scale));
    }
    if (!includeCurrent && segmentIndex < gradients.Count())
    {
        const double scale = references[segmentIndex].length * penetration * directionScale;
        gradients.At(segmentIndex) = Add(gradients.At(segmentIndex), Multiply(normal, scale));
    }
    return true;
}

/**
 * 位置列の全点・線分接触と鎖方向への勾配を評価する。
 */
bool EvaluateChain(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionReferenceSegment* references, uint32_t segmentCount, double toleranceFraction, const gk::Array<gk::FVector3d>& positions, double& merit, gk::Array<gk::FVector3d>& gradients, gk::String& error)
{
    merit = 0.0;
    gradients.Clear();
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        if (!gradients.Append(gk::FVector3d{}))
        {
            error.Assign("Secondary motion chain contact gradient allocation failed");
            return false;
        }
    }
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        const FPreparedSecondaryMotionCollisionShape& shape = shapes[shapeIndex];
        const double rootContactThreshold = shape.hasRootContactOverride ? GetPreparedSecondaryMotionRootContactThreshold(shape) : 0.0;
        if (shape.hasRootContactOverride && (!isfinite(rootContactThreshold) || rootContactThreshold < 0.0 || rootContactThreshold > shape.radius))
        {
            error.Assign("Secondary motion chain root contact threshold is invalid");
            return false;
        }
        for (uint32_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
        {
            const gk::FVector3d& first = positions.At(segmentIndex);
            const gk::FVector3d& second = positions.At(segmentIndex + 1u);
            double penetration = 0.0;
            double spanAmount = 0.0;
            gk::FVector3d normal{};
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, second, second, penetration, spanAmount, normal, error, toleranceFraction))
            {
                return false;
            }
            if (!AddPenetration(penetration, normal, 0.0, merit, gradients, segmentIndex, references, true))
            {
                error.Assign("Secondary motion chain contact merit is outside the numeric range");
                return false;
            }
            bool skipFirstSpan = false;
            double rootDistance = 0.0;
            if (segmentIndex == 0)
            {
                if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, first, first, penetration, spanAmount, normal, error, 0.0))
                {
                    return false;
                }
                rootDistance = shape.radius - penetration;
                if (shape.hasRootContactOverride)
                {
                    skipFirstSpan = penetration > shape.radius - rootContactThreshold;
                }
                else
                {
                    skipFirstSpan = penetration > shape.tolerance;
                }
            }
            if (!skipFirstSpan)
            {
                double spanToleranceFraction = toleranceFraction;
                double rootSpanThreshold = 0.0;
                if (segmentIndex == 0 && shape.hasRootContactOverride)
                {
                    rootSpanThreshold = GetPreparedSecondaryMotionRootSpanThreshold(shape, rootDistance, toleranceFraction);
                    spanToleranceFraction = 0.0;
                }
                if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, first, second, penetration, spanAmount, normal, error, spanToleranceFraction))
                {
                    return false;
                }
                if (segmentIndex == 0 && shape.hasRootContactOverride)
                {
                    penetration += rootSpanThreshold - shape.radius;
                }
                if (!AddPenetration(penetration, normal, spanAmount, merit, gradients, segmentIndex, references, false))
                {
                    error.Assign("Secondary motion chain contact merit is outside the numeric range");
                    return false;
                }
            }
        }
    }
    if (!isfinite(merit))
    {
        error.Assign("Secondary motion chain contact merit is outside the numeric range");
        return false;
    }
    for (uint32_t index = 0; index < gradients.Count(); ++index)
    {
        if (!IsFinite(gradients.At(index)))
        {
            error.Assign("Secondary motion chain contact gradient is outside the numeric range");
            return false;
        }
    }
    return true;
}

/**
 * 現在方向に接線勾配を適用して、全節の許容cone内候補を作る。
 */
bool MakeTrialDirections(const gk::Array<gk::FVector3d>& current, const FSecondaryMotionReferenceSegment* references, uint32_t segmentCount, double cosineLimit, const gk::Array<gk::FVector3d>& gradients, double angle, gk::Array<gk::FVector3d>& trial)
{
    trial.Clear();
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        gk::FVector3d tangent = gradients.At(index);
        if (Normalize(tangent))
        {
            gk::FVector3d candidate = Add(Multiply(current.At(index), cos(angle)), Multiply(tangent, sin(angle)));
            if (!NormalizeAndLimitSecondaryMotionDirection(references[index].direction, cosineLimit, candidate))
            {
                return false;
            }
            if (!trial.Append(candidate))
            {
                return false;
            }
        }
        else if (!trial.Append(current.At(index)))
        {
            return false;
        }
    }
    return true;
}

/**
 * 現在候補を全shape meritが下がるまで接線方向へline searchする。
 */
bool RefineChain(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionReferenceSegment* references, uint32_t segmentCount, double cosineLimit, double toleranceFraction, gk::Array<gk::FVector3d>& directions, gk::Array<gk::FVector3d>& candidatePositions, gk::Array<gk::FVector3d>& gradients, double& merit, bool& clear, gk::String& error)
{
    for (uint32_t iteration = 0; iteration < kMaximumChainContactIterations; ++iteration)
    {
        bool hasGradient = false;
        gk::Array<gk::FVector3d> tangentGradients;
        if (!tangentGradients.Reserve(segmentCount))
        {
            error.Assign("Secondary motion chain tangent allocation failed");
            return false;
        }
        for (uint32_t index = 0; index < segmentCount; ++index)
        {
            gk::FVector3d tangent = gradients.At(index);
            if (ProjectGradientToFeasibleTangent(references[index].direction, cosineLimit, directions.At(index), tangent))
            {
                hasGradient = true;
            }
            if (!tangentGradients.Append(tangent))
            {
                error.Assign("Secondary motion chain tangent allocation failed");
                return false;
            }
        }
        if (!hasGradient)
        {
            return true;
        }
        bool improved = false;
        double angle = 0.35;
        for (uint32_t step = 0; step < kChainContactLineSearchSteps; ++step)
        {
            gk::Array<gk::FVector3d> trialDirections;
            gk::Array<gk::FVector3d> trialPositions;
            gk::Array<gk::FVector3d> trialGradients;
            if (!trialDirections.Reserve(segmentCount) || !trialPositions.Reserve(segmentCount + 1u) || !trialGradients.Reserve(segmentCount))
            {
                error.Assign("Secondary motion chain candidate allocation failed");
                return false;
            }
            if (!MakeTrialDirections(directions, references, segmentCount, cosineLimit, tangentGradients, angle, trialDirections) || !BuildPositions(candidatePositions.At(0), references, segmentCount, trialDirections, trialPositions))
            {
                angle *= 0.5;
                continue;
            }
            double trialMerit = 0.0;
            if (!EvaluateChain(shapes, shapeCount, references, segmentCount, toleranceFraction, trialPositions, trialMerit, trialGradients, error))
            {
                return false;
            }
            if (trialMerit < merit)
            {
                directions.MoveFrom(trialDirections);
                candidatePositions.MoveFrom(trialPositions);
                gradients.MoveFrom(trialGradients);
                merit = trialMerit;
                clear = merit == 0.0;
                improved = true;
                break;
            }
            angle *= 0.5;
        }
        if (!improved || clear)
        {
            return true;
        }
    }
    return true;
}

/**
 * 基準方向群を共通位相で傾けた低密度の候補を作る。
 */
bool MakeCoherentTilt(const FSecondaryMotionReferenceSegment* references, uint32_t segmentCount, double cosineLimit, double ringFraction, double phase, gk::Array<gk::FVector3d>& directions)
{
    directions.Clear();
    const double maximumAngle = acos(cosineLimit);
    const double angle = maximumAngle * ringFraction;
    const double cosine = cos(angle);
    const double sine = sin(angle);
    const double phaseCosine = cos(phase);
    const double phaseSine = sin(phase);
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        gk::FVector3d reference = references[index].direction;
        if (!Normalize(reference))
        {
            return false;
        }
        gk::FVector3d tangent0{}, tangent1{};
        if (!BuildTangentBasis(reference, tangent0, tangent1))
        {
            return false;
        }
        const gk::FVector3d tangent = Add(Multiply(tangent0, phaseCosine), Multiply(tangent1, phaseSine));
        gk::FVector3d candidate = Add(Multiply(reference, cosine), Multiply(tangent, sine));
        if (!NormalizeAndLimitSecondaryMotionDirection(reference, cosineLimit, candidate) || !directions.Append(candidate))
        {
            return false;
        }
    }
    return true;
}

}

bool ResolvePreparedSecondaryMotionChainContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionReferenceSegment* referenceSegments, uint32_t segmentCount, double cosineLimit, gk::Array<gk::FVector3d>& positions, gk::String& error, double toleranceFraction)
{
    error.Clear();
    if (shapeCount > 64 || (shapeCount > 0 && !shapes) || segmentCount == UINT32_MAX || (segmentCount > 0 && !referenceSegments) || positions.Count() != segmentCount + 1u || !isfinite(cosineLimit) || cosineLimit < -1.0 || cosineLimit > 1.0 || !isfinite(toleranceFraction) || toleranceFraction < 0.0 || toleranceFraction > 1.0)
    {
        error.Assign("Secondary motion chain contact input is invalid");
        return false;
    }
    for (uint32_t index = 0; index < positions.Count(); ++index)
    {
        if (!IsFloatPosition(positions.At(index)))
        {
            error.Assign("Secondary motion chain contact position is invalid");
            return false;
        }
    }
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        if (!IsFinite(referenceSegments[index].direction) || !isfinite(referenceSegments[index].length) || !(referenceSegments[index].length > 0.0) || referenceSegments[index].length > FLT_MAX)
        {
            error.Assign("Secondary motion chain reference segment is invalid");
            return false;
        }
    }
    if (shapeCount == 0 || segmentCount == 0)
    {
        return true;
    }
    gk::Array<gk::FVector3d> seedDirections;
    gk::Array<gk::FVector3d> candidatePositions;
    gk::Array<gk::FVector3d> gradients;
    gk::Array<gk::FVector3d> trialDirections;
    gk::Array<gk::FVector3d> trialPositions;
    gk::Array<gk::FVector3d> trialGradients;
    if (!seedDirections.Reserve(segmentCount) || !candidatePositions.Reserve(segmentCount + 1u) || !gradients.Reserve(segmentCount) || !trialDirections.Reserve(segmentCount) || !trialPositions.Reserve(segmentCount + 1u) || !trialGradients.Reserve(segmentCount))
    {
        error.Assign("Secondary motion chain contact allocation failed");
        return false;
    }
    const gk::FVector3d root = positions.At(0);
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        gk::FVector3d reference = referenceSegments[index].direction;
        gk::FVector3d direction = Subtract(positions.At(index + 1u), positions.At(index));
        if (!Normalize(reference))
        {
            error.Assign("Secondary motion chain reference direction is degenerate");
            return false;
        }
        if (!Normalize(direction))
        {
            direction = reference;
        }
        if (!NormalizeAndLimitSecondaryMotionDirection(reference, cosineLimit, direction) || !seedDirections.Append(direction))
        {
            error.Assign("Secondary motion chain seed direction is invalid");
            return false;
        }
    }
    if (!BuildPositions(root, referenceSegments, segmentCount, seedDirections, candidatePositions))
    {
        error.Assign("Secondary motion chain seed position is outside the numeric range");
        return false;
    }
    double bestMerit = 0.0;
    if (!EvaluateChain(shapes, shapeCount, referenceSegments, segmentCount, toleranceFraction, candidatePositions, bestMerit, gradients, error))
    {
        return false;
    }
    if (bestMerit == 0.0)
    {
        positions.MoveFrom(candidatePositions);
        return true;
    }
    bool bestClear = false;
    if (!RefineChain(shapes, shapeCount, referenceSegments, segmentCount, cosineLimit, toleranceFraction, seedDirections, candidatePositions, gradients, bestMerit, bestClear, error))
    {
        return false;
    }
    if (bestClear)
    {
        positions.MoveFrom(candidatePositions);
        return true;
    }
    // アニメーション基準方向も独立seedとして探索する。
    trialDirections.Clear();
    for (uint32_t index = 0; index < segmentCount; ++index)
    {
        gk::FVector3d reference = referenceSegments[index].direction;
        if (!NormalizeAndLimitSecondaryMotionDirection(reference, cosineLimit, reference) || !trialDirections.Append(reference))
        {
            error.Assign("Secondary motion chain reference seed is invalid");
            return false;
        }
    }
    if (!BuildPositions(root, referenceSegments, segmentCount, trialDirections, trialPositions))
    {
        error.Assign("Secondary motion chain reference position is outside the numeric range");
        return false;
    }
    double referenceMerit = 0.0;
    if (!EvaluateChain(shapes, shapeCount, referenceSegments, segmentCount, toleranceFraction, trialPositions, referenceMerit, trialGradients, error))
    {
        return false;
    }
    if (referenceMerit == 0.0)
    {
        positions.MoveFrom(trialPositions);
        return true;
    }
    bool referenceClear = false;
    if (!RefineChain(shapes, shapeCount, referenceSegments, segmentCount, cosineLimit, toleranceFraction, trialDirections, trialPositions, trialGradients, referenceMerit, referenceClear, error))
    {
        return false;
    }
    if (referenceClear)
    {
        positions.MoveFrom(trialPositions);
        return true;
    }
    bestMerit = fmin(bestMerit, referenceMerit);
    for (uint32_t ring = 0; ring < 2; ++ring)
    {
        const double ringFraction = ring == 0 ? 0.5 : 1.0;
        for (uint32_t azimuth = 0; azimuth < kChainContactSeedAzimuths; ++azimuth)
        {
            const double phase = 6.283185307179586476925286766559 * azimuth / kChainContactSeedAzimuths;
            if (!trialDirections.Reserve(segmentCount) || !trialPositions.Reserve(segmentCount + 1u) || !trialGradients.Reserve(segmentCount))
            {
                error.Assign("Secondary motion chain tilt allocation failed");
                return false;
            }
            if (!MakeCoherentTilt(referenceSegments, segmentCount, cosineLimit, ringFraction, phase, trialDirections) || !BuildPositions(root, referenceSegments, segmentCount, trialDirections, trialPositions))
            {
                continue;
            }
            double trialMerit = 0.0;
            if (!EvaluateChain(shapes, shapeCount, referenceSegments, segmentCount, toleranceFraction, trialPositions, trialMerit, trialGradients, error))
            {
                return false;
            }
            if (trialMerit < bestMerit)
            {
                if (!RefineChain(shapes, shapeCount, referenceSegments, segmentCount, cosineLimit, toleranceFraction, trialDirections, trialPositions, trialGradients, trialMerit, bestClear, error))
                {
                    return false;
                }
                if (bestClear)
                {
                    positions.MoveFrom(trialPositions);
                    return true;
                }
                if (trialMerit < bestMerit)
                {
                    bestMerit = trialMerit;
                }
            }
        }
    }
    error.Clear();
    return false;
}

}
