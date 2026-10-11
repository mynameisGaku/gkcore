// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FModelGpuSkinningGeometry.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/FModelSparsePoseGeometry.h"
#include "model/animation/ModelSkinPointContacts.h"
#include "model/animation/ModelSkinPoints.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <float.h>
#include <limits>

namespace
{

using FGeometry = gk::model::animation::FModelGpuSkinningGeometry;
using FMatrix = FGeometry::FMatrix;
using FPoint = gk::FVector3d;
using FShape = gk::model::animation::FModelSecondaryMotionCollisionShape;
using FCorrection = gk::model::animation::FModelSkinPointCorrection;
using FSparseGeometry = gk::model::animation::FModelSparsePoseGeometry;

/**
 * 値が許容誤差内で一致するか確認する。
 */
bool Near(double value, double expected, double tolerance = 1.0e-10)
{
    return fabs(value - expected) <= tolerance;
}

/**
 * 3次元位置が有限か確認する。
 */
bool IsFinite(const FPoint& point)
{
    return isfinite(point.value[0]) && isfinite(point.value[1]) && isfinite(point.value[2]);
}

/**
 * 位置間の距離を計算する。
 */
double Distance(const FPoint& first, const FPoint& second)
{
    const double x = first.value[0] - second.value[0];
    const double y = first.value[1] - second.value[1];
    const double z = first.value[2] - second.value[2];
    return sqrt(x * x + y * y + z * z);
}

/**
 * 点と線分の最短距離を計算する。
 */
double DistanceToSegment(const FPoint& point, const FPoint& start, const FPoint& end)
{
    const double axis[3] = { end.value[0] - start.value[0], end.value[1] - start.value[1], end.value[2] - start.value[2] };
    const double relative[3] = { point.value[0] - start.value[0], point.value[1] - start.value[1], point.value[2] - start.value[2] };
    const double axisLengthSquared = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    double fraction = axisLengthSquared > 0.0 ? (relative[0] * axis[0] + relative[1] * axis[1] + relative[2] * axis[2]) / axisLengthSquared : 0.0;
    fraction = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
    const FPoint closest = { { start.value[0] + axis[0] * fraction, start.value[1] + axis[1] * fraction, start.value[2] + axis[2] * fraction } };
    return Distance(point, closest);
}

/**
 * 失敗時に既存outputを保つことを確認し、理由を表示する。
 */
bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}

/**
 * 点配列を比較用に複製する。
 */
bool CopyPoints(const gk::Array<FPoint>& source, gk::Array<FPoint>& destination)
{
    return destination.AppendRange(source.Data(), source.Count());
}

/**
 * 座標配列が完全に一致するか比較する。
 */
bool SamePoints(const gk::Array<FPoint>& first, const gk::Array<FPoint>& second)
{
    return first.Count() == second.Count() && (!first.Count() || memcmp(first.Data(), second.Data(), static_cast<size_t>(first.Count()) * sizeof(FPoint)) == 0);
}

/**
 * sparse geometryの座標配列が完全に一致するか比較する。
 */
bool SameSparseGeometry(const FSparseGeometry& first, const FSparseGeometry& second)
{
    const bool samePositions = first.positions.Count() == second.positions.Count() && (!first.positions.Count() || memcmp(first.positions.Data(), second.positions.Data(), static_cast<size_t>(first.positions.Count()) * sizeof(FSparseGeometry::FModelVector4)) == 0);
    const bool sameNormals = first.normals.Count() == second.normals.Count() && (!first.normals.Count() || memcmp(first.normals.Data(), second.normals.Data(), static_cast<size_t>(first.normals.Count()) * sizeof(FSparseGeometry::FModelVector4)) == 0);
    return samePositions && sameNormals;
}

/**
 * skin後の単位三角形を作る。mixedWeight時は先頭頂点だけ2clusterを混ぜる。
 */
