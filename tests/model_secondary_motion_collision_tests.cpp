// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionSolver.h"

#include "foundation/Array.h"

#include <math.h>
#include <stdio.h>
#include <limits>

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
 * 倍精度座標の距離を計算する。
 */
double Distance(const gk::FVector3d& first, const gk::FVector3d& second)
{
    const double x = first.value[0] - second.value[0];
    const double y = first.value[1] - second.value[1];
    const double z = first.value[2] - second.value[2];
    return sqrt(x * x + y * y + z * z);
}

/**
 * 倍精度座標の内積を計算する。
 */
double Dot(const gk::FVector3d& first, const gk::FVector3d& second)
{
    return first.value[0] * second.value[0] + first.value[1] * second.value[1] + first.value[2] * second.value[2];
}

/**
 * 倍精度座標がすべて有限か調べる。
 */
bool IsFinite(const gk::FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * 3成分を倍精度座標へ変換する。
 */
gk::FVector3d Point(double x, double y, double z)
{
    return { { x, y, z } };
}

/**
 * 球またはカプセル形状を作る。
 */
FModelSecondaryMotionCollisionShape Shape(gk::Vec3 start, gk::Vec3 end, float radius)
{
    return { start, end, radius };
}

/**
 * 配列を複製して失敗時のatomic挙動を比較する。
 */
bool CopyPositions(const gk::Array<gk::FVector3d>& source, gk::Array<gk::FVector3d>& destination)
{
    return destination.AppendRange(source.Data(), source.Count());
}

/**
 * 座標配列が要素ごとに完全一致するか調べる。
 */
bool SamePositions(const gk::Array<gk::FVector3d>& first, const gk::Array<gk::FVector3d>& second)
{
    if (first.Count() != second.Count())
        return false;
    for (uint32_t index = 0; index < first.Count(); ++index)
    {
        if (first.At(index).value[0] != second.At(index).value[0] || first.At(index).value[1] != second.At(index).value[1] || first.At(index).value[2] != second.At(index).value[2])
            return false;
    }
    return true;
}

/**
 * 鎖の位置と速度がすべて有限か調べる。
 */
bool IsFinite(const FModelSecondaryMotionChainState& state)
{
    for (uint32_t index = 0; index < state.points.Count(); ++index)
    {
        const FModelSecondaryMotionPoint& point = state.points.At(index);
        if (!isfinite(point.position.x) || !isfinite(point.position.y) || !isfinite(point.position.z) || !isfinite(point.velocity.x) || !isfinite(point.velocity.y) || !isfinite(point.velocity.z))
            return false;
    }
    return true;
}

/**
 * 鎖状態の位置、速度、前回目標が完全一致するか調べる。
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
 * 鎖状態を比較用stateへ複製する。
 */
bool CopyState(const FModelSecondaryMotionChainState& source, FModelSecondaryMotionChainState& destination)
{
    return destination.points.AppendRange(source.points.Data(), source.points.Count()) && destination.previousTargets.AppendRange(source.previousTargets.Data(), source.previousTargets.Count());
}

/**
 * 外力を止め、鎖の制約を十分に反復する設定を作る。
 */
gk::FModelSecondaryMotionSettings MakeQuietSettings()
{
    gk::FModelSecondaryMotionSettings settings{};
    settings.gravity = { 0.0f, 0.0f, 0.0f };
    settings.windAcceleration = { 0.0f, 0.0f, 0.0f };
    settings.endOffset = { 0.0f, 0.0f, 0.0f };
    settings.maxAngleDegrees = 60.0f;
    settings.constraintIterations = 32;
    return settings;
}

/**
 * 鎖末端を倍精度の接触判定用座標へ変換する。
 */
gk::FVector3d ToDouble(const gk::Vec3& value)
{
    return Point(value.x, value.y, value.z);
}

/**
 * 各配列位置を接触面から半径比1e-3以内に保つ。
 */
bool TestSphereAndCapsulePointProjection()
{
    const FModelSecondaryMotionCollisionShape capsule = Shape({ -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.5f);
    gk::String error;
    const FModelSecondaryMotionCollisionShape shapes[4] = { Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f), capsule, capsule, Shape({ 2.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f }, 0.25f) };
    const gk::FVector3d roots[4] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0), Point(2.0, 0.0, 0.0) };
    const gk::FVector3d points[4] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.2, 0.0), Point(1.2, 0.1, 0.0), Point(2.0, 0.0, 0.0) };
    const gk::FVector3d references[4] = { Point(0.0, 1.0, 0.0), Point(0.0, 1.0, 0.0), Point(1.0, 0.0, 0.0), Point(2.0, 1.0, 0.0) };
    const gk::FVector3d centers[4] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0), Point(2.0, 0.0, 0.0) };
    const double expectedRadii[4] = { 1.0, 0.5, 0.5, 0.25 };
    for (uint32_t index = 0; index < 4; ++index)
    {
        const gk::FVector3d targets[2] = { roots[index], references[index] };
        gk::Array<gk::FVector3d> positions;
        if (!positions.Append(roots[index]) || !positions.Append(points[index]))
            return Fail("could not allocate projection inputs");
        if (!ProjectSecondaryMotionContacts(&shapes[index], 1, targets, 2, positions, error))
            return Fail(error.CStr());
        if (!IsFinite(positions.At(1)) || fabs(Distance(positions.At(1), centers[index]) - expectedRadii[index]) > expectedRadii[index] * 0.001)
            return Fail("sphere or capsule feature was not projected within the relative tolerance");
    }
    return true;
}

