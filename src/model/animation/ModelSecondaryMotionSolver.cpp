// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "foundation/FVector3d.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

// 積分と接触が共通で使う倍精度座標。
using FVector3 = gk::FVector3d;

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

/**
 * 鎖を候補側で積分し、長さ・曲げ角・任意の身体接触を満たすときだけstateを更新する。
 */
static bool StepSecondaryMotionChainCore(const gk::Vec3* targets, uint32_t count, const gk::FModelSecondaryMotionSettings& settings, double delta, const FModelSecondaryMotionCollisionShape* previousShapes, const FModelSecondaryMotionCollisionShape* currentShapes, uint32_t shapeCount, FModelSecondaryMotionChainState& state, gk::String& error)
{
    error.Clear();
    if (!ValidateSettings(settings, delta) || !ValidateTargets(targets, count, error))
    {
        if (error.Empty())
            error.Assign("Secondary motion settings or delta is invalid");
        return false;
    }
    if (shapeCount > 0 && (!ValidateSecondaryMotionCollisionShapes(previousShapes, shapeCount, error) || !ValidateSecondaryMotionCollisionShapes(currentShapes, shapeCount, error)))
    {
        return false;
    }
    if (shapeCount > 64)
    {
        error.Assign("secondary motion collider count exceeds 64");
        return false;
    }
    // 接触なしでは既存の初期化結果と計算順をそのまま保つ。
    const bool initiallyEmpty = state.points.Count() == 0 && state.previousTargets.Count() == 0;
    if (initiallyEmpty && shapeCount == 0)
    {
        return ResetSecondaryMotionChain(targets, count, state, error);
    }
    if (!initiallyEmpty && (state.points.Count() != count || state.previousTargets.Count() != count))
    {
        error.Assign("Secondary motion state count does not match its target chain");
        return false;
    }
    for (uint32_t index = 0; !initiallyEmpty && index < count; ++index)
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
    const double rootDisplacement = initiallyEmpty ? 0.0 : Length(Subtract(ToDouble(targets[0]), ToDouble(state.previousTargets.At(0))));
    if (!isfinite(rootDisplacement))
    {
        error.Assign("Secondary motion root displacement is invalid");
        return false;
    }
    const bool resetMotion = initiallyEmpty || delta >= 0.25 || rootDisplacement > settings.teleportDistance;
    if (resetMotion && shapeCount == 0)
    {
        if (!ResetSecondaryMotionChain(targets, count, state, error))
            return false;
        error.Clear();
        return true;
    }
    FModelSecondaryMotionChainState candidate;
    if (resetMotion)
    {
        if (!BuildResetState(targets, count, candidate, error))
        {
            return false;
        }
    }
    else if (!candidate.points.AppendRange(state.points.Data(), count) || !candidate.previousTargets.AppendRange(targets, count))
    {
        error.Assign("Secondary motion state allocation failed");
        return false;
    }
    if (delta == 0.0 && shapeCount == 0)
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
        if (!positions.Append(ToDouble(candidate.points.At(index).position)) || !velocities.Append(ToDouble(candidate.points.At(index).velocity)) || !oldPositions.Append({}) || !stepTargets.Append({}))
        {
            error.Assign("Secondary motion solver allocation failed");
            return false;
        }
    }
    // リセットと時間0の呼び出しでも、現在の形状との接触だけは解決する。
    const bool integrateMotion = !resetMotion && delta > 0.0;
    uint32_t substepCount = integrateMotion ? static_cast<uint32_t>(ceil(delta * 120.0)) : 1u;
    if (substepCount == 0)
        substepCount = 1;
    const double step = integrateMotion ? delta / substepCount : 0.0;
    const double omega = 6.283185307179586476925286766559 * settings.frequencyHz;
    const double spring = omega * omega;
    const double damping = exp(-2.0 * settings.dampingRatio * omega * step);
    const double angle = settings.maxAngleDegrees * (3.1415926535897932384626433832795 / 180.0);
    const double cosineLimit = cos(angle);
    const double sineLimit = sin(angle);
    const FVector3 gravity = ToDouble(settings.gravity);
    const FVector3 wind = ToDouble(settings.windAcceleration);
    // 接触形状もアニメーション目標と同じ小刻み更新の時刻で補間する。
    gk::Array<FModelSecondaryMotionCollisionShape> stepShapes;
    if (!stepShapes.Reserve(shapeCount))
    {
        error.Assign("secondary motion collider allocation failed");
        return false;
    }
    for (uint32_t substep = 0; substep < substepCount; ++substep)
    {
        const double weight = static_cast<double>(substep + 1u) / substepCount;
        for (uint32_t index = 0; index < count; ++index)
            stepTargets.At(index) = resetMotion ? ToDouble(targets[index]) : Interpolate(ToDouble(state.previousTargets.At(index)), ToDouble(targets[index]), weight);
        stepShapes.Clear();
        for (uint32_t shape = 0; shape < shapeCount; ++shape)
        {
            const auto& current = currentShapes[shape];
            const auto& previous = integrateMotion ? previousShapes[shape] : current;
            const auto start = Interpolate(ToDouble(previous.start), ToDouble(current.start), weight);
            const auto end = Interpolate(ToDouble(previous.end), ToDouble(current.end), weight);
            const double interpolatedRadius = previous.radius + (static_cast<double>(current.radius) - previous.radius) * weight;
            // 接触許容幅を相殺する余白を計算中だけ加え、float回転へ戻す丸め差に備える。
            const double guardedRadius = interpolatedRadius * 1.001;
            if (!isfinite(guardedRadius) || guardedRadius > FLT_MAX)
            {
                error.Assign("Secondary motion collision radius exceeds the float range");
                return false;
            }
            const float radius = static_cast<float>(guardedRadius);
            const FModelSecondaryMotionCollisionShape interpolated{ { static_cast<float>(start.value[0]), static_cast<float>(start.value[1]), static_cast<float>(start.value[2]) }, { static_cast<float>(end.value[0]), static_cast<float>(end.value[1]), static_cast<float>(end.value[2]) }, radius };
            if (!stepShapes.Append(interpolated))
            {
                error.Assign("secondary motion collider allocation failed");
                return false;
            }
        }
        for (uint32_t index = 0; index < count; ++index)
            oldPositions.At(index) = positions.At(index);
        positions.At(0) = stepTargets.At(0);
        for (uint32_t index = 1; integrateMotion && index < count; ++index)
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
            if (shapeCount > 0 && !ProjectSecondaryMotionContacts(stepShapes.Data(), shapeCount, stepTargets.Data(), count, positions, error))
            {
                return false;
            }
            positions.At(0) = stepTargets.At(0);
            for (uint32_t index = 1; index < count; ++index)
            {
                const FVector3 animatedSegment = Subtract(stepTargets.At(index), stepTargets.At(index - 1));
                const double segmentLength = Length(animatedSegment);
                FVector3 referenceDirection = animatedSegment;
                if (segmentLength == 0.0)
                {
                    // 目標が反転して中間方向を定められないときは、現在の姿勢へ安全に戻す。
                    if (shapeCount == 0)
                    {
                        return ResetSecondaryMotionChain(targets, count, state, error);
                    }
                    FModelSecondaryMotionChainState restarted;
                    if (!ResetSecondaryMotionChain(targets, count, restarted, error) || !StepSecondaryMotionChainCore(targets, count, settings, 0.0, currentShapes, currentShapes, shapeCount, restarted, error))
                    {
                        return false;
                    }
                    state.points.MoveFrom(restarted.points);
                    state.previousTargets.MoveFrom(restarted.previousTargets);
                    return true;
                }
                if (!(segmentLength > 0.0) || !isfinite(segmentLength) || segmentLength > FLT_MAX || !Normalize(referenceDirection))
                {
                    error.Assign("Secondary motion interpolated segment length is invalid");
                    return false;
                }
                FVector3 direction = Subtract(positions.At(index), positions.At(index - 1));
                if (!Normalize(direction))
                    direction = referenceDirection;
                // 押し出しを長さへ戻して再侵入する停滞を避け、固定長の方向を解く。
                for (uint32_t shape = 0; shape < shapeCount; ++shape)
                {
                    if (!AvoidSecondaryMotionCollisionSegment(stepShapes.At(shape), positions.At(index - 1), segmentLength, index == 1, direction, error))
                    {
                        return false;
                    }
                }
                if (settings.maxAngleDegrees < 180.0f)
                    direction = LimitDirection(direction, referenceDirection, cosineLimit, sineLimit);
                for (uint32_t axis = 0; axis < 3; ++axis)
                    positions.At(index).value[axis] = positions.At(index - 1).value[axis] + direction.value[axis] * segmentLength;
            }
            // 骨長と曲げ角を戻した後も接触が解けていれば、残りの反復は不要。
            if (shapeCount > 0 && CheckSecondaryMotionContacts(stepShapes.Data(), shapeCount, positions.Data(), count, error, 0.0))
            {
                break;
            }
        }
        if (shapeCount > 0 && !CheckSecondaryMotionContacts(stepShapes.Data(), shapeCount, positions.Data(), count, error))
        {
            return false;
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            if (integrateMotion)
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    velocities.At(index).value[axis] = (positions.At(index).value[axis] - oldPositions.At(index).value[axis]) / step;
                }
            }
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
    if (shapeCount > 0)
    {
        // 保存後のfloat座標でも接触が成立することを確認してから状態を確定する。
        for (uint32_t index = 0; index < count; ++index)
        {
            positions.At(index) = ToDouble(candidate.points.At(index).position);
        }
        // 接触だけでなく、float保存で失われやすい短い骨長と曲げ角も検算する。
        const double directionCosineLimit = cos(fmin(3.1415926535897932384626433832795, angle + 128.0 * FLT_EPSILON));
        for (uint32_t index = 1; index < count; ++index)
        {
            FVector3 reference = Subtract(ToDouble(targets[index]), ToDouble(targets[index - 1]));
            FVector3 stored = Subtract(positions.At(index), positions.At(index - 1));
            const double targetLength = Length(reference);
            const double storedLength = Length(stored);
            if (fabs(storedLength - targetLength) > targetLength * 128.0 * FLT_EPSILON || !Normalize(reference) || !Normalize(stored) || (settings.maxAngleDegrees < 180.0f && Dot(stored, reference) < directionCosineLimit))
            {
                error.Assign("Secondary motion float state cannot preserve its length or bend limit");
                return false;
            }
        }
        if (!CheckSecondaryMotionContacts(currentShapes, shapeCount, positions.Data(), count, error))
        {
            return false;
        }
    }
    state.points.MoveFrom(candidate.points);
    state.previousTargets.MoveFrom(candidate.previousTargets);
    error.Clear();
    return true;
}

bool StepSecondaryMotionChain(const gk::Vec3* targets, uint32_t count, const gk::FModelSecondaryMotionSettings& settings, double delta, FModelSecondaryMotionChainState& state, gk::String& error)
{
    return StepSecondaryMotionChainCore(targets, count, settings, delta, nullptr, nullptr, 0, state, error);
}

bool StepSecondaryMotionChainWithCollisions(const gk::Vec3* targets, uint32_t count, const gk::FModelSecondaryMotionSettings& settings, double delta, const FModelSecondaryMotionCollisionShape* previousShapes, const FModelSecondaryMotionCollisionShape* currentShapes, uint32_t shapeCount, FModelSecondaryMotionChainState& state, gk::String& error)
{
    return StepSecondaryMotionChainCore(targets, count, settings, delta, previousShapes, currentShapes, shapeCount, state, error);
}

}
