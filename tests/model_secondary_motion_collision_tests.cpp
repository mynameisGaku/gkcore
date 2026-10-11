// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/FModelSecondaryMotionStepShapeCache.h"

#include "foundation/Array.h"
#include "foundation/Memory.h"

#include <math.h>
#include <stdio.h>
#include <limits>
#include <string.h>

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

gk::FVector3d Point(double x, double y, double z);

/**
 * 2本の線分間の最短距離を独立した内積式で計算する。
 */
double SegmentDistance(const gk::FVector3d& firstStart, const gk::FVector3d& firstEnd, const gk::FVector3d& secondStart, const gk::FVector3d& secondEnd)
{
    const gk::FVector3d u = Point(firstEnd.value[0] - firstStart.value[0], firstEnd.value[1] - firstStart.value[1], firstEnd.value[2] - firstStart.value[2]);
    const gk::FVector3d v = Point(secondEnd.value[0] - secondStart.value[0], secondEnd.value[1] - secondStart.value[1], secondEnd.value[2] - secondStart.value[2]);
    const gk::FVector3d w = Point(firstStart.value[0] - secondStart.value[0], firstStart.value[1] - secondStart.value[1], firstStart.value[2] - secondStart.value[2]);
    const double a = Dot(u, u);
    const double b = Dot(u, v);
    const double c = Dot(v, v);
    const double d = Dot(u, w);
    const double e = Dot(v, w);
    if (a < 1.0e-20 && c < 1.0e-20)
    {
        return Distance(firstStart, secondStart);
    }
    if (a < 1.0e-20)
    {
        const double t = fmax(0.0, fmin(1.0, e / c));
        const gk::FVector3d difference = Point(firstStart.value[0] - (secondStart.value[0] + t * v.value[0]), firstStart.value[1] - (secondStart.value[1] + t * v.value[1]), firstStart.value[2] - (secondStart.value[2] + t * v.value[2]));
        return sqrt(Dot(difference, difference));
    }
    if (c < 1.0e-20)
    {
        const double s = fmax(0.0, fmin(1.0, -d / a));
        const gk::FVector3d difference = Point(firstStart.value[0] + s * u.value[0] - secondStart.value[0], firstStart.value[1] + s * u.value[1] - secondStart.value[1], firstStart.value[2] + s * u.value[2] - secondStart.value[2]);
        return sqrt(Dot(difference, difference));
    }
    const double denominator = a * c - b * b;
    double sNumerator = 0.0;
    double sDenominator = denominator;
    double tNumerator = 0.0;
    double tDenominator = denominator;
    if (denominator < 1.0e-20)
    {
        sNumerator = 0.0;
        sDenominator = 1.0;
        tNumerator = e;
        tDenominator = c;
    }
    else
    {
        sNumerator = b * e - c * d;
        tNumerator = a * e - b * d;
        if (sNumerator < 0.0)
        {
            sNumerator = 0.0;
            tNumerator = e;
            tDenominator = c;
        }
        else if (sNumerator > sDenominator)
        {
            sNumerator = sDenominator;
            tNumerator = e + b;
            tDenominator = c;
        }
    }
    if (tNumerator < 0.0)
    {
        tNumerator = 0.0;
        if (-d < 0.0)
        {
            sNumerator = 0.0;
        }
        else if (-d > a)
        {
            sNumerator = sDenominator;
        }
        else
        {
            sNumerator = -d;
            sDenominator = a;
        }
    }
    else if (tNumerator > tDenominator)
    {
        tNumerator = tDenominator;
        if (-d + b < 0.0)
        {
            sNumerator = 0.0;
        }
        else if (-d + b > a)
        {
            sNumerator = sDenominator;
        }
        else
        {
            sNumerator = -d + b;
            sDenominator = a;
        }
    }
    const double s = fabs(sNumerator) < 1.0e-20 ? 0.0 : sNumerator / sDenominator;
    const double t = fabs(tNumerator) < 1.0e-20 ? 0.0 : tNumerator / tDenominator;
    const gk::FVector3d difference = Point(w.value[0] + s * u.value[0] - t * v.value[0], w.value[1] + s * u.value[1] - t * v.value[1], w.value[2] + s * u.value[2] - t * v.value[2]);
    return sqrt(Dot(difference, difference));
}

/**
 * 任意形状軸と自由線分の距離を調べ、必要半径を保つか返す。
 */
bool IsFreeSegmentClear(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, float pointRadius, const gk::FVector3d& first, const gk::FVector3d& second)
{
    for (uint32_t index = 0; index < shapeCount; ++index)
    {
        const gk::FVector3d axisStart = Point(shapes[index].start.x, shapes[index].start.y, shapes[index].start.z);
        const gk::FVector3d axisEnd = Point(shapes[index].end.x, shapes[index].end.y, shapes[index].end.z);
        const double distance = SegmentDistance(first, second, axisStart, axisEnd);
        if (!isfinite(distance) || distance + 2.0e-6 < static_cast<double>(shapes[index].radius) + pointRadius)
        {
            return false;
        }
    }
    return true;
}

/**
 * root内部の最初のspanだけを除外し、自由点と残りのspanを独立距離式で調べる。
 */
bool IsPointChainClearWithRootException(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* positions, uint32_t pointCount)
{
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        const gk::FVector3d axisStart = Point(shapes[shapeIndex].start.x, shapes[shapeIndex].start.y, shapes[shapeIndex].start.z);
        const gk::FVector3d axisEnd = Point(shapes[shapeIndex].end.x, shapes[shapeIndex].end.y, shapes[shapeIndex].end.z);
        const bool rootInside = SegmentDistance(positions[0], positions[0], axisStart, axisEnd) < static_cast<double>(shapes[shapeIndex].radius) - 2.0e-6;
        for (uint32_t pointIndex = 1; pointIndex < pointCount; ++pointIndex)
        {
            const double pointDistance = SegmentDistance(positions[pointIndex], positions[pointIndex], axisStart, axisEnd);
            if (!isfinite(pointDistance) || pointDistance + 2.0e-6 < shapes[shapeIndex].radius)
                return false;
            if (pointIndex == 1 && rootInside)
                continue;
            const double spanDistance = SegmentDistance(positions[pointIndex - 1], positions[pointIndex], axisStart, axisEnd);
            if (!isfinite(spanDistance) || spanDistance + 2.0e-6 < shapes[shapeIndex].radius)
                return false;
        }
    }
    return true;
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
 * AABBで離れた球とカプセルは最近点計算を省き、境界付近は精密判定へ進む。
 */
bool TestCollisionSegmentBoundsFastPath()
{
    const float largest = std::numeric_limits<float>::max();
    const FModelSecondaryMotionCollisionShape farShapes[8] = { Shape({ 4.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f }, 0.5f), Shape({ 0.0f, 4.0f, 0.0f }, { 0.0f, 4.0f, 0.0f }, 0.5f), Shape({ 0.0f, 0.0f, 4.0f }, { 0.0f, 0.0f, 4.0f }, 0.5f), Shape({ -8.0f, 3.0f, -5.0f }, { -8.0f, 3.0f, -5.0f }, 0.25f), Shape({ 5.0f, -3.0f, 4.0f }, { 8.0f, 1.0f, 7.0f }, 0.5f), Shape({ 8.0f, 1.0f, 7.0f }, { 5.0f, -3.0f, 4.0f }, 0.5f), Shape({ largest, 0.0f, 0.0f }, { largest, 0.0f, 0.0f }, largest), Shape({ -largest, 0.0f, 0.0f }, { -largest, 0.0f, 0.0f }, largest) };
    const double independentFarDistances[6] = { 4.0, 2.0, 4.0, sqrt(90.0), 5.0, 5.0 };
    const gk::FVector3d parent = Point(0.0, 0.0, 0.0);
    const gk::FVector3d originalDirection = Point(0.0, 1.0, 0.0);
    for (uint32_t index = 0; index < 6; ++index)
    {
        if (!(independentFarDistances[index] > farShapes[index].radius))
            return Fail("far-path fixture is not geometrically separated from its segment");
        gk::FVector3d direction = originalDirection;
        gk::String error;
        ResetSecondaryMotionCollisionAxisQueryCount();
        if (!AvoidSecondaryMotionCollisionSegment(farShapes[index], parent, 2.0, false, direction, error))
            return Fail(error.CStr());
        if (GetSecondaryMotionCollisionAxisQueryCount() != 0 || memcmp(&direction, &originalDirection, sizeof(direction)) != 0)
            return Fail("separated sphere or capsule used an exact axis query or changed its normalized direction");
    }

    // 半径と端点がFLT_MAX近辺でもdoubleの境界計算は保守的に精密判定へ送る。
    for (uint32_t index = 6; index < 8; ++index)
    {
        gk::FVector3d direction = originalDirection;
        gk::String error;
        ResetSecondaryMotionCollisionAxisQueryCount();
        if (!AvoidSecondaryMotionCollisionSegment(farShapes[index], parent, 2.0, false, direction, error))
            return Fail(error.CStr());
        if (GetSecondaryMotionCollisionAxisQueryCount() == 0 || memcmp(&direction, &originalDirection, sizeof(direction)) != 0)
            return Fail("FLT_MAX radius boundary was skipped or moved a tangent segment");
    }

    const float oneUlpAboveBoundary = 0.750000059604644775390625f;
    const FModelSecondaryMotionCollisionShape justOutside = Shape({ oneUlpAboveBoundary, 2.6f, 0.0f }, { oneUlpAboveBoundary, 2.6f, 0.0f }, 0.75f);
    const double oneUlpClosestDistance = sqrt(static_cast<double>(oneUlpAboveBoundary) * oneUlpAboveBoundary + 0.6 * 0.6);
    gk::FVector3d direction = originalDirection;
    gk::String error;
    ResetSecondaryMotionCollisionAxisQueryCount();
    if (!AvoidSecondaryMotionCollisionSegment(justOutside, parent, 2.0, false, direction, error))
        return Fail(error.CStr());
    if (!(oneUlpClosestDistance > justOutside.radius) || GetSecondaryMotionCollisionAxisQueryCount() != 0 || memcmp(&direction, &originalDirection, sizeof(direction)) != 0)
        return Fail("one-ULP separated bounds performed an unnecessary axis query or changed direction");

    const FModelSecondaryMotionCollisionShape boundaryOverlap = Shape({ 0.75f, 2.6f, 0.0f }, { 0.75f, 2.6f, 0.0f }, 0.75f);
    // 線分最近点は(0,2,0)、球中心との距離はsqrt(0.75^2+0.6^2)で半径より大きい。
    const double independentClosestDistance = sqrt(0.75 * 0.75 + 0.6 * 0.6);
    direction = originalDirection;
    ResetSecondaryMotionCollisionAxisQueryCount();
    if (!AvoidSecondaryMotionCollisionSegment(boundaryOverlap, parent, 2.0, false, direction, error))
        return Fail(error.CStr());
    if (!(independentClosestDistance > boundaryOverlap.radius) || GetSecondaryMotionCollisionAxisQueryCount() == 0 || memcmp(&direction, &originalDirection, sizeof(direction)) != 0)
        return Fail("touching expanded bounds did not reach exact geometry or changed a clear direction");

    const double huge = std::numeric_limits<double>::max();
    const gk::FVector3d overflowingParent = Point(huge * 0.75, 0.0, 0.0);
    gk::FVector3d overflowDirection = Point(1.0, 0.0, 0.0);
    const gk::FVector3d originalOverflowDirection = overflowDirection;
    ResetSecondaryMotionCollisionAxisQueryCount();
    if (AvoidSecondaryMotionCollisionSegment(farShapes[0], overflowingParent, huge * 0.5, false, overflowDirection, error) || error.Empty() || memcmp(&overflowDirection, &originalOverflowDirection, sizeof(overflowDirection)) != 0)
        return Fail("overflowing segment endpoint was accepted or changed its direction");
    return true;
}