bool MakeTriangleFixture(FGeometry& geometry, gk::Array<FMatrix>& matrices, bool mixedWeight)
{
    const FGeometry::FPosition positions[3] = { { { 0.0, 0.0, 0.0 } }, { { 1.0, 0.0, 0.0 } }, { { 0.0, 1.0, 0.0 } } };
    const FGeometry::FInfluenceRange ranges[3] = { mixedWeight ? FGeometry::FInfluenceRange{ 0, 2 } : FGeometry::FInfluenceRange{ 0, 1 }, { mixedWeight ? 2u : 1u, 1 }, { mixedWeight ? 3u : 2u, 1 } };
    const FGeometry::FInfluence influences[4] = { { 0, mixedWeight ? 0.5 : 1.0 }, { mixedWeight ? 1u : 0u, mixedWeight ? 0.5 : 1.0 }, { 0, 1.0 }, { 0, 1.0 } };
    const uint32_t influenceCount = mixedWeight ? 4u : 3u;
    const FGeometry::FFace face{ 0, 3 };
    const FGeometry::FCorner corners[3] = { { 0, 0 }, { 1, 0 }, { 2, 0 } };
    const FGeometry::FNormalGroupRange normalRange{ 0, 1 };
    const uint32_t normalFace = 0;
    FGeometry::FSegment segment{};
    segment.firstPosition = 0;
    segment.positionCount = 3;
    segment.firstCluster = 0;
    segment.clusterCount = mixedWeight ? 2u : 1u;
    segment.firstFace = 0;
    segment.faceCount = 1;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        segment.geometryToNode[axis * 4u + axis] = 1.0;
    }
    FMatrix identity{};
    identity.value[0] = 1.0;
    identity.value[5] = 1.0;
    identity.value[10] = 1.0;
    FMatrix translated = identity;
    translated.value[3] = 2.0;
    const bool clustersAdded = geometry.clusters.Append(FGeometry::FCluster{}) && (!mixedWeight || geometry.clusters.Append(FGeometry::FCluster{}));
    const bool matricesAdded = matrices.Append(identity) && (!mixedWeight || matrices.Append(translated));
    return geometry.positions.AppendRange(positions, 3) && geometry.influenceRanges.AppendRange(ranges, 3) && geometry.influences.AppendRange(influences, influenceCount) && clustersAdded && matricesAdded && geometry.faces.Append(face) && geometry.corners.AppendRange(corners, 3) && geometry.normalGroupRanges.Append(normalRange) && geometry.normalFaceIds.Append(normalFace) && geometry.segments.Append(segment);
}

/**
 * 4角形と1normal groupのNewell法線入力を作る。
 */
bool MakeQuadFixture(FGeometry& geometry, gk::Array<FMatrix>& matrices)
{
    const FGeometry::FPosition positions[4] = { { { 0.0, 0.0, 0.0 } }, { { 1.0, 0.0, 0.0 } }, { { 1.0, 1.0, 0.0 } }, { { 0.0, 1.0, 0.0 } } };
    const FGeometry::FInfluenceRange ranges[4] = { { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 } };
    const FGeometry::FInfluence influences[4] = { { 0, 1.0 }, { 0, 1.0 }, { 0, 1.0 }, { 0, 1.0 } };
    const FGeometry::FFace face{ 0, 4 };
    const FGeometry::FCorner corners[4] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } };
    const FGeometry::FNormalGroupRange normalRange{ 0, 1 };
    const uint32_t normalFace = 0;
    FGeometry::FSegment segment{};
    segment.positionCount = 4;
    segment.clusterCount = 1;
    segment.faceCount = 1;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        segment.geometryToNode[axis * 4u + axis] = 1.0;
    }
    FMatrix identity{};
    identity.value[0] = 1.0;
    identity.value[5] = 1.0;
    identity.value[10] = 1.0;
    return geometry.positions.AppendRange(positions, 4) && geometry.influenceRanges.AppendRange(ranges, 4) && geometry.influences.AppendRange(influences, 4) && geometry.clusters.Append(FGeometry::FCluster{}) && matrices.Append(identity) && geometry.faces.Append(face) && geometry.corners.AppendRange(corners, 4) && geometry.normalGroupRanges.Append(normalRange) && geometry.normalFaceIds.Append(normalFace) && geometry.segments.Append(segment);
}

/**
 * 球・カプセル接触を幾何条件で確認する。
 */
