// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionDistances.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/FModelSecondaryMotionStepShapeCache.h"
#include "model/animation/ModelSecondaryMotionDirection.h"
#include "model/animation/ModelSecondaryMotionChainContacts.h"

#include "foundation/Array.h"
#include "foundation/Memory.h"

#include <math.h>
#include <stdint.h>
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
 * 準備済み形状の方向回避が旧経路と一致し、軸情報を再計算しない。
 */
bool TestPreparedSegmentAvoidanceMatchesRaw()
{
    /**
     * 元形状と準備済み形状へ同じ回避入力を与える検査条件。
     */
    struct FAvoidanceCase
    {
        // 再利用する身体形状。
        FModelSecondaryMotionCollisionShape shape;
        // 固定する親の位置。
        gk::FVector3d parent;
        // 骨の長さ。
        double length;
        // rootから最初の骨かどうか。
        bool firstSegment;
        // 各経路へ渡す初期方向。
        gk::FVector3d direction;
        // 同じ形状を続けて使う回数。
        uint32_t repetitions;
    };
    const float largest = std::numeric_limits<float>::max();
    const FAvoidanceCase cases[] = { { Shape({ 0.0f, -2.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.2f), Point(0.3, 0.0, 0.0), Distance(Point(0.3, 0.0, 0.0), Point(-0.3, -1.0, 0.0)), false, Point(-0.6 / Distance(Point(0.3, 0.0, 0.0), Point(-0.3, -1.0, 0.0)), -1.0 / Distance(Point(0.3, 0.0, 0.0), Point(-0.3, -1.0, 0.0)), 0.0), 8 }, { Shape({ 0.0f, -0.4f, 0.0f }, { 0.0f, 0.4f, 0.0f }, 0.2f), Point(0.3, 0.3, 0.0), 0.8, false, Point(-1.0, 0.0, 0.0), 4 }, { Shape({ 0.1f, -0.2f, 0.3f }, { -0.45f, 2.5f, 0.84f }, 0.2f), Point(0.3, 0.0, 0.0), 1.0, false, Point(-0.7, 0.5, 0.2), 4 }, { Shape({ 1.0e-38f, -1.0e-38f, 2.0e-38f }, { 1.0e-38f, 0.0f, 2.0e-38f }, 1.0e-39f), Point(1.3e-38, -5.0e-39, 2.0e-38), 1.0e-38, false, Point(-1.0, 0.0, 0.0), 2 }, { Shape({ 1.0e-38f, -1.0e-38f, 2.0e-38f }, { 1.0e-38f, -1.0e-38f, 2.0e-38f }, 1.0e-39f), Point(1.3e-38, -1.0e-38, 2.0e-38), 1.0e-38, false, Point(-1.0, 0.0, 0.0), 2 }, { Shape({ 0.3f, 0.0f, 0.0f }, { 0.3f, 0.0f, 0.0f }, 0.5f), Point(0.0, 0.0, 0.0), 0.5, true, Point(1.0, 0.0, 0.0), 2 }, { Shape({ 0.3f, 0.0f, 0.0f }, { 0.3f, 0.0f, 0.0f }, 0.5f), Point(0.0, 0.0, 0.0), 0.5, false, Point(1.0, 0.0, 0.0), 2 }, { Shape({ 4.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f }, 0.5f), Point(0.0, 0.0, 0.0), 2.0, false, Point(-0.0, 1.0, -0.0), 1 }, { Shape({ 0.750000059604644775390625f, 2.6f, 0.0f }, { 0.750000059604644775390625f, 2.6f, 0.0f }, 0.75f), Point(0.0, 0.0, 0.0), 2.0, false, Point(0.0, 1.0, 0.0), 1 }, { Shape({ 0.75f, 2.6f, 0.0f }, { 0.75f, 2.6f, 0.0f }, 0.75f), Point(0.0, 0.0, 0.0), 2.0, false, Point(0.0, 1.0, 0.0), 1 }, { Shape({ largest, 0.0f, 0.0f }, { largest, 0.0f, 0.0f }, largest), Point(0.0, 0.0, 0.0), 2.0, false, Point(0.0, 1.0, 0.0), 1 } };
    gk::String error;
    bool observedExactQuery = false;
    for (uint32_t caseIndex = 0; caseIndex < sizeof(cases) / sizeof(cases[0]); ++caseIndex)
    {
        gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
        if (!PrepareSecondaryMotionCollisionShapes(&cases[caseIndex].shape, 1, prepared, error) || prepared.Count() != 1)
            return Fail("could not prepare a raw/prepared avoidance comparison shape");
        const FPreparedSecondaryMotionCollisionShape& value = prepared.At(0);
        const double axis[3] = { static_cast<double>(cases[caseIndex].shape.end.x) - cases[caseIndex].shape.start.x, static_cast<double>(cases[caseIndex].shape.end.y) - cases[caseIndex].shape.start.y, static_cast<double>(cases[caseIndex].shape.end.z) - cases[caseIndex].shape.start.z };
        const double axisLength = sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        const double axisLengthTolerance = axisLength > 0.0 ? axisLength * 1.0e-15 : 0.0;
        const double axisLengthSquared = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
        if (value.axis.value[0] != axis[0] || value.axis.value[1] != axis[1] || value.axis.value[2] != axis[2] || value.axisLengthSquared != axisLengthSquared || fabs(value.axisLength - axisLength) > axisLengthTolerance)
            return Fail("prepared axis or scale-safe axis length differed from its independent calculation");
        if (caseIndex == 3 && (!(axisLength > 0.0) || !(value.axisLength > 0.0) || value.axisLengthSquared <= 0.0))
            return Fail("tiny nonzero capsule axis was lost while preparing its direction length");
        if (caseIndex == 4 && (axisLength != 0.0 || value.axisLengthSquared != 0.0 || value.axisLength != 0.0))
            return Fail("tiny-coordinate zero-axis sphere was not kept on the sphere calculation path");
        const double starts[3] = { cases[caseIndex].shape.start.x, cases[caseIndex].shape.start.y, cases[caseIndex].shape.start.z };
        const double ends[3] = { cases[caseIndex].shape.end.x, cases[caseIndex].shape.end.y, cases[caseIndex].shape.end.z };
        for (uint32_t component = 0; component < 3; ++component)
        {
            const double low = fmin(starts[component], ends[component]) - cases[caseIndex].shape.radius;
            const double high = fmax(starts[component], ends[component]) + cases[caseIndex].shape.radius;
            if (value.boundsLow[component] != low || value.boundsHigh[component] != high || value.conservativeBoundsLow[component] != nextafter(low, -HUGE_VAL) || value.conservativeBoundsHigh[component] != nextafter(high, HUGE_VAL))
                return Fail("prepared bounds did not preserve the plain range and one-ULP outward conservative range");
        }

        gk::FVector3d rawDirection = cases[caseIndex].direction;
        gk::FVector3d preparedDirection = cases[caseIndex].direction;
        ResetSecondaryMotionCollisionAxisQueryCount();
        ResetSecondaryMotionCollisionPrepareCount();
        bool rawSucceeded = true;
        for (uint32_t repetition = 0; repetition < cases[caseIndex].repetitions; ++repetition)
        {
            rawSucceeded = AvoidSecondaryMotionCollisionSegment(cases[caseIndex].shape, cases[caseIndex].parent, cases[caseIndex].length, cases[caseIndex].firstSegment, rawDirection, error);
            if (!rawSucceeded)
                break;
        }
        const bool rawErrorEmpty = error.Empty();
        const uint64_t rawQueries = GetSecondaryMotionCollisionAxisQueryCount();
        const uint64_t rawPreparations = GetSecondaryMotionCollisionPrepareCount();
        ResetSecondaryMotionCollisionAxisQueryCount();
        ResetSecondaryMotionCollisionPrepareCount();
        bool preparedSucceeded = true;
        for (uint32_t repetition = 0; repetition < cases[caseIndex].repetitions; ++repetition)
        {
            preparedSucceeded = AvoidPreparedSecondaryMotionCollisionSegment(value, cases[caseIndex].parent, cases[caseIndex].length, cases[caseIndex].firstSegment, preparedDirection, error);
            if (!preparedSucceeded)
                break;
        }
        const bool preparedErrorEmpty = error.Empty();
        const uint64_t preparedQueries = GetSecondaryMotionCollisionAxisQueryCount();
        if (rawSucceeded != preparedSucceeded || rawErrorEmpty != preparedErrorEmpty || memcmp(&rawDirection, &preparedDirection, sizeof(rawDirection)) != 0 || rawQueries != preparedQueries || GetSecondaryMotionCollisionPrepareCount() != 0)
            return Fail("raw and prepared avoidance differed in result, direction, exact queries, or repeated preparation");
        if (rawQueries > 0 && rawPreparations == 0)
            return Fail("raw exact geometry path did not account for its shape preparation");
        if (!rawSucceeded)
            continue;
        observedExactQuery = observedExactQuery || rawQueries > 0;
        if (caseIndex == 5)
        {
            const gk::FVector3d endpoint = Point(cases[caseIndex].parent.value[0] + preparedDirection.value[0] * cases[caseIndex].length, cases[caseIndex].parent.value[1] + preparedDirection.value[1] * cases[caseIndex].length, cases[caseIndex].parent.value[2] + preparedDirection.value[2] * cases[caseIndex].length);
            if (Distance(endpoint, Point(0.3, 0.0, 0.0)) < 0.5 * (1.0 - 1.0e-3))
                return Fail("first-span root-inside exception left its free endpoint inside the sphere");
        }
        if (caseIndex == 6 && memcmp(&preparedDirection, &cases[caseIndex].direction, sizeof(preparedDirection)) != 0)
            return Fail("non-first segment with an inside parent did not defer its direction unchanged");
        if (caseIndex == 7 && (!signbit(preparedDirection.value[0]) || !signbit(preparedDirection.value[2])))
            return Fail("prepared far-path normalization did not preserve signed-zero components");
        if (caseIndex == 8 && rawQueries != 0)
            return Fail("one-ULP separated prepared bounds performed an unnecessary exact query");
        if (caseIndex == 9 && rawQueries == 0)
            return Fail("touching expanded bounds skipped the independent exact geometry query");
    }
    if (!observedExactQuery)
        return Fail("all raw/prepared avoidance cases skipped exact geometry queries");

    const FModelSecondaryMotionCollisionShape nearSphere = Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.2f);
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedNearSphere;
    if (!PrepareSecondaryMotionCollisionShapes(&nearSphere, 1, preparedNearSphere, error) || preparedNearSphere.Count() != 1)
        return Fail("could not prepare the near-sphere invalid-input fixture");
    const gk::FVector3d invalidParents[] = { Point(0.0, 0.0, 0.0), Point(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), Point(std::numeric_limits<double>::max() * 0.75, 0.0, 0.0) };
    const double invalidLengths[] = { 1.0, 1.0, std::numeric_limits<double>::max() * 0.5 };
    const gk::FVector3d invalidDirections[] = { Point(0.0, 0.0, 0.0), Point(std::numeric_limits<double>::quiet_NaN(), 1.0, 0.0), Point(1.0, 0.0, 0.0) };
    for (uint32_t invalidIndex = 0; invalidIndex < 3; ++invalidIndex)
    {
        gk::FVector3d rawDirection = invalidDirections[invalidIndex];
        gk::FVector3d preparedDirection = invalidDirections[invalidIndex];
        const gk::FVector3d originalDirection = invalidDirections[invalidIndex];
        const bool rawSucceeded = AvoidSecondaryMotionCollisionSegment(nearSphere, invalidParents[invalidIndex], invalidLengths[invalidIndex], false, rawDirection, error);
        const bool rawHasError = !error.Empty();
        const bool preparedSucceeded = AvoidPreparedSecondaryMotionCollisionSegment(preparedNearSphere.At(0), invalidParents[invalidIndex], invalidLengths[invalidIndex], false, preparedDirection, error);
        if (rawSucceeded || preparedSucceeded || !rawHasError || error.Empty() || memcmp(&rawDirection, &originalDirection, sizeof(rawDirection)) != 0 || memcmp(&preparedDirection, &originalDirection, sizeof(preparedDirection)) != 0)
            return Fail("invalid direction, parent, or endpoint overflow changed a raw or prepared direction");
    }
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

    const FModelSecondaryMotionCollisionShape nearSphere = Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.2f);
    FModelSecondaryMotionChainState contactState;
    if (!ResetSecondaryMotionChain(targets, 2, contactState, error))
        return Fail("could not initialize the solver prepared-avoidance budget state");
    ResetSecondaryMotionCollisionPrepareCount();
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, 0.0, &nearSphere, &nearSphere, 1, contactState, error))
        return Fail(error.CStr());
    const gk::FVector3d contactPositions[2] = { ToDouble(contactState.points.At(0).position), ToDouble(contactState.points.At(1).position) };
    if (GetSecondaryMotionCollisionPrepareCount() != 2 || !CheckSecondaryMotionContacts(&nearSphere, 1, contactPositions, 2, error))
        return Fail("solver prepared avoidance repeated per-pass shape setup or left the near sphere contact unresolved");
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
 * root内のfirst span例外はguard半径ではなく元の身体形状で判定する。
 */
bool TestGuardedRootInsideUsesOriginalShapeRule()
{
    const FModelSecondaryMotionCollisionShape rawSphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f);
    FModelSecondaryMotionStepShapeCache cache(&rawSphere, 1);
    FModelSecondaryMotionStepShapes view{};
    gk::String error;
    if (!cache.Get(0.0f, view, error) || view.count != 1 || !view.prepared)
        return Fail("could not prepare guarded root-exception sphere");
    const double storedThreshold = static_cast<double>(view.expanded[0].radius) - fmax(static_cast<double>(view.expanded[0].radius) * 1.0e-3, static_cast<double>(view.expanded[0].radius) * std::numeric_limits<double>::epsilon() * 64.0);
    if (!view.prepared[0].hasRootContactOverride || view.prepared[0].rootContactThreshold != storedThreshold || !(view.prepared[0].radius > view.expanded[0].radius))
        return Fail("step cache did not preserve the pre-guard root threshold alongside the expanded shape");

    const gk::FVector3d shallowRoot = Point(0.9995, 0.0, 0.0);
    const double rawTolerance = static_cast<double>(rawSphere.radius) * 0.001;
    const double guardedTolerance = view.prepared[0].tolerance;
    const double shallowDistance = Distance(shallowRoot, Point(0.0, 0.0, 0.0));
    if (!(shallowDistance >= static_cast<double>(rawSphere.radius) - rawTolerance) || !(shallowDistance < static_cast<double>(view.prepared[0].radius) - guardedTolerance))
        return Fail("root-exception fixture did not straddle raw and guarded inside thresholds");

    const gk::FVector3d inwardPositions[2] = { shallowRoot, Point(-1.1, 0.0, 0.0) };
    if (SegmentDistance(inwardPositions[0], inwardPositions[1], Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0)) >= static_cast<double>(rawSphere.radius) - rawTolerance)
        return Fail("independent oracle did not confirm the inward span crosses the raw sphere");
    if (CheckSecondaryMotionContacts(&rawSphere, 1, inwardPositions, 2, error))
        return Fail("raw contact checker accepted an inward crossing from a shallow root");
    if (CheckPreparedSecondaryMotionContacts(view.prepared, view.count, inwardPositions, 2, error, 1.0))
        return Fail("guarded prepared checker incorrectly exempted an inward span at a shallow raw root");

    const gk::FVector3d outwardPositions[2] = { shallowRoot, Point(1.1, 0.0, 0.0) };
    if (SegmentDistance(outwardPositions[0], outwardPositions[1], Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0)) < static_cast<double>(rawSphere.radius) - rawTolerance)
        return Fail("independent oracle found an outward span penetrating beyond the raw tolerance shell");
    if (!CheckSecondaryMotionContacts(&rawSphere, 1, outwardPositions, 2, error) || !CheckPreparedSecondaryMotionContacts(view.prepared, view.count, outwardPositions, 2, error, 1.0))
        return Fail("outward shallow-root span did not retain the original tolerance-shell behavior");

    const gk::FVector3d deepPositions[2] = { Point(0.0, 0.0, 0.0), Point(-1.1, 0.0, 0.0) };
    if (!CheckPreparedSecondaryMotionContacts(view.prepared, view.count, deepPositions, 2, error, 1.0))
        return Fail("deep-inside root no longer exempts its first span");
    return true;
}

/**
 * 移動形状の中間半径を使い、root例外を最初の節にだけ適用する。
 */