/**
 * 球中心とカプセル軸上の退避方向が有限かつ決定的であることを確認する。
 */
bool TestInsideCenterAndAxisFallback()
{
    const FModelSecondaryMotionCollisionShape shapes[2] = { Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.75f), Shape({ -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.5f) };
    const double expectedRadii[2] = { 0.75, 0.5 };
    const gk::FVector3d references[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0) };
    for (uint32_t shapeIndex = 0; shapeIndex < 2; ++shapeIndex)
    {
        gk::Array<gk::FVector3d> first;
        gk::Array<gk::FVector3d> second;
        gk::String error;
        const gk::FVector3d values[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0) };
        if (!first.AppendRange(values, 2) || !second.AppendRange(values, 2))
            return Fail("could not allocate fallback inputs");
        if (!ProjectSecondaryMotionContacts(&shapes[shapeIndex], 1, references, 2, first, error) || !ProjectSecondaryMotionContacts(&shapes[shapeIndex], 1, references, 2, second, error))
            return Fail(error.CStr());
        if (!SamePositions(first, second))
            return Fail("inside-center fallback direction was not deterministic");
        if (!IsFinite(first.At(1)) || fabs(Distance(first.At(1), Point(0.0, 0.0, 0.0)) - expectedRadii[shapeIndex]) > expectedRadii[shapeIndex] * 0.001)
            return Fail("inside-center fallback did not reach the collider surface");
    }
    return true;
}

/**
 * 節端点が形状外でも節間が貫く場合を接触判定が見つける。
 */
bool TestSpanningSegmentContact()
{
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    const gk::FVector3d positions[2] = { Point(-2.0, 0.0, 0.0), Point(2.0, 0.0, 0.0) };
    gk::String error;
    if (CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error) || error.Empty())
        return Fail("contact check missed a segment passing through a sphere");
    return true;
}

/**
 * 接触許容幅の割合を検証し、不正値でも入力座標を保つ。
 */
bool TestContactToleranceFraction()
{
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    const gk::FVector3d positions[2] = { Point(0.0, 0.0, 0.0), Point(0.49975, 0.0, 0.0) };
    const gk::FVector3d original[2] = { positions[0], positions[1] };
    gk::String error;
    if (!CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error))
        return Fail("default contact tolerance rejected a point inside its allowed band");
    if (CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error, 0.0) || error.Empty())
        return Fail("zero contact tolerance accepted a shallow penetration or omitted a diagnostic");
    if (positions[0].value[0] != original[0].value[0] || positions[0].value[1] != original[0].value[1] || positions[0].value[2] != original[0].value[2] || positions[1].value[0] != original[1].value[0] || positions[1].value[1] != original[1].value[1] || positions[1].value[2] != original[1].value[2])
        return Fail("contact checks modified input positions");
    const double invalidFractions[3] = { -1.0, 1.000001, std::numeric_limits<double>::quiet_NaN() };
    for (uint32_t index = 0; index < 3; ++index)
    {
        if (CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error, invalidFractions[index]) || error.Empty())
            return Fail("invalid contact tolerance fraction was accepted or omitted a diagnostic");
        if (positions[0].value[0] != original[0].value[0] || positions[0].value[1] != original[0].value[1] || positions[0].value[2] != original[0].value[2] || positions[1].value[0] != original[1].value[0] || positions[1].value[1] != original[1].value[1] || positions[1].value[2] != original[1].value[2])
            return Fail("invalid tolerance fraction modified input positions");
    }
    return true;
}