/**
 * 接触形状の軸は投影と再検査を通して形状ごとに一度だけ準備する。
 */
bool TestShapePreparationBudget()
{
    const FModelSecondaryMotionCollisionShape shapes[2] = { Shape({ 100.0f, 100.0f, 100.0f }, { 100.0f, 100.0f, 100.0f }, 0.5f), Shape({ -50.0f, -40.0f, 20.0f }, { -40.0f, -30.0f, 30.0f }, 0.25f) };
    const gk::FVector3d references[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 1.0, 0.0) };
    gk::Array<gk::FVector3d> positions;
    gk::String error;
    if (!positions.AppendRange(references, 2))
    {
        return Fail("could not initialize shape preparation fixture");
    }
    ResetSecondaryMotionCollisionPrepareCount();
    if (!ProjectSecondaryMotionContacts(shapes, 2, references, 2, positions, error))
    {
        return Fail(error.CStr());
    }
    const uint64_t preparationCount = GetSecondaryMotionCollisionPrepareCount();
    if (preparationCount != 2 || memcmp(positions.Data(), references, sizeof(references)) != 0)
    {
        fprintf(stderr, "shape preparation count=%llu expected=2\n", static_cast<unsigned long long>(preparationCount));
        return Fail("collision projection repeated shape preparation or changed separated points");
    }
    ResetSecondaryMotionCollisionPrepareCount();
    if (!CheckSecondaryMotionContacts(shapes, 2, positions.Data(), 2, error) || GetSecondaryMotionCollisionPrepareCount() != 2)
    {
        return Fail("standalone collision check did not prepare each shape once");
    }
    return true;
}

/**
 * 鎖の1 substepで接触形状を共有し、float保存後の最終検査だけは独立に準備する。
 */
bool TestSolverPreparedShapeBudget()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape farShape = Shape({ 20.0f, 0.0f, 0.0f }, { 20.0f, 0.5f, 0.0f }, 0.25f);
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    gk::String error;
    const auto settings = MakeQuietSettings();
    if (!ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
        return Fail("could not initialize solver shape-preparation budget state");
    ResetSecondaryMotionCollisionPrepareCount();
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, &farShape, &farShape, 1, state, error))
        return Fail(error.CStr());
    if (GetSecondaryMotionCollisionPrepareCount() != 2 || !SameState(state, snapshot))
        return Fail("solver repeated substep shape preparation or changed the quiet pose");
    return true;
}

/**
 * 遠い接触形状では反復後の接触確認を重複実行しない。
 */
bool TestFarColliderContactCheckBudget()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape farSphere = Shape({ 100.0f, 100.0f, 100.0f }, { 100.0f, 100.0f, 100.0f }, 0.5f);
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.constraintIterations = 1;
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error))
        return Fail(error.CStr());
    ResetSecondaryMotionContactCheckCount();
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, &farSphere, &farSphere, 1, state, error))
        return Fail(error.CStr());
    if (GetSecondaryMotionContactCheckCount() != 3)
        return Fail("far-collider solver repeated a successful contact check after the pass was already clear");
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
 * 同じ形状配列を再利用して拡張を一度にし、結果stateを保つ。
 */
bool TestCollisionShapeExpansionReuseBudget()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape shape = Shape({ 20.0f, 0.0f, 0.0f }, { 20.0f, 0.0f, 0.0f }, 0.25f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState sharedPointerState;
    FModelSecondaryMotionChainState sharedPointerSnapshot;
    FModelSecondaryMotionChainState separatePointerState;
    FModelSecondaryMotionChainState separatePointerSnapshot;
    FModelSecondaryMotionChainState zeroRadiusState;
    FModelSecondaryMotionChainState zeroRadiusSnapshot;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, sharedPointerState, error) || !CopyState(sharedPointerState, sharedPointerSnapshot) || !ResetSecondaryMotionChain(targets, 2, separatePointerState, error) || !CopyState(separatePointerState, separatePointerSnapshot) || !ResetSecondaryMotionChain(targets, 2, zeroRadiusState, error) || !CopyState(zeroRadiusState, zeroRadiusSnapshot))
        return Fail("could not initialize expansion-budget states");

    ResetSecondaryMotionCollisionExpansionCount();
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, &shape, &shape, 1, sharedPointerState, error, 0.1f))
        return Fail(error.CStr());
    if (GetSecondaryMotionCollisionExpansionCount() != 1 || !SameState(sharedPointerState, sharedPointerSnapshot))
        return Fail("identical previous/current shape pointers were expanded more than once or changed state");

    const FModelSecondaryMotionCollisionShape previousShape = shape;
    const FModelSecondaryMotionCollisionShape currentShape = shape;
    ResetSecondaryMotionCollisionExpansionCount();
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, &previousShape, &currentShape, 1, separatePointerState, error, 0.1f))
        return Fail(error.CStr());
    if (GetSecondaryMotionCollisionExpansionCount() != 2 || !SameState(separatePointerState, separatePointerSnapshot) || !SameState(sharedPointerState, separatePointerState))
        return Fail("separate equal shape arrays did not expand independently or changed the quiet state");

    ResetSecondaryMotionCollisionExpansionCount();
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, &shape, &shape, 1, zeroRadiusState, error))
        return Fail(error.CStr());
    if (GetSecondaryMotionCollisionExpansionCount() != 0 || !SameState(zeroRadiusState, zeroRadiusSnapshot) || !SameState(sharedPointerState, zeroRadiusState))
        return Fail("zero-radius collision path expanded shapes or changed the quiet state");
    return true;
}

/**
 * 参照segmentの計算回数が反復数で増えず、旧経路と同じ鎖状態を作る。
 */
bool TestReferenceCalculationBudgetAndStateInvariance()
{
    const gk::Vec3 initialTargets[3] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } };
    const gk::Vec3 movedTargets[3] = { { 0.0f, 0.1f, 0.0f }, { 1.0f, 0.1f, 0.0f }, { 2.0f, 0.1f, 0.0f } };
    const double delta = 1.0 / 60.0;
    const uint64_t expectedReferenceCalculations = 4;
    gk::String error;
    for (uint32_t iterationCase = 0; iterationCase < 2; ++iterationCase)
    {
        gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
        settings.constraintIterations = iterationCase == 0 ? 8 : 32;
        FModelSecondaryMotionChainState legacy;
        FModelSecondaryMotionChainState collisionApi;
        if (!ResetSecondaryMotionChain(initialTargets, 3, legacy, error) || !ResetSecondaryMotionChain(initialTargets, 3, collisionApi, error))
            return Fail(error.CStr());
        ResetSecondaryMotionReferenceCalculationCount();
        if (!StepSecondaryMotionChain(movedTargets, 3, settings, delta, legacy, error))
            return Fail(error.CStr());
        if (GetSecondaryMotionReferenceCalculationCount() != expectedReferenceCalculations)
            return Fail("raw solver recomputed reference segments for every constraint pass");
        ResetSecondaryMotionReferenceCalculationCount();
        if (!StepSecondaryMotionChainWithCollisions(movedTargets, 3, settings, delta, nullptr, nullptr, 0, collisionApi, error))
            return Fail(error.CStr());
        if (GetSecondaryMotionReferenceCalculationCount() != expectedReferenceCalculations || !SameState(legacy, collisionApi))
            return Fail("zero-collider solver changed legacy state or repeated reference calculations across passes");
    }

    const gk::Vec3 contactTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape body = Shape({ 0.9f, 0.0f, 0.0f }, { 0.9f, 0.0f, 0.0f }, 0.2f);
    gk::FModelSecondaryMotionSettings contactSettings = MakeQuietSettings();
    contactSettings.constraintIterations = 32;
    FModelSecondaryMotionChainState contactState;
    if (!ResetSecondaryMotionChain(contactTargets, 2, contactState, error))
        return Fail(error.CStr());
    ResetSecondaryMotionReferenceCalculationCount();
    if (!StepSecondaryMotionChainWithCollisions(contactTargets, 2, contactSettings, 0.0, &body, &body, 1, contactState, error))
        return Fail(error.CStr());
    if (GetSecondaryMotionReferenceCalculationCount() != 1)
        return Fail("zero-delta body contact did not calculate one reference segment for its substep");
    const gk::FVector3d contactPositions[2] = { ToDouble(contactState.points.At(0).position), ToDouble(contactState.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&body, 1, contactPositions, 2, error) || fabs(Distance(contactPositions[0], contactPositions[1]) - 1.0) > 0.001)
        return Fail("zero-delta contact result violated independent clearance or bone length");
    const gk::FVector3d solvedDirection = Point(contactPositions[1].value[0] - contactPositions[0].value[0], contactPositions[1].value[1] - contactPositions[0].value[1], contactPositions[1].value[2] - contactPositions[0].value[2]);
    if (solvedDirection.value[0] / Distance(contactPositions[0], contactPositions[1]) < cos(contactSettings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
        return Fail("zero-delta contact result violated the maximum bend angle");

    const FModelSecondaryMotionCollisionShape enclosingBody = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 2.0f);
    FModelSecondaryMotionChainState infeasibleState;
    FModelSecondaryMotionChainState infeasibleSnapshot;
    if (!ResetSecondaryMotionChain(contactTargets, 2, infeasibleState, error) || !CopyState(infeasibleState, infeasibleSnapshot))
        return Fail("could not initialize infeasible reference-cache state");
    ResetSecondaryMotionReferenceCalculationCount();
    if (StepSecondaryMotionChainWithCollisions(contactTargets, 2, contactSettings, 0.0, &enclosingBody, &enclosingBody, 1, infeasibleState, error) || error.Empty() || GetSecondaryMotionReferenceCalculationCount() != 1 || !SameState(infeasibleState, infeasibleSnapshot))
        return Fail("repeated infeasible contact passes recalculated reference data or changed the old state");
    return true;
}