bool CheckSphereAndCapsuleProjection()
{
    gk::String error;
    const FShape sphere{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f };
    const FPoint sphereInput[2] = { { { 2.0, 0.0, 0.0 } }, { { 0.5, 0.0, 0.0 } } };
    gk::Array<FPoint> sphereOutput;
    if (!gk::model::animation::ProjectModelSkinPoints(sphereInput, 2, &sphere, 1, 8, sphereOutput, error) || sphereOutput.Count() != 2 || !error.Empty())
    {
        return Fail("sphere contact projection rejected valid points");
    }
    // 数値誤差分だけ半径の外へ押し出すため、内点の半径には1e-5以内の余白を認める。
    const double projectedSphereRadius = Distance(sphereOutput.At(1), FPoint{ { 0.0, 0.0, 0.0 } });
    if (memcmp(&sphereOutput.At(0), &sphereInput[0], sizeof(FPoint)) != 0 || projectedSphereRadius < 1.0 || projectedSphereRadius > 1.0 + 1.0e-5)
    {
        return Fail("sphere projection moved an outside point or left an inside point penetrating");
    }

    const FShape capsule{ { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 1.0f };
    const FPoint capsuleInput[3] = { { { 0.5, 0.0, 0.0 } }, { { 0.0, 1.5, 0.0 } }, { { 0.0, 3.0, 0.0 } } };
    gk::Array<FPoint> capsuleOutput;
    const FPoint capsuleStart{ { 0.0, -1.0, 0.0 } };
    const FPoint capsuleEnd{ { 0.0, 1.0, 0.0 } };
    // 接触をfloatへ戻した際の小さな誤差を許し、側面・端点の射影方向も調べる。
    if (!gk::model::animation::ProjectModelSkinPoints(capsuleInput, 3, &capsule, 1, 8, capsuleOutput, error) || capsuleOutput.Count() != 3 || DistanceToSegment(capsuleOutput.At(0), capsuleStart, capsuleEnd) < 1.0 || DistanceToSegment(capsuleOutput.At(0), capsuleStart, capsuleEnd) > 1.0 + 1.0e-5 || capsuleOutput.At(0).value[0] < 1.0 || capsuleOutput.At(0).value[0] > 1.0 + 1.0e-5 || fabs(capsuleOutput.At(0).value[1]) > 1.0e-5 || fabs(capsuleOutput.At(0).value[2]) > 1.0e-5 || DistanceToSegment(capsuleOutput.At(1), capsuleStart, capsuleEnd) < 1.0 || DistanceToSegment(capsuleOutput.At(1), capsuleStart, capsuleEnd) > 1.0 + 1.0e-5 || capsuleOutput.At(1).value[1] < 2.0 || capsuleOutput.At(1).value[1] > 2.0 + 1.0e-5 || fabs(capsuleOutput.At(1).value[0]) > 1.0e-5 || fabs(capsuleOutput.At(1).value[2]) > 1.0e-5 || memcmp(&capsuleOutput.At(2), &capsuleInput[2], sizeof(FPoint)) != 0)
    {
        return Fail("capsule side or end cap remained penetrating");
    }

    const FShape zeroAxisSphere{ { 3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, 0.5f };
    const FPoint zeroAxisInput[2] = { { { 3.25, 0.0, 0.0 } }, { { 4.0, 0.0, 0.0 } } };
    gk::Array<FPoint> zeroAxisOutput;
    const FPoint zeroAxisCenter{ { 3.0, 0.0, 0.0 } };
    if (!gk::model::animation::ProjectModelSkinPoints(zeroAxisInput, 2, &zeroAxisSphere, 1, 8, zeroAxisOutput, error) || zeroAxisOutput.Count() != 2 || Distance(zeroAxisOutput.At(0), zeroAxisCenter) < 0.5 || Distance(zeroAxisOutput.At(0), zeroAxisCenter) > 0.5 + 1.0e-5 || zeroAxisOutput.At(0).value[0] < 3.5 || zeroAxisOutput.At(0).value[0] > 3.5 + 1.0e-5 || fabs(zeroAxisOutput.At(0).value[1]) > 1.0e-5 || fabs(zeroAxisOutput.At(0).value[2]) > 1.0e-5 || memcmp(&zeroAxisOutput.At(1), &zeroAxisInput[1], sizeof(FPoint)) != 0)
    {
        return Fail("zero-axis capsule was not treated as a sphere");
    }
    return true;
}

/**
 * 形状の移動、軸向き、半径境界を幾何計算で検査する。
 */
bool CheckTranslatedAndBoundaryGeometry()
{
    gk::String error;
    const FShape spheres[3] = { { { 0.0f, 1.0f, 2.0f }, { 0.0f, 1.0f, 2.0f }, 1.0f }, { { -3.0f, 0.0f, 1.0f }, { -3.0f, 0.0f, 1.0f }, 1.0f }, { { 2.0f, -4.0f, 0.0f }, { 2.0f, -4.0f, 0.0f }, 1.0f } };
    // 各軸で中心成分を0にし、AABB境界の1 ULP内にある点を取りこぼさないことを確かめる。
    const double inward = std::nextafter(1.0, 0.0);
    const double outward = std::nextafter(1.0, 2.0);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        const FPoint center{ { spheres[axis].start.x, spheres[axis].start.y, spheres[axis].start.z } };
        FPoint inside = center;
        FPoint outside = center;
        FPoint insideNear = center;
        FPoint outsideNear = center;
        FPoint boundary = center;
        inside.value[axis] += 0.25;
        outside.value[axis] += 2.0;
        insideNear.value[axis] += inward;
        outsideNear.value[axis] += outward;
        boundary.value[axis] += 1.0;
        const FPoint inputs[5] = { inside, outside, insideNear, outsideNear, boundary };
        gk::Array<FPoint> output;
        if (!gk::model::animation::ProjectModelSkinPoints(inputs, 5, &spheres[axis], 1, 8, output, error) || output.Count() != 5)
        {
            return Fail("translated sphere or radius-boundary projection failed");
        }
        const double projectedInside = Distance(output.At(0), center);
        const double projectedNearInside = Distance(output.At(2), center);
        if (projectedInside < 1.0 || projectedInside > 1.0 + 1.0e-5 || projectedNearInside < 1.0 || projectedNearInside > 1.0 + 1.0e-5 || output.At(0).value[axis] < center.value[axis] + 1.0 || output.At(0).value[axis] > center.value[axis] + 1.0 + 1.0e-5 || memcmp(&output.At(1), &outside, sizeof(FPoint)) != 0 || memcmp(&output.At(3), &outsideNear, sizeof(FPoint)) != 0 || memcmp(&output.At(4), &boundary, sizeof(FPoint)) != 0)
        {
            return Fail("translated sphere moved an outside boundary point or left an inside point penetrating");
        }
    }

    const FShape capsules[3] = { { { 4.0f, -2.0f, 1.0f }, { 6.0f, -2.0f, 1.0f }, 0.5f }, { { -3.0f, 4.0f, 2.0f }, { -3.0f, 6.0f, 2.0f }, 0.5f }, { { 2.0f, -4.0f, 5.0f }, { 2.0f, -4.0f, 7.0f }, 0.5f } };
    const FPoint capsuleInputs[3][3] = { { { { 5.0, -1.75, 1.0 } }, { { 6.25, -2.0, 1.0 } }, { { 5.0, -1.0, 1.0 } } }, { { { -2.75, 5.0, 2.0 } }, { { -3.0, 6.25, 2.0 } }, { { -3.0, 5.0, 3.0 } } }, { { { 2.0, -3.75, 6.0 } }, { { 2.0, -4.0, 7.25 } }, { { 2.0, -3.0, 6.0 } } } };
    const FPoint capsuleExpected[3][2] = { { { { 5.0, -1.5, 1.0 } }, { { 6.5, -2.0, 1.0 } } }, { { { -2.5, 5.0, 2.0 } }, { { -3.0, 6.5, 2.0 } } }, { { { 2.0, -3.5, 6.0 } }, { { 2.0, -4.0, 7.5 } } } };
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        gk::Array<FPoint> output;
        if (!gk::model::animation::ProjectModelSkinPoints(capsuleInputs[axis], 3, &capsules[axis], 1, 8, output, error) || output.Count() != 3)
        {
            return Fail("translated rotated capsule projection failed");
        }
        const FPoint start{ { capsules[axis].start.x, capsules[axis].start.y, capsules[axis].start.z } };
        const FPoint end{ { capsules[axis].end.x, capsules[axis].end.y, capsules[axis].end.z } };
        for (uint32_t point = 0; point < 2; ++point)
        {
            if (DistanceToSegment(output.At(point), start, end) < 0.5 || DistanceToSegment(output.At(point), start, end) > 0.5 + 1.0e-5 || Distance(output.At(point), capsuleExpected[axis][point]) > 1.0e-5)
            {
                return Fail("translated capsule side or end cap projection missed its analytical location");
            }
        }
        if (memcmp(&output.At(2), &capsuleInputs[axis][2], sizeof(FPoint)) != 0)
        {
            return Fail("translated capsule moved an outside point");
        }
    }

    const FShape zeroAxisCapsules[3] = { { { 3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, 0.5f }, { { 0.0f, 4.0f, 0.0f }, { 0.0f, 4.0f, 0.0f }, 0.5f }, { { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, 5.0f }, 0.5f } };
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        const FPoint center{ { zeroAxisCapsules[axis].start.x, zeroAxisCapsules[axis].start.y, zeroAxisCapsules[axis].start.z } };
        FPoint inside = center;
        inside.value[axis] += 0.25;
        const FPoint expected = [&]()
        {
            FPoint value = center;
            value.value[axis] += 0.5;
            return value;
        }();
        gk::Array<FPoint> output;
        if (!gk::model::animation::ProjectModelSkinPoints(&inside, 1, &zeroAxisCapsules[axis], 1, 8, output, error) || output.Count() != 1 || Distance(output.At(0), center) < 0.5 || Distance(output.At(0), center) > 0.5 + 1.0e-5 || Distance(output.At(0), expected) > 1.0e-5)
        {
            return Fail("translated zero-axis capsule did not behave like a sphere");
        }
    }
    return true;
}