/**
 * 長いcapsuleを横切る固定長segmentを、親点を保って回避する。
 */
bool TestLongCapsuleFixedLengthAvoidance()
{
    const FModelSecondaryMotionCollisionShape capsule = Shape({ 0.0f, -2.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.2f);
    const gk::FVector3d parent = Point(0.3, 0.0, 0.0);
    const gk::FVector3d originalEnd = Point(-0.3, -1.0, 0.0);
    const gk::FVector3d originalAxis = Point(originalEnd.value[0] - parent.value[0], originalEnd.value[1] - parent.value[1], originalEnd.value[2] - parent.value[2]);
    const double segmentLength = Distance(parent, originalEnd);
    gk::FVector3d direction = Point(originalAxis.value[0] / segmentLength, originalAxis.value[1] / segmentLength, originalAxis.value[2] / segmentLength);
    gk::String error;
    for (uint32_t iteration = 0; iteration < 8; ++iteration)
    {
        if (!AvoidSecondaryMotionCollisionSegment(capsule, parent, segmentLength, false, direction, error))
            return Fail(error.CStr());
    }
    const gk::FVector3d endpoint = Point(parent.value[0] + direction.value[0] * segmentLength, parent.value[1] + direction.value[1] * segmentLength, parent.value[2] + direction.value[2] * segmentLength);
    const gk::FVector3d positions[2] = { parent, endpoint };
    if (!CheckSecondaryMotionContacts(&capsule, 1, positions, 2, error))
        return Fail("repeated fixed-length avoidance left a span through the long capsule");
    if (fabs(Distance(parent, endpoint) - segmentLength) > segmentLength * 1.0e-9)
        return Fail("long-capsule avoidance changed the fixed segment length");
    return true;
}

/**
 * rootが形状内でもfirst spanだけを除外し、末端節の侵入は解消する。
 */
bool TestRootInsideFirstSpanException()
{
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    const gk::FVector3d references[2] = { Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0) };
    gk::FVector3d input[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.1, 0.0) };
    const gk::FVector3d outsideRootSpan[2] = { Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0) };
    gk::Array<gk::FVector3d> positions;
    gk::String error;
    if (!positions.AppendRange(input, 2))
        return Fail("could not allocate root-inside inputs");
    if (!CheckSecondaryMotionContacts(&sphere, 1, outsideRootSpan, 2, error))
        return Fail("root-inside exception did not ignore only its first span");
    if (!ProjectSecondaryMotionContacts(&sphere, 1, references, 2, positions, error))
        return Fail(error.CStr());
    if (Distance(positions.At(0), input[0]) != 0.0 || !(Distance(positions.At(1), input[1]) > 0.0))
        return Fail("root-inside exception moved the root or ignored its free endpoint");
    if (fabs(Distance(positions.At(1), Point(0.0, 0.0, 0.0)) - 0.5) > 0.0005)
        return Fail("root-inside free endpoint remained inside its collider");
    return true;
}

/**
 * solver本体でもroot内のfirst spanだけを免除し、自由端点を固定長で保つ。
 */
bool TestRootInsideFirstSpanSolverEndpointOnly()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.75f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error) || !StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &sphere, &sphere, 1, state, error))
        return Fail(error.CStr());
    const gk::FVector3d positions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error))
        return Fail("root-inside solver case did not apply its first-span exception");
    if (Distance(positions[0], Point(0.0, 0.0, 0.0)) > 0.000001 || fabs(Distance(positions[0], positions[1]) - 1.0) > 0.001 || Distance(positions[1], Point(0.0, 0.0, 0.0)) < 0.75 - 0.001)
        return Fail("root-inside solver case moved the root or left its free endpoint inside the sphere");
    return true;
}

/**
 * 重なった球と短いcapsuleの接触を解き、解けない複数形状ではstateを保つ。
 */