bool TestInterpolatedRadiusRootOverrideAndSegmentRole()
{
    const FModelSecondaryMotionCollisionShape previousSphere = Shape({ -0.5f, 0.0f, 0.0f }, { -0.5f, 0.0f, 0.0f }, 0.5f);
    const FModelSecondaryMotionCollisionShape currentSphere = Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 1.5f);
    const double weight = 0.5;
    const FModelSecondaryMotionCollisionShape midpointSphere = Shape({ static_cast<float>(static_cast<double>(previousSphere.start.x) + (static_cast<double>(currentSphere.start.x) - previousSphere.start.x) * weight), 0.0f, 0.0f }, { static_cast<float>(static_cast<double>(previousSphere.end.x) + (static_cast<double>(currentSphere.end.x) - previousSphere.end.x) * weight), 0.0f, 0.0f }, static_cast<float>(static_cast<double>(previousSphere.radius) + (static_cast<double>(currentSphere.radius) - previousSphere.radius) * weight));
    const float pointRadius = 0.1f;
    FModelSecondaryMotionStepShapeCache cache(&midpointSphere, 1);
    FModelSecondaryMotionStepShapes view{};
    gk::String error;
    if (!cache.Get(pointRadius, view, error) || view.count != 1 || !view.prepared)
        return Fail("could not prepare the interpolated body sphere and point radius");

    const double expandedRadius = static_cast<double>(view.expanded[0].radius);
    const double expectedThreshold = expandedRadius - fmax(expandedRadius * 1.0e-3, expandedRadius * std::numeric_limits<double>::epsilon() * 64.0);
    const double guardedThreshold = view.prepared[0].radius - view.prepared[0].tolerance;
    if (fabs(static_cast<double>(view.expanded[0].radius) - (static_cast<double>(midpointSphere.radius) + pointRadius)) > 1.0e-6 || !view.prepared[0].hasRootContactOverride || view.prepared[0].rootContactThreshold != expectedThreshold || view.prepared[0].rootContactThreshold >= guardedThreshold)
        return Fail("interpolated radius or point-radius expansion was not retained in the root threshold");

    const gk::FVector3d shallowRoot = Point(expectedThreshold + (guardedThreshold - expectedThreshold) * 0.5, 0.0, 0.0);
    const gk::FVector3d inwardPositions[2] = { shallowRoot, Point(-1.2, 0.0, 0.0) };
    const gk::FVector3d outwardPositions[2] = { shallowRoot, Point(1.3, 0.0, 0.0) };
    if (!(Distance(shallowRoot, Point(0.0, 0.0, 0.0)) > expectedThreshold) || !(Distance(shallowRoot, Point(0.0, 0.0, 0.0)) < guardedThreshold))
        return Fail("interpolated-radius fixture did not place root between original and guarded thresholds");
    if (SegmentDistance(inwardPositions[0], inwardPositions[1], Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0)) >= expectedThreshold || CheckSecondaryMotionContacts(view.expanded, view.count, inwardPositions, 2, error))
        return Fail("independent oracle did not detect the inward span crossing the interpolated body shape");
    if (SegmentDistance(outwardPositions[0], outwardPositions[1], Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0)) < expectedThreshold || !CheckSecondaryMotionContacts(view.expanded, view.count, outwardPositions, 2, error) || !CheckPreparedSecondaryMotionContacts(view.prepared, view.count, outwardPositions, 2, error, 1.0))
        return Fail("outward shallow-root span did not preserve the interpolated shape's original tolerance shell");
    if (CheckPreparedSecondaryMotionContacts(view.prepared, view.count, inwardPositions, 2, error, 1.0))
        return Fail("prepared checker exempted an inward span at a shallow root for a moving shape");

    gk::FVector3d firstDirection = Point(1.0, 0.0, 0.0);
    if (!FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, shallowRoot, 0.2, true, Point(1.0, 0.0, 0.0), 1.0, firstDirection, error) || !error.Empty())
        return Fail("first segment did not allow an outward direction from the original tolerance shell");
    const gk::FVector3d firstEndpoint = Point(shallowRoot.value[0] + firstDirection.value[0] * 0.2, shallowRoot.value[1] + firstDirection.value[1] * 0.2, shallowRoot.value[2] + firstDirection.value[2] * 0.2);
    if (SegmentDistance(shallowRoot, firstEndpoint, Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0)) < expectedThreshold)
        return Fail("first-segment direction entered the interpolated body beyond its original tolerance shell");

    const gk::FVector3d inwardReference = Point(-1.0, 0.0, 0.0);
    gk::FVector3d inwardDirection = inwardReference;
    const gk::FVector3d inwardDirectionBefore = inwardDirection;
    if (FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, shallowRoot, 0.2, true, inwardReference, 1.0, inwardDirection, error) || !error.Empty() || memcmp(&inwardDirection, &inwardDirectionBefore, sizeof(inwardDirection)) != 0)
        return Fail("first-segment direction accepted an inward crossing or changed its output on no solution");

    gk::FVector3d ordinaryDirection = Point(1.0, 0.0, 0.0);
    const gk::FVector3d ordinaryDirectionBefore = ordinaryDirection;
    if (FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, shallowRoot, 0.2, false, Point(1.0, 0.0, 0.0), 1.0, ordinaryDirection, error) || !error.Empty() || memcmp(&ordinaryDirection, &ordinaryDirectionBefore, sizeof(ordinaryDirection)) != 0)
        return Fail("a non-root segment used the root exception instead of the ordinary tolerance");

    const gk::FVector3d outsideRoot = Point(guardedThreshold + 0.01, 0.0, 0.0);
    gk::FVector3d outsideDirection = Point(1.0, 0.0, 0.0);
    if (!FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, outsideRoot, 0.1, false, Point(1.0, 0.0, 0.0), 1.0, outsideDirection, error) || !error.Empty())
        return Fail("a non-root segment outside the guard margin was rejected");
    const gk::FVector3d outsideEndpoint = Point(outsideRoot.value[0] + outsideDirection.value[0] * 0.1, outsideRoot.value[1] + outsideDirection.value[1] * 0.1, outsideRoot.value[2] + outsideDirection.value[2] * 0.1);
    if (SegmentDistance(outsideRoot, outsideEndpoint, Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0)) < guardedThreshold)
        return Fail("ordinary non-root direction crossed the independent guarded-body clearance");

    const gk::FVector3d deepRoot = Point(0.0, 0.0, 0.0);
    const gk::FVector3d deepPositions[2] = { deepRoot, Point(-1.2, 0.0, 0.0) };
    if (!CheckPreparedSecondaryMotionContacts(view.prepared, view.count, deepPositions, 2, error, 1.0) || SegmentDistance(deepPositions[0], deepPositions[1], Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0)) >= expectedThreshold)
        return Fail("deep-root first-span exemption did not preserve its documented behavior");
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
 * 固定根に近い非接触spanでは、移動colliderが鎖へ速度を与えないことを確かめる。
 */
bool TestNearFixedRootMovingCapsuleDoesNotInjectVelocity()
{
    const double delta = 1.0 / 60.0;
    const double spanAmount = 1.0e-4;
    const gk::Vec3 targets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    const FModelSecondaryMotionCollisionShape previous = Shape({ static_cast<float>(spanAmount), 0.1010005f, 0.0f }, { static_cast<float>(spanAmount), 0.1010005f, 0.0f }, 0.1f);
    const FModelSecondaryMotionCollisionShape current = Shape({ static_cast<float>(spanAmount), 0.1000005f, 0.0f }, { static_cast<float>(spanAmount), 0.1000005f, 0.0f }, 0.1f);
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.maxAngleDegrees = 0.0f;
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2, state, error) || !CopyState(state, snapshot))
        return Fail("could not initialize near-root moving-capsule fixture");
    const gk::FVector3d initialPositions[2] = { ToDouble(state.points.At(0).position), ToDouble(state.points.At(1).position) };
    if (!CheckSecondaryMotionContacts(&current, 1, initialPositions, 2, error))
        return Fail("near-root moving-capsule fixture must begin outside the body contact threshold");
    if (!StepSecondaryMotionChainWithCollisions(targets, 2, settings, delta, &previous, &current, 1, state, error))
        return Fail(error.CStr());
    if (!IsFinite(state))
        return Fail("near-root moving-capsule response produced a non-finite velocity");
    const gk::FVector3d rootPosition = ToDouble(state.points.At(0).position);
    const gk::FVector3d tipPosition = ToDouble(state.points.At(1).position);
    const gk::FVector3d segment = Point(tipPosition.value[0] - rootPosition.value[0], tipPosition.value[1] - rootPosition.value[1], tipPosition.value[2] - rootPosition.value[2]);
    const gk::FVector3d axisOffset = Point(current.start.x - rootPosition.value[0], current.start.y - rootPosition.value[1], current.start.z - rootPosition.value[2]);
    const double segmentLengthSquared = Dot(segment, segment);
    const double actualSpanAmount = fmax(0.0, fmin(1.0, Dot(axisOffset, segment) / segmentLengthSquared));
    const gk::FVector3d currentAxis = Point(current.start.x, current.start.y, current.start.z);
    const double actualClearance = SegmentDistance(rootPosition, tipPosition, currentAxis, currentAxis) - current.radius;
    const double rootClearance = Distance(rootPosition, currentAxis) - current.radius;
    const double surfaceSpeed = fabs((static_cast<double>(current.start.y) - previous.start.y) / delta);
    if (fabs(actualSpanAmount - spanAmount) > 1.0e-6 || actualClearance <= 0.0 || actualClearance > 1.0e-5 || rootClearance <= 0.0 || surfaceSpeed <= 0.05)
        return Fail("near-root fixture must keep a moving sphere just outside the real root and span contact surfaces");
    const FModelSecondaryMotionPoint& tip = state.points.At(1);
    if (tip.velocity.x != 0.0f || tip.velocity.y != 0.0f || tip.velocity.z != 0.0f || !SameState(state, snapshot))
    {
        const double actualTipSpeed = sqrt(static_cast<double>(tip.velocity.x) * tip.velocity.x + static_cast<double>(tip.velocity.y) * tip.velocity.y + static_cast<double>(tip.velocity.z) * tip.velocity.z);
        fprintf(stderr, "near-root moving-capsule fixture: span=%.9g clearance=%.9g rootClearance=%.9g surfaceSpeed=%.9g tipSpeed=%.9g\n", actualSpanAmount, actualClearance, rootClearance, surfaceSpeed, actualTipSpeed);
        return Fail("a moving capsule outside the actual contact radius injected velocity into an otherwise stationary chain");
    }
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
 * 実機で記録したSkirt鎖の失敗入力を再生し、接触と鎖制約の両立を確認する。
 */
bool TestRecordedSkinningChainContact()
{
    const gk::Vec3 targets[5] = { { 0.0137152309f, 0.990111709f, 0.798526406f }, { 0.0168546848f, 0.896700263f, 0.803701699f }, { 0.0189411007f, 0.834534466f, 0.807320952f }, { 0.0201907903f, 0.798206449f, 0.807601452f }, { 0.0214216802f, 0.763258517f, 0.806143463f } };
    const gk::Vec3 initialPositions[5] = { { 0.0129065318f, 0.987856388f, 0.777404189f }, { -0.0227742549f, 0.909798503f, 0.814769804f }, { -0.0000708915395f, 0.881434023f, 0.865386486f }, { 0.0160481185f, 0.864139974f, 0.83777374f }, { 0.0228332393f, 0.8393085f, 0.814059615f } };
    const gk::Vec3 initialVelocities[5] = { { 0.0565983541f, 0.0545060635f, 1.37712121f }, { 0.112575158f, 0.0744315684f, 1.47459412f }, { -1.29270697f, -0.0752636492f, 2.3835516f }, { -0.397095621f, 0.07214275f, 2.56566334f }, { 0.113758825f, 0.516395569f, 2.42391777f } };
    const gk::Vec3 previousTargets[5] = { { 0.0129065318f, 0.987856388f, 0.777404189f }, { 0.0163068697f, 0.894374311f, 0.780851007f }, { 0.0185674485f, 0.832158327f, 0.78331995f }, { 0.019913232f, 0.79583472f, 0.782928467f }, { 0.0212312136f, 0.760922909f, 0.780824244f } };
    const FModelSecondaryMotionCollisionShape previousShapes[8] = { Shape({ 0.0161567889f, 0.923788786f, 0.714889944f }, { 0.0161567889f, 0.923788786f, 0.714889944f }, 0.108000003f), Shape({ 0.0141029572f, 0.977692306f, 0.719482958f }, { 0.0105709173f, 1.08754122f, 0.724036276f }, 0.118000008f), Shape({ 0.0102808857f, 1.09656143f, 0.724410117f }, { 0.00837408658f, 1.21642244f, 0.670283556f }, 0.118000008f), Shape({ 0.0113126487f, 1.35527658f, 0.627325535f }, { 0.0113126487f, 1.35527658f, 0.627325535f }, 0.128000006f), Shape({ 0.0805434957f, 0.873250127f, 0.700870812f }, { 0.107641831f, 0.703914762f, 0.822078109f }, 0.088000007f), Shape({ -0.0457378179f, 0.874792814f, 0.705248654f }, { -0.0765114352f, 0.746733546f, 0.868814588f }, 0.0879999995f), Shape({ 0.112692989f, 0.654587448f, 0.821263969f }, { 0.119289987f, 0.441653967f, 0.624500692f }, 0.0780000016f), Shape({ -0.0841958374f, 0.699825048f, 0.880942345f }, { -0.110519975f, 0.445947409f, 0.743272007f }, 0.0780000016f) };
    const FModelSecondaryMotionCollisionShape currentShapes[8] = { Shape({ 0.0169844441f, 0.924896896f, 0.737210929f }, { 0.0169844441f, 0.924896896f, 0.737210929f }, 0.108000003f), Shape({ 0.0150609855f, 0.978881001f, 0.740805805f }, { 0.0114761218f, 1.08873475f, 0.745200336f }, 0.118000008f), Shape({ 0.0111817531f, 1.09775531f, 0.745561182f }, { 0.00944366772f, 1.21850741f, 0.693446755f }, 0.118000008f), Shape({ 0.0124584585f, 1.35788345f, 0.652506113f }, { 0.0124584585f, 1.35788345f, 0.652506113f }, 0.128000006f), Shape({ 0.0808926225f, 0.874606252f, 0.725257754f }, { 0.104470044f, 0.712119758f, 0.856186211f }, 0.088000007f), Shape({ -0.0448668823f, 0.877693534f, 0.729560435f }, { -0.0754696205f, 0.765339732f, 0.904317677f }, 0.0879999995f), Shape({ 0.109497063f, 0.663221359f, 0.857721746f }, { 0.121347837f, 0.443760276f, 0.668522418f }, 0.0780000016f), Shape({ -0.0835563317f, 0.720323741f, 0.919595659f }, { -0.114035763f, 0.460263878f, 0.794936776f }, 0.0780000016f) };
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.frequencyHz = 5.0f;
    settings.dampingRatio = 0.800000012f;
    settings.maxAngleDegrees = 60.0f;
    settings.gravity = { 0.0f, -1.0f, 0.0f };
    settings.windAcceleration = { 0.0f, 0.0f, 0.0f };
    settings.endOffset = { 0.0f, 0.0350000001f, 0.0f };
    settings.teleportDistance = 0.5f;
    settings.constraintIterations = 32;
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 5, state, error))
        return Fail(error.CStr());
    for (uint32_t index = 0; index < 5; ++index)
    {
        state.points.At(index).position = initialPositions[index];
        state.points.At(index).velocity = initialVelocities[index];
        state.previousTargets.At(index) = previousTargets[index];
    }

    if (!StepSecondaryMotionChainWithCollisions(targets, 5, settings, 0.016666666666666666, previousShapes, currentShapes, 8, state, error, 0.0f))
        return Fail(error.CStr());
    if (state.points.Count() != 5 || state.previousTargets.Count() != 5 || !IsFinite(state))
        return Fail("recorded skinning chain returned an invalid state size or non-finite value");
    if (fabs(state.points.At(0).position.x - targets[0].x) > 1.0e-7f || fabs(state.points.At(0).position.y - targets[0].y) > 1.0e-7f || fabs(state.points.At(0).position.z - targets[0].z) > 1.0e-7f)
        return Fail("recorded skinning chain moved its pinned root");
    gk::FVector3d positions[5]{};
    for (uint32_t index = 0; index < 5; ++index)
        positions[index] = ToDouble(state.points.At(index).position);
    if (!CheckSecondaryMotionContacts(currentShapes, 8, positions, 5, error))
        return Fail("recorded skinning chain still penetrates a current body shape");
    for (uint32_t index = 1; index < 5; ++index)
    {
        const gk::FVector3d targetAxis = Point(static_cast<double>(targets[index].x) - targets[index - 1].x, static_cast<double>(targets[index].y) - targets[index - 1].y, static_cast<double>(targets[index].z) - targets[index - 1].z);
        const gk::FVector3d solvedAxis = Point(positions[index].value[0] - positions[index - 1].value[0], positions[index].value[1] - positions[index - 1].value[1], positions[index].value[2] - positions[index - 1].value[2]);
        const double targetLength = Distance(Point(0.0, 0.0, 0.0), targetAxis);
        const double solvedLength = Distance(Point(0.0, 0.0, 0.0), solvedAxis);
        if (!(solvedLength > 0.0) || fabs(solvedLength - targetLength) > targetLength * 0.001 || Dot(solvedAxis, targetAxis) / (solvedLength * targetLength) < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
            return Fail("recorded skinning chain violated a target bone length or bend limit");
    }

    if (!StepSecondaryMotionChainWithCollisions(targets, 5, settings, 0.016666666666666666, previousShapes, currentShapes, 8, state, error, 0.0f) || !IsFinite(state))
        return Fail("recorded skinning chain could not complete its following update");
    FModelSecondaryMotionChainState snapshot;
    if (!CopyState(state, snapshot))
        return Fail("could not snapshot the recorded skinning chain before invalid input");
    gk::Vec3 invalidTargets[5];
    memcpy(invalidTargets, targets, sizeof(invalidTargets));
    invalidTargets[2].z = std::numeric_limits<float>::quiet_NaN();
    if (StepSecondaryMotionChainWithCollisions(invalidTargets, 5, settings, 0.016666666666666666, previousShapes, currentShapes, 8, state, error, 0.0f) || error.Empty() || !SameState(state, snapshot))
        return Fail("invalid follow-up input changed the recorded skinning chain state");
    return true;
}

/**
 * 実機frame 98で失敗したSkirt鎖を、記録された接触形状と物理状態で再生する。
 */
