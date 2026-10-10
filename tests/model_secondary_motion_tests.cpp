// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionSolver.h"

#include <math.h>
#include <limits>
#include <stdio.h>

namespace
{

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
 * 3成分の値の距離をdoubleで求める。
 */
double Distance(const gk::Vec3& first, const gk::Vec3& second)
{
    const double x = static_cast<double>(first.x) - second.x;
    const double y = static_cast<double>(first.y) - second.y;
    const double z = static_cast<double>(first.z) - second.z;
    return sqrt(x * x + y * y + z * z);
}

/**
 * 3成分がすべて有限値かを調べる。
 */
bool IsFinite(const gk::Vec3& value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

/**
 * 鎖の位置と速度がすべて有限値かを調べる。
 */
bool IsFinite(const FModelSecondaryMotionChainState& state)
{
    for (uint32_t index = 0; index < state.points.Count(); ++index)
    {
        if (!IsFinite(state.points.At(index).position) || !IsFinite(state.points.At(index).velocity))
            return false;
    }
    for (uint32_t index = 0; index < state.previousTargets.Count(); ++index)
    {
        if (!IsFinite(state.previousTargets.At(index)))
            return false;
    }
    return true;
}

/**
 * 鎖状態を比較用stateへ複製する。
 */
bool CopyState(const FModelSecondaryMotionChainState& source, FModelSecondaryMotionChainState& destination)
{
    return destination.points.AppendRange(source.points.Data(), source.points.Count()) && destination.previousTargets.AppendRange(source.previousTargets.Data(), source.previousTargets.Count());
}

/**
 * 二つの鎖状態が完全に一致するかを調べる。
 */
bool SameState(const FModelSecondaryMotionChainState& first, const FModelSecondaryMotionChainState& second)
{
    if (first.points.Count() != second.points.Count() || first.previousTargets.Count() != second.previousTargets.Count())
        return false;
    for (uint32_t index = 0; index < first.points.Count(); ++index)
    {
        const FModelSecondaryMotionPoint& a = first.points.At(index);
        const FModelSecondaryMotionPoint& b = second.points.At(index);
        if (a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z || a.velocity.x != b.velocity.x || a.velocity.y != b.velocity.y || a.velocity.z != b.velocity.z)
            return false;
    }
    for (uint32_t index = 0; index < first.previousTargets.Count(); ++index)
    {
        const gk::Vec3& a = first.previousTargets.At(index);
        const gk::Vec3& b = second.previousTargets.At(index);
        if (a.x != b.x || a.y != b.y || a.z != b.z)
            return false;
    }
    return true;
}

/**
 * 重力と風を止め、指定した基準姿勢へ戻る設定を作る。
 */
gk::FModelSecondaryMotionSettings MakeQuietSettings()
{
    gk::FModelSecondaryMotionSettings settings{};
    settings.gravity = { 0.0f, 0.0f, 0.0f };
    settings.windAcceleration = { 0.0f, 0.0f, 0.0f };
    settings.endOffset = { 0.0f, 0.0f, 0.0f };
    return settings;
}

/**
 * 2点のまっすぐな鎖を初期化し、rootと末端を固定目標へ置く。
 */
bool ResetLine(FModelSecondaryMotionChainState& state, gk::String& error)
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    return ResetSecondaryMotionChain(targets, 2, state, error);
}

/**
 * Resetと無風・無重力時の静止鎖を確認する。
 */
bool TestResetAndStationaryChain()
{
    const gk::Vec3 targets[3] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } };
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 3, state, error))
        return Fail(error.CStr());
    if (state.points.Count() != 3 || state.previousTargets.Count() != 3)
        return Fail("reset did not allocate one state and target per chain point");
    for (uint32_t index = 0; index < 3; ++index)
    {
        const FModelSecondaryMotionPoint& point = state.points.At(index);
        if (Distance(point.position, targets[index]) != 0.0 || point.velocity.x != 0.0f || point.velocity.y != 0.0f || point.velocity.z != 0.0f)
            return Fail("reset did not place points at targets with zero velocity");
    }

    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    if (!StepSecondaryMotionChain(targets, 3, settings, 1.0 / 60.0, state, error))
        return Fail(error.CStr());
    for (uint32_t index = 0; index < 3; ++index)
    {
        if (Distance(state.points.At(index).position, targets[index]) > 0.000001 || Distance(state.points.At(index).velocity, { 0.0f, 0.0f, 0.0f }) > 0.000001)
            return Fail("stationary chain moved without external force");
    }
    return true;
}