bool TestMultipleContacts()
{
    const FModelSecondaryMotionCollisionShape shapes[3] = { Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f), Shape({ 0.1f, 0.0f, 0.0f }, { 0.1f, 0.0f, 0.0f }, 0.5f), Shape({ 0.0f, -0.2f, 0.0f }, { 0.0f, 0.2f, 0.0f }, 0.25f) };
    const gk::FVector3d references[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 1.0, 0.0) };
    gk::Array<gk::FVector3d> positions;
    gk::String error;
    const gk::FVector3d input[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0) };
    if (!positions.AppendRange(input, 2))
        return Fail("could not allocate multi-contact inputs");
    if (!ProjectSecondaryMotionContacts(shapes, 3, references, 2, positions, error))
        return Fail(error.CStr());
    if (!CheckSecondaryMotionContacts(shapes, 3, positions.Data(), positions.Count(), error))
        return Fail(error.CStr());
    if (Distance(positions.At(1), Point(0.0, 0.0, 0.0)) < 0.5 * (1.0 - 0.001) || Distance(positions.At(1), Point(0.1, 0.0, 0.0)) < 0.5 * (1.0 - 0.001) || Distance(positions.At(1), Point(0.0, 0.0, 0.0)) < 0.25 * (1.0 - 0.001))
        return Fail("overlapping spheres or capsule still contain the projected endpoint");

    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape impossible[2] = { Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.1f), Shape({ 0.1f, 0.0f, 0.0f }, { 0.1f, 0.0f, 0.0f }, 1.1f) };
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    if (!ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
        return Fail("could not initialize impossible multi-contact state");
    if (StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, impossible, impossible, 2, state, error) || error.Empty() || !SameState(state, snapshot))
        return Fail("infeasible overlapping colliders changed state or omitted a diagnostic");
    return true;
}

/**
 * 不正な形状や配列数を拒否し、projection入力を変更しない。
 */
bool TestInvalidShapesAndAtomicProjection()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const FModelSecondaryMotionCollisionShape valid = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f);
    const FModelSecondaryMotionCollisionShape zeroRadius = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f);
    const FModelSecondaryMotionCollisionShape nonFinite = Shape({ nan, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f);
    gk::String error;
    if (!ValidateSecondaryMotionCollisionShapes(nullptr, 0, error) || !ValidateSecondaryMotionCollisionShapes(&valid, 1, error))
        return Fail("valid empty or spherical collider set was rejected");
    if (ValidateSecondaryMotionCollisionShapes(nullptr, 1, error) || error.Empty() || ValidateSecondaryMotionCollisionShapes(&zeroRadius, 1, error) || error.Empty() || ValidateSecondaryMotionCollisionShapes(&nonFinite, 1, error) || error.Empty())
        return Fail("invalid collider input was accepted or lacked a diagnostic");
    FModelSecondaryMotionCollisionShape tooMany[65]{};
    for (uint32_t index = 0; index < 65; ++index)
        tooMany[index] = valid;
    if (ValidateSecondaryMotionCollisionShapes(tooMany, 65, error) || error.Empty())
        return Fail("collider count above 64 was accepted");

    const gk::FVector3d reference[2] = { Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0) };
    gk::Array<gk::FVector3d> positions;
    gk::Array<gk::FVector3d> snapshot;
    const gk::FVector3d input[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.1, 0.0) };
    if (!positions.AppendRange(input, 2) || !CopyPositions(positions, snapshot))
        return Fail("could not allocate atomicity inputs");
    if (ProjectSecondaryMotionContacts(&valid, 1, reference, 1, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("mismatched point count changed projection positions");
    if (ProjectSecondaryMotionContacts(&nonFinite, 1, reference, 2, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("invalid collider changed projection positions");
    if (ProjectSecondaryMotionContacts(&valid, 1, nullptr, 2, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("null projection references changed positions or omitted a diagnostic");
    if (ProjectSecondaryMotionContacts(&valid, 1, reference, 1026, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("projection point count above 1025 changed positions or was accepted");
    const gk::FVector3d nonFinitePosition[2] = { Point(0.0, 0.0, 0.0), Point(std::numeric_limits<double>::infinity(), 0.0, 0.0) };
    if (CheckSecondaryMotionContacts(&valid, 1, nonFinitePosition, 2, error) || error.Empty())
        return Fail("contact checker accepted non-finite point coordinates");
    const double largest = std::numeric_limits<double>::max();
    const gk::FVector3d overflowingInput[2] = { Point(-largest, 0.0, 0.0), Point(largest, 0.0, 0.0) };
    gk::Array<gk::FVector3d> overflowingPositions;
    gk::Array<gk::FVector3d> overflowingSnapshot;
    if (!overflowingPositions.AppendRange(overflowingInput, 2) || !CopyPositions(overflowingPositions, overflowingSnapshot))
        return Fail("could not allocate overflow inputs");
    if (ProjectSecondaryMotionContacts(&valid, 1, reference, 2, overflowingPositions, error) || error.Empty() || !SamePositions(overflowingPositions, overflowingSnapshot))
        return Fail("overflowing projection changed positions or omitted a diagnostic");
    return true;
}

/**
 * colliderなしの新APIが既存solverと同じstateを作る。
 */
bool TestNoColliderMatchesLegacyStep()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const gk::Vec3 movedTargets[2] = { { 0.0f, 0.05f, 0.0f }, { 1.0f, 0.05f, 0.0f } };
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState legacy;
    FModelSecondaryMotionChainState integrated;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, legacy, error) || !ResetSecondaryMotionChain(targets, 2, integrated, error))
        return Fail(error.CStr());
    if (!StepSecondaryMotionChain(movedTargets, 2, settings, 1.0 / 60.0, legacy, error) || !StepSecondaryMotionChainWithCollisions(movedTargets, 2, settings, 1.0 / 60.0, nullptr, nullptr, 0, integrated, error))
        return Fail(error.CStr());
    if (!SameState(legacy, integrated))
        return Fail("zero-collider solver changed legacy secondary-motion results");
    return true;
}

/**
 * 接触を解消しつつ骨長と最大曲げ角を保つ。
 */
bool TestFeasibleContactWithLengthAndAngle()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.2f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error))
        return Fail(error.CStr());
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &sphere, &sphere, 1, state, error))
        return Fail(error.CStr());
    const gk::FVector3d positions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error))
        return Fail(error.CStr());
    const double length = Distance(positions[0], positions[1]);
    const double cosine = (positions[1].value[0] - positions[0].value[0]) / length;
    if (fabs(length - 1.0) > 0.001 || cosine < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
        return Fail("collision response violated segment length or the maximum bend angle");
    if (!IsFinite(state))
        return Fail("feasible collision produced non-finite chain state");
    return true;
}