bool TestRecordedSkinningChainFrame98Contact()
{
    const gk::Vec3 targets[5] = { { 0.0522558875f, 1.4412837f, 2.16266918f }, { 0.0185298193f, 1.35360909f, 2.17676449f }, { -0.00116496801f, 1.29468632f, 2.18176365f }, { -0.0131363347f, 1.26076651f, 2.182899f }, { -0.0257917773f, 1.22813702f, 2.1826458f } };
    const gk::Vec3 initialPositions[5] = { { 0.0516432561f, 1.44268143f, 2.15808702f }, { 0.0780251995f, 1.39041913f, 2.23289084f }, { 0.0803740546f, 1.3617667f, 2.28819275f }, { 0.0943295062f, 1.33179021f, 2.27398658f }, { 0.0928504243f, 1.31215811f, 2.24505043f } };
    const gk::Vec3 initialVelocities[5] = { { 0.0372096151f, -0.0788068771f, 0.288004875f }, { -0.0189410541f, -0.581553638f, -0.0235349275f }, { 1.00490355f, 2.85015965f, 3.75069356f }, { -0.137468517f, 2.49641037f, 2.48658514f }, { -0.243808091f, 2.53369212f, 2.46474957f } };
    const gk::Vec3 previousTargets[5] = { { 0.0516432561f, 1.44268143f, 2.15808702f }, { 0.0187839605f, 1.35475314f, 2.17264223f }, { -0.000312760036f, 1.29566169f, 2.17796087f }, { -0.0119336778f, 1.26162672f, 2.1792779f }, { -0.0242472049f, 1.22886574f, 2.17919564f } };
    const FModelSecondaryMotionCollisionShape previousShapes[8] = { Shape({ 0.0160482898f, 1.38302505f, 2.08632016f }, { 0.0160482898f, 1.38302505f, 2.08632016f }, 0.108000003f), Shape({ 0.0343445502f, 1.43385482f, 2.08985329f }, { 0.0796978548f, 1.53283799f, 2.10551858f }, 0.118000008f), Shape({ 0.0834220126f, 1.54096603f, 2.10680485f }, { 0.104643799f, 1.67077184f, 2.10651708f }, 0.118000008f), Shape({ 0.166627616f, 1.79795384f, 2.12363029f }, { 0.166627616f, 1.79795384f, 2.12363029f }, 0.128000006f), Shape({ 0.0369305015f, 1.32969368f, 2.02564788f }, { 0.160042509f, 1.16477537f, 2.06742549f }, 0.088000007f), Shape({ -0.040792156f, 1.35613954f, 2.1353302f }, { 0.033236295f, 1.24326432f, 2.29619956f }, 0.088000007f), Shape({ 0.165427089f, 1.11501873f, 2.06175065f }, { 0.0256166663f, 0.89105165f, 1.94178486f }, 0.0780000016f), Shape({ 0.0325165801f, 1.19824207f, 2.32663918f }, { -0.0898085609f, 0.938934922f, 2.37018633f }, 0.0780000016f) };
    const FModelSecondaryMotionCollisionShape currentShapes[8] = { Shape({ 0.0157923214f, 1.38247502f, 2.09063911f }, { 0.0157923214f, 1.38247502f, 2.09063911f }, 0.108000003f), Shape({ 0.0346308611f, 1.43308699f, 2.09444022f }, { 0.0811000615f, 1.53149605f, 2.11044836f }, 0.118000001f), Shape({ 0.0849158466f, 1.53957689f, 2.11176276f }, { 0.107534401f, 1.66914582f, 2.11121702f }, 0.118000001f), Shape({ 0.169579446f, 1.79640996f, 2.12838793f }, { 0.169579446f, 1.79640996f, 2.12838793f }, 0.128000006f), Shape({ 0.0364473723f, 1.32969165f, 2.02937508f }, { 0.161756068f, 1.16576648f, 2.06844211f }, 0.088000007f), Shape({ -0.0406854339f, 1.35646403f, 2.1398735f }, { 0.0360126197f, 1.24598169f, 2.30115294f }, 0.0879999995f), Shape({ 0.168105498f, 1.11549938f, 2.06291747f }, { 0.0341988541f, 0.885049999f, 1.94862795f }, 0.0780000016f), Shape({ 0.035163451f, 1.20142019f, 2.33028865f }, { -0.0925733745f, 0.942837358f, 2.36059022f }, 0.0780000016f) };
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.frequencyHz = 5.0f;
    settings.dampingRatio = 0.800000012f;
    settings.maxAngleDegrees = 60.0f;
    settings.endOffset = { 0.0f, 0.0350000001f, 0.0f };
    settings.gravity = { 0.0f, -1.0f, 0.0f };
    settings.windAcceleration = { 0.0f, 0.0f, 0.0f };
    settings.teleportDistance = 0.5f;
    settings.constraintIterations = 32;
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 5, state, error))
        return Fail(error.CStr());
    for (uint32_t index = 0; index < 5; ++index)
    {
        state.points.At(index).position = initialPositions[index];
        state.points.At(index).velocity = initialVelocities[index];
        state.previousTargets.At(index) = previousTargets[index];
    }
    if (!StepSecondaryMotionChainWithCollisions(targets, 5, settings, 0.016666666666666666, previousShapes, currentShapes, 8, state, error, 0.0f))
        return Fail(error.CStr());
    if (state.points.Count() != 5 || state.previousTargets.Count() != 5 || !IsFinite(state))
        return Fail("frame 98 contact result has an invalid state size or non-finite value");
    if (fabs(state.points.At(0).position.x - targets[0].x) > 1.0e-7f || fabs(state.points.At(0).position.y - targets[0].y) > 1.0e-7f || fabs(state.points.At(0).position.z - targets[0].z) > 1.0e-7f)
        return Fail("frame 98 contact result moved its pinned root");
    gk::FVector3d positions[5]{};
    for (uint32_t index = 0; index < 5; ++index)
        positions[index] = ToDouble(state.points.At(index).position);
    if (!CheckSecondaryMotionContacts(currentShapes, 8, positions, 5, error))
        return Fail("frame 98 contact result still penetrates a body shape");
    for (uint32_t index = 1; index < 5; ++index)
    {
        const gk::FVector3d targetAxis = Point(static_cast<double>(targets[index].x) - targets[index - 1].x, static_cast<double>(targets[index].y) - targets[index - 1].y, static_cast<double>(targets[index].z) - targets[index - 1].z);
        const gk::FVector3d solvedAxis = Point(positions[index].value[0] - positions[index - 1].value[0], positions[index].value[1] - positions[index - 1].value[1], positions[index].value[2] - positions[index - 1].value[2]);
        const double targetLength = Distance(Point(0.0, 0.0, 0.0), targetAxis);
        const double solvedLength = Distance(Point(0.0, 0.0, 0.0), solvedAxis);
        if (!(solvedLength > 0.0) || fabs(solvedLength - targetLength) > targetLength * 0.001 || Dot(solvedAxis, targetAxis) / (solvedLength * targetLength) < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
            return Fail("frame 98 contact result violated a target bone length or bend limit");
    }
    return true;
}

/**
 * 実機frame 33で失敗したSkirt鎖を、記録された形状とstateで再生する。
 */
bool TestRecordedSkinningChainFrame33Contact()
{
    const gk::Vec3 targets[5] = { { 0.0153284529f, 1.0035491f, 0.86382401f }, { 0.0178400464f, 0.91059047f, 0.874535203f }, { 0.0195049793f, 0.848736346f, 0.881838202f }, { 0.0205464922f, 0.812482476f, 0.884275615f }, { 0.0216130223f, 0.777504265f, 0.884896576f } };
    const gk::Vec3 initialPositions[5] = { { 0.0147370044f, 0.997879684f, 0.842226923f }, { -0.0393578075f, 0.936547935f, 0.887773752f }, { 0.00304610864f, 0.910022438f, 0.924926758f }, { 0.0250864569f, 0.881624222f, 0.919529319f }, { 0.0526653565f, 0.864714682f, 0.906170011f } };
    const gk::Vec3 initialVelocities[5] = { { 0.0299438462f, 0.268063545f, 1.35918617f }, { -0.57809186f, 0.916440845f, 1.58825791f }, { 0.465206921f, 1.56869781f, 1.06432712f }, { -0.33724457f, 0.670648634f, 1.3349694f }, { -0.416152656f, 0.608715534f, 1.24426448f } };
    const gk::Vec3 previousTargets[5] = { { 0.0147370044f, 0.997879684f, 0.842226923f }, { 0.0174910463f, 0.904708147f, 0.85081929f }, { 0.0193186607f, 0.842708409f, 0.856712461f }, { 0.0204412974f, 0.806410849f, 0.858323574f }, { 0.021572737f, 0.771429598f, 0.858147264f } };
    const FModelSecondaryMotionCollisionShape previousShapes[8] = { Shape({ 0.0185930934f, 0.930477738f, 0.783359647f }, { 0.0185930934f, 0.930477738f, 0.783359647f }, 0.108000003f), Shape({ 0.0168055519f, 0.98456198f, 0.784973383f }, { 0.0131140389f, 1.09440768f, 0.789476871f }, 0.118000008f), Shape({ 0.0128109129f, 1.10342765f, 0.789846659f }, { 0.0115524065f, 1.22612667f, 0.742482424f }, 0.118000008f), Shape({ 0.0149520896f, 1.36661971f, 0.706597328f }, { 0.0149520896f, 1.36661971f, 0.706597328f }, 0.128000006f), Shape({ 0.0825759694f, 0.881251216f, 0.775822043f }, { 0.104529165f, 0.735094666f, 0.925007582f }, 0.088000007f), Shape({ -0.0429687314f, 0.887203038f, 0.778638601f }, { -0.0738526136f, 0.809302807f, 0.971194446f }, 0.0879999995f), Shape({ 0.109745972f, 0.687393606f, 0.931323588f }, { 0.125962645f, 0.454058409f, 0.759883642f }, 0.0780000016f), Shape({ -0.0825036764f, 0.768381655f, 0.993177235f }, { -0.118000418f, 0.494211733f, 0.905594051f }, 0.0780000016f) };
    const FModelSecondaryMotionCollisionShape currentShapes[8] = { Shape({ 0.0195087641f, 0.934830189f, 0.806522369f }, { 0.0195087641f, 0.934830189f, 0.806522369f }, 0.108000003f), Shape({ 0.0178112816f, 0.98894006f, 0.806902885f }, { 0.0139207728f, 1.09878945f, 0.811141849f }, 0.118000008f), Shape({ 0.0136013068f, 1.10780966f, 0.81148988f }, { 0.0124743786f, 1.23135614f, 0.76637888f }, 0.118000001f), Shape({ 0.0159734208f, 1.37230504f, 0.733028591f }, { 0.0159734208f, 1.37230504f, 0.733028591f }, 0.128000006f), Shape({ 0.083589755f, 0.885942757f, 0.801124156f }, { 0.105068296f, 0.747396111f, 0.95746839f }, 0.088000007f), Shape({ -0.0418595113f, 0.893231034f, 0.802915514f }, { -0.0727071017f, 0.831402183f, 1.00122237f }, 0.0879999995f), Shape({ 0.110421143f, 0.70020175f, 0.965985656f }, { 0.128692657f, 0.459904402f, 0.804668188f }, 0.0780000016f), Shape({ -0.081521675f, 0.792448759f, 1.02647865f }, { -0.11865557f, 0.512254894f, 0.961575389f }, 0.0780000016f) };
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.frequencyHz = 5.0f;
    settings.dampingRatio = 0.800000012f;
    settings.maxAngleDegrees = 60.0f;
    settings.endOffset = { 0.0f, 0.0350000001f, 0.0f };
    settings.gravity = { 0.0f, -1.0f, 0.0f };
    settings.windAcceleration = { 0.0f, 0.0f, 0.0f };
    settings.teleportDistance = 0.5f;
    settings.constraintIterations = 32;
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 5, state, error))
        return Fail(error.CStr());
    for (uint32_t index = 0; index < 5; ++index)
    {
        state.points.At(index).position = initialPositions[index];
        state.points.At(index).velocity = initialVelocities[index];
        state.previousTargets.At(index) = previousTargets[index];
    }
    if (!StepSecondaryMotionChainWithCollisions(targets, 5, settings, 0.016666666666666666, previousShapes, currentShapes, 8, state, error, 0.0f))
        return Fail(error.CStr());
    if (state.points.Count() != 5 || state.previousTargets.Count() != 5 || !IsFinite(state))
        return Fail("frame 33 contact result has an invalid state size or non-finite value");
    if (fabs(state.points.At(0).position.x - targets[0].x) > 1.0e-7f || fabs(state.points.At(0).position.y - targets[0].y) > 1.0e-7f || fabs(state.points.At(0).position.z - targets[0].z) > 1.0e-7f)
        return Fail("frame 33 contact result moved its pinned root");
    gk::FVector3d positions[5]{};
    for (uint32_t index = 0; index < 5; ++index)
        positions[index] = ToDouble(state.points.At(index).position);
    if (!CheckSecondaryMotionContacts(currentShapes, 8, positions, 5, error))
        return Fail("frame 33 contact result still penetrates a body shape");
    for (uint32_t index = 1; index < 5; ++index)
    {
        const gk::FVector3d targetAxis = Point(static_cast<double>(targets[index].x) - targets[index - 1].x, static_cast<double>(targets[index].y) - targets[index - 1].y, static_cast<double>(targets[index].z) - targets[index - 1].z);
        const gk::FVector3d solvedAxis = Point(positions[index].value[0] - positions[index - 1].value[0], positions[index].value[1] - positions[index - 1].value[1], positions[index].value[2] - positions[index - 1].value[2]);
        const double targetLength = Distance(Point(0.0, 0.0, 0.0), targetAxis);
        const double solvedLength = Distance(Point(0.0, 0.0, 0.0), solvedAxis);
        if (!(solvedLength > 0.0) || fabs(solvedLength - targetLength) > targetLength * 0.001 || Dot(solvedAxis, targetAxis) / (solvedLength * targetLength) < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
            return Fail("frame 33 contact result violated a target bone length or bend limit");
    }
    return true;
}

/**
 * 実機frame 433を60度ではatomicに拒否し、90度では接触と鎖制約を確かめる。
 */
bool TestRecordedSkinningChainFrame433Contact()
{
    const gk::Vec3 targets[5] = { { 0.0524181724f, 1.30573833f, 2.30838704f }, { 0.025579948f, 1.21521747f, 2.31881022f }, { 0.0101585453f, 1.15487289f, 2.32115054f }, { 0.000517625362f, 1.12020171f, 2.32080936f }, { -0.00999284349f, 1.08685613f, 2.31922412f } };
    const gk::Vec3 initialPositions[5] = { { 0.0528368317f, 1.31087661f, 2.30467749f }, { 0.113033332f, 1.2387743f, 2.29051375f }, { 0.0827215463f, 1.18431616f, 2.28999019f }, { 0.0581187718f, 1.1581105f, 2.29175806f }, { 0.02991529f, 1.13859797f, 2.29873872f } };
    const gk::Vec3 initialVelocities[5] = { { -0.032729432f, -0.335047245f, 0.241670609f }, { 0.228562832f, -0.0965973064f, 0.0942454189f }, { 0.0898612887f, -0.0225474723f, 0.170084745f }, { 0.224670768f, -0.162892386f, 0.0765588284f }, { 0.260088593f, -0.178806677f, 0.183034956f } };
    const gk::Vec3 previousTargets[5] = { { 0.0528368317f, 1.31087661f, 2.30467749f }, { 0.0256179348f, 1.22057271f, 2.31596041f }, { 0.00996227562f, 1.16031396f, 2.31888461f }, { 0.000193927262f, 1.12567675f, 2.31887579f }, { -0.0104337437f, 1.09235489f, 2.31760502f } };
    const FModelSecondaryMotionCollisionShape previousShapes[8] = { Shape({ 0.0149547318f, 1.25374651f, 2.23203135f }, { 0.0149547318f, 1.25374651f, 2.23203135f }, 0.108000003f), Shape({ 0.0308959652f, 1.3051995f, 2.23745108f }, { 0.0840883404f, 1.39868951f, 2.26047683f }, 0.118000008f), Shape({ 0.0884561986f, 1.40636647f, 2.26236749f }, { 0.125415996f, 1.5323534f, 2.27019453f }, 0.118000001f), Shape({ 0.157499328f, 1.67349601f, 2.2686193f }, { 0.157499328f, 1.67349601f, 2.2686193f }, 0.128000006f), Shape({ 0.0382152349f, 1.20976925f, 2.16110086f }, { 0.202875867f, 1.08058834f, 2.14380431f }, 0.088000007f), Shape({ -0.0294418968f, 1.23463035f, 2.28405356f }, { 0.11220938f, 1.21254826f, 2.43750477f }, 0.088000007f), Shape({ 0.218928427f, 1.03340936f, 2.13124776f }, { 0.117491439f, 0.778664827f, 2.0368247f }, 0.0780000016f), Shape({ 0.123567298f, 1.18290484f, 2.4528687f }, { 0.0126074441f, 0.930764139f, 2.36224341f }, 0.0780000016f) };
    const FModelSecondaryMotionCollisionShape currentShapes[8] = { Shape({ 0.0144355483f, 1.24925363f, 2.23529005f }, { 0.0144355483f, 1.24925363f, 2.23529005f }, 0.108000003f), Shape({ 0.0302106198f, 1.30070412f, 2.24119711f }, { 0.0841112509f, 1.39336157f, 2.2658813f }, 0.118000008f), Shape({ 0.0885372683f, 1.4009701f, 2.26790833f }, { 0.12732403f, 1.52617562f, 2.27882433f }, 0.118000008f), Shape({ 0.159450129f, 1.66695678f, 2.2802732f }, { 0.159450129f, 1.66695678f, 2.2802732f }, 0.128000006f), Shape({ 0.037584126f, 1.20612586f, 2.16409612f }, { 0.202856869f, 1.07772315f, 2.14684272f }, 0.088000007f), Shape({ -0.0294804778f, 1.23011863f, 2.28721166f }, { 0.114214748f, 1.21185112f, 2.43925762f }, 0.0879999995f), Shape({ 0.218926534f, 1.03082323f, 2.13397288f }, { 0.116700955f, 0.777564347f, 2.03645468f }, 0.0780000016f), Shape({ 0.126307994f, 1.18245208f, 2.45496225f }, { 0.0192711167f, 0.926729083f, 2.36982226f }, 0.0780000016f) };
    gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    settings.frequencyHz = 5.0f;
    settings.dampingRatio = 0.800000012f;
    settings.maxAngleDegrees = 60.0f;
    settings.gravity = { 0.0f, -1.0f, 0.0f };
    settings.endOffset = { 0.0f, 0.0350000001f, 0.0f };
    settings.windAcceleration = { 0.0f, 0.0f, 0.0f };
    settings.teleportDistance = 0.5f;
    settings.constraintIterations = 32;
    FModelSecondaryMotionChainState state;
    FModelSecondaryMotionChainState snapshot;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 5, state, error))
        return Fail(error.CStr());
    for (uint32_t index = 0; index < 5; ++index)
    {
        state.points.At(index).position = initialPositions[index];
        state.points.At(index).velocity = initialVelocities[index];
        state.previousTargets.At(index) = previousTargets[index];
    }
    if (!CopyState(state, snapshot))
        return Fail("could not snapshot the recorded frame 433 state before its 60-degree attempt");
    if (StepSecondaryMotionChainWithCollisions(targets, 5, settings, 0.016666666666666666, previousShapes, currentShapes, 8, state, error, 0.0f) || error.Empty() || !SameState(state, snapshot))
        return Fail("frame 433 60-degree solve succeeded or changed state after failing to find a candidate");
    settings.maxAngleDegrees = 90.0f;
    error.Clear();
    if (!StepSecondaryMotionChainWithCollisions(targets, 5, settings, 0.016666666666666666, previousShapes, currentShapes, 8, state, error, 0.0f))
        return Fail(error.CStr());
    if (state.points.Count() != 5 || state.previousTargets.Count() != 5 || !IsFinite(state))
        return Fail("frame 433 contact result has an invalid state size or non-finite value");
    if (fabs(state.points.At(0).position.x - targets[0].x) > 1.0e-7f || fabs(state.points.At(0).position.y - targets[0].y) > 1.0e-7f || fabs(state.points.At(0).position.z - targets[0].z) > 1.0e-7f)
        return Fail("frame 433 contact result moved its pinned root");

    gk::FVector3d positions[5]{};
    for (uint32_t index = 0; index < 5; ++index)
        positions[index] = ToDouble(state.points.At(index).position);
    if (!CheckSecondaryMotionContacts(currentShapes, 8, positions, 5, error))
        return Fail("frame 433 contact result still penetrates a current body shape");
    for (uint32_t shapeIndex = 0; shapeIndex < 8; ++shapeIndex)
    {
        const double radius = currentShapes[shapeIndex].radius;
        const double tolerance = fmax(radius * 1.0e-3, radius * std::numeric_limits<double>::epsilon() * 64.0);
        const double rootDistance = SegmentDistance(positions[0], positions[0], ToDouble(currentShapes[shapeIndex].start), ToDouble(currentShapes[shapeIndex].end));
        const bool rootInside = rootDistance < radius - tolerance;
        for (uint32_t pointIndex = 1; pointIndex < 5; ++pointIndex)
        {
            const double pointDistance = SegmentDistance(positions[pointIndex], positions[pointIndex], ToDouble(currentShapes[shapeIndex].start), ToDouble(currentShapes[shapeIndex].end));
            if (pointDistance < radius - tolerance)
                return Fail("frame 433 result left a free point inside a body shape by the independent distance oracle");
            if (pointIndex == 1 && rootInside)
                continue;
            const double spanDistance = SegmentDistance(positions[pointIndex - 1], positions[pointIndex], ToDouble(currentShapes[shapeIndex].start), ToDouble(currentShapes[shapeIndex].end));
            if (spanDistance < radius - tolerance)
                return Fail("frame 433 result left a span inside a body shape by the independent distance oracle");
        }
    }
    for (uint32_t index = 1; index < 5; ++index)
    {
        const gk::FVector3d targetAxis = Point(static_cast<double>(targets[index].x) - targets[index - 1].x, static_cast<double>(targets[index].y) - targets[index - 1].y, static_cast<double>(targets[index].z) - targets[index - 1].z);
        const gk::FVector3d solvedAxis = Point(positions[index].value[0] - positions[index - 1].value[0], positions[index].value[1] - positions[index - 1].value[1], positions[index].value[2] - positions[index - 1].value[2]);
        const double targetLength = Distance(Point(0.0, 0.0, 0.0), targetAxis);
        const double solvedLength = Distance(Point(0.0, 0.0, 0.0), solvedAxis);
        if (!(solvedLength > 0.0) || fabs(solvedLength - targetLength) > targetLength * 0.001 || Dot(solvedAxis, targetAxis) / (solvedLength * targetLength) < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.001)
            return Fail("frame 433 contact result violated a target bone length or bend limit");
    }
    return true;
}