/**
 * 動くrootへの追従遅れとbone長・最大曲げ角を確認する。
 */
bool TestMovingAnchorLagAndConstraints()
{
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetLine(state, error))
        return Fail(error.CStr());
    const gk::Vec3 targets[2] = { { 0.0f, 0.1f, 0.0f }, { 1.0f, 0.1f, 0.0f } };
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.maxAngleDegrees = 60.0f;
    if (!StepSecondaryMotionChain(targets, 2, settings, 1.0 / 60.0, state, error))
        return Fail(error.CStr());

    const gk::Vec3& root = state.points.At(0).position;
    const gk::Vec3& end = state.points.At(1).position;
    const double length = Distance(root, end);
    const double angleCosine = (static_cast<double>(end.x) - root.x) / length;
    if (fabs(length - 1.0) > 0.001)
        return Fail("moving anchor violated the original bone length");
    if (!(end.y < targets[1].y - 0.005f))
        return Fail("moving anchor did not produce a visible motion lag");
    if (angleCosine < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.01)
        return Fail("moving anchor exceeded the configured bend angle");
    return true;
}

/**
 * 目標が戻った後に揺れが減衰して基準姿勢へ戻ることを確認する。
 */
bool TestDampedReturn()
{
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetLine(state, error))
        return Fail(error.CStr());
    const gk::Vec3 movedTargets[2] = { { 0.0f, 0.1f, 0.0f }, { 1.0f, 0.1f, 0.0f } };
    const gk::Vec3 restTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    if (!StepSecondaryMotionChain(movedTargets, 2, settings, 1.0 / 60.0, state, error))
        return Fail(error.CStr());
    for (uint32_t frame = 0; frame < 120; ++frame)
    {
        if (!StepSecondaryMotionChain(restTargets, 2, settings, 1.0 / 60.0, state, error))
            return Fail(error.CStr());
    }
    if (Distance(state.points.At(0).position, restTargets[0]) != 0.0 || Distance(state.points.At(1).position, restTargets[1]) > 0.01)
        return Fail("damped chain did not return to the animation pose within two seconds");
    if (Distance(state.points.At(1).velocity, { 0.0f, 0.0f, 0.0f }) > 0.01)
        return Fail("damped chain retained significant velocity after settling");
    return true;
}

/**
 * 重力下でも鎖の座標と速度が有限でbone長を保つことを確認する。
 */
bool TestGravityRemainsFinite()
{
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetLine(state, error))
        return Fail(error.CStr());
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    gk::FModelSecondaryMotionSettings settings{};
    settings.windAcceleration = { 0.0f, 0.0f, 0.0f };
    settings.endOffset = { 0.0f, 0.0f, 0.0f };
    for (uint32_t frame = 0; frame < 60; ++frame)
    {
        if (!StepSecondaryMotionChain(targets, 2, settings, 1.0 / 60.0, state, error))
            return Fail(error.CStr());
    }
    if (!IsFinite(state))
        return Fail("gravity produced a non-finite secondary-motion state");
    if (Distance(state.points.At(0).position, targets[0]) != 0.0 || fabs(Distance(state.points.At(0).position, state.points.At(1).position) - 1.0) > 0.001)
        return Fail("gravity broke the pinned root or bone length");
    if (!(state.points.At(1).position.y < -0.001f))
        return Fail("gravity did not affect the free chain point");
    return true;
}

/**
 * 30、60、300FPS相当のdeltaで2秒後の姿勢が近づくことを確認する。
 */