/**
 * 半径より短いcapsuleを避ける可行姿勢と、角度制限で届かない姿勢を区別する。
 */
bool TestShortCapsuleLengthAndAngleFeasibility()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape shortCapsule = Shape({ 0.4f, -0.1f, 0.0f }, { 0.4f, 0.1f, 0.0f }, 0.3f);
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState feasible;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, feasible, error) || !StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &shortCapsule, &shortCapsule, 1, feasible, error))
        return Fail(error.CStr());
    const gk::FVector3d feasiblePositions[2] = { ToDouble(feasible.points.At(0).position), ToDouble(feasible.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&shortCapsule, 1, feasiblePositions, 2, error) || fabs(Distance(feasiblePositions[0], feasiblePositions[1]) - 1.0) > 0.001)
        return Fail("feasible short-capsule contact violated contact or fixed length");
    const double feasibleCosine = feasiblePositions[1].value[0] - feasiblePositions[0].value[0];
    if (feasibleCosine < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
        return Fail("feasible short-capsule contact exceeded its angle cone");

    settings.maxAngleDegrees = 30.0f;
    FModelSecondaryMotionChainState infeasible;
    FModelSecondaryMotionChainState snapshot;
    if (!ResetSecondaryMotionChain(targets, 2, infeasible, error) || !CopyState(infeasible, snapshot))
        return Fail("could not initialize angle-limited short-capsule state");
    if (StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &shortCapsule, &shortCapsule, 1, infeasible, error) || error.Empty() || !SameState(infeasible, snapshot))
        return Fail("short-capsule configuration outside the angle cone changed state or omitted a diagnostic");
    return true;
}

/**
 * 根から届かない大きさのcolliderを失敗として扱いstateを保つ。
 */
bool TestInfeasibleContactPreservesState()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape enclosingSphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 2.0f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
        return Fail("could not initialize infeasible-contact state");
    if (StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &enclosingSphere, &enclosingSphere, 1, state, error) || error.Empty() || !SameState(state, snapshot))
        return Fail("infeasible collision changed state or omitted its diagnostic");
    return true;
}

