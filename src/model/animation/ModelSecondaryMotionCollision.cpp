// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionCollision.h"

#include "foundation/Array.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{

// 受け付ける接触形状の最大数。
const uint32_t kMaximumCollisionShapes = 64;
// 受け付ける鎖の最大点数。
const uint32_t kMaximumPointCount = 1025;
// 複数接触を交互に押し戻す最大回数。
const uint32_t kProjectionPassCount = 64;

/**
 * 1回の接触検査で再利用する形状軸と範囲。
 */
struct FPreparedCollisionShape
{
    gk::FVector3d start;      // 形状軸の始点。
    gk::FVector3d end;        // 形状軸の終点。
    gk::FVector3d axis;       // 始点から終点へ向かう軸。
    double axisLengthSquared; // 最近点計算に使う軸長の二乗。
    double radius;            // 接触判定に使う半径。
    double tolerance;         // 接触判定に使う許容幅。
    double boundsLow[3];      // 半径分広げたAABBの下限。
    double boundsHigh[3];     // 半径分広げたAABBの上限。
};

#if defined(GKCORE_TESTING)
// 接触形状軸への最近点計算回数。
uint64_t secondaryMotionCollisionAxisQueryCount = 0;
// 接触全体の検査呼び出し回数。
uint64_t secondaryMotionContactCheckCount = 0;
// 接触形状の準備回数。
uint64_t secondaryMotionCollisionPrepareCount = 0;
#endif

/**
 * 倍精度座標を形状のfloat位置から作る。
 */
gk::FVector3d ToDouble(const gk::Vec3& value)
{
    return { { value.x, value.y, value.z } };
}

/**
 * 座標の和を計算する。
 */
gk::FVector3d Add(const gk::FVector3d& left, const gk::FVector3d& right)
{
    return { { left.value[0] + right.value[0], left.value[1] + right.value[1], left.value[2] + right.value[2] } };
}

/**
 * 座標の差を計算する。
 */
gk::FVector3d Subtract(const gk::FVector3d& left, const gk::FVector3d& right)
{
    return { { left.value[0] - right.value[0], left.value[1] - right.value[1], left.value[2] - right.value[2] } };
}

/**
 * 座標を有限な係数で拡大する。
 */
gk::FVector3d Multiply(const gk::FVector3d& value, double scale)
{
    return { { value.value[0] * scale, value.value[1] * scale, value.value[2] * scale } };
}

/**
 * 3次元内積を計算する。
 */
double Dot(const gk::FVector3d& left, const gk::FVector3d& right)
{
    return left.value[0] * right.value[0] + left.value[1] * right.value[1] + left.value[2] * right.value[2];
}

/**
 * 3次元外積を計算する。
 */
gk::FVector3d Cross(const gk::FVector3d& left, const gk::FVector3d& right)
{
    return { { left.value[1] * right.value[2] - left.value[2] * right.value[1], left.value[2] * right.value[0] - left.value[0] * right.value[2], left.value[0] * right.value[1] - left.value[1] * right.value[0] } };
}

/**
 * 拡大縮小してoverflowを避けた長さを返す。
 */
double Length(const gk::FVector3d& value)
{
    // 成分の最大絶対値で割り、二乗overflowを避ける。
    const double largest = fmax(fabs(value.value[0]), fmax(fabs(value.value[1]), fabs(value.value[2])));
    if (!(largest > 0.0) || !isfinite(largest))
        return largest;
    // スケールを戻して得る正規化後の成分。
    const double x = value.value[0] / largest;
    const double y = value.value[1] / largest;
    const double z = value.value[2] / largest;
    return largest * sqrt(x * x + y * y + z * z);
}

/**
 * 有限な座標を単位長へ正規化する。
 */
bool Normalize(gk::FVector3d& value)
{
    // 正規化前の長さ。
    const double length = Length(value);
    if (!(length > 0.0) || !isfinite(length))
        return false;
    value = Multiply(value, 1.0 / length);
    return true;
}

/**
 * 入力方向と直交する決定的な単位方向を返す。
 */
gk::FVector3d DeterministicPerpendicular(const gk::FVector3d& direction)
{
    // 入力方向が有効なら、最も平行度の低い基底を使う。
    gk::FVector3d unitDirection = direction;
    if (!Normalize(unitDirection))
        return { { 1.0, 0.0, 0.0 } };
    const double x = fabs(unitDirection.value[0]);
    const double y = fabs(unitDirection.value[1]);
    const double z = fabs(unitDirection.value[2]);
    // 選んだ座標基底と外積を取るための単位値。
    gk::FVector3d basis = {};
    if (x <= y && x <= z)
        basis.value[0] = 1.0;
    else if (y <= z)
        basis.value[1] = 1.0;
    else
        basis.value[2] = 1.0;
    gk::FVector3d perpendicular = Cross(unitDirection, basis);
    if (!Normalize(perpendicular))
        return { { 1.0, 0.0, 0.0 } };
    return perpendicular;
}

/**
 * 座標成分が有限か調べる。
 */
bool IsFinite(const gk::FVector3d& value)
{
    return isfinite(value.value[0]) && isfinite(value.value[1]) && isfinite(value.value[2]);
}

/**
 * 半径に比例する接触許容誤差を返す。
 */
double ContactTolerance(double radius)
{
    // 半径比を基本にし、倍精度丸め誤差だけを下限にする。
    return fmax(radius * 1.0e-3, radius * DBL_EPSILON * 64.0);
}

/**
 * 検証済み形状の軸と判定値を1回だけ計算する。
 */