/**
 * frame 33の全節方向を同時に解き、鎖全体の身体接触・長さ・角度を独立に確かめる。
 */
bool TestPreparedWholeChainContactsForFrame33()
{
    const gk::Vec3 stepTargets[5] = { { 0.015328452922403812f, 1.0035490989685059f, 0.86382400989532471f }, { 0.017840046435594559f, 0.91059046983718872f, 0.8745352029800415f }, { 0.019504979252815247f, 0.84873634576797485f, 0.8818382024765015f }, { 0.02054649218916893f, 0.812482476234436f, 0.8842756152153015f }, { 0.021613022312521935f, 0.7775042653083801f, 0.8848965764045715f } };
    const gk::FVector3d initialPositions[5] = { Point(0.015328452922403812, 1.0035490989685059, 0.86382400989532471), Point(-0.047302105761660773, 0.95173579704578026, 0.91024748835217639), Point(-0.023339989720381673, 0.92713839793601505, 0.9622361738439259), Point(0.0015777348288604776, 0.91019939287402507, 0.98257200175942239), Point(0.018528778633142357, 0.8912036641876121, 0.95855486778161891) };
    const FModelSecondaryMotionCollisionShape currentShapes[8] = { Shape({ 0.0195087641f, 0.934830189f, 0.806522369f }, { 0.0195087641f, 0.934830189f, 0.806522369f }, 0.108000003f), Shape({ 0.0178112816f, 0.98894006f, 0.806902885f }, { 0.0139207728f, 1.09878945f, 0.811141849f }, 0.118000008f), Shape({ 0.0136013068f, 1.10780966f, 0.81148988f }, { 0.0124743786f, 1.23135614f, 0.76637888f }, 0.118000001f), Shape({ 0.0159734208f, 1.37230504f, 0.733028591f }, { 0.0159734208f, 1.37230504f, 0.733028591f }, 0.128000006f), Shape({ 0.083589755f, 0.885942757f, 0.801124156f }, { 0.105068296f, 0.747396111f, 0.95746839f }, 0.088000007f), Shape({ -0.0418595113f, 0.893231034f, 0.802915514f }, { -0.0727071017f, 0.831402183f, 1.00122237f }, 0.0879999995f), Shape({ 0.110421143f, 0.70020175f, 0.965985656f }, { 0.128692657f, 0.459904402f, 0.804668188f }, 0.0780000016f), Shape({ -0.081521675f, 0.792448759f, 1.02647865f }, { -0.11865557f, 0.512254894f, 0.961575389f }, 0.0780000016f) };
    FModelSecondaryMotionStepShapeCache cache(currentShapes, 8);
    FModelSecondaryMotionStepShapes view{};
    gk::Array<gk::FVector3d> positions;
    gk::Array<FSecondaryMotionReferenceSegment> references;
    gk::String error;
    if (!cache.Get(0.0f, view, error) || view.count != 8 || !view.prepared || !positions.AppendRange(initialPositions, 5))
        return Fail("could not prepare frame 33 chain positions and guarded shapes");
    for (uint32_t index = 1; index < 5; ++index)
    {
        FSecondaryMotionReferenceSegment segment{};
        segment.direction = Point(static_cast<double>(stepTargets[index].x) - stepTargets[index - 1].x, static_cast<double>(stepTargets[index].y) - stepTargets[index - 1].y, static_cast<double>(stepTargets[index].z) - stepTargets[index - 1].z);
        segment.length = Distance(Point(0.0, 0.0, 0.0), segment.direction);
        for (uint32_t axis = 0; axis < 3; ++axis)
            segment.direction.value[axis] /= segment.length;
        if (!references.Append(segment))
            return Fail("could not allocate frame 33 reference segments");
    }
    const gk::FVector3d rootBefore = positions.At(0);
    if (!ResolvePreparedSecondaryMotionChainContacts(view.prepared, view.count, references.Data(), references.Count(), 0.5, positions, error, 1.0) || !error.Empty())
        return Fail(error.Empty() ? "whole-chain direction solve found no frame 33 candidate" : error.CStr());
    if (positions.Count() != 5 || memcmp(&positions.At(0), &rootBefore, sizeof(rootBefore)) != 0)
        return Fail("whole-chain direction solve changed its fixed root or point count");
    for (uint32_t index = 0; index < 5; ++index)
    {
        if (!IsFinite(positions.At(index)))
            return Fail("whole-chain direction solve produced a non-finite position");
    }
    if (!CheckPreparedSecondaryMotionContacts(view.prepared, view.count, positions.Data(), positions.Count(), error, 1.0))
        return Fail("whole-chain direction solve left a guarded shape contact");
    for (uint32_t shapeIndex = 0; shapeIndex < view.count; ++shapeIndex)
    {
        const FPreparedSecondaryMotionCollisionShape& shape = view.prepared[shapeIndex];
        const bool rootInside = SegmentDistance(positions.At(0), positions.At(0), shape.start, shape.end) + shape.tolerance < shape.radius;
        for (uint32_t pointIndex = 1; pointIndex < positions.Count(); ++pointIndex)
        {
            const double pointDistance = SegmentDistance(positions.At(pointIndex), positions.At(pointIndex), shape.start, shape.end);
            if (pointDistance + shape.tolerance < shape.radius)
                return Fail("whole-chain solution left a free point inside a guarded shape by the independent distance oracle");
            if (pointIndex == 1 && rootInside)
                continue;
            const double spanDistance = SegmentDistance(positions.At(pointIndex - 1), positions.At(pointIndex), shape.start, shape.end);
            if (spanDistance + shape.tolerance < shape.radius)
                return Fail("whole-chain solution left a free span inside a guarded shape by the independent distance oracle");
        }
    }
    for (uint32_t index = 0; index < references.Count(); ++index)
    {
        const gk::FVector3d solvedAxis = Point(positions.At(index + 1).value[0] - positions.At(index).value[0], positions.At(index + 1).value[1] - positions.At(index).value[1], positions.At(index + 1).value[2] - positions.At(index).value[2]);
        const double solvedLength = Distance(Point(0.0, 0.0, 0.0), solvedAxis);
        if (fabs(solvedLength - references.At(index).length) > references.At(index).length * 0.001 || Dot(solvedAxis, references.At(index).direction) / solvedLength < 0.5 - 0.001)
            return Fail("whole-chain direction solve violated its target bone length or 60-degree cone");
    }

    gk::Array<gk::FVector3d> failureCandidate;
    gk::Array<gk::FVector3d> failureSnapshot;
    if (!CopyPositions(positions, failureCandidate) || !CopyPositions(positions, failureSnapshot))
        return Fail("could not snapshot whole-chain direction failure input");
    if (ResolvePreparedSecondaryMotionChainContacts(view.prepared, view.count, references.Data(), references.Count(), 1.0, failureCandidate, error, 1.0) || !error.Empty() || !SamePositions(failureCandidate, failureSnapshot))
        return Fail("infeasible zero-angle whole-chain solve changed positions or reported the wrong status");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    gk::Array<gk::FVector3d> invalidCandidate;
    gk::Array<gk::FVector3d> invalidSnapshot;
    if (!CopyPositions(positions, invalidCandidate) || !CopyPositions(positions, invalidSnapshot))
        return Fail("could not snapshot invalid whole-chain input");
    invalidCandidate.At(2).value[1] = nan;
    invalidSnapshot.At(2).value[1] = nan;
    if (ResolvePreparedSecondaryMotionChainContacts(view.prepared, view.count, references.Data(), references.Count(), 0.5, invalidCandidate, error, 1.0) || error.Empty() || invalidCandidate.Count() != invalidSnapshot.Count() || memcmp(invalidCandidate.Data(), invalidSnapshot.Data(), static_cast<size_t>(invalidCandidate.Count()) * sizeof(gk::FVector3d)) != 0)
        return Fail("non-finite whole-chain input was accepted or changed positions");
    if (ResolvePreparedSecondaryMotionChainContacts(view.prepared, view.count, references.Data(), references.Count() - 1u, 0.5, failureCandidate, error, 1.0) || error.Empty() || !SamePositions(failureCandidate, failureSnapshot))
        return Fail("mismatched whole-chain reference count was accepted or changed positions");
    if (ResolvePreparedSecondaryMotionChainContacts(nullptr, 1, references.Data(), references.Count(), 0.5, failureCandidate, error, 1.0) || error.Empty() || !SamePositions(failureCandidate, failureSnapshot))
        return Fail("missing prepared shape data was accepted or changed positions");
    if (ResolvePreparedSecondaryMotionChainContacts(view.prepared, view.count, references.Data(), references.Count(), 0.5, failureCandidate, error, -0.1) || error.Empty() || !SamePositions(failureCandidate, failureSnapshot))
        return Fail("invalid whole-chain tolerance was accepted or changed positions");

    gk::Array<gk::FVector3d> emptyShapePositions;
    gk::Array<gk::FVector3d> emptyShapeSnapshot;
    if (!CopyPositions(positions, emptyShapePositions) || !CopyPositions(positions, emptyShapeSnapshot))
        return Fail("could not copy positions for the no-shape whole-chain case");
    if (!ResolvePreparedSecondaryMotionChainContacts(nullptr, 0, references.Data(), references.Count(), 0.5, emptyShapePositions, error, 1.0) || !error.Empty() || !SamePositions(emptyShapePositions, emptyShapeSnapshot))
        return Fail("empty shape set changed the whole-chain positions or failed unexpectedly");
    return true;
}

/**
 * frame 98の固定親で、厳密なguard判定と半径許容幅内の候補を分けて調べる。
 */
bool TestFrame98DirectionToleranceFraction()
{
    const gk::Vec3 targets[2] = { { -0.0131363347f, 1.26076651f, 2.182899f }, { -0.0257917773f, 1.22813702f, 2.1826458f } };
    const FModelSecondaryMotionCollisionShape currentShapes[8] = { Shape({ 0.0157923214f, 1.38247502f, 2.09063911f }, { 0.0157923214f, 1.38247502f, 2.09063911f }, 0.108000003f), Shape({ 0.0346308611f, 1.43308699f, 2.09444022f }, { 0.0811000615f, 1.53149605f, 2.11044836f }, 0.118000001f), Shape({ 0.0849158466f, 1.53957689f, 2.11176276f }, { 0.107534401f, 1.66914582f, 2.11121702f }, 0.118000001f), Shape({ 0.169579446f, 1.79640996f, 2.12838793f }, { 0.169579446f, 1.79640996f, 2.12838793f }, 0.128000006f), Shape({ 0.0364473723f, 1.32969165f, 2.02937508f }, { 0.161756068f, 1.16576648f, 2.06844211f }, 0.088000007f), Shape({ -0.0406854339f, 1.35646403f, 2.1398735f }, { 0.0360126197f, 1.24598169f, 2.30115294f }, 0.0879999995f), Shape({ 0.168105498f, 1.11549938f, 2.06291747f }, { 0.0341988541f, 0.885049999f, 1.94862795f }, 0.0780000016f), Shape({ 0.035163451f, 1.20142019f, 2.33028865f }, { -0.0925733745f, 0.942837358f, 2.36059022f }, 0.0780000016f) };
    const gk::FVector3d parent = Point(0.08720073209638556, 1.3231778912282939, 2.2986010876469303);
    gk::FVector3d reference = Point(static_cast<double>(targets[1].x) - targets[0].x, static_cast<double>(targets[1].y) - targets[0].y, static_cast<double>(targets[1].z) - targets[0].z);
    const double length = Distance(Point(0.0, 0.0, 0.0), reference);
    for (uint32_t axis = 0; axis < 3; ++axis)
        reference.value[axis] /= length;
    gk::FVector3d candidate = Point(0.62145, -0.77805, 0.091835);
    FModelSecondaryMotionStepShapeCache cache(currentShapes, 8);
    FModelSecondaryMotionStepShapes view{};
    gk::String error;
    if (!cache.Get(0.0f, view, error) || view.count != 8 || !view.prepared)
        return Fail("could not prepare guarded frame 98 shapes for direction tolerance checks");

    const gk::FVector3d strictCandidate = candidate;
    if (FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, parent, length, false, reference, 0.5, candidate, error, 0.0) || !error.Empty() || memcmp(&candidate, &strictCandidate, sizeof(candidate)) != 0)
        return Fail("frame 98 candidate was accepted by strict guarded-body clearance or changed on no-solution");
    if (!FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, parent, length, false, reference, 0.5, candidate, error, 1.0) || !error.Empty() || !IsFinite(candidate))
        return Fail(error.Empty() ? "frame 98 candidate was not found inside the prepared tolerance" : error.CStr());

    const double directionLength = Distance(Point(0.0, 0.0, 0.0), candidate);
    const gk::FVector3d endpoint = Point(parent.value[0] + candidate.value[0] * length, parent.value[1] + candidate.value[1] * length, parent.value[2] + candidate.value[2] * length);
    if (fabs(directionLength - 1.0) > 1.0e-8 || Dot(candidate, reference) / directionLength < 0.5 - 0.001 || !CheckPreparedSecondaryMotionFreeSegmentContacts(view.prepared, view.count, parent, endpoint, error, 1.0) || !IsFreeSegmentClear(currentShapes, 8, 0.0f, parent, endpoint))
        return Fail("frame 98 tolerance candidate violated the guarded tolerance, original shape, length, or cone contract");

    const double invalidFractions[3] = { -0.1, 1.01, std::numeric_limits<double>::quiet_NaN() };
    for (uint32_t index = 0; index < 3; ++index)
    {
        gk::FVector3d invalidOutput = strictCandidate;
        const gk::FVector3d savedOutput = invalidOutput;
        gk::String diagnostic;
        if (FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, parent, length, false, reference, 0.5, invalidOutput, diagnostic, invalidFractions[index]) || diagnostic.Empty() || memcmp(&invalidOutput, &savedOutput, sizeof(invalidOutput)) != 0)
            return Fail("invalid direction tolerance fraction was accepted or changed its output");
    }
    return true;
}

/**
 * 固定親位置から全身体形状を同時に避ける方向を探し、角度制約と失敗時の出力保持を調べる。
 */
