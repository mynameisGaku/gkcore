// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionDirection.h"
#include "model/animation/ModelSecondaryMotionCollision.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

// 同時に検査する身体形状の上限。
const uint32_t kMaximumDirectionShapes = 64;
// 一次補正が改善しない場合に縮小する回数。
const uint32_t kGradientStepCount = 14;
// 角度境界の外向き勾配を除く数値判定幅。
const double kConeBoundaryFraction = 64.0 * FLT_EPSILON;

/**
 * 三次元座標が有限か調べる。
 */
bool IsFinite(const gk::FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * 三次元座標がfloat保存範囲に収まるか調べる。
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
 * 基準方向に対する安定した接線基底を作る。
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
 * 方向を基準軸まわりの許容角coneへ戻す。
 */
bool ClampToCone(const gk::FVector3d& reference, double cosineLimit, gk::FVector3d& direction)
{
    if (cosineLimit <= -1.0)
    {
        return true;
    }
    if (cosineLimit >= 1.0)
    {
        direction = reference;
        return true;
    }
    const double cosine = Dot(reference, direction);
    if (!isfinite(cosine))
    {
        return false;
    }
    if (cosine >= cosineLimit)
    {
        return true;
    }
    const double referenceSquared = Dot(reference, reference);
    if (!(referenceSquared > 0.0) || !isfinite(referenceSquared))
    {
        return false;
    }
    gk::FVector3d tangent = Subtract(direction, Multiply(reference, cosine / referenceSquared));
    const double tangentLength = Length(tangent);
    if (!isfinite(tangentLength))
    {
        return false;
    }
    if (tangentLength <= 64.0 * DBL_EPSILON)
    {
        if (!BuildPerpendicular(reference, tangent))
        {
            return false;
        }
    }
    else
    {
        tangent = Multiply(tangent, 1.0 / tangentLength);
    }
    double inwardCosine = fmin(1.0, cosineLimit + 16.0 * DBL_EPSILON);
    for (uint32_t attempt = 0; attempt < 4; ++attempt)
    {
        const double inwardSine = sqrt(fmax(0.0, 1.0 - inwardCosine * inwardCosine));
        gk::FVector3d candidate = Add(Multiply(reference, inwardCosine), Multiply(tangent, inwardSine));
        if (!Normalize(candidate))
        {
            return false;
        }
        if (Dot(reference, candidate) >= cosineLimit)
        {
            direction = candidate;
            return true;
        }
        inwardCosine = fmin(1.0, cosineLimit + (32.0 * static_cast<double>(attempt + 1) * DBL_EPSILON));
    }
    return false;
}

/**
 * 全形状の最大侵入、二乗和、侵入を減らす方向勾配を保持する。
 */
struct FDirectionMerit
{
    double maximum = 0.0;     // 端点または線分の最大侵入量。
    double squaredSum = 0.0;  // 正の侵入量の二乗和。
    gk::FVector3d gradient{}; // 方向球面上の侵入減少方向。
    bool hasClearance = true; // 全検査対象が形状外にあるか。
};

/**
 * 最大侵入を優先し、同じ最大値なら侵入二乗和が小さい候補を比較する。
 */
bool MeritImproves(const FDirectionMerit& candidate, const FDirectionMerit& reference)
{
    return candidate.maximum < reference.maximum || (candidate.maximum == reference.maximum && candidate.squaredSum < reference.squaredSum);
}

/**
 * 接触侵入をmeritとその方向微分へ加える。
 */
void AddPenetration(FDirectionMerit& merit, double penetration, const gk::FVector3d& normal, double directionScale)
{
    const double positive = fmax(0.0, penetration);
    merit.maximum = fmax(merit.maximum, positive);
    merit.squaredSum += positive * positive;
    if (positive > 0.0)
    {
        merit.gradient = Add(merit.gradient, Multiply(normal, positive * directionScale));
        merit.hasClearance = false;
    }
}

/**
 * 固定parentと方向で全shapeの端点・線分侵入を評価する。
 */
bool EvaluateDirection(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d& parent, double length, bool firstSegment, const gk::FVector3d& direction, double toleranceFraction, FDirectionMerit& merit, bool& validCandidate, gk::String& error)
{
    merit = {};
    validCandidate = false;
    const gk::FVector3d endpoint = Add(parent, Multiply(direction, length));
    if (!IsFloatPosition(endpoint))
    {
        return true;
    }
    validCandidate = true;
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        const auto& shape = shapes[shapeIndex];
        const double rootContactThreshold = shape.hasRootContactOverride ? GetPreparedSecondaryMotionRootContactThreshold(shape) : 0.0;
        if (shape.hasRootContactOverride && (!isfinite(rootContactThreshold) || rootContactThreshold < 0.0 || rootContactThreshold > shape.radius))
        {
            error.Assign("Secondary motion direction root contact threshold is invalid");
            return false;
        }
        double penetration = 0.0;
        double spanAmount = 0.0;
        gk::FVector3d normal{};
        // rootは点接触から除き、他の親節は固定位置でも侵入をmeritへ含める。
        if (!firstSegment)
        {
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, parent, parent, penetration, spanAmount, normal, error, toleranceFraction))
            {
                return false;
            }
            AddPenetration(merit, penetration, normal, 0.0);
        }
        if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, endpoint, endpoint, penetration, spanAmount, normal, error, toleranceFraction))
        {
            return false;
        }
        AddPenetration(merit, penetration, normal, length);
        // first spanだけは、固定rootがshape内部にある場合に限り免除する。
        bool skipSpan = false;
        if (firstSegment)
        {
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, parent, parent, penetration, spanAmount, normal, error, 0.0))
            {
                return false;
            }
            if (shape.hasRootContactOverride)
            {
                skipSpan = penetration > shape.radius - rootContactThreshold;
            }
            else
            {
                skipSpan = penetration > shape.tolerance;
            }
        }
        if (!skipSpan)
        {
            double spanToleranceFraction = toleranceFraction;
            double rootSpanThreshold = 0.0;
            if (firstSegment && shape.hasRootContactOverride)
            {
                const double rootDistance = shape.radius - penetration;
                rootSpanThreshold = GetPreparedSecondaryMotionRootSpanThreshold(shape, rootDistance, toleranceFraction);
                spanToleranceFraction = 0.0;
            }
            if (!QueryPreparedSecondaryMotionFreeSegmentContact(shape, parent, endpoint, penetration, spanAmount, normal, error, spanToleranceFraction))
            {
                return false;
            }
            if (firstSegment && shape.hasRootContactOverride)
            {
                penetration += rootSpanThreshold - shape.radius;
            }
            AddPenetration(merit, penetration, normal, length * spanAmount);
        }
    }
    if (!isfinite(merit.maximum) || !isfinite(merit.squaredSum) || !IsFinite(merit.gradient))
    {
        error.Assign("Secondary motion direction merit is outside the numeric range");
        return false;
    }
    return true;
}