FPreparedCollisionShape PrepareCollisionShape(const FModelSecondaryMotionCollisionShape& shape)
{
    // この呼出し中に再利用する形状値。
    FPreparedCollisionShape prepared = {};
    prepared.start = ToDouble(shape.start);
    prepared.end = ToDouble(shape.end);
    prepared.axis = Subtract(prepared.end, prepared.start);
    prepared.axisLengthSquared = Dot(prepared.axis, prepared.axis);
    prepared.radius = shape.radius;
    prepared.tolerance = ContactTolerance(prepared.radius);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        // 半径を含む旧判定範囲を保存する。
        prepared.boundsLow[axis] = fmin(prepared.start.value[axis], prepared.end.value[axis]) - prepared.radius;
        prepared.boundsHigh[axis] = fmax(prepared.start.value[axis], prepared.end.value[axis]) + prepared.radius;
    }
#if defined(GKCORE_TESTING)
    ++secondaryMotionCollisionPrepareCount;
#endif
    return prepared;
}

/**
 * 点と準備済み軸の最近位置を求める。
 */
gk::FVector3d ClosestPointOnPreparedAxis(const gk::FVector3d& point, const FPreparedCollisionShape& shape)
{
#if defined(GKCORE_TESTING)
    ++secondaryMotionCollisionAxisQueryCount;
#endif
    // 点が形状軸上に作る割合。
    double amount = shape.axisLengthSquared > 0.0 ? Dot(Subtract(point, shape.start), shape.axis) / shape.axisLengthSquared : 0.0;
    amount = fmax(0.0, fmin(1.0, amount));
    return Add(shape.start, Multiply(shape.axis, amount));
}

/**
 * 線分が準備済み形状の膨張AABBから離れているか調べる。
 */
bool IsOutsideExpandedBounds(const gk::FVector3d& first, const gk::FVector3d& second, const FPreparedCollisionShape& shape)
{
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        // 線分と膨張形状の座標範囲。
        const double low = fmin(first.value[axis], second.value[axis]);
        const double high = fmax(first.value[axis], second.value[axis]);
        if (high < shape.boundsLow[axis] || low > shape.boundsHigh[axis])
            return true;
    }
    return false;
}

/**
 * AABBが半径分を含めても離れているか調べる。
 */
bool IsOutsideExpandedBounds(const gk::FVector3d& first, const gk::FVector3d& second, const FModelSecondaryMotionCollisionShape& shape)
{
    // 比較に使う形状軸の端点。
    const gk::FVector3d start = ToDouble(shape.start);
    const gk::FVector3d end = ToDouble(shape.end);
    // 旧判定と同じ膨張量。
    const double radius = shape.radius;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        // 線分と膨張形状の座標範囲。
        const double low = fmin(first.value[axis], second.value[axis]);
        const double high = fmax(first.value[axis], second.value[axis]);
        // 半径分を広げた形状範囲。
        const double shapeLow = fmin(start.value[axis], end.value[axis]) - radius;
        const double shapeHigh = fmax(start.value[axis], end.value[axis]) + radius;
        if (high < shapeLow || low > shapeHigh)
            return true;
    }
    return false;
}

/**
 * 丸めで狭まらないAABBを使い、線分が形状から確実に離れているか調べる。
 */
bool IsOutsideExpandedBoundsConservative(const gk::FVector3d& first, const gk::FVector3d& second, const FModelSecondaryMotionCollisionShape& shape)
{
    const double start[3] = { shape.start.x, shape.start.y, shape.start.z };
    const double end[3] = { shape.end.x, shape.end.y, shape.end.z };
    const double radius = shape.radius;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        const double segmentLow = fmin(first.value[axis], second.value[axis]);
        const double segmentHigh = fmax(first.value[axis], second.value[axis]);
        const double shapeLow = nextafter(fmin(start[axis], end[axis]) - radius, -HUGE_VAL);
        const double shapeHigh = nextafter(fmax(start[axis], end[axis]) + radius, HUGE_VAL);
        if (segmentHigh < shapeLow || segmentLow > shapeHigh)
        {
            return true;
        }
    }
    return false;
}

/**
 * pointとカプセル軸上の最近点を求める。
 */
gk::FVector3d ClosestPointOnAxis(const gk::FVector3d& point, const FModelSecondaryMotionCollisionShape& shape)
{
#if defined(GKCORE_TESTING)
    ++secondaryMotionCollisionAxisQueryCount;
    ++secondaryMotionCollisionPrepareCount;
#endif
    // 回避処理で使う形状軸の始点と方向。
    const gk::FVector3d start = ToDouble(shape.start);
    const gk::FVector3d axis = Subtract(ToDouble(shape.end), start);
    // 軸長の二乗と点の軸上割合。
    const double axisLengthSquared = Dot(axis, axis);
    double amount = axisLengthSquared > 0.0 ? Dot(Subtract(point, start), axis) / axisLengthSquared : 0.0;
    amount = fmax(0.0, fmin(1.0, amount));
    return Add(start, Multiply(axis, amount));
}

/**
 * 参照方向をカプセル軸に直交させて退避方向を選ぶ。
 */