/**
 * 斜めで負座標のカプセルを独立距離で検査し、端点順に依存しないことを確かめる。
 */
bool CheckReversedArbitraryCapsule()
{
    const FShape forward{ { -3.0f, 2.0f, -4.0f }, { -1.0f, -2.0f, 2.0f }, 0.75f };
    const FShape reversed{ forward.end, forward.start, forward.radius };
    const FPoint start{ { forward.start.x, forward.start.y, forward.start.z } };
    const FPoint end{ { forward.end.x, forward.end.y, forward.end.z } };
    const FPoint midpoint{ { -2.0, 0.0, -1.0 } };
    const double inverseLength = 1.0 / sqrt(70.0);
    const double direction[3] = { -3.0 * inverseLength, 6.0 * inverseLength, 5.0 * inverseLength };
    FPoint inside = midpoint;
    FPoint outside = midpoint;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        inside.value[axis] += direction[axis] * 0.25;
        outside.value[axis] += direction[axis] * 1.5;
    }
    if (!Near(DistanceToSegment(inside, start, end), 0.25, 1.0e-12) || !Near(DistanceToSegment(outside, start, end), 1.5, 1.0e-12))
    {
        return Fail("arbitrary capsule fixture did not match its independent closest-point distances");
    }

    const FPoint inputs[2] = { inside, outside };
    gk::Array<FPoint> forwardOutput;
    gk::Array<FPoint> reversedOutput;
    gk::String error;
    if (!gk::model::animation::ProjectModelSkinPoints(inputs, 2, &forward, 1, 8, forwardOutput, error) || !gk::model::animation::ProjectModelSkinPoints(inputs, 2, &reversed, 1, 8, reversedOutput, error) || forwardOutput.Count() != 2 || reversedOutput.Count() != 2)
    {
        return Fail("arbitrary capsule projection failed");
    }
    const double forwardDistance = DistanceToSegment(forwardOutput.At(0), start, end);
    const double reversedDistance = DistanceToSegment(reversedOutput.At(0), start, end);
    if (forwardDistance < forward.radius || forwardDistance > forward.radius + 1.0e-5 || reversedDistance < forward.radius || reversedDistance > forward.radius + 1.0e-5 || Distance(forwardOutput.At(0), reversedOutput.At(0)) > 1.0e-5 || memcmp(&forwardOutput.At(1), &outside, sizeof(FPoint)) != 0 || memcmp(&reversedOutput.At(1), &outside, sizeof(FPoint)) != 0)
    {
        return Fail("reversing an arbitrary capsule changed its outside projection result");
    }
    return true;
}