/**
 * 接触shapeの事前検証が失敗したときは参照segmentを計算せず、stateを保つ。
 */
bool TestReferenceCalculationSkippedOnInvalidCollisionInput()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const FModelSecondaryMotionCollisionShape invalid = Shape({ nan, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
        return Fail("could not initialize reference-calculation validation state");
    ResetSecondaryMotionReferenceCalculationCount();
    if (StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, &invalid, &invalid, 1, state, error) || error.Empty() || GetSecondaryMotionReferenceCalculationCount() != 0 || !SameState(state, snapshot))
        return Fail("invalid collision input calculated reference segments or changed the old state");
    // 両sphere内のrootでは最初の線分を除外し、自由節の交互投影が収束しない配置。
    const gk::Vec3 stalledTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 0.1f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape overlapping[2] = { Shape({ -0.5f, 0.0f, 0.0f }, { -0.5f, 0.0f, 0.0f }, 0.8f), Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.8f) };
    FModelSecondaryMotionChainState stalled;
    FModelSecondaryMotionChainState stalledSnapshot;
    if (!ResetSecondaryMotionChain(stalledTargets, 2, stalled, error) || !CopyState(stalled, stalledSnapshot))
        return Fail("could not initialize a stalled contact projection");
    ResetSecondaryMotionReferenceCalculationCount();
    if (StepSecondaryMotionChainWithCollisions(stalledTargets, 2, settings, 0.0, overlapping, overlapping, 2, stalled, error) || error.Empty() || GetSecondaryMotionReferenceCalculationCount() != 0 || !SameState(stalled, stalledSnapshot))
        return Fail("stalled contact projection was bypassed or changed the last state");
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
 * 節自身の半径を加味して身体capsuleから離し、元形状と鎖制約を保つ。
 */