gk::FVector3d FallbackNormal(const gk::FVector3d& point, const gk::FVector3d& axisPoint, const gk::FVector3d& axis, const gk::FVector3d& reference)
{
    // 接触軸から点へ向かう通常方向。
    gk::FVector3d normal = Subtract(point, axisPoint);
    if (Normalize(normal))
        return normal;
    // 方向が定まらない場合に使う目標鎖方向。
    normal = reference;
    // 接触軸の長さと単位方向。
    const double axisLength = Length(axis);
    if (axisLength > 0.0 && isfinite(axisLength))
    {
        // 参照方向を接触軸と直角な成分へ絞る。
        const gk::FVector3d unitAxis = Multiply(axis, 1.0 / axisLength);
        normal = Subtract(normal, Multiply(unitAxis, Dot(normal, unitAxis)));
        if (Normalize(normal))
            return normal;
        // 最小平行度の基底軸を選ぶ値。
        const double x = fabs(unitAxis.value[0]);
        const double y = fabs(unitAxis.value[1]);
        const double z = fabs(unitAxis.value[2]);
        // 接触軸と直交させる決定的な基底方向。
        gk::FVector3d basis = {};
        // 接触軸と最も直角に近い座標軸を選ぶ。
        if (x <= y && x <= z)
            basis.value[0] = 1.0;
        else if (y <= z)
            basis.value[1] = 1.0;
        else
            basis.value[2] = 1.0;
        normal = { { unitAxis.value[1] * basis.value[2] - unitAxis.value[2] * basis.value[1], unitAxis.value[2] * basis.value[0] - unitAxis.value[0] * basis.value[2], unitAxis.value[0] * basis.value[1] - unitAxis.value[1] * basis.value[0] } };
        if (Normalize(normal))
            return normal;
    }
    return { { 1.0, 0.0, 0.0 } };
}

/**
 * 線分同士の最近点と両方の補間率を求める。
 */
void ClosestSegmentPairWithAxis(const gk::FVector3d& firstStart, const gk::FVector3d& firstEnd, const gk::FVector3d& secondStart, const gk::FVector3d& secondAxis, double secondLength, double& firstAmount, double& secondAmount, gk::FVector3d& firstPoint, gk::FVector3d& secondPoint)
{
    // 最近点を求める2本の線分方向と始点差。
    const gk::FVector3d firstAxis = Subtract(firstEnd, firstStart);
    const gk::FVector3d offset = Subtract(firstStart, secondStart);
    // 軸長、相互内積、始点差の射影値。
    const double firstLength = Dot(firstAxis, firstAxis);
    const double mixed = Dot(firstAxis, secondAxis);
    const double firstOffset = Dot(firstAxis, offset);
    const double secondOffset = Dot(secondAxis, offset);
    // 2本の線分が平行かを含む連立式の分母。
    const double denominator = firstLength * secondLength - mixed * mixed;
    // 球軸または鎖線分がゼロ長の場合は、点と線分の最近点へ縮約する。
    if (!(secondLength > 0.0))
    {
        firstAmount = firstLength > 0.0 ? fmax(0.0, fmin(1.0, -firstOffset / firstLength)) : 0.0;
        secondAmount = 0.0;
    }
    else if (!(firstLength > 0.0))
    {
        firstAmount = 0.0;
        secondAmount = fmax(0.0, fmin(1.0, secondOffset / secondLength));
    }
    else
    {
        firstAmount = denominator > 0.0 ? fmax(0.0, fmin(1.0, (mixed * secondOffset - firstOffset * secondLength) / denominator)) : 0.0;
        secondAmount = (mixed * firstAmount + secondOffset) / secondLength;
    }
    // 形状線分の端を越えた割合を端点へ丸め、鎖側を解き直す。
    if (secondAmount < 0.0)
    {
        secondAmount = 0.0;
        firstAmount = firstLength > 0.0 ? fmax(0.0, fmin(1.0, -firstOffset / firstLength)) : 0.0;
    }
    else if (secondAmount > 1.0)
    {
        secondAmount = 1.0;
        firstAmount = firstLength > 0.0 ? fmax(0.0, fmin(1.0, (mixed - firstOffset) / firstLength)) : 0.0;
    }
    firstPoint = Add(firstStart, Multiply(firstAxis, firstAmount));
    secondPoint = Add(secondStart, Multiply(secondAxis, secondAmount));
}

/**
 * 2本の線分の軸データを計算して最近点を求める。
 */
void ClosestSegmentPair(const gk::FVector3d& firstStart, const gk::FVector3d& firstEnd, const gk::FVector3d& secondStart, const gk::FVector3d& secondEnd, double& firstAmount, double& secondAmount, gk::FVector3d& firstPoint, gk::FVector3d& secondPoint)
{
    // 呼出しごとにだけ使う形状線分の軸データ。
    const gk::FVector3d secondAxis = Subtract(secondEnd, secondStart);
    const double secondLength = Dot(secondAxis, secondAxis);
    ClosestSegmentPairWithAxis(firstStart, firstEnd, secondStart, secondAxis, secondLength, firstAmount, secondAmount, firstPoint, secondPoint);
}

/**
 * 検査対象の線分が接触形状へ侵入しているか調べる。
 */
bool SpanPenetrates(const gk::FVector3d& first, const gk::FVector3d& second, const FPreparedCollisionShape& shape, double toleranceFraction = 1.0)
{
    // 軸方向の範囲が離れている線分は最近点計算を省く。
    if (IsOutsideExpandedBounds(first, second, shape))
        return false;
    // 最近点の補間率と両線分上の最近位置。
    double spanAmount = 0.0;
    double axisAmount = 0.0;
    gk::FVector3d spanPoint{};
    gk::FVector3d axisPoint{};
    ClosestSegmentPairWithAxis(first, second, shape.start, shape.axis, shape.axisLengthSquared, spanAmount, axisAmount, spanPoint, axisPoint);
    (void)spanAmount;
    (void)axisAmount;
    return Length(Subtract(spanPoint, axisPoint)) < shape.radius - shape.tolerance * toleranceFraction;
}

/**
 * 配列内の自由点を球またはカプセル表面へ押し出す。
 */