/**
 * 離れた点群が正確な形状距離評価へ入らない予算を確認する。
 */
bool CheckFarPointProjectionBudget()
{
    constexpr uint32_t pointCount = 4096;
    FPoint points[pointCount]{};
    for (uint32_t point = 0; point < pointCount; ++point)
    {
        points[point] = { { 1000.0 + static_cast<double>(point) * 0.125, -700.0 + static_cast<double>(point % 17), 500.0 - static_cast<double>(point % 29) } };
    }
    FShape shapes[8]{};
    for (uint32_t shape = 0; shape < 8; ++shape)
    {
        const float x = static_cast<float>(static_cast<int32_t>(shape) * 3 - 10);
        const float y = static_cast<float>(static_cast<int32_t>(shape % 3) * 4 - 5);
        const float z = static_cast<float>(static_cast<int32_t>(shape % 5) * 2 - 4);
        shapes[shape].start = { x, y, z };
        shapes[shape].end = shape % 2 == 0 ? gk::Vec3{ x, y, z } : gk::Vec3{ x, y + 1.5f, z - 0.5f };
        shapes[shape].radius = 0.75f;
    }
    gk::Array<FPoint> output;
    gk::String error;
    gk::model::animation::ResetModelSkinContactWorkForTesting();
    const bool projected = gk::model::animation::ProjectModelSkinPoints(points, pointCount, shapes, 8, 64, output, error);
    const uint64_t exactDistanceCount = gk::model::animation::GetModelSkinContactExactDistanceCountForTesting();
    if (!projected || !error.Empty() || output.Count() != pointCount || memcmp(output.Data(), points, sizeof(points)) != 0)
    {
        return Fail("far finite points were not preserved by contact projection");
    }
    if (exactDistanceCount != 0)
    {
        fprintf(stderr, "far point projection used %llu exact distance evaluations\n", static_cast<unsigned long long>(exactDistanceCount));
        return false;
    }

    FShape oneNearShapes[8]{};
    oneNearShapes[0] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f };
    for (uint32_t shape = 1; shape < 8; ++shape)
    {
        oneNearShapes[shape] = shapes[shape - 1];
    }
    const FPoint inside{ { 0.25, 0.0, 0.0 } };
    gk::Array<FPoint> projectedInside;
    gk::model::animation::ResetModelSkinContactWorkForTesting();
    const bool resolved = gk::model::animation::ProjectModelSkinPoints(&inside, 1, oneNearShapes, 8, 64, projectedInside, error);
    const uint64_t insideDistanceCount = gk::model::animation::GetModelSkinContactExactDistanceCountForTesting();
    const double resolvedRadius = projectedInside.Count() == 1 ? Distance(projectedInside.At(0), FPoint{}) : -1.0;
    if (!resolved || !error.Empty() || projectedInside.Count() != 1 || resolvedRadius < 1.0 || resolvedRadius > 1.0 + 1.0e-5 || insideDistanceCount == 0 || insideDistanceCount > 4)
    {
        fprintf(stderr, "near shape projection or far-shape gate failed (radius=%.12g exact=%llu)\n", resolvedRadius, static_cast<unsigned long long>(insideDistanceCount));
        return false;
    }
    return true;
}

/**
 * 巨大有限座標の距離overflowを成功扱いせずoutputを保つ。
 */