/**
 * 前回形状から移動したcolliderの現在位置で接触を解消する。
 */
bool TestMovingColliderUsesCurrentGeometry()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape previous = Shape({ 10.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, 0.2f);
    const FModelSecondaryMotionCollisionShape current = Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.2f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error))
        return Fail(error.CStr());
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &previous, &current, 1, state, error))
        return Fail(error.CStr());
    const gk::FVector3d positions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&current, 1, positions, 2, error))
        return Fail("moving collider was not resolved using its current geometry");
    return true;
}

/**
 * 実モデルで失敗した5点鎖と8形状を、同じ条件で接触・長さ・角度まで解く。
 */
bool TestHumanoidContactDiagnosticFixture()
{
    const gk::Vec3 targets[5] = { { -0.292088538f, 0.735598028f, 0.0779823661f }, { -0.309515864f, 0.642467797f, 0.0847655386f }, { -0.316058636f, 0.580570996f, 0.0880436525f }, { -0.31906569f, 0.544709206f, 0.0882549584f }, { -0.321827203f, 0.509852648f, 0.0867370144f } };
    const gk::Vec3 initialPositions[5] = { { -0.292088538f, 0.735598028f, 0.0779823661f }, { -0.309515864f, 0.642422117f, 0.0847655386f }, { -0.316058636f, 0.580525316f, 0.0880436525f }, { -0.31906569f, 0.544663526f, 0.0882549584f }, { -0.321827203f, 0.509806968f, 0.0867370144f } };
    const FModelSecondaryMotionCollisionShape shapes[8] = { Shape({ -0.257206559f, 0.666783452f, 0.01454602f }, { -0.257206559f, 0.666783452f, 0.01454602f }, 0.0700000003f), Shape({ -0.255830616f, 0.720732927f, 0.0188435949f }, { -0.25763166f, 0.819524765f, 0.0671854168f }, 0.0800000057f), Shape({ -0.257779539f, 0.827636957f, 0.0711549744f }, { -0.264181793f, 0.95585072f, 0.0997946933f }, 0.0800000057f), Shape({ -0.271002412f, 1.09116471f, 0.141395211f }, { -0.271002412f, 1.09116471f, 0.141395211f }, 0.0900000036f), Shape({ -0.196682364f, 0.608489573f, -0.0214490816f }, { -0.146920756f, 0.406621516f, 0.00809899252f }, 0.0400000066f), Shape({ -0.324333489f, 0.615972042f, 0.0102782352f }, { -0.350094438f, 0.45510757f, 0.142788574f }, 0.0399999991f), Shape({ -0.13756229f, 0.353622943f, -0.00904272217f }, { -0.124646932f, 0.155900031f, -0.220794544f }, 0.0320000015f), Shape({ -0.359082699f, 0.401296169f, 0.154177889f }, { -0.405822426f, 0.131814614f, 0.0577669367f }, 0.0320000015f) };
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.frequencyHz = 5.0f;
    settings.dampingRatio = 0.8f;
    settings.gravity = { 0.0f, -1.0f, 0.0f };
    settings.endOffset = { 0.0f, 0.0f, 0.035f };
    settings.constraintIterations = 8;
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 5, state, error))
        return Fail(error.CStr());
    for (uint32_t index = 0; index < 5; ++index)
        state.points.At(index).position = initialPositions[index];
    if (!StepSecondaryMotionChainWithCollisions(targets, 5, settings, 0.0, shapes, shapes, 8, state, error))
        return Fail(error.CStr());
    gk::FVector3d positions[5]{};
    for (uint32_t index = 0; index < 5; ++index)
        positions[index] = ToDouble(state.points.At(index).position);
    if (!CheckSecondaryMotionContacts(shapes, 8, positions, 5, error))
        return Fail("humanoid contact fixture left a point or span inside a collider");
    for (uint32_t index = 1; index < 5; ++index)
    {
        const gk::FVector3d targetAxis = Point(targets[index].x - targets[index - 1].x, targets[index].y - targets[index - 1].y, targets[index].z - targets[index - 1].z);
        const gk::FVector3d solvedAxis = Point(positions[index].value[0] - positions[index - 1].value[0], positions[index].value[1] - positions[index - 1].value[1], positions[index].value[2] - positions[index - 1].value[2]);
        const double targetLength = Distance(Point(0.0, 0.0, 0.0), targetAxis);
        const double solvedLength = Distance(Point(0.0, 0.0, 0.0), solvedAxis);
        if (!(solvedLength > 0.0) || fabs(solvedLength - targetLength) > targetLength * 0.001 || Dot(solvedAxis, targetAxis) / (solvedLength * targetLength) < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
            return Fail("humanoid contact fixture violated a segment length or bend limit");
    }
    return true;
}