void ProjectPoint(const FPreparedCollisionShape& shape, const gk::FVector3d& reference, gk::FVector3d& point)
{
    // 形状の膨張AABBから外れた点はそのまま保つ。
    if (IsOutsideExpandedBounds(point, point, shape))
        return;
    // 形状軸上の最近点と点からの差。
    const gk::FVector3d closest = ClosestPointOnPreparedAxis(point, shape);
    gk::FVector3d normal = Subtract(point, closest);
    // 接触面までの距離と必要半径。
    const double distance = Length(normal);
    // 侵入していない点はそのまま保つ。
    if (distance >= shape.radius - shape.tolerance)
        return;
    if (!Normalize(normal))
        normal = FallbackNormal(point, closest, shape.axis, reference);
    point = Add(closest, Multiply(normal, shape.radius));
}

/**
 * 侵入した鎖線分を自由端点の重みに応じて押し戻す。
 */
void ProjectSpan(const FPreparedCollisionShape& shape, bool firstFixed, gk::FVector3d& first, gk::FVector3d& second)
{
    // 接触形状から離れた線分は最近点計算を省く。
    if (IsOutsideExpandedBounds(first, second, shape))
        return;
    // 線分間の最近点、補間率、距離。
    double spanAmount = 0.0;
    double axisAmount = 0.0;
    gk::FVector3d spanPoint{};
    gk::FVector3d axisPoint{};
    ClosestSegmentPairWithAxis(first, second, shape.start, shape.axis, shape.axisLengthSquared, spanAmount, axisAmount, spanPoint, axisPoint);
    const double distance = Length(Subtract(spanPoint, axisPoint));
    // 半径を超えて鎖線分を押し戻す量。
    const double penetration = shape.radius - distance;
    // 許容誤差を超える侵入がなければ動かさない。
    if (!(penetration > shape.tolerance))
        return;
    // 退避方向と端点ごとの移動割合。
    gk::FVector3d normal = Subtract(spanPoint, axisPoint);
    if (!Normalize(normal))
    {
        // 中心交差時は線分方向とカプセル軸の外積で押し出し軸を作る。
        const gk::FVector3d spanDirection = Subtract(second, first);
        normal = Cross(spanDirection, shape.axis);
        if (!Normalize(normal))
            normal = DeterministicPerpendicular(spanDirection);
    }
    const double firstWeight = firstFixed ? 0.0 : 1.0 - spanAmount;
    const double secondWeight = spanAmount;
    // 最近点に強く寄与する自由端点を優先して動かす。
    if (secondWeight > 0.1 || firstFixed)
    {
        if (!(secondWeight > 0.0))
            return;
        second = Add(second, Multiply(normal, penetration / secondWeight));
        return;
    }
    if (!(firstWeight > 0.0))
        return;
    first = Add(first, Multiply(normal, penetration / firstWeight));
}

/**
 * 候補位置列の要素数と座標を検証する。
 */
bool ValidatePositions(const gk::FVector3d* positions, uint32_t pointCount, gk::String& error)
{
    if (!positions || pointCount < 2 || pointCount > kMaximumPointCount)
    {
        error.Assign("Secondary motion collision point count is invalid");
        return false;
    }
    // 節の有限値と隣接差の計算可能性を検証する。
    for (uint32_t index = 0; index < pointCount; ++index)
    {
        if (!IsFinite(positions[index]))
        {
            error.Assign("Secondary motion collision point is non-finite");
            return false;
        }
        // 隣り合う節の差が計算範囲内か調べる。
        if (index > 0)
        {
            const gk::FVector3d span = Subtract(positions[index], positions[index - 1]);
            if (!IsFinite(span) || !isfinite(Length(span)))
            {
                error.Assign("Secondary motion collision span is outside the numeric range");
                return false;
            }
        }
    }
    return true;
}

/**
 * 準備済み形状で点と鎖線分の接触を検査する。
 */
bool CheckPreparedContacts(const FPreparedCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* positions, uint32_t pointCount, gk::String& error, double toleranceFraction)
{
    // rootを除く全節は必ず形状の外にあることを確認する。
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        // この順で検査する準備済み形状。
        const FPreparedCollisionShape& shape = shapes[shapeIndex];
        for (uint32_t pointIndex = 1; pointIndex < pointCount; ++pointIndex)
        {
            if (IsOutsideExpandedBounds(positions[pointIndex], positions[pointIndex], shape))
                continue;
            // 点と形状軸の最近位置。
            const gk::FVector3d closest = ClosestPointOnPreparedAxis(positions[pointIndex], shape);
            if (Length(Subtract(positions[pointIndex], closest)) < shape.radius - shape.tolerance * toleranceFraction)
            {
                error.Assign("Secondary motion point intersects a collision shape");
                error.Append(" (shape: ");
                error.AppendUnsigned(shapeIndex);
                error.Append(", point: ");
                error.AppendUnsigned(pointIndex);
                error.Append(")");
                return false;
            }
        }
    }
    // 各形状についてroot例外を適用し、全鎖線分を調べる。
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        // root例外を調べる準備済み形状。
        const FPreparedCollisionShape& shape = shapes[shapeIndex];
        // 鎖の固定root位置。
        const gk::FVector3d root = positions[0];
        // 固定rootと形状軸の最近位置。
        const gk::FVector3d rootClosest = ClosestPointOnPreparedAxis(root, shape);
        // rootが内部なら最初の線分を免除する。
        const bool rootInside = Length(Subtract(root, rootClosest)) < shape.radius - shape.tolerance;
        // rootが形状内なら最初の線分だけを検査対象から外す。
        for (uint32_t spanIndex = 1; spanIndex < pointCount; ++spanIndex)
        {
            if (spanIndex == 1 && rootInside)
                continue;
            if (SpanPenetrates(positions[spanIndex - 1], positions[spanIndex], shape, toleranceFraction))
            {
                error.Assign("Secondary motion chain intersects a collision shape");
                error.Append(" (shape: ");
                error.AppendUnsigned(shapeIndex);
                error.Append(", span: ");
                error.AppendUnsigned(spanIndex);
                error.Append(")");
                return false;
            }
        }
    }
    error.Clear();
    return true;
}

}