bool TestSolverPointRadiusKeepsBoneSphereClear()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape body = Shape({ 0.9f, 0.0f, 0.0f }, { 0.9f, 0.0f, 0.0f }, 0.2f);
    const FModelSecondaryMotionCollisionShape originalBody = body;
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    const float pointRadius = 0.15f;
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error))
        return Fail(error.CStr());
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, &body, &body, 1, state, error, pointRadius))
        return Fail(error.CStr());
    if (memcmp(&body, &originalBody, sizeof(body)) != 0)
        return Fail("point-radius solver modified the caller-owned body shape");
    const gk::FVector3d positions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    const double bodyDistance = Distance(positions[1], Point(0.9, 0.0, 0.0));
    if (!IsFinite(state) || fabs(Distance(positions[0], positions[1]) - 1.0) > 0.001 || bodyDistance + 0.000001 < static_cast<double>(body.radius) + pointRadius)
        return Fail("point-radius solver violated bone length, finiteness, or body-sphere clearance");
    const double cosine = (positions[1].value[0] - positions[0].value[0]) / Distance(positions[0], positions[1]);
    if (cosine < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
        return Fail("point-radius solver violated the chain angle limit");
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
 * 身体形状へ節の半径を足し、丸め後も接触余白を失わない。
 */
bool TestExpandCollisionShapesForPointRadius()
{
    const FModelSecondaryMotionCollisionShape input[2] = { Shape({ -1.0f, 2.0f, 3.0f }, { 1.0f, 2.0f, 3.0f }, 0.2f), Shape({ 4.0f, 5.0f, 6.0f }, { 4.0f, 5.0f, 6.0f }, 0.75f) };
    gk::Array<FModelSecondaryMotionCollisionShape> expanded;
    gk::String error;
    const float pointRadius = 0.1f;
    if (!ExpandSecondaryMotionCollisionShapes(input, 2, pointRadius, expanded, error))
        return Fail(error.CStr());
    if (expanded.Count() != 2)
        return Fail("point-radius expansion returned the wrong shape count");
    for (uint32_t index = 0; index < 2; ++index)
    {
        const double exactRadius = static_cast<double>(input[index].radius) + static_cast<double>(pointRadius);
        const FModelSecondaryMotionCollisionShape& actual = expanded.At(index);
        if (static_cast<double>(actual.radius) < exactRadius || actual.start.x != input[index].start.x || actual.start.y != input[index].start.y || actual.start.z != input[index].start.z || actual.end.x != input[index].end.x || actual.end.y != input[index].end.y || actual.end.z != input[index].end.z)
            return Fail("point-radius expansion lost clearance or changed the collider axis");
    }

    gk::Array<FModelSecondaryMotionCollisionShape> unchanged;
    if (!ExpandSecondaryMotionCollisionShapes(input, 2, 0.0f, unchanged, error) || unchanged.Count() != 2 || memcmp(unchanged.Data(), input, sizeof(input)) != 0)
        return Fail("zero point radius did not preserve the shape bytes");

    gk::Array<FModelSecondaryMotionCollisionShape> alias;
    if (!alias.AppendRange(input, 2) || !ExpandSecondaryMotionCollisionShapes(alias.Data(), alias.Count(), pointRadius, alias, error) || alias.Count() != 2)
        return Fail("in-place point-radius expansion failed");
    if (alias.At(0).radius < expanded.At(0).radius || alias.At(1).radius < expanded.At(1).radius)
        return Fail("in-place point-radius expansion lost its enlarged radius");
    const FModelSecondaryMotionCollisionShape aliasBefore[2] = { alias.At(0), alias.At(1) };
    if (ExpandSecondaryMotionCollisionShapes(alias.Data(), alias.Count(), std::numeric_limits<float>::max(), alias, error) || error.Empty() || alias.Count() != 2 || memcmp(alias.Data(), aliasBefore, sizeof(aliasBefore)) != 0)
        return Fail("failed in-place point-radius expansion changed its aliased output");

    gk::Array<FModelSecondaryMotionCollisionShape> preserved;
    const FModelSecondaryMotionCollisionShape sentinel = Shape({ 8.0f, 9.0f, 10.0f }, { 8.0f, 9.0f, 10.0f }, 0.5f);
    if (!preserved.Append(sentinel))
        return Fail("could not initialize expansion atomicity output");
    const FModelSecondaryMotionCollisionShape overflow = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, std::numeric_limits<float>::max());
    if (ExpandSecondaryMotionCollisionShapes(&overflow, 1, std::numeric_limits<float>::max(), preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("overflowing point-radius expansion was accepted or changed output");
    const FModelSecondaryMotionCollisionShape invalid = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, std::numeric_limits<float>::quiet_NaN());
    if (ExpandSecondaryMotionCollisionShapes(&invalid, 1, 0.1f, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("non-finite point-radius expansion input was accepted or changed output");
    if (ExpandSecondaryMotionCollisionShapes(nullptr, 1, 0.1f, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("null point-radius expansion input was accepted or changed output");
    if (ExpandSecondaryMotionCollisionShapes(input, 65, 0.1f, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("excessive point-radius expansion count was accepted or changed output");
    if (ExpandSecondaryMotionCollisionShapes(input, 2, -0.1f, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("negative point radius was accepted or changed output");
    if (ExpandSecondaryMotionCollisionShapes(input, 2, std::numeric_limits<float>::quiet_NaN(), preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("non-finite point radius was accepted or changed output");
    return true;
}

/**
 * 自由な線分の両端を押し出し、球・カプセル軸との距離を保つ。
 */
bool TestFreeSegmentSphereAndCapsuleContacts()
{
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    const FModelSecondaryMotionCollisionShape capsule = Shape({ 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.4f);
    const FModelSecondaryMotionCollisionShape shapes[3] = { sphere, capsule, capsule };
    for (uint32_t shapeIndex = 0; shapeIndex < 3; ++shapeIndex)
    {
        const FModelSecondaryMotionCollisionShape& shape = shapes[shapeIndex];
        const gk::FVector3d referenceFirst = shapeIndex == 2 ? Point(-0.2, 1.05, 0.0) : Point(-0.2, 0.0, 0.0);
        const gk::FVector3d referenceSecond = shapeIndex == 2 ? Point(0.2, 1.05, 0.0) : Point(0.2, 0.0, 0.0);
        gk::FVector3d first = referenceFirst;
        gk::FVector3d second = referenceSecond;
        gk::String error;
        if (!ProjectSecondaryMotionFreeSegmentContacts(&shape, 1, 0.1f, referenceFirst, referenceSecond, first, second, error))
            return Fail(error.CStr());
        if (!IsFinite(first) || !IsFinite(second) || Distance(first, referenceFirst) < 0.01 || Distance(second, referenceSecond) < 0.01)
            return Fail("free-segment contact did not move both endpoints from the overlap");
        if (!IsFreeSegmentClear(&shape, 1, 0.1f, first, second) || !CheckSecondaryMotionFreeSegmentContacts(&shape, 1, 0.1f, first, second, error))
            return Fail("free-segment contact left the sphere or capsule axis penetrating");
    }
    return true;
}

/**
 * 複数shape、球端部、接触端点、零長線分を決定的に処理する。
 */
bool TestFreeSegmentMultipleShapesAndDegenerateCases()
{
    const FModelSecondaryMotionCollisionShape shapes[2] = { Shape({ -0.75f, 0.0f, 0.0f }, { -0.75f, 0.0f, 0.0f }, 0.25f), Shape({ 0.75f, 0.0f, 0.0f }, { 0.75f, 0.0f, 0.0f }, 0.25f) };
    const gk::FVector3d referenceFirst = Point(-2.0, 0.0, 0.0);
    const gk::FVector3d referenceSecond = Point(2.0, 0.0, 0.0);
    gk::FVector3d first = referenceFirst;
    gk::FVector3d second = referenceSecond;
    gk::String error;
    if (!ProjectSecondaryMotionFreeSegmentContacts(shapes, 2, 0.1f, referenceFirst, referenceSecond, first, second, error))
        return Fail(error.CStr());
    if (!IsFreeSegmentClear(shapes, 2, 0.1f, first, second) || !CheckSecondaryMotionFreeSegmentContacts(shapes, 2, 0.1f, first, second, error))
        return Fail("multiple free-segment contacts left one of the colliders penetrating");

    const FModelSecondaryMotionCollisionShape endpointSphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f);
    const gk::FVector3d touchingFirst = Point(1.0, 0.0, 0.0);
    const gk::FVector3d touchingSecond = Point(2.0, 0.0, 0.0);
    first = touchingFirst;
    second = touchingSecond;
    if (!ProjectSecondaryMotionFreeSegmentContacts(&endpointSphere, 1, 0.0f, touchingFirst, touchingSecond, first, second, error) || memcmp(&first, &touchingFirst, sizeof(first)) != 0 || memcmp(&second, &touchingSecond, sizeof(second)) != 0)
        return Fail("an exactly touching endpoint was rejected or moved");
    if (!CheckSecondaryMotionFreeSegmentContacts(&endpointSphere, 1, 0.0f, first, second, error))
        return Fail("an exactly touching free segment was rejected by the independent check");

    const gk::FVector3d center = Point(0.0, 0.0, 0.0);
    gk::FVector3d firstResult = center;
    gk::FVector3d secondResult = center;
    gk::FVector3d repeatedFirst = center;
    gk::FVector3d repeatedSecond = center;
    if (!ProjectSecondaryMotionFreeSegmentContacts(&endpointSphere, 1, 0.1f, center, center, firstResult, secondResult, error) || !ProjectSecondaryMotionFreeSegmentContacts(&endpointSphere, 1, 0.1f, center, center, repeatedFirst, repeatedSecond, error))
        return Fail(error.CStr());
    if (memcmp(&firstResult, &repeatedFirst, sizeof(firstResult)) != 0 || memcmp(&secondResult, &repeatedSecond, sizeof(secondResult)) != 0 || !IsFreeSegmentClear(&endpointSphere, 1, 0.1f, firstResult, secondResult))
        return Fail("zero-length free-segment fallback was not deterministic or clear");
    return true;
}

/**
 * shapeなしなら座標を保ち、不正入力は両端をatomicに保持する。
 */
bool TestFreeSegmentValidationAndAtomicity()
{
    const gk::FVector3d referenceFirst = Point(2.0, 3.0, 4.0);
    const gk::FVector3d referenceSecond = Point(5.0, 6.0, 7.0);
    gk::FVector3d first = referenceFirst;
    gk::FVector3d second = referenceSecond;
    gk::String error;
    if (!ProjectSecondaryMotionFreeSegmentContacts(nullptr, 0, 0.0f, referenceFirst, referenceSecond, first, second, error) || memcmp(&first, &referenceFirst, sizeof(first)) != 0 || memcmp(&second, &referenceSecond, sizeof(second)) != 0 || !CheckSecondaryMotionFreeSegmentContacts(nullptr, 0, 0.0f, first, second, error))
        return Fail("empty free-segment contact set changed positions or failed");

    const FModelSecondaryMotionCollisionShape valid = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const FModelSecondaryMotionCollisionShape invalid = Shape({ nan, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f);
    FModelSecondaryMotionCollisionShape tooMany[65]{};
    for (uint32_t index = 0; index < 65; ++index)
        tooMany[index] = valid;
    const gk::FVector3d nonFinite = Point(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
    const gk::FVector3d hugeFirst = Point(std::numeric_limits<double>::max(), 0.0, 0.0);
    const gk::FVector3d hugeSecond = Point(-std::numeric_limits<double>::max(), 0.0, 0.0);
    const double quarterDoubleMax = std::numeric_limits<double>::max() * 0.25;
    const gk::FVector3d finiteDifferenceFirst = Point(-quarterDoubleMax, 0.0, 0.0);
    const gk::FVector3d finiteDifferenceSecond = Point(quarterDoubleMax, 0.0, 0.0);
    first = referenceFirst;
    second = referenceSecond;
    const FModelSecondaryMotionCollisionShape* invalidShapes[5] = { nullptr, &invalid, tooMany, &valid, &valid };
    const uint32_t invalidCounts[5] = { 1, 1, 65, 1, 1 };
    const float invalidRadii[5] = { 0.0f, 0.0f, 0.0f, -0.1f, nan };
    for (uint32_t index = 0; index < 5; ++index)
    {
        if (ProjectSecondaryMotionFreeSegmentContacts(invalidShapes[index], invalidCounts[index], invalidRadii[index], referenceFirst, referenceSecond, first, second, error) || error.Empty() || memcmp(&first, &referenceFirst, sizeof(first)) != 0 || memcmp(&second, &referenceSecond, sizeof(second)) != 0)
            return Fail("invalid free-segment shape input was accepted or changed endpoints");
    }
    if (ProjectSecondaryMotionFreeSegmentContacts(&valid, 1, 0.1f, nonFinite, referenceSecond, first, second, error) || error.Empty() || memcmp(&first, &referenceFirst, sizeof(first)) != 0 || memcmp(&second, &referenceSecond, sizeof(second)) != 0)
        return Fail("non-finite free-segment endpoint was accepted or changed output");
    if (ProjectSecondaryMotionFreeSegmentContacts(&valid, 1, 0.1f, hugeFirst, hugeSecond, first, second, error) || error.Empty() || memcmp(&first, &referenceFirst, sizeof(first)) != 0 || memcmp(&second, &referenceSecond, sizeof(second)) != 0)
        return Fail("overflowing free-segment coordinates were accepted or changed output");
    if (ProjectSecondaryMotionFreeSegmentContacts(&valid, 1, 0.1f, finiteDifferenceFirst, finiteDifferenceSecond, first, second, error) || error.Empty() || memcmp(&first, &referenceFirst, sizeof(first)) != 0 || memcmp(&second, &referenceSecond, sizeof(second)) != 0)
        return Fail("finite free-segment length with overflowing dot products was accepted or changed output");
    if (ProjectSecondaryMotionFreeSegmentContacts(&valid, 1, 0.1f, referenceFirst, referenceSecond, first, first, error) || error.Empty() || memcmp(&first, &referenceFirst, sizeof(first)) != 0)
        return Fail("aliased free-segment endpoints were accepted or changed output");
    const float largestFloat = std::numeric_limits<float>::max();
    const float nearBoundary = largestFloat * 0.75f;
    const FModelSecondaryMotionCollisionShape boundarySphere = Shape({ nearBoundary, 0.0f, 0.0f }, { nearBoundary, 0.0f, 0.0f }, nearBoundary);
    const gk::FVector3d insideBoundarySphere = Point(nearBoundary, 0.0, 0.0);
    const gk::FVector3d outwardReference = Point(largestFloat, 0.0, 0.0);
    gk::FVector3d boundaryFirst = insideBoundarySphere;
    gk::FVector3d boundarySecond = insideBoundarySphere;
    if (ProjectSecondaryMotionFreeSegmentContacts(&boundarySphere, 1, 0.0f, outwardReference, outwardReference, boundaryFirst, boundarySecond, error) || error.Empty() || memcmp(&boundaryFirst, &insideBoundarySphere, sizeof(boundaryFirst)) != 0 || memcmp(&boundarySecond, &insideBoundarySphere, sizeof(boundarySecond)) != 0)
    {
        return Fail("float-range projection overflow was accepted or changed endpoints");
    }
    if (CheckSecondaryMotionFreeSegmentContacts(&valid, 1, -0.1f, referenceFirst, referenceSecond, error) || error.Empty())
        return Fail("negative free-segment radius passed validation");
    if (CheckSecondaryMotionFreeSegmentContacts(&valid, 1, nan, referenceFirst, referenceSecond, error) || error.Empty())
        return Fail("non-finite free-segment radius passed validation");
    if (CheckSecondaryMotionFreeSegmentContacts(nullptr, 1, 0.1f, referenceFirst, referenceSecond, error) || error.Empty())
        return Fail("null free-segment shape passed validation");
    if (CheckSecondaryMotionFreeSegmentContacts(&valid, 1, 0.1f, nonFinite, referenceSecond, error) || error.Empty())
        return Fail("non-finite free-segment endpoint passed validation");
    return true;
}

/**
 * 準備済み形状を再利用し、raw経路と同じ接触結果および失敗時の保持を確かめる。
 */
bool TestPreparedFreeSegmentShapeReuseAndAtomicity()
{
    const FModelSecondaryMotionCollisionShape shapes[2] = { Shape({ -0.75f, 0.0f, 0.0f }, { -0.75f, 0.0f, 0.0f }, 0.25f), Shape({ 0.75f, -0.5f, 0.0f }, { 0.75f, 0.5f, 0.0f }, 0.2f) };
    const float pointRadius = 0.1f;
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    ResetSecondaryMotionCollisionPrepareCount();
    if (!PrepareSecondaryMotionFreeSegmentShapes(shapes, 2, pointRadius, prepared, error) || !error.Empty() || prepared.Count() != 2)
        return Fail("prepared free-segment shapes were not created");
    if (GetSecondaryMotionCollisionPrepareCount() != 2)
        return Fail("preparing two free-segment shapes did not perform exactly two preparations");

    const gk::FVector3d referenceFirst = Point(-2.0, 0.0, 0.0);
    const gk::FVector3d referenceSecond = Point(2.0, 0.0, 0.0);
    gk::FVector3d preparedFirst = referenceFirst;
    gk::FVector3d preparedSecond = referenceSecond;
    if (!ProjectPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), referenceFirst, referenceSecond, preparedFirst, preparedSecond, error))
        return Fail(error.CStr());
    if (!IsFreeSegmentClear(shapes, 2, pointRadius, preparedFirst, preparedSecond) || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), preparedFirst, preparedSecond, error))
        return Fail("prepared projection did not clear every shape by the independent distance check");
    if (GetSecondaryMotionCollisionPrepareCount() != 2)
        return Fail("prepared projection or check rebuilt its shape data");

    gk::FVector3d rawFirst = referenceFirst;
    gk::FVector3d rawSecond = referenceSecond;
    if (!ProjectSecondaryMotionFreeSegmentContacts(shapes, 2, pointRadius, referenceFirst, referenceSecond, rawFirst, rawSecond, error))
        return Fail(error.CStr());
    if (memcmp(&preparedFirst, &rawFirst, sizeof(rawFirst)) != 0 || memcmp(&preparedSecond, &rawSecond, sizeof(rawSecond)) != 0)
        return Fail("prepared and raw free-segment projection results differed");

    gk::Array<FPreparedSecondaryMotionCollisionShape> largerPrepared;
    ResetSecondaryMotionCollisionPrepareCount();
    if (!PrepareSecondaryMotionFreeSegmentShapes(shapes, 2, 0.2f, largerPrepared, error) || largerPrepared.Count() != 2 || GetSecondaryMotionCollisionPrepareCount() != 2)
        return Fail("a second point radius did not produce an independent prepared shape set");
    if (fabs(largerPrepared.At(0).radius - (static_cast<double>(shapes[0].radius) + 0.2)) > 1.0e-7 || fabs(largerPrepared.At(1).radius - (static_cast<double>(shapes[1].radius) + 0.2)) > 1.0e-7)
        return Fail("prepared radii did not include the requested point radius");

    FPreparedSecondaryMotionCollisionShape sentinel{};
    sentinel.start = Point(17.0, 18.0, 19.0);
    sentinel.end = Point(-4.0, -5.0, -6.0);
    sentinel.axis = Point(1.0, 2.0, 3.0);
    sentinel.axisLengthSquared = 23.0;
    sentinel.radius = 29.0;
    sentinel.tolerance = 31.0;
    sentinel.boundsLow[0] = 37.0;
    sentinel.boundsLow[1] = 41.0;
    sentinel.boundsLow[2] = 43.0;
    sentinel.boundsHigh[0] = 47.0;
    sentinel.boundsHigh[1] = 53.0;
    sentinel.boundsHigh[2] = 59.0;
    gk::Array<FPreparedSecondaryMotionCollisionShape> preserved;
    if (!preserved.Append(sentinel))
        return Fail("could not initialize prepared-shape atomicity sentinel");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const FModelSecondaryMotionCollisionShape invalid = Shape({ nan, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    FModelSecondaryMotionCollisionShape tooMany[65]{};
    for (uint32_t index = 0; index < 65; ++index)
        tooMany[index] = shapes[0];
    if (PrepareSecondaryMotionFreeSegmentShapes(&invalid, 1, pointRadius, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("invalid shape preparation was accepted or changed the previous output");
    if (PrepareSecondaryMotionFreeSegmentShapes(tooMany, 65, pointRadius, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("oversized shape preparation was accepted or changed the previous output");
    if (PrepareSecondaryMotionFreeSegmentShapes(nullptr, 1, pointRadius, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("null shape preparation was accepted or changed the previous output");
    if (PrepareSecondaryMotionFreeSegmentShapes(shapes, 2, -0.1f, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
        return Fail("negative radius preparation was accepted or changed the previous output");

    const gk::FVector3d center = Point(0.75, 0.0, 0.0);
    const gk::FVector3d zero = Point(0.0, 0.0, 0.0);
    gk::FVector3d projectedFirst = center;
    gk::FVector3d projectedSecond = center;
    gk::FVector3d repeatedFirst = center;
    gk::FVector3d repeatedSecond = center;
    const FModelSecondaryMotionCollisionShape centerSphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f);
    gk::Array<FPreparedSecondaryMotionCollisionShape> centerPrepared;
    if (!PrepareSecondaryMotionFreeSegmentShapes(&centerSphere, 1, 0.1f, centerPrepared, error))
        return Fail(error.CStr());
    if (!ProjectPreparedSecondaryMotionFreeSegmentContacts(centerPrepared.Data(), centerPrepared.Count(), zero, zero, projectedFirst, projectedSecond, error) || !ProjectPreparedSecondaryMotionFreeSegmentContacts(centerPrepared.Data(), centerPrepared.Count(), zero, zero, repeatedFirst, repeatedSecond, error))
        return Fail(error.CStr());
    if (memcmp(&projectedFirst, &repeatedFirst, sizeof(projectedFirst)) != 0 || memcmp(&projectedSecond, &repeatedSecond, sizeof(projectedSecond)) != 0 || !IsFreeSegmentClear(&centerSphere, 1, 0.1f, projectedFirst, projectedSecond))
        return Fail("prepared zero-length fallback was not deterministic and clear");

    const gk::FVector3d emptyFirst = Point(8.0, 9.0, 10.0);
    const gk::FVector3d emptySecond = Point(11.0, 12.0, 13.0);
    gk::FVector3d emptyProjectedFirst = emptyFirst;
    gk::FVector3d emptyProjectedSecond = emptySecond;
    if (!ProjectPreparedSecondaryMotionFreeSegmentContacts(nullptr, 0, emptyFirst, emptySecond, emptyProjectedFirst, emptyProjectedSecond, error) || !error.Empty() || memcmp(&emptyProjectedFirst, &emptyFirst, sizeof(emptyFirst)) != 0 || memcmp(&emptyProjectedSecond, &emptySecond, sizeof(emptySecond)) != 0)
        return Fail("empty prepared projection changed its endpoints or failed");
    if (!CheckPreparedSecondaryMotionFreeSegmentContacts(nullptr, 0, emptyFirst, emptySecond, error) || !error.Empty())
        return Fail("empty prepared check failed");

    const gk::FVector3d nonFinite = Point(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
    const gk::FVector3d savedFirst = Point(3.0, 4.0, 5.0);
    const gk::FVector3d savedSecond = Point(6.0, 7.0, 8.0);
    projectedFirst = savedFirst;
    projectedSecond = savedSecond;
    if (ProjectPreparedSecondaryMotionFreeSegmentContacts(centerPrepared.Data(), centerPrepared.Count(), nonFinite, savedSecond, projectedFirst, projectedSecond, error) || error.Empty() || memcmp(&projectedFirst, &savedFirst, sizeof(savedFirst)) != 0 || memcmp(&projectedSecond, &savedSecond, sizeof(savedSecond)) != 0)
        return Fail("invalid prepared projection changed its endpoints or omitted an error");
    if (ProjectPreparedSecondaryMotionFreeSegmentContacts(nullptr, 1, savedFirst, savedSecond, projectedFirst, projectedSecond, error) || error.Empty() || memcmp(&projectedFirst, &savedFirst, sizeof(savedFirst)) != 0 || memcmp(&projectedSecond, &savedSecond, sizeof(savedSecond)) != 0)
        return Fail("null prepared projection input was accepted or changed endpoints");
    if (ProjectPreparedSecondaryMotionFreeSegmentContacts(centerPrepared.Data(), 65, savedFirst, savedSecond, projectedFirst, projectedSecond, error) || error.Empty() || memcmp(&projectedFirst, &savedFirst, sizeof(savedFirst)) != 0 || memcmp(&projectedSecond, &savedSecond, sizeof(savedSecond)) != 0)
        return Fail("oversized prepared projection input was accepted or changed endpoints");
    gk::FVector3d aliasedEndpoint = savedFirst;
    if (ProjectPreparedSecondaryMotionFreeSegmentContacts(centerPrepared.Data(), centerPrepared.Count(), savedFirst, savedSecond, aliasedEndpoint, aliasedEndpoint, error) || error.Empty() || memcmp(&aliasedEndpoint, &savedFirst, sizeof(savedFirst)) != 0)
        return Fail("aliased prepared projection outputs were accepted or changed the endpoint");
    if (CheckPreparedSecondaryMotionFreeSegmentContacts(nullptr, 1, savedFirst, savedSecond, error) || error.Empty())
        return Fail("null prepared check input was accepted");
    return true;
}

/**
 * float保存された候補と描画FKの再構成が許容境界を跨ぐことを確認する。
 */
bool TestPreparedFreeSegmentToleranceBoundaryRoundTrip()
{
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.999097705f, 0.00032666701f, 0.0f }, { 0.999097705f, 0.00032666701f, 0.0f }, 0.00100001157f);
    const gk::FVector3d candidateFirst = Point(1.00963557f, 0.0226749871f, 0.0f);
    const gk::FVector3d candidateSecond = Point(0.990400016f, -0.0228000209f, 0.0f);
    const gk::FVector3d renderedFirst = Point(1.00963557f, 0.0226749908f, 0.0f);
    const gk::FVector3d renderedSecond = Point(0.990400016f, -0.0228000227f, 0.0f);
    const gk::FVector3d center = Point(sphere.start.x, sphere.start.y, sphere.start.z);
    const double tolerance = static_cast<double>(sphere.radius) * 1.0e-3;
    const double threshold = static_cast<double>(sphere.radius) - tolerance;
    const double candidateDistance = SegmentDistance(candidateFirst, candidateSecond, center, center);
    const double renderedDistance = SegmentDistance(renderedFirst, renderedSecond, center, center);
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sphere, 1, prepared, error) || prepared.Count() != 1)
        return Fail("could not prepare the FK-roundtrip sphere");
    if (!(candidateDistance >= threshold) || !(candidateDistance < sphere.radius) || !(renderedDistance < threshold) || !(candidateDistance - renderedDistance > 0.0))
        return Fail("independent segment-distance oracle did not reproduce the candidate/FK tolerance crossing");
    if (!CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), candidateFirst, candidateSecond, error, 1.0) || !error.Empty())
        return Fail("the projected float candidate was not accepted inside the regular tolerance band");
    if (CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), candidateFirst, candidateSecond, error, 0.0) || error.Empty())
        return Fail("strict candidate checking did not reject the sub-radius gap");
    if (CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), candidateFirst, candidateSecond, error, 0.5) || error.Empty())
        return Fail("candidate checking did not reserve half of the tolerance for FK");
    const double safeDistance = static_cast<double>(sphere.radius) - tolerance * 0.25;
    const gk::FVector3d safeFirst = Point(center.value[0] - 0.02, center.value[1] + safeDistance, 0.0);
    const gk::FVector3d safeSecond = Point(center.value[0] + 0.02, center.value[1] + safeDistance, 0.0);
    const gk::FVector3d shiftedFirst = Point(safeFirst.value[0], safeFirst.value[1] - tolerance * 0.5, 0.0);
    const gk::FVector3d shiftedSecond = Point(safeSecond.value[0], safeSecond.value[1] - tolerance * 0.5, 0.0);
    if (SegmentDistance(safeFirst, safeSecond, center, center) < static_cast<double>(sphere.radius) - tolerance * 0.5 || SegmentDistance(shiftedFirst, shiftedSecond, center, center) < threshold || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), safeFirst, safeSecond, error, 0.5) || !CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), shiftedFirst, shiftedSecond, error))
        return Fail("the reserved half-band did not preserve the final acceptance after an inward shift");
    if (CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), renderedFirst, renderedSecond, error, 1.0) || error.Empty())
        return Fail("the FK-roundtrip line remained inside the regular contact tolerance");
    if (CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), candidateFirst, candidateSecond, error, -1.0) || error.Empty())
        return Fail("negative prepared free-segment tolerance fraction was accepted");
    if (CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), candidateFirst, candidateSecond, error, std::numeric_limits<double>::quiet_NaN()) || error.Empty())
        return Fail("non-finite prepared free-segment tolerance fraction was accepted");
    if (CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), candidateFirst, candidateSecond, error, 1.01) || error.Empty())
        return Fail("oversized prepared free-segment tolerance fraction was accepted");
    return true;
}

