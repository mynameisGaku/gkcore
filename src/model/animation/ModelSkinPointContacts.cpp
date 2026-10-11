// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSkinPointContacts.h"

#include "model/animation/ModelSkinPoints.h"

#include <float.h>
#include <math.h>

/**
 * 変形後の点接触と、補正後の位置・法線を評価する処理。
 */
namespace gk::model::animation
{
/**
 * 接触形状の距離と有限な描画値を組み立てる補助処理。
 */
namespace
{

/**
 * 3成分が有限か確認する。
 */
bool IsFinite3(const double value[3])
{
    return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

/**
 * 桁あふれを抑えて3次元ベクトルの長さを計算する。
 */
double Length3(const double value[3])
{
    const double largest = fmax(fabs(value[0]), fmax(fabs(value[1]), fabs(value[2])));
    if (!(largest > 0.0) || !isfinite(largest))
    {
        return largest;
    }
    const double x = value[0] / largest;
    const double y = value[1] / largest;
    const double z = value[2] / largest;
    return largest * sqrt(x * x + y * y + z * z);
}

/**
 * 接触形状から呼出し内で再利用する検証済み軸情報。
 */
struct FPreparedSkinContactShape
{
    // 形状軸の始点。
    double start[3];
    // 始点から終点への軸差。
    double axis[3];
    // 正規化した軸方向。
    double unit[3];
    // 軸の長さ。
    double axisLength;
    // 接触形状の半径。
    double radius;
    // 中心軸での退化時に使う決定論的な法線。
    double fallbackNormal[3];
};

/**
 * 軸への最近点を求め、点からの距離も返す。
 */
bool ClosestShapeAxisPoint(const gk::FVector3d& point, const FPreparedSkinContactShape& shape, double closest[3], double& distance)
{
    const double offset[3] = { point.value[0] - shape.start[0], point.value[1] - shape.start[1], point.value[2] - shape.start[2] };
    if (!IsFinite3(offset))
    {
        return false;
    }
    double amount = 0.0;
    if (shape.axisLength > 0.0)
    {
        const double projected = offset[0] * shape.unit[0] + offset[1] * shape.unit[1] + offset[2] * shape.unit[2];
        if (!isfinite(projected))
        {
            return false;
        }
        amount = fmax(0.0, fmin(1.0, projected / shape.axisLength));
    }
    for (uint32_t component = 0; component < 3; ++component)
    {
        closest[component] = shape.start[component] + shape.axis[component] * amount;
    }
    const double delta[3] = { point.value[0] - closest[0], point.value[1] - closest[1], point.value[2] - closest[2] };
    distance = Length3(delta);
    return IsFinite3(closest) && isfinite(distance);
}

/**
 * 退化時に軸と直交する決定論的な単位方向を作る。
 */
bool MakeFallbackNormal(const double axisUnit[3], double axisLength, double normal[3])
{
    if (!(axisLength > 0.0))
    {
        normal[0] = 1.0;
        normal[1] = 0.0;
        normal[2] = 0.0;
        return true;
    }
    const double absolute[3] = { fabs(axisUnit[0]), fabs(axisUnit[1]), fabs(axisUnit[2]) };
    uint32_t basis = 0;
    if (absolute[1] < absolute[basis])
    {
        basis = 1;
    }
    if (absolute[2] < absolute[basis])
    {
        basis = 2;
    }
    double unit[3] = { 0.0, 0.0, 0.0 };
    unit[basis] = 1.0;
    normal[0] = axisUnit[1] * unit[2] - axisUnit[2] * unit[1];
    normal[1] = axisUnit[2] * unit[0] - axisUnit[0] * unit[2];
    normal[2] = axisUnit[0] * unit[1] - axisUnit[1] * unit[0];
    const double normalLength = Length3(normal);
    if (!(normalLength > 0.0) || !isfinite(normalLength))
    {
        return false;
    }
    normal[0] /= normalLength;
    normal[1] /= normalLength;
    normal[2] /= normalLength;
    return IsFinite3(normal);
}

/**
 * 指定方向に直交する決定論的な単位方向を作る。
 */
bool MakePerpendicular(const double direction[3], double perpendicular[3])
{
    const double length = Length3(direction);
    if (!(length > 0.0) || !isfinite(length))
    {
        return false;
    }
    const double unit[3] = { direction[0] / length, direction[1] / length, direction[2] / length };
    const double absolute[3] = { fabs(unit[0]), fabs(unit[1]), fabs(unit[2]) };
    uint32_t basis = 0;
    if (absolute[1] < absolute[basis])
    {
        basis = 1;
    }
    if (absolute[2] < absolute[basis])
    {
        basis = 2;
    }
    double unitBasis[3] = { 0.0, 0.0, 0.0 };
    unitBasis[basis] = 1.0;
    perpendicular[0] = unit[1] * unitBasis[2] - unit[2] * unitBasis[1];
    perpendicular[1] = unit[2] * unitBasis[0] - unit[0] * unitBasis[2];
    perpendicular[2] = unit[0] * unitBasis[1] - unit[1] * unitBasis[0];
    const double perpendicularLength = Length3(perpendicular);
    if (!(perpendicularLength > 0.0) || !isfinite(perpendicularLength))
    {
        return false;
    }
    perpendicular[0] /= perpendicularLength;
    perpendicular[1] /= perpendicularLength;
    perpendicular[2] /= perpendicularLength;
    return IsFinite3(perpendicular);
}

/**
 * 指定点が全接触形状の外にあるか調べる。
 */
bool IsOutsideAllShapes(const gk::FVector3d& point, const FPreparedSkinContactShape* shapes, uint32_t shapeCount)
{
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        double closest[3]{};
        double distance = 0.0;
        if (!ClosestShapeAxisPoint(point, shapes[shapeIndex], closest, distance) || distance < shapes[shapeIndex].radius)
        {
            return false;
        }
    }
    return true;
}

/**
 * affine行列の係数が有限か確認する。
 */
bool IsFiniteMatrix(const FModelGpuSkinningGeometry::FMatrix& matrix)
{
    for (uint32_t element = 0; element < 12; ++element)
    {
        if (!isfinite(matrix.value[element]))
        {
            return false;
        }
    }
    return true;
}

/**
 * 倍精度位置を描画用floatへ変換できるか確認する。
 */
bool ToFloatVector(const double value[3], FModelSparsePoseGeometry::FModelVector4& output, float w)
{
    if (!IsFinite3(value) || fabs(value[0]) > FLT_MAX || fabs(value[1]) > FLT_MAX || fabs(value[2]) > FLT_MAX)
    {
        return false;
    }
    output.value[0] = static_cast<float>(value[0]);
    output.value[1] = static_cast<float>(value[1]);
    output.value[2] = static_cast<float>(value[2]);
    output.value[3] = w;
    return isfinite(output.value[0]) && isfinite(output.value[1]) && isfinite(output.value[2]);
}

}

/**
 * skin位置群を球またはカプセルの外へ投影する。
 */
bool ProjectModelSkinPoints(const gk::FVector3d* points, uint32_t count, const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, uint32_t passes, gk::Array<gk::FVector3d>& output, gk::String& error)
{
    error.Clear();
    if ((count && !points) || shapeCount > 64u || (shapeCount && !shapes) || passes < 1u || passes > 64u)
    {
        error.Assign("model skin point projection input counts or pointers are invalid");
        return false;
    }
    FPreparedSkinContactShape preparedShapes[64]{};
    for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
    {
        const FModelSecondaryMotionCollisionShape& shape = shapes[shapeIndex];
        if (!isfinite(shape.start.x) || !isfinite(shape.start.y) || !isfinite(shape.start.z) || !isfinite(shape.end.x) || !isfinite(shape.end.y) || !isfinite(shape.end.z) || !(shape.radius > 0.0f) || !isfinite(shape.radius))
        {
            error.Assign("model skin point projection shape is invalid");
            return false;
        }
        FPreparedSkinContactShape& prepared = preparedShapes[shapeIndex];
        prepared.start[0] = shape.start.x;
        prepared.start[1] = shape.start.y;
        prepared.start[2] = shape.start.z;
        prepared.axis[0] = static_cast<double>(shape.end.x) - shape.start.x;
        prepared.axis[1] = static_cast<double>(shape.end.y) - shape.start.y;
        prepared.axis[2] = static_cast<double>(shape.end.z) - shape.start.z;
        prepared.axisLength = Length3(prepared.axis);
        prepared.radius = shape.radius;
        if (!IsFinite3(prepared.axis) || !isfinite(prepared.axisLength))
        {
            error.Assign("model skin point projection shape axis is invalid");
            return false;
        }
        if (prepared.axisLength > 0.0)
        {
            prepared.unit[0] = prepared.axis[0] / prepared.axisLength;
            prepared.unit[1] = prepared.axis[1] / prepared.axisLength;
            prepared.unit[2] = prepared.axis[2] / prepared.axisLength;
        }
        if (!MakeFallbackNormal(prepared.unit, prepared.axisLength, prepared.fallbackNormal))
        {
            error.Assign("model skin point projection shape axis is invalid");
            return false;
        }
    }
    for (uint32_t pointIndex = 0; pointIndex < count; ++pointIndex)
    {
        if (!IsFinite3(points[pointIndex].value))
        {
            error.Assign("model skin point projection point is non-finite");
            return false;
        }
    }

    gk::Array<gk::FVector3d> candidate;
    if (!candidate.AppendRange(points, count))
    {
        error.Assign("model skin point projection allocation failed");
        return false;
    }
    for (uint32_t pointIndex = 0; pointIndex < count; ++pointIndex)
    {
        gk::FVector3d& point = candidate.At(pointIndex);
        if (IsOutsideAllShapes(point, preparedShapes, shapeCount))
        {
            continue;
        }
        for (uint32_t pass = 0; pass < passes; ++pass)
        {
            for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
            {
                const FPreparedSkinContactShape& shape = preparedShapes[shapeIndex];
                double closest[3]{};
                double distance = 0.0;
                if (!ClosestShapeAxisPoint(point, shape, closest, distance))
                {
                    error.Assign("model skin point projection distance overflowed");
                    return false;
                }
                if (distance >= shape.radius)
                {
                    continue;
                }
                double normal[3]{};
                if (distance > 0.0)
                {
                    normal[0] = (point.value[0] - closest[0]) / distance;
                    normal[1] = (point.value[1] - closest[1]) / distance;
                    normal[2] = (point.value[2] - closest[2]) / distance;
                }
                else
                {
                    normal[0] = shape.fallbackNormal[0];
                    normal[1] = shape.fallbackNormal[1];
                    normal[2] = shape.fallbackNormal[2];
                }
                const double scale = fmax(fabs(point.value[0]), fmax(fabs(point.value[1]), fabs(point.value[2])));
                const double margin = fmax(1.0e-7, fmax(shape.radius * 1.0e-6, scale * static_cast<double>(FLT_EPSILON) * 2.0));
                const double targetDistance = shape.radius + margin;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    point.value[axis] = closest[axis] + normal[axis] * targetDistance;
                }
                if (!IsFinite3(point.value))
                {
                    error.Assign("model skin point projection result is non-finite");
                    return false;
                }
            }
            if (IsOutsideAllShapes(point, preparedShapes, shapeCount))
            {
                break;
            }
            // sphere同士の対称な押し合いで往復するときは、接触法線と直交する方向へ逃がす。
            double deepestPenetration = 0.0;
            uint32_t deepestShape = shapeCount;
            double deepestClosest[3]{};
            for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
            {
                double closest[3]{};
                double distance = 0.0;
                if (!ClosestShapeAxisPoint(point, preparedShapes[shapeIndex], closest, distance))
                {
                    error.Assign("model skin point projection distance overflowed");
                    return false;
                }
                const double penetration = preparedShapes[shapeIndex].radius - distance;
                if (penetration > deepestPenetration)
                {
                    deepestPenetration = penetration;
                    deepestShape = shapeIndex;
                    deepestClosest[0] = closest[0];
                    deepestClosest[1] = closest[1];
                    deepestClosest[2] = closest[2];
                }
            }
            if (deepestShape < shapeCount)
            {
                const FPreparedSkinContactShape& shape = preparedShapes[deepestShape];
                if (!(shape.axisLength > 0.0))
                {
                    const double contactNormal[3] = { point.value[0] - deepestClosest[0], point.value[1] - deepestClosest[1], point.value[2] - deepestClosest[2] };
                    double perpendicular[3]{};
                    if (MakePerpendicular(contactNormal, perpendicular))
                    {
                        const double scale = fmax(fabs(point.value[0]), fmax(fabs(point.value[1]), fabs(point.value[2])));
                        const double margin = fmax(1.0e-7, fmax(shape.radius * 1.0e-6, scale * static_cast<double>(FLT_EPSILON) * 2.0));
                        const double targetDistance = shape.radius + margin;
                        for (uint32_t axis = 0; axis < 3; ++axis)
                        {
                            point.value[axis] = deepestClosest[axis] + perpendicular[axis] * targetDistance;
                        }
                    }
                }
            }
        }
    }
    for (uint32_t pointIndex = 0; pointIndex < count; ++pointIndex)
    {
        if (!IsOutsideAllShapes(candidate.At(pointIndex), preparedShapes, shapeCount))
        {
            double deepestPenetration = 0.0;
            uint32_t deepestShape = shapeCount;
            for (uint32_t shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
            {
                double closest[3]{};
                double distance = 0.0;
                if (!ClosestShapeAxisPoint(candidate.At(pointIndex), preparedShapes[shapeIndex], closest, distance))
                {
                    error.Assign("model skin point projection final distance overflowed");
                    return false;
                }
                const double penetration = preparedShapes[shapeIndex].radius - distance;
                if (penetration > deepestPenetration)
                {
                    deepestPenetration = penetration;
                    deepestShape = shapeIndex;
                }
            }
            error.Assign("model skin point projection did not clear point ");
            error.AppendUnsigned(pointIndex);
            error.Append("; deepest shape ");
            error.AppendUnsigned(deepestShape);
            error.Append(" remains inside");
            return false;
        }
    }
    output.MoveFrom(candidate);
    error.Clear();
    return true;
}

/**
 * skin補正を適用し、GPU参照と同じ手順で位置と法線を組み立てる。
 */
bool EvaluateModelSkinContactGeometry(const FModelGpuSkinningGeometry& geometry, const gk::Array<FModelGpuSkinningGeometry::FMatrix>& matrices, const FModelSkinPointCorrection* corrections, uint32_t correctionCount, FModelSparsePoseGeometry& output, gk::String& error)
{
    error.Clear();
    const uint32_t positionCount = geometry.positions.Count();
    const uint32_t faceCount = geometry.faces.Count();
    const uint32_t cornerCount = geometry.corners.Count();
    const uint32_t normalCount = geometry.normalGroupRanges.Count();
    if ((correctionCount && !corrections) || geometry.influenceRanges.Count() != positionCount || matrices.Count() != geometry.clusters.Count())
    {
        error.Assign("model skin contact geometry input counts or pointers are invalid");
        return false;
    }
    for (uint32_t segmentIndex = 0; segmentIndex < geometry.segments.Count(); ++segmentIndex)
    {
        const FModelGpuSkinningGeometry::FSegment& segment = geometry.segments.At(segmentIndex);
        if (segment.firstPosition > positionCount || segment.positionCount > positionCount - segment.firstPosition || segment.firstCluster > geometry.clusters.Count() || segment.clusterCount > geometry.clusters.Count() - segment.firstCluster || segment.firstFace > faceCount || segment.faceCount > faceCount - segment.firstFace)
        {
            error.Assign("model skin contact geometry segment range is invalid");
            return false;
        }
    }
    for (uint32_t matrixIndex = 0; matrixIndex < matrices.Count(); ++matrixIndex)
    {
        if (!IsFiniteMatrix(matrices.At(matrixIndex)))
        {
            error.Assign("model skin contact geometry matrix is non-finite");
            return false;
        }
    }
    for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex)
    {
        const FModelGpuSkinningGeometry::FFace& face = geometry.faces.At(faceIndex);
        if (face.firstCorner > cornerCount || face.cornerCount > cornerCount - face.firstCorner)
        {
            error.Assign("model skin contact geometry face range is invalid");
            return false;
        }
    }
    for (uint32_t cornerIndex = 0; cornerIndex < cornerCount; ++cornerIndex)
    {
        const FModelGpuSkinningGeometry::FCorner& corner = geometry.corners.At(cornerIndex);
        if (corner.positionIndex >= positionCount || corner.normalGroupIndex >= normalCount)
        {
            error.Assign("model skin contact geometry corner reference is invalid");
            return false;
        }
    }
    for (uint32_t groupIndex = 0; groupIndex < normalCount; ++groupIndex)
    {
        const FModelGpuSkinningGeometry::FNormalGroupRange& range = geometry.normalGroupRanges.At(groupIndex);
        if (range.firstFace > geometry.normalFaceIds.Count() || range.faceCount > geometry.normalFaceIds.Count() - range.firstFace)
        {
            error.Assign("model skin contact geometry normal incidence range is invalid");
            return false;
        }
        for (uint32_t incidence = 0; incidence < range.faceCount; ++incidence)
        {
            if (geometry.normalFaceIds.At(range.firstFace + incidence) >= faceCount)
            {
                error.Assign("model skin contact geometry normal incidence references an invalid face");
                return false;
            }
        }
    }
    uint32_t previousCorrectionId = 0;
    for (uint32_t correctionIndex = 0; correctionIndex < correctionCount; ++correctionIndex)
    {
        const FModelSkinPointCorrection& correction = corrections[correctionIndex];
        if (correction.positionId >= positionCount || (correctionIndex && correction.positionId <= previousCorrectionId) || !IsFinite3(correction.position) || fabs(correction.position[0]) > FLT_MAX || fabs(correction.position[1]) > FLT_MAX || fabs(correction.position[2]) > FLT_MAX)
        {
            error.Assign("model skin contact correction is duplicate, unordered, out of range, or non-finite");
            return false;
        }
        previousCorrectionId = correction.positionId;
    }