bool CheckOverflowProjectionAtomicity()
{
    const FPoint huge{ { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), 0.0 } };
    const FShape sphere{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f };
    const FPoint sentinel{ { -9.0, 8.0, 7.0 } };
    gk::Array<FPoint> output;
    gk::String error;
    if (!output.Append(sentinel) || gk::model::animation::ProjectModelSkinPoints(&huge, 1, &sphere, 1, 64, output, error) || error.Empty() || output.Count() != 1 || memcmp(output.Data(), &sentinel, sizeof(sentinel)) != 0)
    {
        return Fail("overflowing finite distance was accepted or modified output");
    }
    return true;
}

/**
 * FLT_MAX端点と半径の拡張範囲でもdouble計算が有限で外点を保つ。
 */
bool CheckExtremeFiniteCapsuleBounds()
{
    const FShape capsule{ { -FLT_MAX, 0.0f, 0.0f }, { FLT_MAX, 0.0f, 0.0f }, FLT_MAX };
    const FPoint outside{ { 0.0, static_cast<double>(FLT_MAX), static_cast<double>(FLT_MAX) } };
    const FPoint sentinel{ { -4.0, -5.0, -6.0 } };
    gk::Array<FPoint> output;
    gk::String error;
    if (!output.Append(sentinel) || !gk::model::animation::ProjectModelSkinPoints(&outside, 1, &capsule, 1, 8, output, error) || !error.Empty() || output.Count() != 1 || memcmp(output.Data(), &outside, sizeof(outside)) != 0)
    {
        return Fail("FLT_MAX capsule bounds became non-finite or moved an outside point");
    }
    const FPoint start{ { capsule.start.x, capsule.start.y, capsule.start.z } };
    const FPoint end{ { capsule.end.x, capsule.end.y, capsule.end.z } };
    const double distance = DistanceToSegment(output.At(0), start, end);
    if (!isfinite(distance) || distance <= static_cast<double>(capsule.radius))
    {
        return Fail("FLT_MAX capsule outside result did not remain geometrically clear");
    }
    return true;
}

/**
 * 中心の決定論的fallback、複数形状、反復不足を検査する。
 */