bool RunRateScenario(uint32_t framesPerSecond, FModelSecondaryMotionChainState& output, gk::String& error)
{
    if (!ResetLine(output, error))
        return false;
    const gk::Vec3 targets[2] = { { 0.0f, 0.1f, 0.0f }, { 1.0f, 0.1f, 0.0f } };
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    const double delta = 1.0 / framesPerSecond;
    for (uint32_t frame = 0; frame < framesPerSecond * 2u; ++frame)
    {
        if (!StepSecondaryMotionChain(targets, 2, settings, delta, output, error))
            return false;
    }
    return true;
}

/**
 * 異なるframe rateで同じ2秒を進めた結果を比較する。
 */
bool TestFrameRateConvergence()
{
    FModelSecondaryMotionChainState at30;
    FModelSecondaryMotionChainState at60;
    FModelSecondaryMotionChainState at300;
    gk::String error;
    if (!RunRateScenario(30, at30, error) || !RunRateScenario(60, at60, error) || !RunRateScenario(300, at300, error))
        return Fail(error.CStr());
    if (!IsFinite(at30) || !IsFinite(at60) || !IsFinite(at300))
        return Fail("frame-rate scenarios produced non-finite state");
    for (uint32_t index = 0; index < 2; ++index)
    {
        if (Distance(at30.points.At(index).position, at60.points.At(index).position) > 0.01 || Distance(at60.points.At(index).position, at300.points.At(index).position) > 0.01 || Distance(at30.points.At(index).position, at300.points.At(index).position) > 0.01)
            return Fail("secondary-motion result varied too much between frame rates");
    }
    return true;
}

/**
 * teleportと長いhitchが古い速度を引き継がず状態を再配置することを確認する。
 */
bool TestTeleportAndHitchReset()
{
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetLine(state, error))
        return Fail(error.CStr());
    const gk::Vec3 teleportedTargets[2] = { { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } };
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.teleportDistance = 0.5f;
    if (!StepSecondaryMotionChain(teleportedTargets, 2, settings, 1.0 / 60.0, state, error))
        return Fail(error.CStr());
    for (uint32_t index = 0; index < 2; ++index)
    {
        if (Distance(state.points.At(index).position, teleportedTargets[index]) > 0.000001 || Distance(state.points.At(index).velocity, { 0.0f, 0.0f, 0.0f }) > 0.000001)
            return Fail("teleport did not reset point positions and velocities");
    }

    if (!ResetLine(state, error))
        return Fail(error.CStr());
    const gk::Vec3 restTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    if (!StepSecondaryMotionChain(restTargets, 2, settings, 0.25, state, error))
        return Fail(error.CStr());
    for (uint32_t index = 0; index < 2; ++index)
    {
        if (Distance(state.points.At(index).position, restTargets[index]) > 0.000001 || Distance(state.points.At(index).velocity, { 0.0f, 0.0f, 0.0f }) > 0.000001)
            return Fail("long frame hitch retained spring energy");
    }
    return true;
}

/**
 * 目標方向が瞬時に反転したとき、中間のゼロ長方向で失敗せず速度をリセットする。
 */
bool TestOppositeDirectionResets()
{
    FModelSecondaryMotionChainState state;
    gk::String error;
    const gk::Vec3 original[2] = { { 0, 0, 0 }, { 1, 0, 0 } };
    const gk::Vec3 opposite[2] = { { 0, 0, 0 }, { -1, 0, 0 } };
    const auto settings = MakeQuietSettings();
    if (!ResetSecondaryMotionChain(original, 2, state, error))
    {
        return Fail(error.CStr());
    }
    state.points.At(1).velocity.y = 2.0f;
    if (!StepSecondaryMotionChain(opposite, 2, settings, 1.0 / 60.0, state, error) || Distance(state.points.At(1).position, opposite[1]) > 0.000001 || Distance(state.points.At(1).velocity, {}) > 0.000001)
    {
        return Fail("opposite secondary target directions did not reset safely");
    }
    return true;
}

/**
 * deltaが0なら積分せず、root目標だけを更新することを確認する。
 */