#if defined(GKCORE_TESTING)
void ResetSecondaryMotionCollisionAxisQueryCount()
{
    secondaryMotionCollisionAxisQueryCount = 0;
}

uint64_t GetSecondaryMotionCollisionAxisQueryCount()
{
    return secondaryMotionCollisionAxisQueryCount;
}

void ResetSecondaryMotionContactCheckCount()
{
    secondaryMotionContactCheckCount = 0;
}

uint64_t GetSecondaryMotionContactCheckCount()
{
    return secondaryMotionContactCheckCount;
}

void ResetSecondaryMotionCollisionPrepareCount()
{
    secondaryMotionCollisionPrepareCount = 0;
}

uint64_t GetSecondaryMotionCollisionPrepareCount()
{
    return secondaryMotionCollisionPrepareCount;
}
#endif

bool ValidateSecondaryMotionCollisionShapes(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, gk::String& error)
{
    error.Clear();
    if (shapeCount > kMaximumCollisionShapes || (shapeCount > 0 && !shapes))
    {
        error.Assign("Secondary motion collision shape count is invalid");
        return false;
    }
    // 形状軸と正の半径が有限値か調べる。
    for (uint32_t index = 0; index < shapeCount; ++index)
    {
        const FModelSecondaryMotionCollisionShape& shape = shapes[index];
        if (!isfinite(shape.start.x) || !isfinite(shape.start.y) || !isfinite(shape.start.z) || !isfinite(shape.end.x) || !isfinite(shape.end.y) || !isfinite(shape.end.z) || !isfinite(shape.radius) || !(shape.radius > 0.0f))
        {
            error.Assign("Secondary motion collision shape is invalid");
            return false;
        }
    }
    return true;
}

bool ExpandSecondaryMotionCollisionShapes(const FModelSecondaryMotionCollisionShape* input, uint32_t count, float pointRadius, gk::Array<FModelSecondaryMotionCollisionShape>& output, gk::String& error)
{
    error.Clear();
    if (!isfinite(pointRadius) || pointRadius < 0.0f || !ValidateSecondaryMotionCollisionShapes(input, count, error))
    {
        if (error.Empty())
        {
            error.Assign("Secondary motion skin point radius is invalid");
        }
        return false;
    }
    gk::Array<FModelSecondaryMotionCollisionShape> candidate;
    if (!candidate.Reserve(count))
    {
        error.Assign("Secondary motion collision shape expansion allocation failed");
        return false;
    }
    if (pointRadius == 0.0f)
    {
        if (!candidate.AppendRange(input, count))
        {
            error.Assign("Secondary motion collision shape copy failed");
            return false;
        }
    }
    else
    {
        for (uint32_t index = 0; index < count; ++index)
        {
            FModelSecondaryMotionCollisionShape shape = input[index];
            const double radiusSum = static_cast<double>(shape.radius) + static_cast<double>(pointRadius);
            if (!isfinite(radiusSum) || radiusSum > FLT_MAX)
            {
                error.Assign("Secondary motion expanded collision radius exceeds float range");
                return false;
            }
            float expandedRadius = static_cast<float>(radiusSum);
            if (static_cast<double>(expandedRadius) < radiusSum)
            {
                expandedRadius = nextafterf(expandedRadius, INFINITY);
                if (!isfinite(expandedRadius))
                {
                    error.Assign("Secondary motion expanded collision radius cannot be rounded outward");
                    return false;
                }
            }
            const float dominantRadius = fmaxf(shape.radius, pointRadius);
            if (expandedRadius <= dominantRadius)
            {
                expandedRadius = nextafterf(dominantRadius, INFINITY);
                if (!isfinite(expandedRadius) || expandedRadius > FLT_MAX)
                {
                    error.Assign("Secondary motion expanded collision radius cannot exceed its larger input radius");
                    return false;
                }
            }
            shape.radius = expandedRadius;
            if (!candidate.Append(shape))
            {
                error.Assign("Secondary motion collision shape expansion allocation failed");
                return false;
            }
        }
    }
    output.MoveFrom(candidate);
    error.Clear();
    return true;
}