bool CheckFallbackAndMultipleShapes()
{
    gk::String error;
    const FShape sphere{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f };
    const FPoint center{};
    gk::Array<FPoint> first;
    gk::Array<FPoint> second;
    if (!gk::model::animation::ProjectModelSkinPoints(&center, 1, &sphere, 1, 8, first, error) || !gk::model::animation::ProjectModelSkinPoints(&center, 1, &sphere, 1, 8, second, error) || first.Count() != 1 || !SamePoints(first, second) || !IsFinite(first.At(0)) || Distance(first.At(0), center) < 1.0 || Distance(first.At(0), center) > 1.0 + 1.0e-5)
    {
        return Fail("sphere-center fallback was not finite, deterministic, and outside");
    }

    const FShape overlapping[2] = { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f }, { { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 1.0f } };
    const FPoint overlapInput[2] = { { { 0.1, 0.0, 0.0 } }, { { 3.0, 0.0, 0.0 } } };
    gk::Array<FPoint> overlapOutput;
    // 重なった球すべてから数値余白内で離れ、もともと外の点は厳密に保つ。
    if (!gk::model::animation::ProjectModelSkinPoints(overlapInput, 2, overlapping, 2, 32, overlapOutput, error) || overlapOutput.Count() != 2 || !IsFinite(overlapOutput.At(0)) || Distance(overlapOutput.At(0), FPoint{ { 0.0, 0.0, 0.0 } }) < 1.0 || Distance(overlapOutput.At(0), FPoint{ { 1.0, 0.0, 0.0 } }) < 1.0 || memcmp(&overlapOutput.At(1), &overlapInput[1], sizeof(FPoint)) != 0)
    {
        return Fail("multiple overlapping shapes were not all clear at the final point");
    }

    gk::Array<FPoint> preserved;
    const FPoint sentinel{ { -7.0, 8.0, 9.0 } };
    if (!preserved.Append(sentinel) || gk::model::animation::ProjectModelSkinPoints(&center, 1, &sphere, 1, 0, preserved, error) || error.Empty() || preserved.Count() != 1 || !Near(preserved.At(0).value[0], -7.0) || !Near(preserved.At(0).value[1], 8.0) || !Near(preserved.At(0).value[2], 9.0))
    {
        return Fail("insufficient projection passes did not fail atomically");
    }
    const FShape opposingCapsules[2] = { { { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }, 1.0f }, { { 1.5f, 0.0f, -1.0f }, { 1.5f, 0.0f, 1.0f }, 1.0f } };
    const FPoint unresolved{ { 0.1, 0.0, 0.0 } };
    if (gk::model::animation::ProjectModelSkinPoints(&unresolved, 1, opposingCapsules, 2, 1, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
    {
        return Fail("one-pass capsule projection did not reject an unresolved contact atomically");
    }
    return true;
}

/**
 * 空形状のcopy、入力alias、無効入力時のatomic保持を確認する。
 */
bool CheckProjectionInputs()
{
    gk::String error;
    const FPoint inputs[2] = { { { 1.0, 2.0, 3.0 } }, { { -4.0, 5.0, 6.0 } } };
    gk::Array<FPoint> output;
    const FPoint sentinel{ { 70.0, 80.0, 90.0 } };
    if (!output.Append(sentinel) || !gk::model::animation::ProjectModelSkinPoints(inputs, 2, nullptr, 0, 8, output, error) || output.Count() != 2 || memcmp(output.Data(), inputs, sizeof(inputs)) != 0 || !error.Empty())
    {
        return Fail("shape-free projection did not copy points");
    }

    gk::Array<FPoint> alias;
    if (!CopyPoints(output, alias) || !alias.Count())
    {
        return Fail("could not create projection alias input");
    }
    const FPoint aliasSnapshot[2] = { alias.At(0), alias.At(1) };
    if (!gk::model::animation::ProjectModelSkinPoints(alias.Data(), alias.Count(), nullptr, 0, 8, alias, error) || alias.Count() != 2 || memcmp(alias.Data(), aliasSnapshot, sizeof(aliasSnapshot)) != 0)
    {
        return Fail("shape-free projection did not support aliased input and output");
    }

    gk::Array<FPoint> preserved;
    if (!preserved.Append(sentinel))
    {
        return Fail("could not allocate projection sentinel");
    }
    const FShape validShape{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f };
    const FShape nonFiniteShape{ { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f };
    if (gk::model::animation::ProjectModelSkinPoints(nullptr, 1, &validShape, 1, 8, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
    {
        return Fail("null points were not rejected atomically");
    }
    if (gk::model::animation::ProjectModelSkinPoints(inputs, 2, &nonFiniteShape, 1, 8, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
    {
        return Fail("non-finite shape was not rejected atomically");
    }
    if (gk::model::animation::ProjectModelSkinPoints(inputs, 2, &validShape, 65, 8, preserved, error) || error.Empty() || preserved.Count() != 1 || memcmp(preserved.Data(), &sentinel, sizeof(sentinel)) != 0)
    {
        return Fail("excessive shape count was not rejected atomically");
    }
    return true;
}

/**
 * 三角形のvertex補正と再計算normalを解析値で確かめる。
 */
bool CheckTriangleGeometryCorrection()
{
    FGeometry geometry;
    gk::Array<FMatrix> matrices;
    if (!MakeTriangleFixture(geometry, matrices, false))
    {
        return Fail("could not create triangle skin-contact fixture");
    }
    FCorrection correction{};
    correction.positionId = 1;
    correction.position[0] = 1.0;
    correction.position[1] = 0.0;
    correction.position[2] = 1.0;
    FSparseGeometry output;
    gk::String error;
    if (!gk::model::animation::EvaluateModelSkinContactGeometry(geometry, matrices, &correction, 1, output, error) || output.positions.Count() != 3 || output.normals.Count() != 1 || !error.Empty())
    {
        return Fail("triangle contact correction failed");
    }
    const auto& corrected = output.positions.At(1);
    const auto& normal = output.normals.At(0);
    const float inverseLength = static_cast<float>(1.0 / sqrt(2.0));
    if (!Near(corrected.value[0], 1.0) || !Near(corrected.value[1], 0.0) || !Near(corrected.value[2], 1.0) || !Near(normal.value[0], -inverseLength, 1.0e-6) || !Near(normal.value[1], 0.0, 1.0e-6) || !Near(normal.value[2], inverseLength, 1.0e-6) || normal.value[3] != 0.0f || corrected.value[3] != 1.0f)
    {
        return Fail("triangle corrected position or analytical normal is incorrect");
    }
    for (uint32_t position = 0; position < output.positions.Count(); ++position)
    {
        for (uint32_t axis = 0; axis < 4; ++axis)
        {
            if (!isfinite(output.positions.At(position).value[axis]))
            {
                return Fail("triangle output position is non-finite");
            }
        }
    }
    for (uint32_t normalIndex = 0; normalIndex < output.normals.Count(); ++normalIndex)
    {
        for (uint32_t axis = 0; axis < 4; ++axis)
        {
            if (!isfinite(output.normals.At(normalIndex).value[axis]))
            {
                return Fail("triangle output normal is non-finite");
            }
        }
    }
    return true;
}

/**
 * 4角形Newell法線とmixed weight skin位置を検査する。
 */
bool CheckQuadAndMixedWeightGeometry()
{
    FGeometry quad;
    gk::Array<FMatrix> quadMatrices;
    if (!MakeQuadFixture(quad, quadMatrices))
    {
        return Fail("could not create quad skin-contact fixture");
    }
    FCorrection quadCorrection{};
    quadCorrection.positionId = 2;
    quadCorrection.position[0] = 1.0;
    quadCorrection.position[1] = 1.0;
    quadCorrection.position[2] = 1.0;
    FSparseGeometry quadOutput;
    gk::String error;
    if (!gk::model::animation::EvaluateModelSkinContactGeometry(quad, quadMatrices, &quadCorrection, 1, quadOutput, error) || quadOutput.positions.Count() != 4 || quadOutput.normals.Count() != 1)
    {
        return Fail("quad Newell geometry correction failed");
    }
    const double inverseSqrtSix = 1.0 / sqrt(6.0);
    if (!Near(quadOutput.normals.At(0).value[0], -inverseSqrtSix, 1.0e-6) || !Near(quadOutput.normals.At(0).value[1], -inverseSqrtSix, 1.0e-6) || !Near(quadOutput.normals.At(0).value[2], 2.0 * inverseSqrtSix, 1.0e-6))
    {
        return Fail("quad normal did not match the analytical Newell result");
    }

    FGeometry mixed;
    gk::Array<FMatrix> mixedMatrices;
    if (!MakeTriangleFixture(mixed, mixedMatrices, true))
    {
        return Fail("could not create mixed-weight triangle fixture");
    }
    const uint32_t ids[3] = { 0, 1, 2 };
    gk::Array<FPoint> skinnedPoints;
    if (!gk::model::animation::EvaluateModelSkinPoints(mixed, mixedMatrices, ids, 3, skinnedPoints, error) || skinnedPoints.Count() != 3 || !Near(skinnedPoints.At(0).value[0], 1.0))
    {
        return Fail("mixed-weight skin helper did not produce the expected source point");
    }
    FCorrection mixedCorrection{};
    mixedCorrection.positionId = 0;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        mixedCorrection.position[axis] = skinnedPoints.At(0).value[axis];
    }
    mixedCorrection.position[2] = 1.0;
    FSparseGeometry mixedOutput;
    if (!gk::model::animation::EvaluateModelSkinContactGeometry(mixed, mixedMatrices, &mixedCorrection, 1, mixedOutput, error) || mixedOutput.positions.Count() != 3 || !Near(mixedOutput.positions.At(0).value[0], 1.0) || !Near(mixedOutput.positions.At(0).value[2], 1.0))
    {
        return Fail("mixed-weight contact correction did not preserve the evaluated skin position");
    }
    return true;
}

/**
 * 重複・範囲外correctionの失敗時にgeometry outputを保つ。
 */
bool CheckGeometryCorrectionAtomicity()
{
    FGeometry geometry;
    gk::Array<FMatrix> matrices;
    if (!MakeTriangleFixture(geometry, matrices, false))
    {
        return Fail("could not create atomic geometry fixture");
    }
    FSparseGeometry output;
    FSparseGeometry snapshot;
    const FSparseGeometry::FModelVector4 sentinelPosition{ { -7.0f, 8.0f, 9.0f, 1.0f } };
    const FSparseGeometry::FModelVector4 sentinelNormal{ { 4.0f, 5.0f, 6.0f, 0.0f } };
    if (!output.positions.Append(sentinelPosition) || !output.normals.Append(sentinelNormal) || !snapshot.positions.Append(sentinelPosition) || !snapshot.normals.Append(sentinelNormal))
    {
        return Fail("could not allocate sparse geometry sentinel");
    }
    const FCorrection duplicate[2] = { { 0, { 0.0, 0.0, 0.0 } }, { 0, { 1.0, 0.0, 0.0 } } };
    const FCorrection outOfRange{ 3, { 0.0, 0.0, 0.0 } };
    gk::String error;
    if (gk::model::animation::EvaluateModelSkinContactGeometry(geometry, matrices, duplicate, 2, output, error) || error.Empty() || !SameSparseGeometry(output, snapshot))
    {
        return Fail("duplicate contact corrections were not rejected atomically");
    }
    if (gk::model::animation::EvaluateModelSkinContactGeometry(geometry, matrices, &outOfRange, 1, output, error) || error.Empty() || !SameSparseGeometry(output, snapshot))
    {
        return Fail("out-of-range contact correction was not rejected atomically");
    }
    const FCorrection nonFinite{ 1, { 0.0, std::numeric_limits<double>::quiet_NaN(), 0.0 } };
    if (gk::model::animation::EvaluateModelSkinContactGeometry(geometry, matrices, &nonFinite, 1, output, error) || error.Empty() || !SameSparseGeometry(output, snapshot))
    {
        return Fail("non-finite contact correction was not rejected atomically");
    }
    return true;
}

}

int main()
{
    if (!CheckSphereAndCapsuleProjection() || !CheckTranslatedAndBoundaryGeometry() || !CheckReversedArbitraryCapsule() || !CheckFarPointProjectionBudget() || !CheckOverflowProjectionAtomicity() || !CheckExtremeFiniteCapsuleBounds() || !CheckFallbackAndMultipleShapes() || !CheckProjectionInputs() || !CheckTriangleGeometryCorrection() || !CheckQuadAndMixedWeightGeometry() || !CheckGeometryCorrectionAtomicity())
    {
        return 1;
    }
    return 0;
}