bool TestZeroDeltaUpdatesOnlyRoot()
{
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetLine(state, error))
        return Fail(error.CStr());
    const gk::Vec3 targets[2] = { { 0.0f, 0.1f, 0.0f }, { 1.0f, 0.1f, 0.0f } };
    const FModelSecondaryMotionPoint previousEnd = state.points.At(1);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    if (!StepSecondaryMotionChain(targets, 2, settings, 0.0, state, error))
        return Fail(error.CStr());
    if (Distance(state.points.At(0).position, targets[0]) > 0.000001)
        return Fail("zero-delta update did not follow the current root target");
    const FModelSecondaryMotionPoint& end = state.points.At(1);
    if (Distance(end.position, previousEnd.position) != 0.0 || Distance(end.velocity, previousEnd.velocity) != 0.0)
        return Fail("zero-delta update integrated a non-root point");
    return true;
}

/**
 * 失敗したstepが既存stateを変更せず、診断を返すことを確認する。
 */
bool ExpectRejectedStep(const gk::Vec3* targets, uint32_t count, const gk::FModelSecondaryMotionSettings& settings, double delta, bool mismatchState = false)
{
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetLine(state, error))
        return Fail(error.CStr());
    const gk::Vec3 movedTargets[2] = { { 0.0f, 0.05f, 0.0f }, { 1.0f, 0.05f, 0.0f } };
    const gk::FModelSecondaryMotionSettings validSettings = MakeQuietSettings();
    if (!StepSecondaryMotionChain(movedTargets, 2, validSettings, 1.0 / 60.0, state, error))
        return Fail(error.CStr());
    if (mismatchState && state.previousTargets.RemoveAt(1) == false)
        return Fail("could not create mismatched secondary-motion state");
    FModelSecondaryMotionChainState snapshot;
    if (!CopyState(state, snapshot))
        return Fail("secondary-motion state snapshot allocation failed");
    error.Clear();
    if (StepSecondaryMotionChain(targets, count, settings, delta, state, error) || error.Empty() || !SameState(state, snapshot))
        return Fail("rejected secondary-motion input changed state or omitted its diagnostic");
    return true;
}

/**
 * 不正入力、設定、鎖長、配列数を拒否して既存stateを保つことを確認する。
 */
bool TestInvalidInputsPreserveState()
{
    const gk::Vec3 validTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const gk::FModelSecondaryMotionSettings validSettings = MakeQuietSettings();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    if (!ExpectRejectedStep(validTargets, 2, validSettings, -0.01) || !ExpectRejectedStep(validTargets, 2, validSettings, nan) || !ExpectRejectedStep(validTargets, 2, validSettings, infinity))
        return false;
    gk::Vec3 tooManyTargets[1026]{};
    for (uint32_t index = 0; index < 1026; ++index)
        tooManyTargets[index] = { static_cast<float>(index), 0.0f, 0.0f };
    if (!ExpectRejectedStep(validTargets, 1, validSettings, 1.0 / 60.0) || !ExpectRejectedStep(tooManyTargets, 1026, validSettings, 1.0 / 60.0) || !ExpectRejectedStep(nullptr, 2, validSettings, 1.0 / 60.0))
        return false;
    const gk::Vec3 threeTargets[3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };
    if (!ExpectRejectedStep(threeTargets, 3, validSettings, 1.0 / 60.0) || !ExpectRejectedStep(validTargets, 2, validSettings, 1.0 / 60.0, true))
        return false;

    const float floatInfinity = std::numeric_limits<float>::infinity();
    const float floatNaN = std::numeric_limits<float>::quiet_NaN();
    const gk::Vec3 nonFiniteTargets[2] = { { 0.0f, 0.0f, 0.0f }, { floatNaN, 0.0f, 0.0f } };
    const gk::Vec3 infiniteTargets[2] = { { 0.0f, 0.0f, 0.0f }, { floatInfinity, 0.0f, 0.0f } };
    if (!ExpectRejectedStep(nonFiniteTargets, 2, validSettings, 1.0 / 60.0) || !ExpectRejectedStep(infiniteTargets, 2, validSettings, 1.0 / 60.0))
        return false;

    const gk::Vec3 zeroLengthTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    if (!ExpectRejectedStep(zeroLengthTargets, 2, validSettings, 1.0 / 60.0))
        return false;

    const float maximum = std::numeric_limits<float>::max();
    const gk::Vec3 overflowingTargets[2] = { { -maximum, 0.0f, 0.0f }, { maximum, 0.0f, 0.0f } };
    if (!ExpectRejectedStep(overflowingTargets, 2, validSettings, 1.0 / 60.0))
        return false;

    for (uint32_t invalidSetting = 0; invalidSetting < 8; ++invalidSetting)
    {
        gk::FModelSecondaryMotionSettings settings = validSettings;
        switch (invalidSetting)
        {
        case 0:
            settings.frequencyHz = 0.0f;
            break;
        case 1:
            settings.dampingRatio = -1.0f;
            break;
        case 2:
            settings.gravity.x = floatNaN;
            break;
        case 3:
            settings.windAcceleration.y = floatInfinity;
            break;
        case 4:
            settings.maxAngleDegrees = floatNaN;
            break;
        case 5:
            settings.endOffset.z = floatInfinity;
            break;
        case 6:
            settings.teleportDistance = -1.0f;
            break;
        case 7:
            settings.constraintIterations = 0;
            break;
        }
        if (!ExpectRejectedStep(validTargets, 2, settings, 1.0 / 60.0))
            return false;
    }

    return true;
}