/**
 * 準備済み形状でroot例外と複数shapeを保ち、raw経路と同じ座標へ投影する。
 */
bool TestPreparedPointContactsReuseAndRootException()
{
    const FModelSecondaryMotionCollisionShape shapes[2] = { Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f), Shape({ 0.1f, 0.0f, 0.0f }, { 0.1f, 0.0f, 0.0f }, 0.5f) };
    const gk::FVector3d references[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.1, 0.0) };
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::Array<gk::FVector3d> preparedPositions;
    gk::Array<gk::FVector3d> rawPositions;
    gk::String error;
    ResetSecondaryMotionCollisionPrepareCount();
    if (!PrepareSecondaryMotionFreeSegmentShapes(shapes, 2, 0.0f, prepared, error) || !error.Empty() || prepared.Count() != 2 || !preparedPositions.AppendRange(references, 2) || !rawPositions.AppendRange(references, 2))
        return Fail("could not prepare point-contact shapes and endpoints");
    const uint64_t preparationCount = GetSecondaryMotionCollisionPrepareCount();
    if (preparationCount != 2)
        return Fail("preparing point-contact shapes did not prepare each body shape exactly once");
    if (!ProjectPreparedSecondaryMotionContacts(prepared.Data(), prepared.Count(), references, 2, preparedPositions, error))
        return Fail(error.CStr());
    if (preparedPositions.At(0).value[0] != references[0].value[0] || preparedPositions.At(0).value[1] != references[0].value[1] || preparedPositions.At(0).value[2] != references[0].value[2] || Distance(preparedPositions.At(1), references[1]) < 0.1)
        return Fail("prepared point projection moved the pinned root or left its free point unchanged");
    if (!IsPointChainClearWithRootException(shapes, 2, preparedPositions.Data(), preparedPositions.Count()) || !CheckPreparedSecondaryMotionContacts(prepared.Data(), prepared.Count(), preparedPositions.Data(), preparedPositions.Count(), error))
        return Fail("prepared point projection left an independent point or span distance inside a shape");
    if (GetSecondaryMotionCollisionPrepareCount() != preparationCount)
        return Fail("prepared point projection or check rebuilt a prepared shape");
    if (!CheckSecondaryMotionContacts(shapes, 2, preparedPositions.Data(), preparedPositions.Count(), error))
        return Fail("raw contact check disagreed with the prepared result");

    if (!ProjectSecondaryMotionContacts(shapes, 2, references, 2, rawPositions, error))
        return Fail(error.CStr());
    if (memcmp(preparedPositions.Data(), rawPositions.Data(), sizeof(references)) != 0)
        return Fail("prepared and raw point projection results differed byte-for-byte");

    gk::Array<gk::FVector3d> aliasPositions;
    if (!aliasPositions.AppendRange(references, 2) || !ProjectPreparedSecondaryMotionContacts(prepared.Data(), prepared.Count(), aliasPositions.Data(), aliasPositions.Count(), aliasPositions, error))
        return Fail("prepared point projection did not support references aliased to positions");
    if (memcmp(preparedPositions.Data(), aliasPositions.Data(), sizeof(references)) != 0)
        return Fail("aliased prepared references produced different projection results");
    return true;
}