bool TestPreparedJointDirectionForRecordedSkinningChain()
{
    const gk::Vec3 targets[5] = { { 0.0137152309f, 0.990111709f, 0.798526406f }, { 0.0168546848f, 0.896700263f, 0.803701699f }, { 0.0189411007f, 0.834534466f, 0.807320952f }, { 0.0201907903f, 0.798206449f, 0.807601452f }, { 0.0214216802f, 0.763258517f, 0.806143463f } };
    const FModelSecondaryMotionCollisionShape currentShapes[8] = { Shape({ 0.0169844441f, 0.924896896f, 0.737210929f }, { 0.0169844441f, 0.924896896f, 0.737210929f }, 0.108000003f), Shape({ 0.0150609855f, 0.978881001f, 0.740805805f }, { 0.0114761218f, 1.08873475f, 0.745200336f }, 0.118000008f), Shape({ 0.0111817531f, 1.09775531f, 0.745561182f }, { 0.00944366772f, 1.21850741f, 0.693446755f }, 0.118000008f), Shape({ 0.0124584585f, 1.35788345f, 0.652506113f }, { 0.0124584585f, 1.35788345f, 0.652506113f }, 0.128000006f), Shape({ 0.0808926225f, 0.874606252f, 0.725257754f }, { 0.104470044f, 0.712119758f, 0.856186211f }, 0.088000007f), Shape({ -0.0448668823f, 0.877693534f, 0.729560435f }, { -0.0754696205f, 0.765339732f, 0.904317677f }, 0.0879999995f), Shape({ 0.109497063f, 0.663221359f, 0.857721746f }, { 0.121347837f, 0.443760276f, 0.668522418f }, 0.0780000016f), Shape({ -0.0835563317f, 0.720323741f, 0.919595659f }, { -0.114035763f, 0.460263878f, 0.794936776f }, 0.0780000016f) };
    const gk::FVector3d parent = Point(0.011513302372902844, 0.8537709445352083, 0.8582320426416739);
    const gk::FVector3d targetEnd = Point(targets[4].x, targets[4].y, targets[4].z);
    const gk::FVector3d targetStart = Point(targets[3].x, targets[3].y, targets[3].z);
    gk::FVector3d reference = Point(targetEnd.value[0] - targetStart.value[0], targetEnd.value[1] - targetStart.value[1], targetEnd.value[2] - targetStart.value[2]);
    const double length = Distance(Point(0.0, 0.0, 0.0), reference);
    for (uint32_t axis = 0; axis < 3; ++axis)
        reference.value[axis] /= length;
    gk::FVector3d seed = Point(0.02850950543130424 - parent.value[0], 0.8303153472273358 - parent.value[1], 0.838586106831392 - parent.value[2]);
    const double seedLength = Distance(Point(0.0, 0.0, 0.0), seed);
    for (uint32_t axis = 0; axis < 3; ++axis)
        seed.value[axis] /= seedLength;
    const double cosineLimit = cos(60.0 * 3.14159265358979323846 / 180.0);
    FModelSecondaryMotionStepShapeCache cache(currentShapes, 8);
    FModelSecondaryMotionStepShapes view{};
    gk::String error;
    if (!cache.Get(0.0f, view, error) || view.count != 8 || !view.prepared)
        return Fail("could not prepare guarded shapes for the recorded direction fixture");

    const gk::FVector3d originalSeed = seed;
    if (!FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, parent, length, false, reference, cosineLimit, seed, error) || !error.Empty() || !IsFinite(seed))
        return Fail(error.Empty() ? "joint direction search failed on the recorded feasible tip" : error.CStr());
    const double directionLength = Distance(Point(0.0, 0.0, 0.0), seed);
    const gk::FVector3d child = Point(parent.value[0] + seed.value[0] * length, parent.value[1] + seed.value[1] * length, parent.value[2] + seed.value[2] * length);
    const double cosine = Dot(seed, reference) / directionLength;
    if (fabs(directionLength - 1.0) > 1.0e-8 || cosine < cosineLimit - 0.001 || !IsFreeSegmentClear(currentShapes, 8, 0.0f, parent, child) || !CheckPreparedSecondaryMotionFreeSegmentContacts(view.prepared, view.count, parent, child, error, 0.0))
        return Fail("joint direction search did not independently clear every guarded body shape within the length and cone");

    gk::FVector3d clearSeed = seed;
    if (!FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, parent, length, false, reference, cosineLimit, clearSeed, error) || !error.Empty() || Distance(seed, clearSeed) > 1.0e-10)
        return Fail("joint direction search changed an already-clear direction");

    const gk::FVector3d root = Point(targets[0].x, targets[0].y, targets[0].z);
    gk::FVector3d firstReference = Point(static_cast<double>(targets[1].x) - targets[0].x, static_cast<double>(targets[1].y) - targets[0].y, static_cast<double>(targets[1].z) - targets[0].z);
    const double firstLength = Distance(Point(0.0, 0.0, 0.0), firstReference);
    for (uint32_t axis = 0; axis < 3; ++axis)
        firstReference.value[axis] /= firstLength;
    bool rootInside = false;
    for (uint32_t shapeIndex = 0; shapeIndex < view.count; ++shapeIndex)
    {
        const FPreparedSecondaryMotionCollisionShape& shape = view.prepared[shapeIndex];
        if (SegmentDistance(root, root, shape.start, shape.end) < shape.radius)
            rootInside = true;
    }
    if (!rootInside)
        return Fail("recorded first segment did not exercise the root-inside exception");
    gk::FVector3d firstDirection = reference;
    if (!FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, root, firstLength, true, firstReference, cosineLimit, firstDirection, error) || !error.Empty())
        return Fail(error.Empty() ? "joint direction search failed to keep a free first endpoint outside the shapes" : error.CStr());
    const gk::FVector3d firstChild = Point(root.value[0] + firstDirection.value[0] * firstLength, root.value[1] + firstDirection.value[1] * firstLength, root.value[2] + firstDirection.value[2] * firstLength);
    bool firstChildClear = true;
    for (uint32_t shapeIndex = 0; shapeIndex < view.count; ++shapeIndex)
    {
        const FPreparedSecondaryMotionCollisionShape& shape = view.prepared[shapeIndex];
        if (SegmentDistance(firstChild, firstChild, shape.start, shape.end) + 1.0e-10 < shape.radius)
            firstChildClear = false;
    }
    if (!firstChildClear || Dot(firstDirection, firstReference) / Distance(Point(0.0, 0.0, 0.0), firstDirection) < cosineLimit - 0.001)
        return Fail("root-inside exception did not preserve a clear free endpoint and the reference cone");

    gk::FVector3d impossibleDirection = firstReference;
    const gk::FVector3d savedImpossibleDirection = impossibleDirection;
    if (FindPreparedSecondaryMotionCollisionDirection(view.prepared, view.count, root, firstLength, true, firstReference, 1.0, impossibleDirection, error) || !error.Empty() || memcmp(&impossibleDirection, &savedImpossibleDirection, sizeof(impossibleDirection)) != 0)
        return Fail("infeasible zero-angle direction changed its output or reported the wrong status");

    const auto RejectAtomic = [&](const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d& candidateParent, double candidateLength, const gk::FVector3d& candidateReference, double candidateCosine)
    {
        gk::FVector3d candidate = originalSeed;
        const gk::FVector3d saved = candidate;
        gk::String diagnostic;
        return !FindPreparedSecondaryMotionCollisionDirection(shapes, shapeCount, candidateParent, candidateLength, false, candidateReference, candidateCosine, candidate, diagnostic) && !diagnostic.Empty() && memcmp(&candidate, &saved, sizeof(candidate)) == 0;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const gk::FVector3d invalidParent = Point(nan, 0.0, 0.0);
    const gk::FVector3d invalidReference = Point(0.0, 0.0, 0.0);
    if (!RejectAtomic(nullptr, 1, parent, length, reference, cosineLimit) || !RejectAtomic(view.prepared, 65, parent, length, reference, cosineLimit) || !RejectAtomic(view.prepared, view.count, invalidParent, length, reference, cosineLimit) || !RejectAtomic(view.prepared, view.count, parent, 0.0, reference, cosineLimit) || !RejectAtomic(view.prepared, view.count, parent, length, invalidReference, cosineLimit) || !RejectAtomic(view.prepared, view.count, parent, length, reference, 1.01))
        return Fail("joint direction search accepted invalid inputs or changed its output on failure");

    gk::FVector3d emptyDirection = originalSeed;
    if (!FindPreparedSecondaryMotionCollisionDirection(nullptr, 0, parent, length, false, reference, cosineLimit, emptyDirection, error) || !error.Empty() || memcmp(&emptyDirection, &originalSeed, sizeof(emptyDirection)) != 0)
        return Fail("empty prepared shape set changed the direction or failed unexpectedly");
    return true;
}

/**
 * 非軸参照のcone境界と、球中心からcone内へ抜ける方向を確認する。
 */
bool TestJointDirectionNormalizationAndConeBoundaries()
{
    const FModelSecondaryMotionCollisionShape farSphere = Shape({ 100.0f, -100.0f, 80.0f }, { 100.0f, -100.0f, 80.0f }, 0.1f);
    gk::Array<FPreparedSecondaryMotionCollisionShape> farPrepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&farSphere, 1, farPrepared, error) || farPrepared.Count() != 1)
        return Fail("could not prepare a far sphere for cone-boundary checks");
    const gk::FVector3d references[3] = { Point(0.2672612419124244, -0.5345224838248488, 0.8017837257372732), Point(-0.31234752377721214, 0.875, 0.369958), Point(0.731, 0.221, -0.645) };
    const double cosineLimits[5] = { -1.0, 0.0, 0.5, 0.7071067811865476, 1.0 };
    const gk::FVector3d parent = Point(0.17, -0.23, 0.41);
    for (uint32_t referenceIndex = 0; referenceIndex < 3; ++referenceIndex)
    {
        const double referenceLength = Distance(Point(0.0, 0.0, 0.0), references[referenceIndex]);
        gk::FVector3d normalizedReference = Point(references[referenceIndex].value[0] / referenceLength, references[referenceIndex].value[1] / referenceLength, references[referenceIndex].value[2] / referenceLength);
        for (uint32_t limitIndex = 0; limitIndex < 5; ++limitIndex)
        {
            gk::FVector3d candidate = limitIndex == 4 ? references[referenceIndex] : Point(-3.0 * references[referenceIndex].value[0], -3.0 * references[referenceIndex].value[1], -3.0 * references[referenceIndex].value[2]);
            if (!FindPreparedSecondaryMotionCollisionDirection(farPrepared.Data(), farPrepared.Count(), parent, 0.75, false, references[referenceIndex], cosineLimits[limitIndex], candidate, error) || !error.Empty() || !IsFinite(candidate))
            {
                fprintf(stderr, "cone-boundary case failed: reference=%u cosineIndex=%u cosine=%.17g error=%s\n", referenceIndex, limitIndex, cosineLimits[limitIndex], error.CStr());
                return Fail(error.Empty() ? "non-axis direction failed to normalize inside a valid cone" : error.CStr());
            }
            const double outputLength = Distance(Point(0.0, 0.0, 0.0), candidate);
            const double outputCosine = Dot(candidate, normalizedReference) / outputLength;
            if (fabs(outputLength - 1.0) > 1.0e-12 || outputCosine < cosineLimits[limitIndex] - 2.0e-12)
                return Fail("non-axis cone clamp returned a non-unit direction or crossed its cosine boundary");
        }
    }

    const gk::FVector3d sphereCenter = Point(0.0, 0.0, 1.0);
    const FModelSecondaryMotionCollisionShape centerSphere = Shape({ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, 0.2f);
    gk::Array<FPreparedSecondaryMotionCollisionShape> centerPrepared;
    if (!PrepareSecondaryMotionCollisionShapes(&centerSphere, 1, centerPrepared, error) || centerPrepared.Count() != 1)
        return Fail("could not prepare the endpoint-center sphere");
    const gk::FVector3d centerParent = Point(0.0, 0.0, 0.0);
    const gk::FVector3d centerReference = Point(0.0, 0.0, 1.0);
    const double centerCosineLimits[2] = { 0.5, 0.7071067811865476 };
    for (uint32_t index = 0; index < 2; ++index)
    {
        gk::FVector3d candidate = centerReference;
        if (!FindPreparedSecondaryMotionCollisionDirection(centerPrepared.Data(), centerPrepared.Count(), centerParent, 1.0, false, centerReference, centerCosineLimits[index], candidate, error) || !error.Empty() || !IsFinite(candidate))
        {
            fprintf(stderr, "sphere-center cone case failed: reference=3 cosineIndex=%u cosine=%.17g error=%s\n", index, centerCosineLimits[index], error.CStr());
            return Fail(error.Empty() ? "cone grid failed to find a feasible escape from a sphere-centered endpoint" : error.CStr());
        }
        const gk::FVector3d endpoint = Point(candidate.value[0], candidate.value[1], candidate.value[2]);
        const double outputLength = Distance(Point(0.0, 0.0, 0.0), candidate);
        const double outputCosine = Dot(candidate, centerReference) / outputLength;
        if (SegmentDistance(endpoint, endpoint, sphereCenter, sphereCenter) + 1.0e-12 < centerPrepared.At(0).radius || fabs(outputLength - 1.0) > 1.0e-12 || outputCosine < centerCosineLimits[index] - 2.0e-12 || !CheckPreparedSecondaryMotionFreeSegmentContacts(centerPrepared.Data(), centerPrepared.Count(), centerParent, endpoint, error, 0.0))
            return Fail("sphere-centered endpoint escape violated independent clearance, bone length, or cone bounds");
    }

    gk::FVector3d clearZeroAngle = centerReference;
    if (!FindPreparedSecondaryMotionCollisionDirection(farPrepared.Data(), farPrepared.Count(), parent, 1.0, false, centerReference, 1.0, clearZeroAngle, error) || !error.Empty() || memcmp(&clearZeroAngle, &centerReference, sizeof(clearZeroAngle)) != 0)
        return Fail("zero-angle clear reference direction was rejected or changed");
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
 * 準備済み形状に対する自由線分の侵入深さを独立距離式と比較する。
 */
bool TestPreparedFreeSegmentPenetrationMeasurement()
{
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    const FModelSecondaryMotionCollisionShape capsule = Shape({ 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.4f);
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedSphere;
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedCapsule;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sphere, 1, preparedSphere, error) || preparedSphere.Count() != 1 || !PrepareSecondaryMotionCollisionShapes(&capsule, 1, preparedCapsule, error) || preparedCapsule.Count() != 1)
        return Fail("could not prepare the penetration measurement shapes");

    const auto MeasureAgainstOracle = [&](const FPreparedSecondaryMotionCollisionShape& shape, const gk::FVector3d& first, const gk::FVector3d& second, double expected, const char* message)
    {
        const double oracle = shape.radius - SegmentDistance(first, second, shape.start, shape.end);
        double measured = -123.5;
        error.Clear();
        if (fabs(oracle - expected) > 1.0e-12 || !MeasurePreparedSecondaryMotionFreeSegmentPenetration(shape, first, second, measured, error) || !error.Empty() || fabs(measured - oracle) > 1.0e-12)
            return Fail(message);
        return true;
    };
    const gk::FVector3d sphereCrossFirst = Point(-1.0, 0.0, 0.0);
    const gk::FVector3d sphereCrossSecond = Point(1.0, 0.0, 0.0);
    if (!MeasureAgainstOracle(preparedSphere.At(0), sphereCrossFirst, sphereCrossSecond, 0.5, "sphere-crossing segment did not report its radius as penetration depth"))
        return false;
    if (!MeasureAgainstOracle(preparedSphere.At(0), Point(-1.0, 0.5, 0.0), Point(1.0, 0.5, 0.0), 0.0, "tangent sphere segment did not report zero penetration"))
        return false;
    if (!MeasureAgainstOracle(preparedSphere.At(0), Point(-1.0, 0.75, 0.0), Point(1.0, 0.75, 0.0), -0.25, "outside sphere segment did not report a negative clearance depth"))
        return false;
    if (!MeasureAgainstOracle(preparedCapsule.At(0), Point(0.1, -0.5, 0.0), Point(0.1, 0.5, 0.0), static_cast<double>(capsule.radius) - 0.1, "capsule-side distance did not produce the expected penetration depth"))
        return false;
    if (!MeasureAgainstOracle(preparedCapsule.At(0), Point(0.0, 1.3, 0.0), Point(0.2, 1.3, 0.0), static_cast<double>(capsule.radius) - 0.3, "capsule-endpoint distance did not produce the expected penetration depth"))
        return false;
    if (!MeasureAgainstOracle(preparedCapsule.At(0), Point(0.0, 1.25, 0.0), Point(0.0, 1.25, 0.0), static_cast<double>(capsule.radius) - 0.25, "degenerate segment against a capsule endpoint was measured incorrectly"))
        return false;

    const double sentinelDepth = 37.25;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const auto RejectAtomic = [&](const FPreparedSecondaryMotionCollisionShape& shape, const gk::FVector3d& first, const gk::FVector3d& second)
    {
        double depth = sentinelDepth;
        error.Clear();
        const bool rejected = !MeasurePreparedSecondaryMotionFreeSegmentPenetration(shape, first, second, depth, error) && !error.Empty() && memcmp(&depth, &sentinelDepth, sizeof(depth)) == 0;
        return rejected;
    };
    FPreparedSecondaryMotionCollisionShape invalid = preparedSphere.At(0);
    invalid.radius = std::numeric_limits<double>::quiet_NaN();
    if (!RejectAtomic(invalid, sphereCrossFirst, sphereCrossSecond))
        return Fail("non-finite prepared radius was accepted or changed the depth output");
    invalid = preparedSphere.At(0);
    invalid.radius = -0.1;
    if (!RejectAtomic(invalid, sphereCrossFirst, sphereCrossSecond))
        return Fail("negative prepared radius was accepted or changed the depth output");
    invalid = preparedSphere.At(0);
    invalid.boundsLow[1] = infinity;
    if (!RejectAtomic(invalid, sphereCrossFirst, sphereCrossSecond))
        return Fail("non-finite prepared bounds were accepted or changed the depth output");
    if (!RejectAtomic(preparedSphere.At(0), Point(nan, 0.0, 0.0), sphereCrossSecond) || !RejectAtomic(preparedSphere.At(0), sphereCrossFirst, Point(0.0, infinity, 0.0)) || !RejectAtomic(preparedSphere.At(0), Point(static_cast<double>(std::numeric_limits<float>::max()) * 2.0, 0.0, 0.0), sphereCrossSecond))
        return Fail("invalid or out-of-float-range segment input was accepted or changed the depth output");
    return true;
}

/**
 * 親位置と曲げ制約を加えた線分投影が通常のPBD補正を保ち、失敗時に端点を保つ。
 */
