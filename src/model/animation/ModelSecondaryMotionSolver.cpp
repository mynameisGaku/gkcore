// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionSolver.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

/**
 * 積分中に使う倍精度model-space位置。
 */
struct FVector3
{
    // XYZ位置または方向。
    double value[3];
};

/**
 * 3成分を倍精度位置へ変換する。
 */
FVector3 ToDouble(const gk::Vec3& value)
{
    return { { value.x, value.y, value.z } };
}

/**
 * 二つの位置の差を計算する。
 */
FVector3 Subtract(const FVector3& left, const FVector3& right)
{
    return { { left.value[0] - right.value[0], left.value[1] - right.value[1], left.value[2] - right.value[2] } };
}

/**
 * 二つの位置を係数で混ぜる。
 */
FVector3 Interpolate(const FVector3& first, const FVector3& second, double weight)
{
    return { { first.value[0] + (second.value[0] - first.value[0]) * weight, first.value[1] + (second.value[1] - first.value[1]) * weight, first.value[2] + (second.value[2] - first.value[2]) * weight } };
}

/**
 * 3次元内積を計算する。
 */
double Dot(const FVector3& first, const FVector3& second)
{
    return first.value[0] * second.value[0] + first.value[1] * second.value[1] + first.value[2] * second.value[2];
}

/**
 * 3次元ベクトルの長さをoverflowを避けて計算する。
 */
double Length(const FVector3& value)
{
    const double largest = fmax(fabs(value.value[0]), fmax(fabs(value.value[1]), fabs(value.value[2])));
    if (!(largest > 0.0) || !isfinite(largest))
        return largest;
    const double x = value.value[0] / largest;
    const double y = value.value[1] / largest;
    const double z = value.value[2] / largest;
    return largest * sqrt(x * x + y * y + z * z);
}

/**
 * 有効な3次元方向を単位長へ正規化する。
 */
bool Normalize(FVector3& value)
{
    const double length = Length(value);
    if (!(length > 0.0) || !isfinite(length))
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
        value.value[axis] /= length;
    return true;
}

/**
 * 3成分が有限値かを調べる。
 */
bool IsFinite(const gk::Vec3& value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

/**
 * solver設定とdeltaを検証する。
 */
bool ValidateSettings(const gk::FModelSecondaryMotionSettings& settings, double delta)
{
    return isfinite(delta) && delta >= 0.0 && isfinite(settings.frequencyHz) && settings.frequencyHz > 0.0f && settings.frequencyHz <= 20.0f && isfinite(settings.dampingRatio) && settings.dampingRatio >= 0.0f && settings.dampingRatio <= 2.0f && IsFinite(settings.gravity) && IsFinite(settings.windAcceleration) && isfinite(settings.maxAngleDegrees) && settings.maxAngleDegrees >= 0.0f && settings.maxAngleDegrees <= 180.0f && IsFinite(settings.endOffset) && isfinite(settings.teleportDistance) && settings.teleportDistance > 0.0f && settings.constraintIterations > 0 && settings.constraintIterations <= 32;
}

/**
 * target列が有限で、各節の長さが正かつfloat範囲内かを調べる。
 */
bool ValidateTargets(const gk::Vec3* targets, uint32_t count, gk::String& error)
{
    if (!targets || count < 2 || count > 1025)
    {
        error.Assign("Secondary motion target count is invalid");
        return false;
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        if (!IsFinite(targets[index]))
        {
            error.Assign("Secondary motion target is non-finite");
            return false;
        }
        if (index > 0)
        {
            const double length = Length(Subtract(ToDouble(targets[index]), ToDouble(targets[index - 1])));
            if (!(length > 0.0) || !isfinite(length) || length > FLT_MAX)
            {
                error.Assign("Secondary motion segment length is invalid");
                return false;
            }
        }
    }
    return true;
}

/**
 * Reset候補を作り、すべて成功した後でstateを置き換える。
 */
bool BuildResetState(const gk::Vec3* targets, uint32_t count, FModelSecondaryMotionChainState& output, gk::String& error)
{
    FModelSecondaryMotionChainState candidate;
    if (!candidate.points.Reserve(count) || !candidate.previousTargets.Reserve(count))
    {
        error.Assign("Secondary motion state allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        FModelSecondaryMotionPoint point{};
        point.position = targets[index];
        if (!candidate.points.Append(point) || !candidate.previousTargets.Append(targets[index]))
        {
            error.Assign("Secondary motion state allocation failed");
            return false;
        }
    }
    output.points.MoveFrom(candidate.points);
    output.previousTargets.MoveFrom(candidate.previousTargets);
    return true;
}

/**
 * 正規化済み方向を基準方向から最大角度以内へ制限する。
 */
FVector3 LimitDirection(const FVector3& direction, const FVector3& reference, double cosineLimit, double sineLimit)
{
    double cosine = Dot(direction, reference);
    if (cosine > 1.0)
        cosine = 1.0;
    if (cosine < -1.0)
        cosine = -1.0;
    if (cosine >= cosineLimit)
        return direction;
    FVector3 tangent = { { direction.value[0] - reference.value[0] * cosine, direction.value[1] - reference.value[1] * cosine, direction.value[2] - reference.value[2] * cosine } };
    if (!Normalize(tangent))
    {
        const double x = fabs(reference.value[0]);
        const double y = fabs(reference.value[1]);
        const double z = fabs(reference.value[2]);
        FVector3 basis{};
        if (x <= y && x <= z)
            basis.value[0] = 1.0;
        else if (y <= z)
            basis.value[1] = 1.0;
        else
            basis.value[2] = 1.0;
        tangent = { { reference.value[1] * basis.value[2] - reference.value[2] * basis.value[1], reference.value[2] * basis.value[0] - reference.value[0] * basis.value[2], reference.value[0] * basis.value[1] - reference.value[1] * basis.value[0] } };
        Normalize(tangent);
    }
    return { { reference.value[0] * cosineLimit + tangent.value[0] * sineLimit, reference.value[1] * cosineLimit + tangent.value[1] * sineLimit, reference.value[2] * cosineLimit + tangent.value[2] * sineLimit } };
}

/**
 * 倍精度位置と速度をfloat状態へ安全に変換する。
 */
bool StorePoint(const FVector3& position, const FVector3& velocity, FModelSecondaryMotionPoint& output)
{
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(position.value[axis]) || !isfinite(velocity.value[axis]) || fabs(position.value[axis]) > FLT_MAX || fabs(velocity.value[axis]) > FLT_MAX)
            return false;
    }
    output.position = { static_cast<float>(position.value[0]), static_cast<float>(position.value[1]), static_cast<float>(position.value[2]) };
    output.velocity = { static_cast<float>(velocity.value[0]), static_cast<float>(velocity.value[1]), static_cast<float>(velocity.value[2]) };
    return IsFinite(output.position) && IsFinite(output.velocity);
}

}