/**
 * 不正な前回・今回形状を拒否し、鎖状態を変更しない。
 */
bool ExpectRejectedCollisionStep(const FModelSecondaryMotionCollisionShape* previousShapes, const FModelSecondaryMotionCollisionShape* currentShapes, uint32_t shapeCount)
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error))
        return Fail(error.CStr());
    state.points.At(1).velocity = { 0.25f, -0.5f, 0.75f };
    if (!CopyState(state, snapshot))
        return Fail("could not copy collider-validation state");
    if (StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, previousShapes, currentShapes, shapeCount, state, error) || error.Empty() || !SameState(state, snapshot))
        return Fail("invalid previous or current collider input changed the chain state");
    return true;
}

/**
 * 前回形状と今回形状のnull、NaN、上限超過をatomicに拒否する。
 */
bool TestCollisionShapeValidationIsAtomic()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const FModelSecondaryMotionCollisionShape valid = Shape({ 4.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f }, 0.5f);
    const FModelSecondaryMotionCollisionShape invalidPrevious = Shape({ nan, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f }, 0.5f);
    const FModelSecondaryMotionCollisionShape invalidCurrent = Shape({ 4.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f }, nan);
    if (!ExpectRejectedCollisionStep(nullptr, &valid, 1) || !ExpectRejectedCollisionStep(&valid, nullptr, 1) || !ExpectRejectedCollisionStep(&invalidPrevious, &valid, 1) || !ExpectRejectedCollisionStep(&valid, &invalidCurrent, 1))
        return false;
    FModelSecondaryMotionCollisionShape tooMany[65]{};
    for (uint32_t index = 0; index < 65; ++index)
        tooMany[index] = valid;
    if (!ExpectRejectedCollisionStep(tooMany, tooMany, 65))
        return false;
    return true;
}

/**
 * 未初期化鎖でも現在形状の接触を解消してからstateを作る。
 */
bool TestEmptyStateInitializationResolvesContact()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.2f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (state.points.Count() != 0 || state.previousTargets.Count() != 0)
        return Fail("new chain state was not empty");
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &sphere, &sphere, 1, state, error))
        return Fail(error.CStr());
    const gk::FVector3d positions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error))
        return Fail(error.CStr());
    if (Distance(positions[0], Point(0.0, 0.0, 0.0)) > 0.000001 || fabs(Distance(positions[0], positions[1]) - 1.0) > 0.001)
        return Fail("contact-aware initialization broke the root or bone length");
    return true;
}

/**
 * deltaが0でも移動後のroot目標と今回形状で接触を解く。
 */
bool TestZeroDeltaPoseChangeResolvesCurrentContact()
{
    const gk::Vec3 initialTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const gk::Vec3 movedTargets[2] = { { 0.0f, 0.1f, 0.0f }, { 1.0f, 0.1f, 0.0f } };
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 1.0f, 0.1f, 0.0f }, { 1.0f, 0.1f, 0.0f }, 0.2f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(initialTargets, 2, state, error))
        return Fail(error.CStr());
    const gk::Vec3 oldEnd = state.points.At(1).position;
    if (!StepSecondaryMotionChainWithCollisions(movedTargets, 2, settings, 0.0, &sphere, &sphere, 1, state, error))
        return Fail(error.CStr());
    const gk::FVector3d positions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error))
        return Fail(error.CStr());
    if (Distance(positions[0], ToDouble(movedTargets[0])) > 0.000001 || Distance(ToDouble(state.points.At(1).position), ToDouble(oldEnd)) < 0.01)
        return Fail("zero-delta pose change skipped root following or current contact resolution");
    return true;
}

/**
 * hitchとteleportで接触不能ならreset結果を確定せずstateを保つ。
 */