bool TestAnchoredPreparedFreeSegmentProjection()
{
    const FModelSecondaryMotionCollisionShape sphere = Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f);
    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    gk::String error;
    if (!PrepareSecondaryMotionCollisionShapes(&sphere, 1, prepared, error) || prepared.Count() != 1)
        return Fail("could not prepare the anchored-projection sphere");

    const gk::FVector3d referenceFirst = Point(-1.0, 0.25, 0.0);
    const gk::FVector3d referenceSecond = Point(1.0, 0.25, 0.0);
    const gk::model::animation::FSecondaryMotionFreeSegmentAnchor firstAnchor = { Point(-1.0, 0.25, 1.0), Point(0.0, 0.0, -1.0), -1.0 };
    const gk::model::animation::FSecondaryMotionFreeSegmentAnchor secondAnchor = { Point(1.0, 0.25, 1.0), Point(0.0, 0.0, -1.0), -1.0 };
    gk::FVector3d anchoredFirst = referenceFirst;
    gk::FVector3d anchoredSecond = referenceSecond;
    gk::FVector3d legacyFirst = referenceFirst;
    gk::FVector3d legacySecond = referenceSecond;
    if (!ProjectPreparedSecondaryMotionFreeSegmentContactsWithAnchors(prepared.Data(), prepared.Count(), referenceFirst, referenceSecond, firstAnchor, secondAnchor, anchoredFirst, anchoredSecond, error) || !error.Empty())
        return Fail("anchored sphere projection did not succeed");
    if (!ProjectPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), referenceFirst, referenceSecond, legacyFirst, legacySecond, error) || !error.Empty())
        return Fail("legacy sphere projection did not succeed for comparison");
    if (memcmp(&anchoredFirst, &legacyFirst, sizeof(legacyFirst)) != 0 || memcmp(&anchoredSecond, &legacySecond, sizeof(legacySecond)) != 0)
        return Fail("orthogonal parent anchors changed the unconstrained PBD projection result");

    // 中点の法線は+y、両端の重みは1/2なので、PBD分母1/2で両端が同量だけ動く。
    const double guard = fmax(1.0e-7, fmax(static_cast<double>(sphere.radius) * 1.0e-6, std::numeric_limits<float>::epsilon()));
    const double penetration = static_cast<double>(sphere.radius) + guard - 0.25;
    const double expectedY = 0.25 + penetration * 0.5 / 0.5;
    if (fabs(anchoredFirst.value[0] + 1.0) > 1.0e-12 || fabs(anchoredSecond.value[0] - 1.0) > 1.0e-12 || fabs(anchoredFirst.value[1] - expectedY) > 1.0e-12 || fabs(anchoredSecond.value[1] - expectedY) > 1.0e-12 || fabs(anchoredFirst.value[2]) > 1.0e-12 || fabs(anchoredSecond.value[2]) > 1.0e-12)
        return Fail("anchored sphere projection did not match the independent single-step PBD equation");
    if (SegmentDistance(anchoredFirst, anchoredSecond, Point(0.0, 0.0, 0.0), Point(0.0, 0.0, 0.0)) < sphere.radius)
        return Fail("anchored sphere projection remained penetrating");

    gk::FVector3d emptyFirst = Point(4.0, -3.0, 2.0);
    gk::FVector3d emptySecond = Point(-2.0, 5.0, 1.0);
    const gk::FVector3d savedEmptyFirst = emptyFirst;
    const gk::FVector3d savedEmptySecond = emptySecond;
    if (!ProjectPreparedSecondaryMotionFreeSegmentContactsWithAnchors(nullptr, 0, savedEmptyFirst, savedEmptySecond, firstAnchor, secondAnchor, emptyFirst, emptySecond, error) || !error.Empty() || memcmp(&emptyFirst, &savedEmptyFirst, sizeof(emptyFirst)) != 0 || memcmp(&emptySecond, &savedEmptySecond, sizeof(emptySecond)) != 0)
        return Fail("zero-body anchored projection did not preserve both endpoints");

    gk::FVector3d aliased = Point(7.0, 8.0, 9.0);
    const gk::FVector3d savedAlias = aliased;
    if (ProjectPreparedSecondaryMotionFreeSegmentContactsWithAnchors(prepared.Data(), prepared.Count(), savedAlias, savedAlias, firstAnchor, secondAnchor, aliased, aliased, error) || error.Empty() || memcmp(&aliased, &savedAlias, sizeof(aliased)) != 0)
        return Fail("aliased anchored-projection endpoints were accepted or changed");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto RejectAtomic = [&](const gk::model::animation::FSecondaryMotionFreeSegmentAnchor& invalidFirst, const gk::model::animation::FSecondaryMotionFreeSegmentAnchor& invalidSecond)
    {
        gk::FVector3d first = Point(3.0, 4.0, 5.0);
        gk::FVector3d second = Point(6.0, 7.0, 8.0);
        const gk::FVector3d savedFirst = first;
        const gk::FVector3d savedSecond = second;
        error.Clear();
        return !ProjectPreparedSecondaryMotionFreeSegmentContactsWithAnchors(prepared.Data(), prepared.Count(), referenceFirst, referenceSecond, invalidFirst, invalidSecond, first, second, error) && !error.Empty() && memcmp(&first, &savedFirst, sizeof(first)) == 0 && memcmp(&second, &savedSecond, sizeof(second)) == 0;
    };
    const gk::model::animation::FSecondaryMotionFreeSegmentAnchor zeroDirection = { Point(-1.0, 0.25, 1.0), Point(0.0, 0.0, 0.0), -1.0 };
    const gk::model::animation::FSecondaryMotionFreeSegmentAnchor nonFiniteParent = { Point(nan, 0.25, 1.0), Point(0.0, 0.0, -1.0), -1.0 };
    const gk::model::animation::FSecondaryMotionFreeSegmentAnchor invalidCosine = { Point(1.0, 0.25, 1.0), Point(0.0, 0.0, -1.0), 1.0001 };
    if (!RejectAtomic(zeroDirection, secondAnchor) || !RejectAtomic(firstAnchor, nonFiniteParent) || !RejectAtomic(firstAnchor, invalidCosine))
        return Fail("invalid anchored projection input was accepted or changed an endpoint");
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
 * 別所有のprojection候補を3→5→3点で再利用し、失敗後も正しい結果へ戻れる。
 */
bool TestPreparedPointContactsCallerScratchReuseAndAtomicity()
{
    const FModelSecondaryMotionCollisionShape farSphere = Shape({ 100.0f, 100.0f, 100.0f }, { 100.0f, 100.0f, 100.0f }, 0.5f);
    const FModelSecondaryMotionCollisionShape contactShapes[2] = { Shape({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f), Shape({ 0.0f, -0.2f, 0.0f }, { 0.0f, 0.2f, 0.0f }, 0.25f) };
    const FModelSecondaryMotionCollisionShape blockingShapes[2] = { Shape({ -0.5f, 0.0f, 0.0f }, { -0.5f, 0.0f, 0.0f }, 0.8f), Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.8f) };
    const gk::FVector3d targetsThreeA[3] = { Point(-0.0, 0.0, -0.0), Point(3.0, 0.0, 0.0), Point(4.0, 1.0, -0.0) };
    const gk::FVector3d targetsFive[5] = { Point(0.0, 1.0, 0.0), Point(1.0, 1.0, 0.0), Point(2.0, 1.0, 0.0), Point(3.0, 1.0, 0.0), Point(4.0, 1.0, 0.0) };
    const gk::FVector3d targetsThreeB[3] = { Point(10.0, -2.0, 1.0), Point(11.0, -2.0, 1.0), Point(12.0, -2.0, 1.0) };
    const gk::FVector3d contactTargets[2] = { Point(0.0, 0.0, 0.0), Point(0.0, 0.1, 0.0) };
    const gk::FVector3d blockedTargets[2] = { Point(0.0, 0.0, 0.0), Point(0.1, 0.0, 0.0) };
    const auto SameBytes = [](const gk::Array<gk::FVector3d>& first, const gk::Array<gk::FVector3d>& second) { return first.Count() == second.Count() && (!first.Count() || memcmp(first.Data(), second.Data(), static_cast<size_t>(first.Count()) * sizeof(gk::FVector3d)) == 0); };
    const auto Prepare = [](const FModelSecondaryMotionCollisionShape* shapes, uint32_t count, gk::Array<FPreparedSecondaryMotionCollisionShape>& prepared, gk::String& error) { return PrepareSecondaryMotionCollisionShapes(shapes, count, prepared, error) && prepared.Count() == count; };
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedFar;
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedContact;
    gk::Array<FPreparedSecondaryMotionCollisionShape> preparedBlocking;
    gk::String error;
    if (!Prepare(&farSphere, 1, preparedFar, error) || !Prepare(contactShapes, 2, preparedContact, error) || !Prepare(blockingShapes, 2, preparedBlocking, error))
        return Fail("could not prepare projection scratch fixtures");

    gk::Array<gk::FVector3d> sharedScratch;
    const auto CompareProjection = [&](const FPreparedSecondaryMotionCollisionShape* prepared, uint32_t shapeCount, const FModelSecondaryMotionCollisionShape* rawShapes, const gk::FVector3d* references, uint32_t count, bool aliasReferences, bool expectContact)
    {
        gk::Array<gk::FVector3d> scratchPositions;
        gk::Array<gk::FVector3d> oldPreparedPositions;
        gk::Array<gk::FVector3d> rawPositions;
        if (!scratchPositions.AppendRange(references, count) || !oldPreparedPositions.AppendRange(references, count) || !rawPositions.AppendRange(references, count))
            return false;
        const gk::FVector3d* scratchReferences = aliasReferences ? scratchPositions.Data() : references;
        if (!ProjectPreparedSecondaryMotionContacts(prepared, shapeCount, scratchReferences, count, scratchPositions, sharedScratch, error) || !ProjectPreparedSecondaryMotionContacts(prepared, shapeCount, references, count, oldPreparedPositions, error) || !ProjectSecondaryMotionContacts(rawShapes, shapeCount, references, count, rawPositions, error))
            return false;
        if (!SameBytes(scratchPositions, oldPreparedPositions) || !SameBytes(scratchPositions, rawPositions))
            return false;
        if (expectContact && (!IsPointChainClearWithRootException(rawShapes, shapeCount, scratchPositions.Data(), count) || memcmp(scratchPositions.Data(), references, static_cast<size_t>(count) * sizeof(gk::FVector3d)) == 0))
            return false;
        return true;
    };

    if (!CompareProjection(preparedFar.Data(), preparedFar.Count(), &farSphere, targetsThreeA, 3, false, false) || sharedScratch.Data() == nullptr || sharedScratch.Capacity() < 3 || memcmp(sharedScratch.Data(), targetsThreeA, sizeof(targetsThreeA)) != 0)
        return Fail("cold caller scratch did not match the old projection path at three points");
    if (!CompareProjection(preparedFar.Data(), preparedFar.Count(), &farSphere, targetsFive, 5, false, false))
        return Fail("caller scratch did not grow from three to five points with byte-identical output");
    if (!CompareProjection(preparedFar.Data(), preparedFar.Count(), &farSphere, targetsThreeB, 3, true, false))
        return Fail("caller scratch did not shrink from five to three points or accept references aliased to positions");
    if (memcmp(sharedScratch.Data(), targetsThreeB, sizeof(targetsThreeB)) != 0)
        return Fail("far projection did not preserve distinct targets and signed-zero bytes");
    if (!CompareProjection(preparedContact.Data(), preparedContact.Count(), contactShapes, contactTargets, 2, false, true))
        return Fail("scratch projection differed from raw results for sphere and capsule contacts");

    gk::Array<gk::FVector3d> warmPositions;
    gk::Array<gk::FVector3d> warmExpected;
    if (!warmPositions.AppendRange(targetsFive, 5) || !warmExpected.AppendRange(targetsFive, 5) || !ProjectPreparedSecondaryMotionContacts(preparedFar.Data(), preparedFar.Count(), targetsFive, 5, warmExpected, error))
        return Fail("could not prepare the warm zero-allocation projection result");
    error.Assign("preallocated diagnostic for warm projection scratch");
    gk::SetAllocationFailureAfterForTesting(0);
    const bool warmSucceeded = ProjectPreparedSecondaryMotionContacts(preparedFar.Data(), preparedFar.Count(), targetsFive, 5, warmPositions, sharedScratch, error);
    gk::ResetAllocationFailureForTesting();
    if (!warmSucceeded || !error.Empty() || !SameBytes(warmPositions, warmExpected))
        return Fail("warmed caller scratch allocated or changed the projection result with allocation budget zero");

    gk::Array<gk::FVector3d> coldScratch;
    gk::Array<gk::FVector3d> coldPositions;
    gk::Array<gk::FVector3d> coldSnapshot;
    if (!coldPositions.AppendRange(contactTargets, 2) || !coldSnapshot.AppendRange(contactTargets, 2))
        return Fail("could not initialize cold projection scratch allocation fixture");
    error.Assign("preallocated diagnostic for cold projection scratch, keeping space for the longer allocation failure message");
    gk::SetAllocationFailureAfterForTesting(0);
    const bool coldSucceeded = ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), contactTargets, 2, coldPositions, coldScratch, error);
    gk::ResetAllocationFailureForTesting();
    if (coldSucceeded || error.Empty() || !SameBytes(coldPositions, coldSnapshot) || coldScratch.Count() != 0)
        return Fail("cold projection scratch did not fail atomically when its first reservation was denied");
    if (!ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), contactTargets, 2, coldPositions, coldScratch, error) || !error.Empty() || !IsPointChainClearWithRootException(contactShapes, 2, coldPositions.Data(), 2))
        return Fail("cold projection scratch could not retry and resolve contact after allocation failure");

    gk::Array<gk::FVector3d> blockedPositions;
    gk::Array<gk::FVector3d> blockedSnapshot;
    if (!blockedPositions.AppendRange(blockedTargets, 2) || !blockedSnapshot.AppendRange(blockedTargets, 2))
        return Fail("could not initialize nonconvergent projection scratch fixture");
    gk::Array<gk::FVector3d> oldBlockedPositions;
    gk::String oldBlockedError;
    if (!oldBlockedPositions.AppendRange(blockedTargets, 2) || ProjectPreparedSecondaryMotionContacts(preparedBlocking.Data(), preparedBlocking.Count(), blockedTargets, 2, oldBlockedPositions, oldBlockedError) || oldBlockedError.Empty() || !SameBytes(oldBlockedPositions, blockedSnapshot))
        return Fail("the old prepared path did not reject the nonconvergent projection fixture atomically");
    if (ProjectPreparedSecondaryMotionContacts(preparedBlocking.Data(), preparedBlocking.Count(), blockedTargets, 2, blockedPositions, sharedScratch, error) || error.Empty() || !SameBytes(blockedPositions, blockedSnapshot))
        return Fail("a blocked span changed positions or omitted its projection failure diagnostic");
    if (strcmp(error.CStr(), oldBlockedError.CStr()) != 0)
        return Fail("shared projection changed the old nonconvergence diagnostic");
    if (!ProjectPreparedSecondaryMotionContacts(preparedFar.Data(), preparedFar.Count(), blockedTargets, 2, blockedPositions, sharedScratch, error) || !error.Empty() || !SameBytes(blockedPositions, blockedSnapshot))
        return Fail("the same caller scratch failed to retry after an infeasible projection");

    gk::Array<gk::FVector3d> aliasPositions;
    gk::Array<gk::FVector3d> aliasSnapshot;
    if (!aliasPositions.AppendRange(contactTargets, 2) || !aliasSnapshot.AppendRange(contactTargets, 2))
        return Fail("could not initialize projection scratch object-alias fixture");
    if (ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), contactTargets, 2, aliasPositions, aliasPositions, error) || error.Empty() || !SameBytes(aliasPositions, aliasSnapshot))
        return Fail("projection scratch aliased with positions was accepted or changed output bytes");

    gk::Array<gk::FVector3d> overlapScratch;
    gk::Array<gk::FVector3d> overlapSnapshot;
    gk::Array<gk::FVector3d> overlapPositions;
    gk::Array<gk::FVector3d> overlapPositionsSnapshot;
    if (!overlapScratch.AppendRange(contactTargets, 2) || !CopyPositions(overlapScratch, overlapSnapshot) || !overlapPositions.AppendRange(contactTargets, 2) || !CopyPositions(overlapPositions, overlapPositionsSnapshot))
        return Fail("could not initialize reference/scratch overlap fixture");
    if (ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), overlapScratch.Data(), 2, overlapPositions, overlapScratch, error) || error.Empty() || !SameBytes(overlapScratch, overlapSnapshot) || !SameBytes(overlapPositions, overlapPositionsSnapshot))
        return Fail("reference targets overlapping scratch storage were accepted or changed either array");

    gk::Array<gk::FVector3d> unusedCapacityScratch;
    gk::Array<gk::FVector3d> unusedCapacityPositions;
    gk::FVector3d unusedCapacityValues[8] = { Point(10.0, 0.0, 0.0), Point(11.0, 0.0, 0.0), Point(12.0, 0.0, 0.0), Point(13.0, 0.0, 0.0), contactTargets[0], contactTargets[1], Point(16.0, 0.0, 0.0), Point(17.0, 0.0, 0.0) };
    if (!unusedCapacityScratch.AppendRange(unusedCapacityValues, 8) || !unusedCapacityPositions.AppendRange(contactTargets, 2))
        return Fail("could not initialize unused-capacity reference overlap fixture");
    unusedCapacityScratch.Clear();
    if (unusedCapacityScratch.Count() != 0 || unusedCapacityScratch.Capacity() < 8 || ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), unusedCapacityScratch.Data() + 4, 2, unusedCapacityPositions, unusedCapacityScratch, error) || error.Empty() || unusedCapacityScratch.Count() != 0 || memcmp(unusedCapacityScratch.Data(), unusedCapacityValues, sizeof(unusedCapacityValues)) != 0 || unusedCapacityPositions.Count() != 2 || memcmp(unusedCapacityPositions.Data(), contactTargets, sizeof(contactTargets)) != 0)
        return Fail("references overlapping unused caller-scratch capacity were accepted or changed either array");

    gk::Array<gk::FVector3d> invalidPositions;
    gk::Array<gk::FVector3d> invalidSnapshot;
    if (!invalidPositions.AppendRange(contactTargets, 2) || !CopyPositions(invalidPositions, invalidSnapshot))
        return Fail("could not initialize invalid-reference projection fixture");
    const gk::FVector3d invalidReferences[2] = { contactTargets[0], Point(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0) };
    FPreparedSecondaryMotionCollisionShape excessive[65]{};
    for (uint32_t index = 0; index < 65; ++index)
        excessive[index] = preparedContact.At(0);
    if (ProjectPreparedSecondaryMotionContacts(nullptr, 1, contactTargets, 2, invalidPositions, sharedScratch, error) || error.Empty() || !SameBytes(invalidPositions, invalidSnapshot))
        return Fail("null prepared shapes changed positions or lacked a diagnostic");
    if (ProjectPreparedSecondaryMotionContacts(excessive, 65, contactTargets, 2, invalidPositions, sharedScratch, error) || error.Empty() || !SameBytes(invalidPositions, invalidSnapshot))
        return Fail("excessive prepared shape count changed positions or lacked a diagnostic");
    if (ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), nullptr, 2, invalidPositions, sharedScratch, error) || error.Empty() || !SameBytes(invalidPositions, invalidSnapshot))
        return Fail("null references changed positions or lacked a diagnostic");
    if (ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), invalidReferences, 2, invalidPositions, sharedScratch, error) || error.Empty() || !SameBytes(invalidPositions, invalidSnapshot))
        return Fail("non-finite reference changed positions or lacked a diagnostic");
    gk::Array<gk::FVector3d> invalidPointPositions;
    gk::Array<gk::FVector3d> invalidPointSnapshot;
    const gk::FVector3d nonFinitePoints[2] = { contactTargets[0], Point(0.0, std::numeric_limits<double>::infinity(), 0.0) };
    if (!invalidPointPositions.AppendRange(nonFinitePoints, 2) || !CopyPositions(invalidPointPositions, invalidPointSnapshot))
        return Fail("could not initialize non-finite caller-scratch positions");
    if (ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), contactTargets, 2, invalidPointPositions, sharedScratch, error) || error.Empty() || !SameBytes(invalidPointPositions, invalidPointSnapshot))
        return Fail("non-finite positions changed output or lacked a diagnostic");
    if (ProjectPreparedSecondaryMotionContacts(preparedContact.Data(), preparedContact.Count(), contactTargets, 1, invalidPositions, sharedScratch, error) || error.Empty() || !SameBytes(invalidPositions, invalidSnapshot))
        return Fail("mismatched point count changed positions or lacked a diagnostic");

    gk::Array<gk::FVector3d> emptyPositions;
    if (!emptyPositions.AppendRange(targetsThreeA, 3))
        return Fail("could not initialize zero-shape no-op positions");
    const gk::FVector3d emptySnapshot[3] = { emptyPositions.At(0), emptyPositions.At(1), emptyPositions.At(2) };
    error.Assign("preallocated diagnostic for zero-shape projection");
    gk::SetAllocationFailureAfterForTesting(0);
    const bool emptySucceeded = ProjectPreparedSecondaryMotionContacts(nullptr, 0, nullptr, 3, emptyPositions, emptyPositions, error);
    gk::ResetAllocationFailureForTesting();
    if (!emptySucceeded || !error.Empty() || memcmp(emptyPositions.Data(), emptySnapshot, sizeof(emptySnapshot)) != 0)
        return Fail("zero-shape aliased projection was not an allocation-free no-op");
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
 * 身体形状maskはradiusと独立したcache keyで、選択した元indexを保つ。
 */