    // cornerが参照するnormal groupを一度だけ記録する。
    gk::Array<uint8_t> referencedNormalGroups;
    if (!referencedNormalGroups.Reserve(normalCount))
    {
        error.Assign("model skin contact geometry normal reference allocation failed");
        return false;
    }
    for (uint32_t groupIndex = 0; groupIndex < normalCount; ++groupIndex)
    {
        if (!referencedNormalGroups.Append(0))
        {
            error.Assign("model skin contact geometry normal reference allocation failed");
            return false;
        }
    }
    for (uint32_t cornerIndex = 0; cornerIndex < cornerCount; ++cornerIndex)
    {
        referencedNormalGroups.At(geometry.corners.At(cornerIndex).normalGroupIndex) = 1;
    }

    gk::Array<gk::FVector3d> evaluatedPositions;
    gk::Array<uint32_t> chunkIds;
    if (!evaluatedPositions.Reserve(positionCount) || !chunkIds.Reserve(positionCount < 8192u ? positionCount : 8192u))
    {
        error.Assign("model skin contact geometry position allocation failed");
        return false;
    }
    for (uint32_t firstPosition = 0; firstPosition < positionCount;)
    {
        const uint32_t chunkCount = positionCount - firstPosition < 8192u ? positionCount - firstPosition : 8192u;
        chunkIds.Clear();
        for (uint32_t offset = 0; offset < chunkCount; ++offset)
        {
            if (!chunkIds.Append(firstPosition + offset))
            {
                error.Assign("model skin contact geometry chunk allocation failed");
                return false;
            }
        }
        gk::Array<gk::FVector3d> chunkPositions;
        if (!EvaluateModelSkinPoints(geometry, matrices, chunkIds.Data(), chunkCount, chunkPositions, error) || !evaluatedPositions.AppendRange(chunkPositions.Data(), chunkPositions.Count()))
        {
            if (error.Empty())
            {
                error.Assign("model skin contact geometry evaluated position allocation failed");
            }
            return false;
        }
        firstPosition += chunkCount;
    }
    for (uint32_t correctionIndex = 0; correctionIndex < correctionCount; ++correctionIndex)
    {
        const FModelSkinPointCorrection& correction = corrections[correctionIndex];
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            evaluatedPositions.At(correction.positionId).value[axis] = correction.position[axis];
        }
    }

    gk::Array<FModelSparsePoseGeometry::FModelVector4> candidatePositions;
    gk::Array<FModelSparsePoseGeometry::FModelVector4> candidateNormals;
    if (!candidatePositions.Reserve(positionCount) || !candidateNormals.Reserve(normalCount))
    {
        error.Assign("model skin contact geometry output allocation failed");
        return false;
    }
    for (uint32_t positionIndex = 0; positionIndex < positionCount; ++positionIndex)
    {
        FModelSparsePoseGeometry::FModelVector4 position{};
        if (!ToFloatVector(evaluatedPositions.At(positionIndex).value, position, 1.0f) || !candidatePositions.Append(position))
        {
            error.Assign("model skin contact geometry position exceeds finite float range");
            return false;
        }
    }

    gk::Array<gk::FVector3d> faceNormals;
    if (!faceNormals.Reserve(faceCount))
    {
        error.Assign("model skin contact geometry face normal allocation failed");
        return false;
    }
    for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex)
    {
        const FModelGpuSkinningGeometry::FFace& face = geometry.faces.At(faceIndex);
        gk::FVector3d normal{};
        if (face.cornerCount >= 3u)
        {
            const uint32_t firstCorner = face.firstCorner;
            const uint32_t ia = geometry.corners.At(firstCorner).positionIndex;
            const uint32_t ib = geometry.corners.At(firstCorner + 1u).positionIndex;
            const uint32_t ic = geometry.corners.At(firstCorner + 2u).positionIndex;
            const gk::FVector3d& a = evaluatedPositions.At(ia);
            const gk::FVector3d& b = evaluatedPositions.At(ib);
            const gk::FVector3d& c = evaluatedPositions.At(ic);
            if (face.cornerCount == 3u)
            {
                const double ab[3] = { b.value[0] - a.value[0], b.value[1] - a.value[1], b.value[2] - a.value[2] };
                const double ac[3] = { c.value[0] - a.value[0], c.value[1] - a.value[1], c.value[2] - a.value[2] };
                normal.value[0] = ab[1] * ac[2] - ab[2] * ac[1];
                normal.value[1] = ab[2] * ac[0] - ab[0] * ac[2];
                normal.value[2] = ab[0] * ac[1] - ab[1] * ac[0];
            }
            else if (face.cornerCount == 4u)
            {
                const uint32_t id = geometry.corners.At(firstCorner + 3u).positionIndex;
                const gk::FVector3d& d = evaluatedPositions.At(id);
                const double ac[3] = { c.value[0] - a.value[0], c.value[1] - a.value[1], c.value[2] - a.value[2] };
                const double db[3] = { d.value[0] - b.value[0], d.value[1] - b.value[1], d.value[2] - b.value[2] };
                normal.value[0] = ac[1] * db[2] - ac[2] * db[1];
                normal.value[1] = ac[2] * db[0] - ac[0] * db[2];
                normal.value[2] = ac[0] * db[1] - ac[1] * db[0];
            }
            else
            {
                for (uint32_t cornerOffset = 0; cornerOffset < face.cornerCount; ++cornerOffset)
                {
                    const uint32_t currentId = geometry.corners.At(firstCorner + cornerOffset).positionIndex;
                    const uint32_t nextOffset = cornerOffset + 1u == face.cornerCount ? 0u : cornerOffset + 1u;
                    const uint32_t nextId = geometry.corners.At(firstCorner + nextOffset).positionIndex;
                    const gk::FVector3d& p = evaluatedPositions.At(currentId);
                    const gk::FVector3d& q = evaluatedPositions.At(nextId);
                    normal.value[0] += (p.value[1] - q.value[1]) * (p.value[2] + q.value[2]);
                    normal.value[1] += (p.value[2] - q.value[2]) * (p.value[0] + q.value[0]);
                    normal.value[2] += (p.value[0] - q.value[0]) * (p.value[1] + q.value[1]);
                }
            }
        }
        if (!IsFinite3(normal.value) || !faceNormals.Append(normal))
        {
            error.Assign("model skin contact geometry face normal is invalid");
            return false;
        }
    }

    for (uint32_t groupIndex = 0; groupIndex < normalCount; ++groupIndex)
    {
        gk::FVector3d sum{};
        const FModelGpuSkinningGeometry::FNormalGroupRange& range = geometry.normalGroupRanges.At(groupIndex);
        for (uint32_t incidence = 0; incidence < range.faceCount; ++incidence)
        {
            const gk::FVector3d& faceNormal = faceNormals.At(geometry.normalFaceIds.At(range.firstFace + incidence));
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                sum.value[axis] += faceNormal.value[axis];
            }
        }
        if (!IsFinite3(sum.value))
        {
            error.Assign("model skin contact geometry normal group sum is non-finite");
            return false;
        }
        const double largest = fmax(fabs(sum.value[0]), fmax(fabs(sum.value[1]), fabs(sum.value[2])));
        double normal[3] = { 0.0, 0.0, 0.0 };
        if (largest > 0.0)
        {
            const double scaled[3] = { sum.value[0] / largest, sum.value[1] / largest, sum.value[2] / largest };
            const double scaledLengthSquared = scaled[0] * scaled[0] + scaled[1] * scaled[1] + scaled[2] * scaled[2];
            if (!(scaledLengthSquared > 0.0) || !isfinite(scaledLengthSquared))
            {
                error.Assign("model skin contact geometry normal group cannot be normalized");
                return false;
            }
            const double inverseLength = 1.0 / sqrt(scaledLengthSquared);
            normal[0] = scaled[0] * inverseLength;
            normal[1] = scaled[1] * inverseLength;
            normal[2] = scaled[2] * inverseLength;
        }
        if (referencedNormalGroups.At(groupIndex) && !(largest > 0.0))
        {
            error.Assign("model skin contact geometry referenced normal group is degenerate");
            return false;
        }
        FModelSparsePoseGeometry::FModelVector4 normalOutput{};
        if (!ToFloatVector(normal, normalOutput, 0.0f) || !candidateNormals.Append(normalOutput))
        {
            error.Assign("model skin contact geometry normal exceeds finite float range");
            return false;
        }
    }

    output.positions.MoveFrom(candidatePositions);
    output.normals.MoveFrom(candidateNormals);
    error.Clear();
    return true;
}

}