bool TestHitchAndTeleportInfeasibleContactsAreAtomic()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const gk::Vec3 teleportedTargets[2] = { { 0.6f, 0.0f, 0.0f }, { 1.6f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape enclosingSphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 2.0f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    for (uint32_t scenario = 0; scenario < 2; ++scenario)
    {
        FModelSecondaryMotionChainState state;
        FModelSecondaryMotionChainState snapshot;
        gk::String error;
        if (!ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
            return Fail("could not initialize hitch or teleport state");
        const gk::Vec3* stepTargets = scenario == 0 ? targets : teleportedTargets;
        const double delta = scenario == 0 ? 0.25 : 1.0 / 60.0;
        if (StepSecondaryMotionChainWithCollisions(stepTargets, 2, settings, delta, &enclosingSphere, &enclosingSphere, 1, state, error) || error.Empty() || !SameState(state, snapshot))
            return Fail("infeasible hitch or teleport contact changed the existing state");
    }
    return true;
}

/**
 * floatへ保存した後も小半径接触を許容誤差内に保つか、stateを保って失敗する。
 */
bool TestFloatRoundingDoesNotHideSmallColliderPenetration()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 1.0e-8f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
        return Fail("could not initialize float-rounding state");
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &sphere, &sphere, 1, state, error))
    {
        if (error.Empty() || !SameState(state, snapshot))
            return Fail("small-collider float failure changed state or omitted its diagnostic");
        return true;
    }
    const gk::FVector3d positions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error))
        return Fail("float output rounding accepted a small-collider penetration");
    return true;
}

/**
 * float保存後にも固定長と角度coneを保つか、崩れる配置をatomicに拒否する。
 */
bool TestFloatRoundingPreservesLengthAndAngle()
{
    const gk::Vec3 targets[2] = { { 1000000.0f, 0.0f, 0.0f }, { 1000001.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 1000000.5f, 0.0f, 0.0f }, { 1000000.5f, 0.0f, 0.0f }, 0.27f);
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.maxAngleDegrees = 33.0f;
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
        return Fail("could not initialize float-constraint state");
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 1.0 / 60.0, &sphere, &sphere, 1, state, error))
    {
        if (error.Empty() || !SameState(state, snapshot))
            return Fail("float-constraint rejection changed state or omitted a diagnostic");
        return true;
    }
    const gk::FVector3d positions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&sphere, 1, positions, 2, error))
        return Fail("float-constraint success accepted a collider penetration");
    const double length = Distance(positions[0], positions[1]);
    const double cosine = (positions[1].value[0] - positions[0].value[0]) / length;
    if (fabs(length - 1.0) > 0.001 || cosine < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
        return Fail("float output rounding violated fixed length or the maximum bend angle");
    return true;
}

}

int main()
{
    bool (*tests[])(void) = { TestSphereAndCapsulePointProjection, TestInsideCenterAndAxisFallback, TestSpanningSegmentContact, TestContactToleranceFraction, TestLongCapsuleFixedLengthAvoidance, TestRootInsideFirstSpanException, TestRootInsideFirstSpanSolverEndpointOnly, TestMultipleContacts, TestInvalidShapesAndAtomicProjection, TestNoColliderMatchesLegacyStep, TestFeasibleContactWithLengthAndAngle, TestShortCapsuleLengthAndAngleFeasibility, TestInfeasibleContactPreservesState, TestMovingColliderUsesCurrentGeometry, TestHumanoidContactDiagnosticFixture, TestCollisionShapeValidationIsAtomic, TestEmptyStateInitializationResolvesContact, TestZeroDeltaPoseChangeResolvesCurrentContact, TestHitchAndTeleportInfeasibleContactsAreAtomic, TestFloatRoundingDoesNotHideSmallColliderPenetration, TestFloatRoundingPreservesLengthAndAngle };
    const char* names[] = { "sphere and capsule projection", "inside fallback", "spanning segment", "contact tolerance fraction", "long-capsule fixed-length avoidance", "root-inside exception", "root-inside solver endpoint-only", "multiple contacts", "invalid input atomicity", "legacy equivalence", "feasible contact", "short capsule feasibility", "infeasible atomicity", "moving collider", "humanoid contact fixture", "collision shape validation", "empty state initialization", "zero-delta pose change", "hitch and teleport atomicity", "float contact rounding", "float constraint rounding" };
    bool succeeded = true;
    for (uint32_t index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index)
    {
        if (!tests[index]())
        {
            fprintf(stderr, "Failed test: %s\n", names[index]);
            succeeded = false;
        }
    }
    return succeeded ? 0 : 1;
}