bool TestStepShapeCacheColliderMaskSelection()
{
    const FModelSecondaryMotionCollisionShape shapes[3] = { Shape({ 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.1f), Shape({ 2.0f, 0.0f, 0.0f }, { 2.0f, 1.0f, 0.0f }, 0.2f), Shape({ 3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, 0.3f) };
    FModelSecondaryMotionStepShapeCache cache(shapes, 3);
    FModelSecondaryMotionStepShapes allView{};
    FModelSecondaryMotionStepShapes firstMaskView{};
    FModelSecondaryMotionStepShapes firstAndThirdView{};
    FModelSecondaryMotionStepShapes noShapeView{};
    gk::String error;
    ResetSecondaryMotionCollisionExpansionCount();
    ResetSecondaryMotionCollisionPrepareCount();
    if (!cache.Get(0.05f, UINT64_MAX, allView, error) || !error.Empty() || allView.count != 3 || GetSecondaryMotionCollisionExpansionCount() != 1 || GetSecondaryMotionCollisionPrepareCount() != 3)
        return Fail("all-body mask did not preserve the legacy cache group and preparation budget");
    if (!cache.Get(0.05f, 1u, firstMaskView, error) || !error.Empty() || firstMaskView.count != 1 || firstMaskView.expanded[0].start.x != 1.0f || GetSecondaryMotionCollisionExpansionCount() != 2 || GetSecondaryMotionCollisionPrepareCount() != 4)
        return Fail("single-body mask did not select the first original shape");
    if (!cache.Get(0.05f, 5u, firstAndThirdView, error) || !error.Empty() || firstAndThirdView.count != 2 || firstAndThirdView.expanded[0].start.x != 1.0f || firstAndThirdView.expanded[1].start.x != 3.0f || GetSecondaryMotionCollisionExpansionCount() != 3 || GetSecondaryMotionCollisionPrepareCount() != 6)
        return Fail("multi-body mask did not preserve selected shape order");
    if (!cache.Get(0.05f, 1u, firstMaskView, error) || !error.Empty())
        return Fail("could not reacquire the single-body cache view before checking unused bits");
    const FModelSecondaryMotionStepShapes cachedMaskView = firstMaskView;
    if (!cache.Get(0.05f, (UINT64_C(1) << 63) | 1u, firstMaskView, error) || !error.Empty() || firstMaskView.expanded != cachedMaskView.expanded || firstMaskView.prepared != cachedMaskView.prepared || firstMaskView.count != cachedMaskView.count || GetSecondaryMotionCollisionExpansionCount() != 3 || GetSecondaryMotionCollisionPrepareCount() != 6)
        return Fail("unused mask bits changed the effective cache key or selected shapes");
    if (!cache.Get(0.05f, 0u, noShapeView, error) || !error.Empty() || noShapeView.count != 0 || noShapeView.expanded || noShapeView.guarded || noShapeView.prepared || GetSecondaryMotionCollisionExpansionCount() != 3 || GetSecondaryMotionCollisionPrepareCount() != 6)
        return Fail("zero body mask did not return an empty group without preparation");

    FModelSecondaryMotionCollisionShape indexedShapes[64]{};
    for (uint32_t shapeIndex = 0; shapeIndex < 64; ++shapeIndex)
        indexedShapes[shapeIndex] = Shape({ static_cast<float>(shapeIndex + 1u), 0.0f, 0.0f }, { static_cast<float>(shapeIndex + 1u), 0.0f, 0.0f }, 0.1f);
    FModelSecondaryMotionStepShapeCache indexedCache(indexedShapes, 64);
    FModelSecondaryMotionStepShapes index63View{};
    ResetSecondaryMotionCollisionExpansionCount();
    ResetSecondaryMotionCollisionPrepareCount();
    if (!indexedCache.Get(0.0f, UINT64_C(1) << 63, index63View, error) || !error.Empty() || index63View.count != 1 || index63View.expanded[0].start.x != 64.0f || GetSecondaryMotionCollisionExpansionCount() != 0 || GetSecondaryMotionCollisionPrepareCount() != 1)
        return Fail("mask bit 63 did not select exactly body shape index 63");
    FModelSecondaryMotionStepShapes index0And63View{};
    if (!indexedCache.Get(0.0f, (UINT64_C(1) << 63) | 1u, index0And63View, error) || !error.Empty() || index0And63View.count != 2 || index0And63View.expanded[0].start.x != 1.0f || index0And63View.expanded[1].start.x != 64.0f || GetSecondaryMotionCollisionExpansionCount() != 0 || GetSecondaryMotionCollisionPrepareCount() != 3)
        return Fail("bits 0 and 63 did not select the expected shape indices in source order");
    FModelSecondaryMotionStepShapes preservedView = firstAndThirdView;
    if (cache.Get(std::numeric_limits<float>::quiet_NaN(), 5u, firstAndThirdView, error) || error.Empty() || firstAndThirdView.expanded != preservedView.expanded || firstAndThirdView.prepared != preservedView.prepared || firstAndThirdView.count != preservedView.count)
        return Fail("invalid radius changed a previously selected mask view");
    const FModelSecondaryMotionCollisionShape invalidShape = Shape({}, {}, 0.0f);
    FModelSecondaryMotionStepShapeCache invalidSourceCache(&invalidShape, 1);
    if (invalidSourceCache.Get(0.05f, 1u, firstAndThirdView, error) || error.Empty() || firstAndThirdView.expanded != preservedView.expanded || firstAndThirdView.prepared != preservedView.prepared || firstAndThirdView.count != preservedView.count)
        return Fail("invalid selected body shape changed an existing mask view");
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
 * ゼロ時間restoreでscratch容量を鎖間再利用し、旧経路と状態をbyte単位で一致させる。
 */
bool TestPreparedSolverWorkspaceReuseAndAtomicity()
{
    const FModelSecondaryMotionCollisionShape farBody = Shape({ 100.0f, -10.0f, 0.0f }, { 100.0f, 10.0f, 0.0f }, 0.25f);
    FModelSecondaryMotionStepShapeCache cache(&farBody, 1);
    FModelSecondaryMotionStepShapes view{};
    gk::String error;
    if (!cache.Get(0.0f, view, error) || view.count != 1)
        return Fail("could not prepare the solver workspace test body");
    const gk::FModelSecondaryMotionSettings settings = MakeQuietSettings();
    const gk::Vec3 targetsThreeA[3] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } };
    const gk::Vec3 targetsFive[5] = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { 2.0f, 1.0f, 0.0f }, { 3.0f, 1.0f, 0.0f }, { 4.0f, 1.0f, 0.0f } };
    const gk::Vec3 targetsThreeB[3] = { { 10.0f, -2.0f, 1.0f }, { 11.0f, -2.0f, 1.0f }, { 12.0f, -2.0f, 1.0f } };
    const auto SameStateBytes = [](const FModelSecondaryMotionChainState& first, const FModelSecondaryMotionChainState& second) { return first.points.Count() == second.points.Count() && first.previousTargets.Count() == second.previousTargets.Count() && (!first.points.Count() || memcmp(first.points.Data(), second.points.Data(), static_cast<size_t>(first.points.Count()) * sizeof(FModelSecondaryMotionPoint)) == 0) && (!first.previousTargets.Count() || memcmp(first.previousTargets.Data(), second.previousTargets.Data(), static_cast<size_t>(first.previousTargets.Count()) * sizeof(gk::Vec3)) == 0); };
    const auto Initialize = [&](const gk::Vec3* targets, uint32_t count, FModelSecondaryMotionChainState& state)
    {
        if (!ResetSecondaryMotionChain(targets, count, state, error))
            return false;
        for (uint32_t point = 1; point < count; ++point)
        {
            auto& velocity = state.points.At(point).velocity;
            velocity.x = point == 1u ? -0.0f : -0.125f * point;
            velocity.y = 1.25f * point;
            velocity.z = point == 1u ? -0.0f : 0.375f;
        }
        return true;
    };
    const auto CompareRestoreToRaw = [&](const gk::Vec3* targets, uint32_t count, FModelSecondaryMotionSolverWorkspace& workspace)
    {
        FModelSecondaryMotionChainState workspaceState;
        FModelSecondaryMotionChainState rawState;
        if (!Initialize(targets, count, workspaceState) || !CopyState(workspaceState, rawState))
            return false;
        workspace.Clear();
        if (!RestoreSecondaryMotionChainWithPreparedCollisions(targets, count, settings, view, workspace, workspaceState, error) || !StepSecondaryMotionChainWithCollisions(targets, count, settings, 0.0, &farBody, &farBody, 1, rawState, error))
            return false;
        if (!SameStateBytes(workspaceState, rawState))
            return false;
        const float negativeZero = -0.0f;
        return memcmp(&workspaceState.points.At(1).velocity.x, &negativeZero, sizeof(float)) == 0 && workspaceState.points.At(1).velocity.y == 1.25f && memcmp(&workspaceState.points.At(1).velocity.z, &negativeZero, sizeof(float)) == 0;
    };

    FModelSecondaryMotionSolverWorkspace sharedWorkspace;
    if (!CompareRestoreToRaw(targetsThreeA, 3, sharedWorkspace) || !CompareRestoreToRaw(targetsFive, 5, sharedWorkspace) || !CompareRestoreToRaw(targetsThreeB, 3, sharedWorkspace))
        return Fail("cold-to-warm workspace reuse at counts 3, 5, 3 changed bytes or velocity values");

    const FModelSecondaryMotionCollisionShape contactSphere = Shape({ 0.5f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, 0.27f);
    FModelSecondaryMotionStepShapeCache contactCache(&contactSphere, 1);
    FModelSecondaryMotionStepShapes contactView{};
    const gk::Vec3 contactTargets[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    gk::FModelSecondaryMotionSettings feasibleContactSettings = MakeQuietSettings();
    if (!contactCache.Get(0.0f, contactView, error) || !contactView.count)
        return Fail("could not prepare the workspace collision-retry sphere");
    FModelSecondaryMotionChainState preparedContactState;
    FModelSecondaryMotionChainState rawContactState;
    if (!ResetSecondaryMotionChain(contactTargets, 2, preparedContactState, error) || !ResetSecondaryMotionChain(contactTargets, 2, rawContactState, error) || !RestoreSecondaryMotionChainWithPreparedCollisions(contactTargets, 2, feasibleContactSettings, contactView, sharedWorkspace, preparedContactState, error) || !StepSecondaryMotionChainWithCollisions(contactTargets, 2, feasibleContactSettings, 0.0, &contactSphere, &contactSphere, 1, rawContactState, error))
        return Fail("workspace restore and raw zero-delta solver did not resolve a feasible sphere contact");
    if (!SameStateBytes(preparedContactState, rawContactState))
        return Fail("workspace sphere-contact result differed bytewise from the raw zero-delta result");
    const gk::FVector3d contactFirst = ToDouble(preparedContactState.points.At(0).position);
    const gk::FVector3d contactSecond = ToDouble(preparedContactState.points.At(1).position);
    if (!IsFreeSegmentClear(&contactSphere, 1, 0.0f, contactFirst, contactSecond))
        return Fail("workspace sphere-contact result left the free span penetrating");

    FModelSecondaryMotionChainState infeasibleContactState;
    FModelSecondaryMotionChainState infeasibleContactSnapshot;
    gk::FModelSecondaryMotionSettings infeasibleContactSettings = feasibleContactSettings;
    infeasibleContactSettings.maxAngleDegrees = 5.0f;
    if (!ResetSecondaryMotionChain(contactTargets, 2, infeasibleContactState, error) || !CopyState(infeasibleContactState, infeasibleContactSnapshot))
        return Fail("could not snapshot the infeasible workspace contact state");
    if (RestoreSecondaryMotionChainWithPreparedCollisions(contactTargets, 2, infeasibleContactSettings, contactView, sharedWorkspace, infeasibleContactState, error) || error.Empty() || !SameStateBytes(infeasibleContactState, infeasibleContactSnapshot))
        return Fail("infeasible workspace sphere contact changed state or lacked a diagnostic");
    FModelSecondaryMotionChainState retriedContactState;
    if (!CopyState(infeasibleContactSnapshot, retriedContactState) || !RestoreSecondaryMotionChainWithPreparedCollisions(contactTargets, 2, feasibleContactSettings, contactView, sharedWorkspace, retriedContactState, error) || !SameStateBytes(retriedContactState, rawContactState) || !IsFreeSegmentClear(&contactSphere, 1, 0.0f, ToDouble(retriedContactState.points.At(0).position), ToDouble(retriedContactState.points.At(1).position)))
        return Fail("the same workspace could not retry with feasible sphere-contact settings");

    FModelSecondaryMotionChainState initial;
    if (!Initialize(targetsFive, 5, initial))
        return Fail("could not initialize workspace allocation sweep state");
    initial.points.At(1).position.y += 0.3f;
    FModelSecondaryMotionChainState rawExpected;
    if (!CopyState(initial, rawExpected) || !StepSecondaryMotionChainWithCollisions(targetsFive, 5, settings, 0.0, &farBody, &farBody, 1, rawExpected, error))
        return Fail("could not create the raw reference result for workspace allocation sweeps");
    const auto RetryAndCompare = [&](FModelSecondaryMotionSolverWorkspace& workspace, FModelSecondaryMotionChainState& state) { return RestoreSecondaryMotionChainWithPreparedCollisions(targetsFive, 5, settings, view, workspace, state, error) && SameStateBytes(state, rawExpected); };

    bool coldObservedFailure = false;
    bool coldReachedSuccess = false;
    for (uint32_t allowedAllocations = 0; allowedAllocations <= 12; ++allowedAllocations)
    {
        FModelSecondaryMotionSolverWorkspace coldWorkspace;
        FModelSecondaryMotionChainState candidate;
        FModelSecondaryMotionChainState snapshot;
        if (!CopyState(initial, candidate) || !CopyState(candidate, snapshot) || !error.Assign("preallocated diagnostic for cold solver workspace failure injection"))
            return Fail("could not initialize a cold workspace failure attempt");
        gk::SetAllocationFailureAfterForTesting(allowedAllocations);
        const bool restored = RestoreSecondaryMotionChainWithPreparedCollisions(targetsFive, 5, settings, view, coldWorkspace, candidate, error);
        gk::ResetAllocationFailureForTesting();
        if (restored)
        {
            if (!error.Empty() || !SameStateBytes(candidate, rawExpected))
                return Fail("cold workspace success changed the raw result or retained an error");
            coldReachedSuccess = true;
            break;
        }
        coldObservedFailure = true;
        if (error.Empty() || !SameStateBytes(candidate, snapshot))
            return Fail("cold workspace allocation failure changed chain state or lost its diagnostic");
        if (!RetryAndCompare(coldWorkspace, candidate))
            return Fail("cold workspace could not retry after an allocation failure");
    }
    if (!coldObservedFailure || !coldReachedSuccess)
        return Fail("cold workspace allocation sweep did not cover failure and success");

    FModelSecondaryMotionSolverWorkspace warmWorkspace;
    FModelSecondaryMotionChainState warmed;
    if (!CopyState(initial, warmed) || !RetryAndCompare(warmWorkspace, warmed))
        return Fail("could not warm solver workspace before bounded allocation checks");
    for (uint32_t allowedAllocations = 0; allowedAllocations <= 2; ++allowedAllocations)
    {
        FModelSecondaryMotionChainState candidate;
        FModelSecondaryMotionChainState snapshot;
        if (!CopyState(initial, candidate) || !CopyState(candidate, snapshot))
            return Fail("could not copy the warm workspace failure state");
        error.Assign("preallocated diagnostic for warm solver workspace failure injection");
        gk::SetAllocationFailureAfterForTesting(allowedAllocations);
        const bool restored = RestoreSecondaryMotionChainWithPreparedCollisions(targetsFive, 5, settings, view, warmWorkspace, candidate, error);
        gk::ResetAllocationFailureForTesting();
        if (allowedAllocations < 2)
        {
            if (restored || error.Empty() || !SameStateBytes(candidate, snapshot))
                return Fail("warm workspace failure sweep did not preserve state below the two-allocation budget");
            if (!RetryAndCompare(warmWorkspace, candidate))
                return Fail("warm workspace could not retry after an injected allocation failure");
        }
        else if (!restored || !error.Empty() || !SameStateBytes(candidate, rawExpected))
        {
            return Fail("warm workspace did not finish with the candidate-state two-allocation budget");
        }
    }

    FModelSecondaryMotionChainState invalidTargetState;
    FModelSecondaryMotionChainState invalidTargetSnapshot;
    if (!CopyState(initial, invalidTargetState) || !CopyState(invalidTargetState, invalidTargetSnapshot))
        return Fail("could not snapshot invalid-target workspace case");
    gk::Vec3 invalidTargets[5];
    memcpy(invalidTargets, targetsFive, sizeof(invalidTargets));
    invalidTargets[2].y = std::numeric_limits<float>::quiet_NaN();
    if (RestoreSecondaryMotionChainWithPreparedCollisions(invalidTargets, 5, settings, view, warmWorkspace, invalidTargetState, error) || error.Empty() || !SameStateBytes(invalidTargetState, invalidTargetSnapshot) || !RetryAndCompare(warmWorkspace, invalidTargetState))
        return Fail("invalid targets changed state or prevented a valid workspace retry");

    FModelSecondaryMotionChainState invalidVelocityState;
    FModelSecondaryMotionChainState invalidVelocitySnapshot;
    if (!CopyState(initial, invalidVelocityState) || !CopyState(invalidVelocityState, invalidVelocitySnapshot))
        return Fail("could not snapshot invalid-velocity workspace case");
    invalidVelocityState.points.At(2).velocity.z = std::numeric_limits<float>::quiet_NaN();
    invalidVelocitySnapshot.points.At(2).velocity.z = std::numeric_limits<float>::quiet_NaN();
    if (RestoreSecondaryMotionChainWithPreparedCollisions(targetsFive, 5, settings, view, warmWorkspace, invalidVelocityState, error) || error.Empty() || !SameStateBytes(invalidVelocityState, invalidVelocitySnapshot))
        return Fail("non-finite velocity was accepted or changed the old state");
    invalidVelocityState.points.At(2).velocity.z = 0.375f;
    if (!RetryAndCompare(warmWorkspace, invalidVelocityState))
        return Fail("valid retry after repairing non-finite velocity failed");

    FModelSecondaryMotionChainState invalidViewState;
    FModelSecondaryMotionChainState invalidViewSnapshot;
    if (!CopyState(initial, invalidViewState) || !CopyState(invalidViewState, invalidViewSnapshot))
        return Fail("could not snapshot invalid-view workspace case");
    FModelSecondaryMotionStepShapes invalidView = view;
    invalidView.guarded = nullptr;
    if (RestoreSecondaryMotionChainWithPreparedCollisions(targetsFive, 5, settings, invalidView, warmWorkspace, invalidViewState, error) || error.Empty() || !SameStateBytes(invalidViewState, invalidViewSnapshot) || !RetryAndCompare(warmWorkspace, invalidViewState))
        return Fail("invalid prepared view changed state or prevented a valid workspace retry");
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
 * 剛体・柔軟な距離投影と一致点の決定的方向を検査する。
 */
bool TestDistanceProjectionRigidSoftAndCoincident()
{
    const gk::FVector3d referenceFirst = Point(0.0, 0.0, 0.0);
    const gk::FVector3d referenceSecond = Point(2.0, 0.0, 0.0);
    gk::FVector3d first = referenceFirst;
    gk::FVector3d second = referenceSecond;
    double lambda = 0.0;
    gk::String error;
    if (!ProjectSecondaryMotionDistance(referenceFirst, referenceSecond, 1.0, 0.0, 0.1, lambda, first, second, error) || !error.Empty())
        return Fail("rigid distance projection failed");
    if (fabs(Distance(first, second) - 1.0) > 1.0e-12 || fabs((first.value[0] + second.value[0]) * 0.5 - 1.0) > 1.0e-12 || fabs(first.value[1]) > 1.0e-12 || fabs(second.value[1]) > 1.0e-12)
        return Fail("rigid distance projection changed its center or missed the rest length");
    if (!CheckSecondaryMotionDistance(1.0, 0.0, 0.1, lambda, first, second, error) || !error.Empty())
        return Fail("rigid distance residual did not pass the independent check");

    const gk::FVector3d rigidFirstSnapshot = first;
    const gk::FVector3d rigidSecondSnapshot = second;
    const double rigidLambdaSnapshot = lambda;
    if (!ProjectSecondaryMotionDistance(referenceFirst, referenceSecond, 1.0, 0.0, 0.1, lambda, first, second, error) || memcmp(&first, &rigidFirstSnapshot, sizeof(first)) != 0 || memcmp(&second, &rigidSecondSnapshot, sizeof(second)) != 0 || memcmp(&lambda, &rigidLambdaSnapshot, sizeof(lambda)) != 0)
        return Fail("reapplying a satisfied rigid distance added another correction");

    first = Point(0.4, 0.0, 0.0);
    second = Point(1.6, 0.0, 0.0);
    lambda = 0.0;
    if (!ProjectSecondaryMotionDistance(referenceFirst, referenceSecond, 1.5, 0.0, 0.1, lambda, first, second, error) || fabs(Distance(first, second) - 1.5) > 1.0e-12 || fabs((first.value[0] + second.value[0]) * 0.5 - 1.0) > 1.0e-12)
        return Fail("rigid distance projection did not correct compression symmetrically");

    first = referenceFirst;
    second = referenceSecond;
    lambda = 0.0;
    const double compliance = 0.02;
    const double stepSeconds = 0.1;
    const double alpha = compliance / (stepSeconds * stepSeconds);
    if (!ProjectSecondaryMotionDistance(referenceFirst, referenceSecond, 1.0, compliance, stepSeconds, lambda, first, second, error) || !error.Empty())
        return Fail("compliant distance projection failed");
    const double residual = Distance(first, second) - 1.0;
    if (fabs(residual - alpha / (2.0 + alpha)) > 1.0e-12 || fabs(lambda - 1.0 / (2.0 + alpha)) > 1.0e-12 || fabs((first.value[0] + second.value[0]) * 0.5 - 1.0) > 1.0e-12 || !CheckSecondaryMotionDistance(1.0, compliance, stepSeconds, lambda, first, second, error) || !error.Empty())
        return Fail("compliant distance did not match its one-step XPBD analytical residual");
    const gk::FVector3d softFirstSnapshot = first;
    const gk::FVector3d softSecondSnapshot = second;
    const double softLambdaSnapshot = lambda;
    if (!ProjectSecondaryMotionDistance(referenceFirst, referenceSecond, 1.0, compliance, stepSeconds, lambda, first, second, error) || memcmp(&first, &softFirstSnapshot, sizeof(first)) != 0 || memcmp(&second, &softSecondSnapshot, sizeof(second)) != 0 || memcmp(&lambda, &softLambdaSnapshot, sizeof(lambda)) != 0)
        return Fail("reapplying a satisfied compliant distance added another correction");

    const gk::FVector3d coincident = Point(4.0, -2.0, 3.0);
    first = coincident;
    second = coincident;
    lambda = 0.0;
    if (!ProjectSecondaryMotionDistance(coincident, coincident, 1.0, 0.0, 0.1, lambda, first, second, error) || !error.Empty())
        return Fail("coincident distance fallback failed");
    const gk::FVector3d firstResult = first;
    const gk::FVector3d secondResult = second;
    const double firstLambda = lambda;
    first = coincident;
    second = coincident;
    lambda = 0.0;
    if (!ProjectSecondaryMotionDistance(coincident, coincident, 1.0, 0.0, 0.1, lambda, first, second, error) || memcmp(&first, &firstResult, sizeof(first)) != 0 || memcmp(&second, &secondResult, sizeof(second)) != 0 || memcmp(&lambda, &firstLambda, sizeof(lambda)) != 0 || fabs(Distance(first, second) - 1.0) > 1.0e-12 || fabs((first.value[0] + second.value[0]) * 0.5 - coincident.value[0]) > 1.0e-12 || fabs((first.value[1] + second.value[1]) * 0.5 - coincident.value[1]) > 1.0e-12 || fabs((first.value[2] + second.value[2]) * 0.5 - coincident.value[2]) > 1.0e-12)
        return Fail("coincident reference fallback was not deterministic or did not preserve center");
    return true;
}

/**
 * 距離投影の不正値、alias、overflowで点とlambdaを保つ。
 */
bool TestDistanceProjectionValidationAndAtomicity()
{
    const gk::FVector3d referenceFirst = Point(0.0, 0.0, 0.0);
    const gk::FVector3d referenceSecond = Point(2.0, 0.0, 0.0);
    const gk::FVector3d originalFirst = Point(0.0, 1.0, 0.0);
    const gk::FVector3d originalSecond = Point(2.0, 1.0, 0.0);
    const double originalLambda = 0.125;
    gk::String error;
    const auto RejectAtomic = [&](const gk::FVector3d& refFirst, const gk::FVector3d& refSecond, double restDistance, double compliance, double stepSeconds, double lambdaInput, gk::FVector3d firstInput, gk::FVector3d secondInput)
    {
        const gk::FVector3d beforeFirst = firstInput;
        const gk::FVector3d beforeSecond = secondInput;
        double candidateLambda = lambdaInput;
        const bool rejected = !ProjectSecondaryMotionDistance(refFirst, refSecond, restDistance, compliance, stepSeconds, candidateLambda, firstInput, secondInput, error) && !error.Empty() && memcmp(&firstInput, &beforeFirst, sizeof(beforeFirst)) == 0 && memcmp(&secondInput, &beforeSecond, sizeof(beforeSecond)) == 0 && memcmp(&candidateLambda, &lambdaInput, sizeof(lambdaInput)) == 0;
        error.Clear();
        return rejected;
    };
    if (!RejectAtomic(referenceFirst, referenceSecond, -1.0, 0.0, 0.1, originalLambda, originalFirst, originalSecond) || !RejectAtomic(referenceFirst, referenceSecond, 1.0, -0.1, 0.1, originalLambda, originalFirst, originalSecond) || !RejectAtomic(referenceFirst, referenceSecond, 1.0, 0.0, 0.0, originalLambda, originalFirst, originalSecond) || !RejectAtomic(referenceFirst, referenceSecond, 1.0, 0.0, 0.1, std::numeric_limits<double>::quiet_NaN(), originalFirst, originalSecond))
        return Fail("invalid distance parameters were accepted or changed output values");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const gk::FVector3d invalidReference = Point(nan, 0.0, 0.0);
    const gk::FVector3d invalidPosition = Point(0.0, std::numeric_limits<double>::infinity(), 0.0);
    if (!RejectAtomic(invalidReference, referenceSecond, 1.0, 0.0, 0.1, originalLambda, originalFirst, originalSecond) || !RejectAtomic(referenceFirst, referenceSecond, 1.0, 0.0, 0.1, originalLambda, invalidPosition, originalSecond))
        return Fail("non-finite distance inputs were accepted or changed output values");

    gk::FVector3d aliased = originalFirst;
    double aliasLambda = originalLambda;
    if (ProjectSecondaryMotionDistance(referenceFirst, referenceSecond, 1.0, 0.0, 0.1, aliasLambda, aliased, aliased, error) || error.Empty() || memcmp(&aliased, &originalFirst, sizeof(aliased)) != 0 || memcmp(&aliasLambda, &originalLambda, sizeof(aliasLambda)) != 0)
        return Fail("aliased distance outputs were accepted or changed output values");

    const double maximum = std::numeric_limits<double>::max();
    const gk::FVector3d extremeFirst = Point(-maximum * 0.75, 0.0, 0.0);
    const gk::FVector3d extremeSecond = Point(maximum * 0.75, 0.0, 0.0);
    if (!RejectAtomic(referenceFirst, referenceSecond, 1.0, 0.0, 0.1, originalLambda, extremeFirst, extremeSecond))
        return Fail("overflowing distance computation was accepted or changed output values");
    if (CheckSecondaryMotionDistance(1.0, 0.0, 0.1, originalLambda, originalFirst, originalSecond, error, -1.0) || error.Empty())
        return Fail("negative distance-check tolerance was accepted");
    error.Clear();
    if (CheckSecondaryMotionDistance(1.0, 0.0, 0.1, originalLambda, originalFirst, originalSecond, error, std::numeric_limits<double>::quiet_NaN()) || error.Empty())
        return Fail("non-finite distance-check tolerance was accepted");
    error.Clear();
    if (CheckSecondaryMotionDistance(1.0, 0.0, 0.1, originalLambda, originalFirst, originalSecond, error, 1.01) || error.Empty())
        return Fail("distance-check tolerance greater than one was accepted");
    return true;
}

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
    bool (*tests[])(void) = { TestSphereAndCapsulePointProjection, TestInsideCenterAndAxisFallback, TestSpanningSegmentContact, TestContactToleranceFraction, TestLongCapsuleFixedLengthAvoidance, TestCollisionSegmentBoundsFastPath, TestPreparedSegmentAvoidanceMatchesRaw, TestShapePreparationBudget, TestSolverPreparedShapeBudget, TestFarColliderContactCheckBudget, TestRootInsideFirstSpanException, TestRootInsideFirstSpanSolverEndpointOnly, TestGuardedRootInsideUsesOriginalShapeRule, TestInterpolatedRadiusRootOverrideAndSegmentRole, TestMultipleContacts, TestInvalidShapesAndAtomicProjection, TestNoColliderMatchesLegacyStep, TestCollisionShapeExpansionReuseBudget, TestReferenceCalculationBudgetAndStateInvariance, TestReferenceCalculationSkippedOnInvalidCollisionInput, TestFeasibleContactWithLengthAndAngle, TestSolverPointRadiusKeepsBoneSphereClear, TestShortCapsuleLengthAndAngleFeasibility, TestInfeasibleContactPreservesState, TestMovingColliderUsesCurrentGeometry, TestNearFixedRootMovingCapsuleDoesNotInjectVelocity, TestHumanoidContactDiagnosticFixture, TestRecordedSkinningChainContact, TestRecordedSkinningChainFrame98Contact, TestRecordedSkinningChainFrame33Contact, TestRecordedSkinningChainFrame433Contact, TestPreparedWholeChainContactsForFrame33, TestFrame98DirectionToleranceFraction, TestPreparedJointDirectionForRecordedSkinningChain, TestJointDirectionNormalizationAndConeBoundaries, TestCollisionShapeValidationIsAtomic, TestEmptyStateInitializationResolvesContact, TestZeroDeltaPoseChangeResolvesCurrentContact, TestHitchAndTeleportInfeasibleContactsAreAtomic, TestFloatRoundingDoesNotHideSmallColliderPenetration, TestExpandCollisionShapesForPointRadius, TestFloatRoundingPreservesLengthAndAngle, TestFreeSegmentSphereAndCapsuleContacts, TestFreeSegmentMultipleShapesAndDegenerateCases, TestFreeSegmentValidationAndAtomicity, TestPreparedFreeSegmentShapeReuseAndAtomicity, TestPreparedFreeSegmentToleranceBoundaryRoundTrip, TestPreparedFreeSegmentPenetrationMeasurement, TestAnchoredPreparedFreeSegmentProjection, TestPreparedPointContactsReuseAndRootException, TestPreparedPointContactsCallerScratchReuseAndAtomicity, TestPreparedPointContactsValidationAndAtomicity, TestStepShapeCacheReuseAndAtomicity, TestStepShapeCacheColliderMaskSelection, TestStepShapeCacheAllocationFailureAtomicity, TestPreparedCollisionStepMatchesRawAndRejectsInvalidViews, TestPreparedSolverWorkspaceReuseAndAtomicity, TestPreparedProjectionFailureSkipsReferenceWork, TestDistanceProjectionRigidSoftAndCoincident, TestDistanceProjectionValidationAndAtomicity };
    const char* names[] = { "sphere and capsule projection", "inside fallback", "spanning segment", "contact tolerance fraction", "long-capsule fixed-length avoidance", "collision bounds fast path", "prepared segment avoidance", "shape preparation budget", "solver prepared-shape budget", "far-collider contact-check budget", "root-inside exception", "root-inside solver endpoint-only", "guarded root-inside uses original shape", "interpolated-radius root override and segment role", "multiple contacts", "invalid input atomicity", "legacy equivalence", "shape expansion reuse budget", "reference calculation budget", "reference calculation validation order", "feasible contact", "bone sphere contact", "short capsule feasibility", "infeasible atomicity", "moving collider", "near-root moving-capsule no impulse", "humanoid contact fixture", "recorded skinning chain contact", "recorded skinning chain frame 98 contact", "recorded skinning chain frame 33 contact", "recorded skinning chain frame 433 contact", "prepared whole-chain contacts frame 33", "frame 98 direction tolerance fraction", "prepared joint direction recorded skinning chain", "joint direction normalization and cone boundaries", "collision shape validation", "empty state initialization", "zero-delta pose change", "hitch and teleport atomicity", "float contact rounding", "point-radius collider expansion", "float constraint rounding", "free segment sphere and capsule", "free segment multiple shapes and degenerate cases", "free segment validation and atomicity", "prepared free-segment reuse and atomicity", "prepared free-segment FK tolerance boundary", "prepared free-segment penetration measurement", "anchored prepared free-segment projection", "prepared point-contact reuse and root exception", "prepared point-contact caller scratch", "prepared point-contact validation and atomicity", "step-shape cache reuse and atomicity", "step-shape cache collider mask selection", "step-shape cache allocation atomicity", "prepared collision step raw equivalence", "prepared solver workspace reuse and atomicity", "prepared projection failure skips reference work", "distance projection rigid soft and coincident", "distance projection validation and atomicity" };
    static_assert(sizeof(tests) / sizeof(tests[0]) == sizeof(names) / sizeof(names[0]), "test names must match test functions");
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