/**
 * 準備済み点接触APIの空shape、不正引数、別名、失敗時の保持を確かめる。
 */
bool TestPreparedPointContactsValidationAndAtomicity()
{
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionFreeSegmentShapes(&sphere, 1, 0.0f, prepared, error))
        return Fail(error.CStr());
    const gk::FVector3d references[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.1, 0.0) };
    gk::Array<gk::FVector3d> positions;
    gk::Array<gk::FVector3d> snapshot;
    if (!positions.AppendRange(references, 2) || !CopyPositions(positions, snapshot))
        return Fail("could not initialize prepared-point atomicity input");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const gk::FVector3d nonFiniteReference[2] = { references[0], Point(nan, 0.0, 0.0) };
    const gk::FVector3d nonFinitePosition[2] = { references[0], Point(0.0, std::numeric_limits<double>::infinity(), 0.0) };
    FPreparedSecondaryMotionCollisionShape tooMany[65]{};
    for (uint32_t index = 0; index < 65; ++index)
        tooMany[index] = prepared.At(0);

    if (ProjectPreparedSecondaryMotionContacts(nullptr, 1, references, 2, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("null prepared point shapes were accepted or changed positions");
    if (ProjectPreparedSecondaryMotionContacts(tooMany, 65, references, 2, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("oversized prepared point shape count was accepted or changed positions");
    if (ProjectPreparedSecondaryMotionContacts(prepared.Data(), 1, nullptr, 2, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("null prepared point references were accepted or changed positions");
    if (ProjectPreparedSecondaryMotionContacts(prepared.Data(), 1, references, 1, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("mismatched prepared point count was accepted or changed positions");
    if (ProjectPreparedSecondaryMotionContacts(prepared.Data(), 1, nonFiniteReference, 2, positions, error) || error.Empty() || !SamePositions(positions, snapshot))
        return Fail("non-finite prepared point reference was accepted or changed positions");
    gk::Array<gk::FVector3d> invalidPositions;
    if (!invalidPositions.AppendRange(nonFinitePosition, 2))
        return Fail("could not initialize non-finite prepared-point input");
    gk::Array<gk::FVector3d> invalidSnapshot;
    if (!CopyPositions(invalidPositions, invalidSnapshot))
        return Fail("could not snapshot non-finite prepared-point input");
    if (ProjectPreparedSecondaryMotionContacts(prepared.Data(), 1, references, 2, invalidPositions, error) || error.Empty() || !SamePositions(invalidPositions, invalidSnapshot))
        return Fail("non-finite prepared point position was accepted or changed output");

    const gk::FVector3d clearPositions[2] = { references[0], Point(0.6, 0.0, 0.0) };
    if (!CheckPreparedSecondaryMotionContacts(prepared.Data(), prepared.Count(), clearPositions, 2, error))
        return Fail("valid prepared point contact check failed");
    if (CheckPreparedSecondaryMotionContacts(nullptr, 1, references, 2, error) || error.Empty())
        return Fail("null prepared check shapes were accepted");
    if (CheckPreparedSecondaryMotionContacts(tooMany, 65, references, 2, error) || error.Empty())
        return Fail("oversized prepared check shape count was accepted");
    if (CheckPreparedSecondaryMotionContacts(prepared.Data(), 1, nullptr, 2, error) || error.Empty())
        return Fail("null prepared check positions were accepted");
    if (CheckPreparedSecondaryMotionContacts(prepared.Data(), 1, nonFinitePosition, 2, error) || error.Empty())
        return Fail("non-finite prepared check positions were accepted");
    if (CheckPreparedSecondaryMotionContacts(prepared.Data(), 1, references, 2, error, -0.1) || error.Empty() || CheckPreparedSecondaryMotionContacts(prepared.Data(), 1, references, 2, error, 1.1) || error.Empty() || CheckPreparedSecondaryMotionContacts(prepared.Data(), 1, references, 2, error, std::numeric_limits<double>::quiet_NaN()) || error.Empty())
        return Fail("invalid prepared check tolerance was accepted");

    gk::Array<gk::FVector3d> emptyPositions;
    if (!emptyPositions.AppendRange(references, 2))
        return Fail("could not initialize empty-shape prepared positions");
    if (!ProjectPreparedSecondaryMotionContacts(nullptr, 0, nullptr, 2, emptyPositions, error) || !error.Empty() || memcmp(emptyPositions.Data(), references, sizeof(references)) != 0 || !CheckPreparedSecondaryMotionContacts(nullptr, 0, references, 2, error) || !error.Empty())
        return Fail("empty prepared point shapes changed positions or failed");
    return true;
}

/**
 * 更新中の形状準備を半径別に再利用し、失敗時にviewを保つ。
 */
bool TestStepShapeCacheReuseAndAtomicity()
{
    const FModelSecondaryMotionCollisionShape shapes[2] = { Shape({ 0.9f, 0.0f, 0.0f }, { 0.9f, 0.0f, 0.0f }, 0.2f), Shape({ 3.0f, 3.0f, 3.0f }, { 3.0f, 4.0f, 3.0f }, 0.15f) };
    FModelSecondaryMotionStepShapeCache cache(shapes, 2);
    FModelSecondaryMotionStepShapes firstView{};
    gk::String error;
    ResetSecondaryMotionCollisionExpansionCount();
    ResetSecondaryMotionCollisionPrepareCount();
    if (!cache.Get(0.1f, firstView, error) || !error.Empty() || firstView.count != 2 || !firstView.expanded || !firstView.guarded || !firstView.prepared)
        return Fail("step-shape cache did not prepare the first radius group");
    if (GetSecondaryMotionCollisionExpansionCount() != 1 || GetSecondaryMotionCollisionPrepareCount() != 2)
        return Fail("first step-shape cache group did not expand once and prepare each body once");
    for (uint32_t index = 0; index < 2; ++index)
    {
        const double radiusSum = static_cast<double>(shapes[index].radius) + 0.1;
        float expectedExpandedRadius = static_cast<float>(radiusSum);
        if (static_cast<double>(expectedExpandedRadius) < radiusSum)
            expectedExpandedRadius = nextafterf(expectedExpandedRadius, INFINITY);
        const float expectedGuardedRadius = static_cast<float>(static_cast<double>(expectedExpandedRadius) * 1.001);
        const double expectedPreparedRadius = expectedGuardedRadius;
        if (memcmp(&firstView.expanded[index].start, &shapes[index].start, sizeof(gk::Vec3)) != 0 || memcmp(&firstView.expanded[index].end, &shapes[index].end, sizeof(gk::Vec3)) != 0 || memcmp(&firstView.expanded[index].radius, &expectedExpandedRadius, sizeof(float)) != 0 || memcmp(&firstView.guarded[index].radius, &expectedGuardedRadius, sizeof(float)) != 0 || memcmp(&firstView.prepared[index].radius, &expectedPreparedRadius, sizeof(double)) != 0)
            return Fail("step-shape cache expansion or nearest guard rounding differed from the independent oracle");
    }

    FModelSecondaryMotionStepShapes repeatedView{};
    if (!cache.Get(0.1f, repeatedView, error) || repeatedView.expanded != firstView.expanded || repeatedView.guarded != firstView.guarded || repeatedView.prepared != firstView.prepared || repeatedView.count != firstView.count)
        return Fail("same-radius step-shape lookup did not reuse its view");
    if (GetSecondaryMotionCollisionExpansionCount() != 1 || GetSecondaryMotionCollisionPrepareCount() != 2)
        return Fail("same-radius step-shape lookup repeated expansion or preparation");

    FModelSecondaryMotionStepShapes secondRadiusView{};
    if (!cache.Get(0.2f, secondRadiusView, error) || secondRadiusView.count != 2 || fabs(secondRadiusView.expanded[0].radius - 0.4f) > 1.0e-6f)
        return Fail("different-radius step-shape lookup did not create a distinct group");
    if (GetSecondaryMotionCollisionExpansionCount() != 2 || GetSecondaryMotionCollisionPrepareCount() != 4)
        return Fail("different-radius group did not expand once and prepare each body once");
    // 別groupの確保後はviewを取り直し、現在の配列内で別の範囲を指すことを確認する。
    if (!cache.Get(0.1f, firstView, error) || firstView.expanded == secondRadiusView.expanded || firstView.prepared == secondRadiusView.prepared || fabs(firstView.expanded[0].radius - 0.3f) > 1.0e-6f)
        return Fail("radius groups did not remain distinct after reacquiring the first view");

    const FModelSecondaryMotionStepShapes preservedView = firstView;
    const auto SameView = [](const FModelSecondaryMotionStepShapes& first, const FModelSecondaryMotionStepShapes& second) { return first.expanded == second.expanded && first.guarded == second.guarded && first.prepared == second.prepared && first.count == second.count; };
    if (cache.Get(std::numeric_limits<float>::quiet_NaN(), firstView, error) || error.Empty() || !SameView(firstView, preservedView))
        return Fail("invalid point radius changed an existing step-shape view");
    if (GetSecondaryMotionCollisionExpansionCount() != 2 || GetSecondaryMotionCollisionPrepareCount() != 4)
        return Fail("invalid point radius changed cached shape preparation counts");

    FModelSecondaryMotionCollisionShape tooMany[65]{};
    for (uint32_t index = 0; index < 65; ++index)
        tooMany[index] = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.1f);
    FModelSecondaryMotionStepShapeCache oversizedCache(tooMany, 65);
    if (oversizedCache.Get(0.0f, firstView, error) || error.Empty() || !SameView(firstView, preservedView))
        return Fail("oversized step-shape cache input was accepted or changed output");
    FModelSecondaryMotionStepShapeCache nullCache(nullptr, 1);
    if (nullCache.Get(0.0f, firstView, error) || error.Empty() || !SameView(firstView, preservedView))
        return Fail("null step-shape cache input was accepted or changed output");

    const FModelSecondaryMotionCollisionShape nearFloatLimit = Shape({}, {}, std::numeric_limits<float>::max() / 1.0005f);
    FModelSecondaryMotionStepShapeCache guardOverflowCache(&nearFloatLimit, 1);
    if (guardOverflowCache.Get(0.0f, firstView, error) || error.Empty() || !SameView(firstView, preservedView))
        return Fail("guard-radius overflow was accepted or changed output");

    FModelSecondaryMotionStepShapeCache emptyCache(nullptr, 0);
    FModelSecondaryMotionStepShapes emptyView{ shapes, shapes, nullptr, 2 };
    if (!emptyCache.Get(0.0f, emptyView, error) || !error.Empty() || emptyView.count != 0 || emptyView.expanded || emptyView.guarded || emptyView.prepared)
        return Fail("empty step-shape cache did not return an empty view");

    const FModelSecondaryMotionCollisionShape signedZeroShape = Shape({ -0.0f, -0.0f, -0.0f }, { -0.0f, -0.0f, -0.0f }, 0.2f);
    FModelSecondaryMotionStepShapeCache signedZeroCache(&signedZeroShape, 1);
    FModelSecondaryMotionStepShapes signedZeroView{};
    const float pointRadius = 0.1f;
    if (!signedZeroCache.Get(pointRadius, signedZeroView, error) || memcmp(&signedZeroView.expanded[0].start, &signedZeroShape.start, sizeof(gk::Vec3)) != 0 || memcmp(&signedZeroView.expanded[0].end, &signedZeroShape.end, sizeof(gk::Vec3)) != 0)
        return Fail("radius expansion did not preserve signed-zero body coordinates");
    const float positiveZero = 0.0f;
    const float guardedStart[3] = { signedZeroView.guarded[0].start.x, signedZeroView.guarded[0].start.y, signedZeroView.guarded[0].start.z };
    const float guardedEnd[3] = { signedZeroView.guarded[0].end.x, signedZeroView.guarded[0].end.y, signedZeroView.guarded[0].end.z };
    for (uint32_t axis = 0; axis < 3; ++axis)
        if (memcmp(&guardedStart[axis], &positiveZero, sizeof(float)) != 0 || memcmp(&guardedEnd[axis], &positiveZero, sizeof(float)) != 0)
            return Fail("guarded body coordinates did not match the Core zero-step positive-zero interpolation");
    return true;
}

/**
 * 新しい半径groupの各確保段階が失敗しても既存groupと出力viewを保つ。
 */
bool TestStepShapeCacheAllocationFailureAtomicity()
{
    const FModelSecondaryMotionCollisionShape shapes[2] = { Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.2f), Shape({ 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.3f) };
    bool sawFailure = false;
    bool reachedSuccess = false;
    for (uint32_t allowedAllocations = 0; allowedAllocations <= 12; ++allowedAllocations)
    {
        FModelSecondaryMotionStepShapeCache cache(shapes, 2);
        FModelSecondaryMotionStepShapes originalView{};
        gk::String error;
        if (!cache.Get(0.1f, originalView, error))
            return Fail("could not seed the allocation-failure cache");
        const FModelSecondaryMotionStepShapes outputBefore = originalView;
        if (!error.Assign("preallocated diagnostic buffer for deterministic allocation failure checks"))
            return Fail("could not reserve diagnostic string storage before allocation injection");
        FModelSecondaryMotionStepShapes output = outputBefore;
        gk::SetAllocationFailureAfterForTesting(allowedAllocations);
        const bool created = cache.Get(0.2f, output, error);
        gk::ResetAllocationFailureForTesting();
        if (created)
        {
            if (!error.Empty() || output.count != 2 || fabs(output.expanded[0].radius - 0.4f) > 1.0e-6f)
                return Fail("allocation sweep success returned an invalid new radius group");
            reachedSuccess = true;
            break;
        }

        sawFailure = true;
        if (error.Empty() || output.expanded != outputBefore.expanded || output.guarded != outputBefore.guarded || output.prepared != outputBefore.prepared || output.count != outputBefore.count)
            return Fail("failed cache allocation changed the caller view or omitted a diagnostic");
        // Reserveが既存配列を移動し得るため、古いviewは参照せず同じradiusを取り直す。
        FModelSecondaryMotionStepShapes recoveredOriginal{};
        if (!cache.Get(0.1f, recoveredOriginal, error) || recoveredOriginal.count != 2 || fabs(recoveredOriginal.expanded[0].radius - 0.3f) > 1.0e-6f || fabs(recoveredOriginal.expanded[1].radius - 0.4f) > 1.0e-6f)
            return Fail("failed cache allocation lost an existing radius group");
        FModelSecondaryMotionStepShapes retried{};
        if (!cache.Get(0.2f, retried, error) || retried.count != 2 || fabs(retried.expanded[0].radius - 0.4f) > 1.0e-6f)
            return Fail("cache could not retry a radius group after allocation failure");
    }
    if (!sawFailure || !reachedSuccess)
        return Fail("allocation-failure sweep did not cover both failures and successful cache creation");
    return true;
}

/**
 * 準備済みviewのstepをraw経路とbyte単位で比べ、再準備を防ぐ。
 */
bool TestPreparedCollisionStepMatchesRawAndRejectsInvalidViews()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape shapes[2] = { Shape({ 0.9f, 0.0f, 0.0f }, { 0.9f, 0.0f, 0.0f }, 0.2f), Shape({ 3.0f, 3.0f, 3.0f }, { 3.0f, 4.0f, 3.0f }, 0.15f) };
    const float pointRadius = 0.15f;
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    FModelSecondaryMotionStepShapeCache cache(shapes, 2);
    FModelSecondaryMotionStepShapes view{};
    gk::String error;
    ResetSecondaryMotionCollisionExpansionCount();
    ResetSecondaryMotionCollisionPrepareCount();
    if (!cache.Get(pointRadius, view, error) || view.count != 2 || GetSecondaryMotionCollisionExpansionCount() != 1 || GetSecondaryMotionCollisionPrepareCount() != 2)
        return Fail("could not create the prepared solver-step view");

    FModelSecondaryMotionChainState preparedState;
    FModelSecondaryMotionChainState rawState;
    FModelSecondaryMotionChainState preparedSnapshot;
    if (!ResetSecondaryMotionChain(targets, 2, preparedState, error) || !ResetSecondaryMotionChain(targets, 2, rawState, error))
        return Fail("could not initialize prepared and raw solver states");
    const auto SameStateBytes = [](const FModelSecondaryMotionChainState& first, const FModelSecondaryMotionChainState& second) { return first.points.Count() == second.points.Count() && first.previousTargets.Count() == second.previousTargets.Count() && memcmp(first.points.Data(), second.points.Data(), static_cast<size_t>(first.points.Count()) * sizeof(FModelSecondaryMotionPoint)) == 0 && memcmp(first.previousTargets.Data(), second.previousTargets.Data(), static_cast<size_t>(first.previousTargets.Count()) * sizeof(gk::Vec3)) == 0; };
    for (uint32_t iteration = 0; iteration < 3; ++iteration)
    {
        const uint64_t preparedCountBefore = GetSecondaryMotionCollisionPrepareCount();
        if (!StepSecondaryMotionChainWithPreparedCollisions(targets, 2, settings, view, preparedState, error))
            return Fail("prepared solver step failed");
        const uint64_t preparedStepCount = GetSecondaryMotionCollisionPrepareCount() - preparedCountBefore;
        const uint64_t rawCountBefore = GetSecondaryMotionCollisionPrepareCount();
        if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, shapes, shapes, 2, rawState, error, pointRadius))
            return Fail(error.CStr());
        // raw回避と保存後のraw検査は残し、guard形状の準備2件だけを省く。
        if (GetSecondaryMotionCollisionPrepareCount() - rawCountBefore != preparedStepCount + 2u)
            return Fail("prepared solver did not remove only the two guarded-shape preparations");
        if (!SameStateBytes(preparedState, rawState))
            return Fail("prepared and raw collision steps differed byte-for-byte");
    }
    const gk::FVector3d solvedPositions[2] = { ToDouble(preparedState.points.At(0).position), ToDouble(preparedState.points.At(1).position) };
    if (!IsFinite(preparedState) || !IsFreeSegmentClear(shapes, 2, pointRadius, solvedPositions[0], solvedPositions[1]))
        return Fail("prepared step did not preserve finite point-radius contact geometry");

    if (!CopyState(preparedState, preparedSnapshot))
        return Fail("could not snapshot prepared solver state");
    FModelSecondaryMotionStepShapes invalidView = view;
    invalidView.prepared = nullptr;
    if (StepSecondaryMotionChainWithPreparedCollisions(targets, 2, settings, invalidView, preparedState, error) || error.Empty() || !SameStateBytes(preparedState, preparedSnapshot))
        return Fail("prepared solver accepted a missing prepared-shape pointer or changed state");
    FModelSecondaryMotionCollisionShape oversizedExpanded[65]{};
    FModelSecondaryMotionCollisionShape oversizedGuarded[65]{};
    FPreparedSecondaryMotionCollisionShape oversizedPrepared[65]{};
    invalidView = { oversizedExpanded, oversizedGuarded, oversizedPrepared, 65 };
    if (StepSecondaryMotionChainWithPreparedCollisions(targets, 2, settings, invalidView, preparedState, error) || error.Empty() || !SameStateBytes(preparedState, preparedSnapshot))
        return Fail("prepared solver accepted an oversized shape view or changed state");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    gk::FModelSecondaryMotionSettings invalidSettings = settings;
    invalidSettings.frequencyHz = nan;
    if (StepSecondaryMotionChainWithPreparedCollisions(targets, 2, invalidSettings, view, preparedState, error) || error.Empty() || !SameStateBytes(preparedState, preparedSnapshot))
        return Fail("prepared solver accepted invalid settings or changed state");
    if (StepSecondaryMotionChainWithPreparedCollisions(nullptr, 2, settings, view, preparedState, error) || error.Empty() || !SameStateBytes(preparedState, preparedSnapshot))
        return Fail("prepared solver accepted null targets or changed state");

    FModelSecondaryMotionStepShapeCache emptyCache(nullptr, 0);
    FModelSecondaryMotionStepShapes emptyView{};
    if (!emptyCache.Get(0.0f, emptyView, error))
        return Fail("could not create the empty prepared collision view");
    FModelSecondaryMotionChainState emptyPreparedState;
    FModelSecondaryMotionChainState emptyRawState;
    if (!ResetSecondaryMotionChain(targets, 2, emptyPreparedState, error) || !ResetSecondaryMotionChain(targets, 2, emptyRawState, error) || !StepSecondaryMotionChainWithPreparedCollisions(targets, 2, settings, emptyView, emptyPreparedState, error) || !StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, nullptr, nullptr, 0, emptyRawState, error) || !SameStateBytes(emptyPreparedState, emptyRawState))
        return Fail("empty prepared collision view differed from the no-collider solver path");
    return true;
}

/**
 * 準備済み形状の投影が収束しない場合は参照方向を計算せずstateを保つ。
 */
bool TestPreparedProjectionFailureSkipsReferenceWork()
{
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 0.1f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape overlapping[2] = { Shape({ -0.5f, 0.0f, 0.0f }, { -0.5f, 0.0f, 0.0f }, 0.8f), Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.8f) };
    FModelSecondaryMotionStepShapeCache cache(overlapping, 2);
    FModelSecondaryMotionStepShapes view{};
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    gk::String error;
    if (!cache.Get(0.0f, view, error) || !ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
        return Fail("could not initialize the prepared projection-failure fixture");
    ResetSecondaryMotionReferenceCalculationCount();
    if (StepSecondaryMotionChainWithPreparedCollisions(targets, 2, settings, view, state, error) || error.Empty() || GetSecondaryMotionReferenceCalculationCount() != 0 || !SameState(state, snapshot))
        return Fail("prepared projection failure calculated reference segments or changed the old state");
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
    bool (*tests[])(void) = { TestSphereAndCapsulePointProjection, TestInsideCenterAndAxisFallback, TestSpanningSegmentContact, TestContactToleranceFraction, TestLongCapsuleFixedLengthAvoidance, TestCollisionSegmentBoundsFastPath, TestShapePreparationBudget, TestSolverPreparedShapeBudget, TestFarColliderContactCheckBudget, TestRootInsideFirstSpanException, TestRootInsideFirstSpanSolverEndpointOnly, TestMultipleContacts, TestInvalidShapesAndAtomicProjection, TestNoColliderMatchesLegacyStep, TestCollisionShapeExpansionReuseBudget, TestReferenceCalculationBudgetAndStateInvariance, TestReferenceCalculationSkippedOnInvalidCollisionInput, TestFeasibleContactWithLengthAndAngle, TestSolverPointRadiusKeepsBoneSphereClear, TestShortCapsuleLengthAndAngleFeasibility, TestInfeasibleContactPreservesState, TestMovingColliderUsesCurrentGeometry, TestHumanoidContactDiagnosticFixture, TestCollisionShapeValidationIsAtomic, TestEmptyStateInitializationResolvesContact, TestZeroDeltaPoseChangeResolvesCurrentContact, TestHitchAndTeleportInfeasibleContactsAreAtomic, TestFloatRoundingDoesNotHideSmallColliderPenetration, TestExpandCollisionShapesForPointRadius, TestFloatRoundingPreservesLengthAndAngle, TestFreeSegmentSphereAndCapsuleContacts, TestFreeSegmentMultipleShapesAndDegenerateCases, TestFreeSegmentValidationAndAtomicity, TestPreparedFreeSegmentShapeReuseAndAtomicity, TestPreparedFreeSegmentToleranceBoundaryRoundTrip, TestPreparedPointContactsReuseAndRootException, TestPreparedPointContactsValidationAndAtomicity, TestStepShapeCacheReuseAndAtomicity, TestStepShapeCacheAllocationFailureAtomicity, TestPreparedCollisionStepMatchesRawAndRejectsInvalidViews, TestPreparedProjectionFailureSkipsReferenceWork };
    const char* names[] = { "sphere and capsule projection", "inside fallback", "spanning segment", "contact tolerance fraction", "long-capsule fixed-length avoidance", "collision bounds fast path", "shape preparation budget", "solver prepared-shape budget", "far-collider contact-check budget", "root-inside exception", "root-inside solver endpoint-only", "multiple contacts", "invalid input atomicity", "legacy equivalence", "shape expansion reuse budget", "reference calculation budget", "reference calculation validation order", "feasible contact", "bone sphere contact", "short capsule feasibility", "infeasible atomicity", "moving collider", "humanoid contact fixture", "collision shape validation", "empty state initialization", "zero-delta pose change", "hitch and teleport atomicity", "float contact rounding", "point-radius collider expansion", "float constraint rounding", "free segment sphere and capsule", "free segment multiple shapes and degenerate cases", "free segment validation and atomicity", "prepared free-segment reuse and atomicity", "prepared free-segment FK tolerance boundary", "prepared point-contact reuse and root exception", "prepared point-contact validation and atomicity", "step-shape cache reuse and atomicity", "step-shape cache allocation atomicity", "prepared collision step raw equivalence", "prepared projection failure skips reference work" };
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