bool ProjectSecondaryMotionContacts(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* referenceTargets, uint32_t pointCount, gk::Array<gk::FVector3d>& positions, gk::String& error)
{
    error.Clear();
    // 入力数、既存位置、目標方向を先に検証し、出力をまだ変更しない。
    if (!ValidateSecondaryMotionCollisionShapes(shapes, shapeCount, error) || !ValidatePositions(positions.Data(), positions.Count(), error) || positions.Count() != pointCount)
    {
        if (error.Empty())
            error.Assign("Secondary motion collision point count does not match its positions");
        return false;
    }
    if (shapeCount == 0)
        return true;
    if (!referenceTargets)
    {
        error.Assign("Secondary motion collision references are missing");
        return false;
    }
    // fallbackに使う全目標位置が有限値か調べる。
    for (uint32_t index = 0; index < pointCount; ++index)
    {
        if (!IsFinite(referenceTargets[index]))
        {
            error.Assign("Secondary motion collision reference is non-finite");
            return false;
        }
    }
    // すべての接触が解けたときだけpositionsへ確定する候補。
    gk::Array<gk::FVector3d> candidate;
    if (!candidate.AppendRange(positions.Data(), pointCount))
    {
        error.Assign("Secondary motion collision workspace allocation failed");
        return false;
    }
    // 形状ごとの軸、半径、許容幅、旧AABBを呼出し中だけ保持する。
    FPreparedCollisionShape preparedShapes[kMaximumCollisionShapes] = {};
    // 動かないrootの形状内判定を一度だけ保持する。
    bool rootInsideByShape[kMaximumCollisionShapes] = {};
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        // この順で使う接触形状の準備値。
        preparedShapes[shapeIndex] = PrepareCollisionShape(shapes[shapeIndex]);
        // 固定rootと形状軸の最近位置。
        const gk::FVector3d rootClosest = ClosestPointOnPreparedAxis(candidate.At(0), preparedShapes[shapeIndex]);
        // 最初の線分を免除するためのroot侵入状態。
        rootInsideByShape[shapeIndex] = Length(Subtract(candidate.At(0), rootClosest)) < preparedShapes[shapeIndex].radius - preparedShapes[shapeIndex].tolerance;
    }
    // 形状ごとに線分接触と点接触を反復して解く。
    for (uint32_t pass = 0; pass < kProjectionPassCount; ++pass)
    {
        for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
        {
            // この反復で解く接触形状とrootの侵入状態。
            const FPreparedCollisionShape& shape = preparedShapes[shapeIndex];
            const bool rootInside = rootInsideByShape[shapeIndex];
            // rootが形状内なら、動かせないrootを含む最初の線分だけを省く。
            for (uint32_t spanIndex = 1; spanIndex < pointCount; ++spanIndex)
            {
                if (spanIndex == 1 && rootInside)
                    continue;
                ProjectSpan(shape, spanIndex == 1, candidate.At(spanIndex - 1), candidate.At(spanIndex));
            }
            // root以外の各点を形状外へ投影する。
            for (uint32_t pointIndex = 1; pointIndex < pointCount; ++pointIndex)
            {
                ProjectPoint(shape, Subtract(referenceTargets[pointIndex], referenceTargets[pointIndex - 1]), candidate.At(pointIndex));
            }
        }
        // 反復後に残る点・線分侵入を調べる。
        bool anyPenetration = false;
        for (uint32_t shapeIndex = 0; shapeIndex < shapeCount && !anyPenetration; ++shapeIndex)
        {
            // 残留接触を調べる形状。
            const FPreparedCollisionShape& shape = preparedShapes[shapeIndex];
            // 投影後も形状内に残る自由点を探す。
            for (uint32_t pointIndex = 1; pointIndex < pointCount; ++pointIndex)
            {
                // 投影後の自由点と形状軸の最近位置。
                const gk::FVector3d closest = ClosestPointOnPreparedAxis(candidate.At(pointIndex), shape);
                if (Length(Subtract(candidate.At(pointIndex), closest)) < shape.radius - shape.tolerance)
                {
                    anyPenetration = true;
                    break;
                }
            }
            if (anyPenetration)
                break;
            // root侵入時は最初の線分だけ免除する。
            const bool rootInside = rootInsideByShape[shapeIndex];
            // 形状を横切る線分が残っていないか調べる。
            for (uint32_t spanIndex = 1; spanIndex < pointCount; ++spanIndex)
            {
                if (spanIndex == 1 && rootInside)
                    continue;
                if (SpanPenetrates(candidate.At(spanIndex - 1), candidate.At(spanIndex), shape))
                {
                    anyPenetration = true;
                    break;
                }
            }
        }
        if (!anyPenetration)
            break;
    }
    // 収束上限で止まった場合も点侵入を許さない。
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        const FPreparedCollisionShape& shape = preparedShapes[shapeIndex];
        // rootを除く各点の最終侵入を検証する。
        for (uint32_t pointIndex = 1; pointIndex < pointCount; ++pointIndex)
        {
            // 最終自由点と形状軸の最近位置。
            const gk::FVector3d closest = ClosestPointOnPreparedAxis(candidate.At(pointIndex), shape);
            if (Length(Subtract(candidate.At(pointIndex), closest)) < shape.radius - shape.tolerance)
            {
                error.Assign("Secondary motion collision point remains inside a collision shape");
                return false;
            }
        }
    }
    // 公開検査と同じ呼出し数を保ち、準備済み形状をそのまま最終確認へ渡す。
#if defined(GKCORE_TESTING)
    ++secondaryMotionContactCheckCount;
#endif
    if (!ValidatePositions(candidate.Data(), pointCount, error))
        return false;
    if (!CheckPreparedContacts(preparedShapes, shapeCount, candidate.Data(), pointCount, error, 1.0))
        return false;
    // 確定前に候補位置の有限性を確認する。
    for (uint32_t index = 0; index < pointCount; ++index)
    {
        if (!IsFinite(candidate.At(index)))
        {
            error.Assign("Secondary motion collision projection produced a non-finite position");
            return false;
        }
    }
    positions.MoveFrom(candidate);
    error.Clear();
    return true;
}

bool CheckSecondaryMotionContacts(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* positions, uint32_t pointCount, gk::String& error, double toleranceFraction)
{
#if defined(GKCORE_TESTING)
    ++secondaryMotionContactCheckCount;
#endif
    error.Clear();
    if (!isfinite(toleranceFraction) || toleranceFraction < 0.0 || toleranceFraction > 1.0)
    {
        error.Assign("Secondary motion collision tolerance fraction is invalid");
        return false;
    }
    if (!ValidateSecondaryMotionCollisionShapes(shapes, shapeCount, error) || !ValidatePositions(positions, pointCount, error))
        return false;
    // 接触形状の軸と範囲をこの検査中だけ再利用する。
    FPreparedCollisionShape preparedShapes[kMaximumCollisionShapes] = {};
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        // この順で検査する形状の準備値。
        preparedShapes[shapeIndex] = PrepareCollisionShape(shapes[shapeIndex]);
    }
    return CheckPreparedContacts(preparedShapes, shapeCount, positions, pointCount, error, toleranceFraction);
}