bool ResetSecondaryMotionChain(const gk::Vec3* targets, uint32_t count, FModelSecondaryMotionChainState& output, gk::String& error)
{
    error.Clear();
    if (!ValidateTargets(targets, count, error))
        return false;
    if (!BuildResetState(targets, count, output, error))
        return false;
    error.Clear();
    return true;
}

bool StepSecondaryMotionChain(const gk::Vec3* targets, uint32_t count, const gk::FModelSecondaryMotionSettings& settings, double delta, FModelSecondaryMotionChainState& state, gk::String& error)
{
    error.Clear();
    if (!ValidateSettings(settings, delta) || !ValidateTargets(targets, count, error))
    {
        if (error.Empty())
            error.Assign("Secondary motion settings or delta is invalid");
        return false;
    }
    if (state.points.Count() == 0 && state.previousTargets.Count() == 0)
        return ResetSecondaryMotionChain(targets, count, state, error);
    if (state.points.Count() != count || state.previousTargets.Count() != count)
    {
        error.Assign("Secondary motion state count does not match its target chain");
        return false;
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        if (!IsFinite(state.points.At(index).position) || !IsFinite(state.points.At(index).velocity) || !IsFinite(state.previousTargets.At(index)))
        {
            error.Assign("Secondary motion state is non-finite");
            return false;
        }
        if (index > 0)
        {
            const double previousLength = Length(Subtract(ToDouble(state.previousTargets.At(index)), ToDouble(state.previousTargets.At(index - 1))));
            if (!(previousLength > 0.0) || !isfinite(previousLength) || previousLength > FLT_MAX)
            {
                error.Assign("Secondary motion previous target length is invalid");
                return false;
            }
        }
    }
    const double rootDisplacement = Length(Subtract(ToDouble(targets[0]), ToDouble(state.previousTargets.At(0))));
    if (!isfinite(rootDisplacement))
    {
        error.Assign("Secondary motion root displacement is invalid");
        return false;
    }
    if (delta >= 0.25 || rootDisplacement > settings.teleportDistance)
    {
        if (!ResetSecondaryMotionChain(targets, count, state, error))
            return false;
        error.Clear();
        return true;
    }
    FModelSecondaryMotionChainState candidate;
    if (!candidate.points.AppendRange(state.points.Data(), count) || !candidate.previousTargets.AppendRange(targets, count))
    {
        error.Assign("Secondary motion state allocation failed");
        return false;
    }
    if (delta == 0.0)
    {
        candidate.points.At(0).position = targets[0];
        candidate.points.At(0).velocity = {};
        state.points.MoveFrom(candidate.points);
        state.previousTargets.MoveFrom(candidate.previousTargets);
        error.Clear();
        return true;
    }

    gk::Array<FVector3> positions;
    gk::Array<FVector3> velocities;
    gk::Array<FVector3> oldPositions;
    gk::Array<FVector3> stepTargets;
    if (!positions.Reserve(count) || !velocities.Reserve(count) || !oldPositions.Reserve(count) || !stepTargets.Reserve(count))
    {
        error.Assign("Secondary motion solver allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        if (!positions.Append(ToDouble(state.points.At(index).position)) || !velocities.Append(ToDouble(state.points.At(index).velocity)) || !oldPositions.Append({}) || !stepTargets.Append({}))
        {
            error.Assign("Secondary motion solver allocation failed");
            return false;
        }
    }
    uint32_t substepCount = static_cast<uint32_t>(ceil(delta * 120.0));
    if (substepCount == 0)
        substepCount = 1;
    const double step = delta / substepCount;
    const double omega = 6.283185307179586476925286766559 * settings.frequencyHz;
    const double spring = omega * omega;
    const double damping = exp(-2.0 * settings.dampingRatio * omega * step);
    const double angle = settings.maxAngleDegrees * (3.1415926535897932384626433832795 / 180.0);
    const double cosineLimit = cos(angle);
    const double sineLimit = sin(angle);
    const FVector3 gravity = ToDouble(settings.gravity);
    const FVector3 wind = ToDouble(settings.windAcceleration);
    for (uint32_t substep = 0; substep < substepCount; ++substep)
    {
        const double weight = static_cast<double>(substep + 1u) / substepCount;
        for (uint32_t index = 0; index < count; ++index)
            stepTargets.At(index) = Interpolate(ToDouble(state.previousTargets.At(index)), ToDouble(targets[index]), weight);
        for (uint32_t index = 0; index < count; ++index)
            oldPositions.At(index) = positions.At(index);
        positions.At(0) = stepTargets.At(0);
        for (uint32_t index = 1; index < count; ++index)
        {
            FVector3 acceleration = Subtract(stepTargets.At(index), positions.At(index));
            for (uint32_t axis = 0; axis < 3; ++axis)
                acceleration.value[axis] = acceleration.value[axis] * spring + gravity.value[axis] + wind.value[axis];
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                velocities.At(index).value[axis] = (velocities.At(index).value[axis] + acceleration.value[axis] * step) * damping;
                positions.At(index).value[axis] += velocities.At(index).value[axis] * step;
            }
        }
        for (uint32_t pass = 0; pass < settings.constraintIterations; ++pass)
        {
            positions.At(0) = stepTargets.At(0);
            for (uint32_t index = 1; index < count; ++index)
            {
                const FVector3 animatedSegment = Subtract(stepTargets.At(index), stepTargets.At(index - 1));
                const double segmentLength = Length(animatedSegment);
                FVector3 referenceDirection = animatedSegment;
                if (segmentLength == 0.0)
                {
                    // 目標が反転して中間方向を定められないときは、現在の姿勢へ安全に戻す。
                    return ResetSecondaryMotionChain(targets, count, state, error);
                }
                if (!(segmentLength > 0.0) || !isfinite(segmentLength) || segmentLength > FLT_MAX || !Normalize(referenceDirection))
                {
                    error.Assign("Secondary motion interpolated segment length is invalid");
                    return false;
                }
                FVector3 direction = Subtract(positions.At(index), positions.At(index - 1));
                if (!Normalize(direction))
                    direction = referenceDirection;
                if (settings.maxAngleDegrees < 180.0f)
                    direction = LimitDirection(direction, referenceDirection, cosineLimit, sineLimit);
                for (uint32_t axis = 0; axis < 3; ++axis)
                    positions.At(index).value[axis] = positions.At(index - 1).value[axis] + direction.value[axis] * segmentLength;
            }
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
                velocities.At(index).value[axis] = (positions.At(index).value[axis] - oldPositions.At(index).value[axis]) / step;
            if (!isfinite(positions.At(index).value[0]) || !isfinite(positions.At(index).value[1]) || !isfinite(positions.At(index).value[2]) || !isfinite(velocities.At(index).value[0]) || !isfinite(velocities.At(index).value[1]) || !isfinite(velocities.At(index).value[2]))
            {
                error.Assign("Secondary motion produced a non-finite state");
                return false;
            }
        }
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        if (!StorePoint(positions.At(index), velocities.At(index), candidate.points.At(index)))
        {
            error.Assign("Secondary motion state exceeds the float range");
            return false;
        }
    }
    state.points.MoveFrom(candidate.points);
    state.previousTargets.MoveFrom(candidate.previousTargets);
    error.Clear();
    return true;
}

}