/**
 * 方向勾配を単位球面と許容coneの接線へ射影する。
 */
bool ProjectGradientToFeasibleTangent(const gk::FVector3d& reference, double cosineLimit, const gk::FVector3d& direction, gk::FVector3d& gradient)
{
    gradient = Subtract(gradient, Multiply(direction, Dot(gradient, direction)));
    if (Dot(reference, direction) <= cosineLimit + kConeBoundaryFraction)
    {
        gk::FVector3d coneNormal = Subtract(reference, Multiply(direction, Dot(reference, direction)));
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
 * 参照方向のconeを中心・中間角・境界角の16方位で走査する。
 */
bool BuildConeGrid(const gk::FVector3d& reference, double cosineLimit, gk::FVector3d directions[33])
{
    directions[0] = reference;
    gk::FVector3d tangent0{}, tangent1{};
    if (!BuildTangentBasis(reference, tangent0, tangent1))
    {
        return false;
    }
    const double limit = acos(cosineLimit);
    const double ringFractions[2] = { 0.5, 1.0 };
    uint32_t index = 1;
    for (uint32_t ring = 0; ring < 2; ++ring)
    {
        const double angle = limit * ringFractions[ring];
        for (uint32_t azimuth = 0; azimuth < 16; ++azimuth)
        {
            const double phase = (6.283185307179586476925286766559 * azimuth) / 16.0;
            const gk::FVector3d tangent = Add(Multiply(tangent0, cos(phase)), Multiply(tangent1, sin(phase)));
            directions[index] = Add(Multiply(reference, cos(angle)), Multiply(tangent, sin(angle)));
            if (!Normalize(directions[index]) || !ClampToCone(reference, cosineLimit, directions[index]))
            {
                return false;
            }
            ++index;
        }
    }
    return true;
}

/**
 * 一つの有効候補からmeritを厳密に減らす接線方向のline searchを行う。
 */
bool RefineDirection(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d& parent, double length, bool firstSegment, const gk::FVector3d& reference, double cosineLimit, double toleranceFraction, gk::FVector3d& direction, FDirectionMerit& merit, gk::String& error)
{
    for (uint32_t iteration = 0; iteration < 48; ++iteration)
    {
        gk::FVector3d tangent = merit.gradient;
        if (!ProjectGradientToFeasibleTangent(reference, cosineLimit, direction, tangent))
        {
            break;
        }
        bool improved = false;
        double angle = 0.5;
        for (uint32_t stepIndex = 0; stepIndex < kGradientStepCount; ++stepIndex)
        {
            gk::FVector3d trialDirection = Add(Multiply(direction, cos(angle)), Multiply(tangent, sin(angle)));
            if (!Normalize(trialDirection) || !ClampToCone(reference, cosineLimit, trialDirection))
            {
                angle *= 0.5;
                continue;
            }
            FDirectionMerit trialMerit{};
            bool trialValid = false;
            if (!EvaluateDirection(shapes, shapeCount, parent, length, firstSegment, trialDirection, toleranceFraction, trialMerit, trialValid, error))
            {
                return false;
            }
            if (trialValid && (MeritImproves(trialMerit, merit) || (trialMerit.hasClearance && !merit.hasClearance)))
            {
                direction = trialDirection;
                merit = trialMerit;
                improved = true;
                if (merit.hasClearance)
                {
                    return true;
                }
                break;
            }
            angle *= 0.5;
        }
        if (!improved)
        {
            break;
        }
    }
    error.Clear();
    return true;
}

/**
 * 全形状clearを保てる範囲で候補方向を入力方向へ近づける。
 */
bool ContractClearDirection(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d& parent, double length, bool firstSegment, const gk::FVector3d& reference, double cosineLimit, double toleranceFraction, const gk::FVector3d& initialDirection, gk::FVector3d& direction, FDirectionMerit& merit, gk::String& error)
{
    const gk::FVector3d clearDirection = direction;
    const FDirectionMerit clearMerit = merit;
    double lowerFraction = 0.0;
    double upperFraction = 1.0;
    for (uint32_t probe = 0; probe < 12u; ++probe)
    {
        const double fraction = (lowerFraction + upperFraction) * 0.5;
        gk::FVector3d trialDirection = Add(Multiply(initialDirection, 1.0 - fraction), Multiply(clearDirection, fraction));
        if (!Normalize(trialDirection) || !ClampToCone(reference, cosineLimit, trialDirection))
        {
            lowerFraction = fraction;
            continue;
        }
        FDirectionMerit trialMerit{};
        bool trialValid = false;
        if (!EvaluateDirection(shapes, shapeCount, parent, length, firstSegment, trialDirection, toleranceFraction, trialMerit, trialValid, error))
        {
            return false;
        }
        if (trialValid && trialMerit.hasClearance)
        {
            upperFraction = fraction;
            direction = trialDirection;
            merit = trialMerit;
        }
        else
        {
            lowerFraction = fraction;
        }
    }
    if (upperFraction == 1.0)
    {
        direction = clearDirection;
        merit = clearMerit;
    }
    error.Clear();
    return true;
}

}

bool FindPreparedSecondaryMotionCollisionDirection(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d& parent, double length, bool firstSegment, const gk::FVector3d& referenceDirection, double cosineLimit, gk::FVector3d& direction, gk::String& error, double toleranceFraction)
{
    error.Clear();
    if (shapeCount > kMaximumDirectionShapes || (shapeCount > 0 && !shapes) || !IsFloatPosition(parent) || !isfinite(length) || !(length > 0.0) || length > FLT_MAX || !IsFinite(referenceDirection) || !isfinite(cosineLimit) || cosineLimit < -1.0 || cosineLimit > 1.0 || !isfinite(toleranceFraction) || toleranceFraction < 0.0 || toleranceFraction > 1.0 || !IsFinite(direction))
    {
        error.Assign("Secondary motion direction input is invalid");
        return false;
    }
    // 入出力が同じ記憶域でも参照方向と初期候補を先に保存する。
    const gk::FVector3d initialDirection = direction;
    gk::FVector3d reference = referenceDirection;
    gk::FVector3d seed = initialDirection;
    if (!Normalize(reference) || !Normalize(seed))
    {
        error.Assign("Secondary motion direction is degenerate");
        return false;
    }
    if (shapeCount == 0)
    {
        // 身体接触がなければ、呼出側の角度・長さ復元結果を変更しない。
        return true;
    }
    if (!ClampToCone(reference, cosineLimit, seed))
    {
        error.Assign("Secondary motion direction cannot be clamped to its angle limit");
        return false;
    }
    // 元候補が既に全接触と角度制約を満たすなら、その方向を直ちに採用する。
    FDirectionMerit bestMerit{};
    bool validCandidate = false;
    if (!EvaluateDirection(shapes, shapeCount, parent, length, firstSegment, seed, toleranceFraction, bestMerit, validCandidate, error))
    {
        return false;
    }
    if (validCandidate && bestMerit.hasClearance)
    {
        direction = seed;
        error.Clear();
        return true;
    }
    gk::FVector3d bestDirection = seed;
    // 現在方向から局所探索し、近いclear解を先に探す。
    gk::FVector3d localDirection = seed;
    FDirectionMerit localMerit = bestMerit;
    if (!RefineDirection(shapes, shapeCount, parent, length, firstSegment, reference, cosineLimit, toleranceFraction, localDirection, localMerit, error))
    {
        return false;
    }
    if (localMerit.hasClearance)
    {
        if (!ContractClearDirection(shapes, shapeCount, parent, length, firstSegment, reference, cosineLimit, toleranceFraction, seed, localDirection, localMerit, error))
        {
            return false;
        }
        direction = localDirection;
        error.Clear();
        return true;
    }
    if (MeritImproves(localMerit, bestMerit))
    {
        bestDirection = localDirection;
        bestMerit = localMerit;
    }
    // 参照方向も候補として評価するが、近い局所解より先には採用しない。
    gk::FVector3d referenceSeed = reference;
    FDirectionMerit referenceMerit{};
    bool referenceValid = false;
    if (!EvaluateDirection(shapes, shapeCount, parent, length, firstSegment, referenceSeed, toleranceFraction, referenceMerit, referenceValid, error))
    {
        return false;
    }
    if (referenceValid && referenceMerit.hasClearance)
    {
        if (!ContractClearDirection(shapes, shapeCount, parent, length, firstSegment, reference, cosineLimit, toleranceFraction, seed, referenceSeed, referenceMerit, error))
        {
            return false;
        }
        direction = referenceSeed;
        error.Clear();
        return true;
    }
    if (referenceValid && MeritImproves(referenceMerit, bestMerit))
    {
        bestDirection = referenceSeed;
        bestMerit = referenceMerit;
    }
    // 局所勾配が停滞した場合だけ、有限なcone格子を試す。
    gk::FVector3d gridDirections[33]{};
    if (!BuildConeGrid(reference, cosineLimit, gridDirections))
    {
        error.Assign("Secondary motion direction cone grid is invalid");
        return false;
    }
    double bestMovement = DBL_MAX;
    for (uint32_t index = 0; index < 33; ++index)
    {
        FDirectionMerit trialMerit{};
        bool trialValid = false;
        gk::String trialError;
        if (!EvaluateDirection(shapes, shapeCount, parent, length, firstSegment, gridDirections[index], toleranceFraction, trialMerit, trialValid, trialError) || !trialValid)
        {
            continue;
        }
        const gk::FVector3d difference = Subtract(gridDirections[index], seed);
        const double movement = Dot(difference, difference);
        const bool improves = MeritImproves(trialMerit, bestMerit);
        const bool sameClearanceSmallerMove = trialMerit.hasClearance && bestMerit.hasClearance && movement < bestMovement;
        if (trialMerit.hasClearance && !bestMerit.hasClearance)
        {
            bestDirection = gridDirections[index];
            bestMerit = trialMerit;
            bestMovement = movement;
        }
        else if (improves)
        {
            bestDirection = gridDirections[index];
            bestMerit = trialMerit;
            bestMovement = movement;
        }
        else if (sameClearanceSmallerMove)
        {
            bestDirection = gridDirections[index];
            bestMerit = trialMerit;
            bestMovement = movement;
        }
    }
    if (bestMerit.hasClearance)
    {
        if (!ContractClearDirection(shapes, shapeCount, parent, length, firstSegment, reference, cosineLimit, toleranceFraction, seed, bestDirection, bestMerit, error))
        {
            return false;
        }
        direction = bestDirection;
        error.Clear();
        return true;
    }
    if (!RefineDirection(shapes, shapeCount, parent, length, firstSegment, reference, cosineLimit, toleranceFraction, bestDirection, bestMerit, error))
    {
        return false;
    }
    if (bestMerit.hasClearance)
    {
        if (!ContractClearDirection(shapes, shapeCount, parent, length, firstSegment, reference, cosineLimit, toleranceFraction, seed, bestDirection, bestMerit, error))
        {
            return false;
        }
        direction = bestDirection;
        error.Clear();
        return true;
    }
    // 候補が見つからない場合はdirectionを変えず、次の反復と鎖全体の検査へ委ねる。
    error.Clear();
    return false;
}

bool NormalizeAndLimitSecondaryMotionDirection(const gk::FVector3d& referenceDirection, double cosineLimit, gk::FVector3d& direction)
{
    if (!IsFinite(referenceDirection) || !IsFinite(direction) || !isfinite(cosineLimit) || cosineLimit < -1.0 || cosineLimit > 1.0)
    {
        return false;
    }
    gk::FVector3d normalizedReference = referenceDirection;
    gk::FVector3d candidate = direction;
    if (!Normalize(normalizedReference) || !Normalize(candidate) || !ClampToCone(normalizedReference, cosineLimit, candidate))
    {
        return false;
    }
    direction = candidate;
    return true;
}

}