bool AvoidSecondaryMotionCollisionSegment(const FModelSecondaryMotionCollisionShape& shape, const gk::FVector3d& parent, double length, bool firstSegment, gk::FVector3d& direction, gk::String& error)
{
    error.Clear();
    // 形状、始点、長さ、現在方向を検証する。
    if (!ValidateSecondaryMotionCollisionShapes(&shape, 1, error) || !IsFinite(parent) || !isfinite(length) || !(length > 0.0) || !IsFinite(direction))
    {
        if (error.Empty())
            error.Assign("Secondary motion collision segment input is invalid");
        return false;
    }
    gk::FVector3d candidateDirection = direction;
    if (!Normalize(candidateDirection))
    {
        error.Assign("Secondary motion collision segment direction is invalid");
        return false;
    }
    // float座標の安全域だけ先にAABBで除外し、遠方の親軸距離を計算しない。
    if (fabs(parent.value[0]) <= FLT_MAX && fabs(parent.value[1]) <= FLT_MAX && fabs(parent.value[2]) <= FLT_MAX && length <= FLT_MAX)
    {
        const gk::FVector3d boundedEnd = Add(parent, Multiply(candidateDirection, length));
        if (IsFinite(boundedEnd) && IsOutsideExpandedBoundsConservative(parent, boundedEnd, shape))
        {
            direction = candidateDirection;
            return true;
        }
    }
    // 親からカプセル軸への最近点で、親が形状内か調べる。
    const gk::FVector3d parentClosest = ClosestPointOnAxis(parent, shape);
    const double parentDistance = Length(Subtract(parent, parentClosest));
    const double radius = shape.radius;
    const double tolerance = ContactTolerance(radius);
    const bool parentInside = parentDistance < radius - tolerance;
    if (parentInside && !firstSegment)
    {
        // 親節の侵入は外側の反復処理で解き、方向coneの補正を保留する。
        direction = candidateDirection;
        return true;
    }
    // root内部の最初の節はendpointだけを外へ出し、通常は線分全体を判定する。
    const bool endpointOnly = firstSegment && parentInside;
    // 現在の長さと方向で作る終点。
    const gk::FVector3d currentEnd = Add(parent, Multiply(candidateDirection, length));
    if (!IsFinite(currentEnd))
    {
        error.Assign("Secondary motion collision endpoint exceeds the numeric range");
        return false;
    }
    // 線分とカプセル軸の最近位置を、方向coneの中心に使う。
    gk::FVector3d closest = {};
    if (endpointOnly)
    {
        closest = ClosestPointOnAxis(currentEnd, shape);
        if (Length(Subtract(currentEnd, closest)) >= radius - tolerance)
        {
            direction = candidateDirection;
            return true;
        }
    }
    else
    {
        if (IsOutsideExpandedBounds(parent, currentEnd, shape))
        {
            direction = candidateDirection;
            return true;
        }
        // 最近点の線分側・形状軸側の割合と線分位置。
        double spanAmount = 0.0;
        double axisAmount = 0.0;
        gk::FVector3d spanPoint = {};
        ClosestSegmentPair(parent, currentEnd, ToDouble(shape.start), ToDouble(shape.end), spanAmount, axisAmount, spanPoint, closest);
        (void)spanAmount;
        if (Length(Subtract(spanPoint, closest)) >= radius - tolerance)
        {
            direction = candidateDirection;
            return true;
        }
        // 軸の途中への侵入は、軸に直交する平面内の円筒coneで先に解く。
        // collider軸とその長さ。
        const gk::FVector3d shapeAxis = Subtract(ToDouble(shape.end), ToDouble(shape.start));
        const double shapeAxisLength = Length(shapeAxis);
        if (axisAmount > 0.0 && axisAmount < 1.0 && shapeAxisLength > 0.0 && isfinite(shapeAxisLength))
        {
            // 軸の単位方向と親位置の軸直交成分。
            const gk::FVector3d axisUnit = Multiply(shapeAxis, 1.0 / shapeAxisLength);
            const gk::FVector3d parentOffset = Subtract(parent, ToDouble(shape.start));
            const gk::FVector3d parentRadial = Subtract(parentOffset, Multiply(axisUnit, Dot(parentOffset, axisUnit)));
            // 親から無限軸までの半径方向距離。
            const double radialDistance = Length(parentRadial);
            if (radialDistance >= radius + tolerance)
            {
                // 進行方向の軸平行成分と直交成分。
                const gk::FVector3d directionParallel = Multiply(axisUnit, Dot(candidateDirection, axisUnit));
                const gk::FVector3d directionPerpendicular = Subtract(candidateDirection, directionParallel);
                const double perpendicularAmount = Length(directionPerpendicular);
                // 鎖節の軸直交方向への移動距離。
                const double radialTravel = length * perpendicularAmount;
                if (perpendicularAmount > DBL_EPSILON * 64.0 && radialTravel > 0.0 && isfinite(radialTravel))
                {
                    // 親から軸へ向かう方向と半径距離の比。
                    const gk::FVector3d towardAxis = Multiply(parentRadial, -1.0 / radialDistance);
                    const double radiusRatio = radius / radialDistance;
                    // 半径に接するために必要な軸直交移動量。
                    const double tangentTravel = radialDistance * sqrt(fmax(0.0, 1.0 - radiusRatio * radiusRatio));
                    // 許される内積の上限。初期値は実現不能を表す。
                    double cosineLimit = -2.0;
                    if (radialTravel >= tangentTravel)
                    {
                        cosineLimit = tangentTravel / radialDistance;
                    }
                    else if (radialTravel >= radialDistance - radius)
                    {
                        cosineLimit = 0.5 * (radialTravel / radialDistance + ((radialDistance - radius) / radialTravel) * (1.0 + radiusRatio));
                    }
                    if (cosineLimit >= -1.0 && cosineLimit <= 1.0)
                    {
                        // 現在の軸直交方向と軸方向成分の内積。
                        const gk::FVector3d perpendicularUnit = Multiply(directionPerpendicular, 1.0 / perpendicularAmount);
                        const double cosine = fmax(-1.0, fmin(1.0, Dot(perpendicularUnit, towardAxis)));
                        if (cosine <= cosineLimit)
                        {
                            direction = candidateDirection;
                            return true;
                        }
                        // 現在の進行方向に最も近い接線方向。
                        gk::FVector3d tangent = Subtract(perpendicularUnit, Multiply(towardAxis, cosine));
                        if (!Normalize(tangent))
                        {
                            tangent = Cross(axisUnit, towardAxis);
                            if (!Normalize(tangent))
                                tangent = DeterministicPerpendicular(axisUnit);
                        }
                        // 軸平行成分を保った、cone境界上の新しい単位方向。
                        const double boundedLimit = fmin(1.0, fmax(-1.0, cosineLimit));
                        gk::FVector3d adjusted = Add(directionParallel, Multiply(Add(Multiply(towardAxis, boundedLimit), Multiply(tangent, sqrt(fmax(0.0, 1.0 - boundedLimit * boundedLimit)))), perpendicularAmount));
                        if (Normalize(adjusted) && IsFinite(adjusted))
                        {
                            direction = adjusted;
                            return true;
                        }
                    }
                }
            }
        }
    }
    const gk::FVector3d parentToAxis = Subtract(closest, parent);
    const double distance = Length(parentToAxis);
    if (!(distance > 0.0))
    {
        if (endpointOnly && length >= radius + tolerance)
        {
            direction = candidateDirection;
            return true;
        }
        if (length < radius - tolerance)
        {
            error.Assign("Secondary motion collision segment cannot reach outside a shape");
            return false;
        }
        direction = candidateDirection;
        return true;
    }
    if (!endpointOnly && distance >= radius + tolerance)
    {
        // 軸から半径だけ離れる接線距離と安全な方向角の上限。
        const double tangentDistance = sqrt(fmax(0.0, distance * distance - radius * radius));
        if (length >= tangentDistance)
        {
            // 親から軸最近点へ向かう単位方向と許される最大内積。
            const gk::FVector3d axisDirection = Multiply(parentToAxis, 1.0 / distance);
            const double cosineLimit = fmin(1.0, tangentDistance / distance);
            const double cosine = fmax(-1.0, fmin(1.0, Dot(candidateDirection, axisDirection)));
            if (cosine <= cosineLimit)
            {
                direction = candidateDirection;
                return true;
            }
            // 現在の方向から軸方向成分を除いた回転面内の方向。
            gk::FVector3d tangent = Subtract(candidateDirection, Multiply(axisDirection, cosine));
            if (!Normalize(tangent))
                tangent = FallbackNormal(closest, closest, axisDirection, candidateDirection);
            candidateDirection = Add(Multiply(axisDirection, cosineLimit), Multiply(tangent, sqrt(fmax(0.0, 1.0 - cosineLimit * cosineLimit))));
            if (!Normalize(candidateDirection) || !IsFinite(candidateDirection))
            {
                error.Assign("Secondary motion collision cone produced an invalid direction");
                return false;
            }
            direction = candidateDirection;
            return true;
        }
    }
    // 有限長の終点を半径外へ置くための最大内積。
    const double denominator = 2.0 * length * distance;
    if (!(denominator > 0.0) || !isfinite(denominator))
    {
        error.Assign("Secondary motion collision endpoint constraint is invalid");
        return false;
    }
    const double cosineLimit = (length * length + distance * distance - radius * radius) / denominator;
    if (cosineLimit < -1.0 - DBL_EPSILON * 64.0)
    {
        error.Assign("Secondary motion collision segment cannot reach outside a shape");
        return false;
    }
    // 浮動小数点誤差を除いて実現可能な範囲へ制限する。
    const double boundedLimit = fmin(1.0, fmax(-1.0, cosineLimit));
    const gk::FVector3d axisDirection = Multiply(parentToAxis, 1.0 / distance);
    const double cosine = fmax(-1.0, fmin(1.0, Dot(candidateDirection, axisDirection)));
    if (cosine <= boundedLimit)
    {
        direction = candidateDirection;
        return true;
    }
    // 現在の方向を保ちながら許容cone境界へ回す接線方向。
    gk::FVector3d tangent = Subtract(candidateDirection, Multiply(axisDirection, cosine));
    if (!Normalize(tangent))
        tangent = FallbackNormal(closest, closest, axisDirection, candidateDirection);
    candidateDirection = Add(Multiply(axisDirection, boundedLimit), Multiply(tangent, sqrt(fmax(0.0, 1.0 - boundedLimit * boundedLimit))));
    if (!Normalize(candidateDirection) || !IsFinite(candidateDirection))
    {
        error.Assign("Secondary motion collision cone produced an invalid direction");
        return false;
    }
    direction = candidateDirection;
    error.Clear();
    return true;
}

}