/**
 * Resetが不正な入力を拒否し、以前の鎖状態を保つことを確認する。
 */
bool TestResetFailuresPreserveState()
{
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetLine(state, error))
        return Fail(error.CStr());
    FModelSecondaryMotionChainState snapshot;
    if (!CopyState(state, snapshot))
        return Fail("reset state snapshot allocation failed");
    const gk::Vec3 zeroLengthTargets[2] = { { 1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f } };
    if (ResetSecondaryMotionChain(zeroLengthTargets, 2, state, error) || error.Empty() || !SameState(state, snapshot))
        return Fail("zero-length reset changed state or omitted its diagnostic");
    error.Clear();
    if (ResetSecondaryMotionChain(nullptr, 2, state, error) || error.Empty() || !SameState(state, snapshot))
        return Fail("null reset input changed state or omitted its diagnostic");
    error.Clear();
    if (ResetSecondaryMotionChain(zeroLengthTargets, 1, state, error) || error.Empty() || !SameState(state, snapshot))
        return Fail("invalid reset count changed state or omitted its diagnostic");
    return true;
}

/**
 * count上限2と1025を受け入れ、1026を拒否する。
 */
bool TestChainCountLimits()
{
    const gk::Vec3 minimumTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    FModelSecondaryMotionChainState minimumState;
    gk::String error;
    if (!ResetSecondaryMotionChain(minimumTargets, 2, minimumState, error) || minimumState.points.Count() != 2)
        return Fail(error.CStr());

    gk::Vec3 maximumTargets[1025]{};
    for (uint32_t index = 0; index < 1025; ++index)
        maximumTargets[index] = { static_cast<float>(index), 0.0f, 0.0f };
    FModelSecondaryMotionChainState maximumState;
    if (!ResetSecondaryMotionChain(maximumTargets, 1025, maximumState, error) || maximumState.points.Count() != 1025)
        return Fail(error.CStr());
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    if (!StepSecondaryMotionChain(maximumTargets, 1025, settings, 0.0, maximumState, error))
        return Fail(error.CStr());
    return true;
}

}

int main()
{
    if (!TestResetAndStationaryChain() || !TestMovingAnchorLagAndConstraints() || !TestDampedReturn() || !TestGravityRemainsFinite() || !TestFrameRateConvergence() || !TestTeleportAndHitchReset() || !TestOppositeDirectionResets() || !TestZeroDeltaUpdatesOnlyRoot() || !TestInvalidInputsPreserveState() || !TestResetFailuresPreserveState() || !TestChainCountLimits())
        return 1;
    return 0;
}
