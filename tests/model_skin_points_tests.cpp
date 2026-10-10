// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSkinPoints.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits>

namespace
{

using FGeometry = gk::model::animation::FModelGpuSkinningGeometry;
using gk::FVector3d;

/**
 * 許容誤差付きで3次元位置を比較する。
 */
bool Near(const FVector3d& value, double x, double y, double z, double tolerance = 1.0e-12)
{
    return fabs(value.value[0] - x) <= tolerance && fabs(value.value[1] - y) <= tolerance && fabs(value.value[2] - z) <= tolerance;
}

/**
 * 失敗理由を表示してテストを停止する。
 */
bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}

/**
 * 配列を複製し、入力geometryの不変性比較に使う。
 */
template <class T> bool CopyArray(const gk::Array<T>& source, gk::Array<T>& destination)
{
    return destination.AppendRange(source.Data(), source.Count());
}

/**
 * 配列の要素がbyte単位で変わっていないか調べる。
 */
template <class T> bool SameArray(const gk::Array<T>& first, const gk::Array<T>& second)
{
    return first.Count() == second.Count() && (!first.Count() || memcmp(first.Data(), second.Data(), static_cast<size_t>(first.Count()) * sizeof(T)) == 0);
}

/**
 * source geometryを複製し、呼出し後の入力不変性を調べる。
 */
bool CopyGeometry(const FGeometry& source, FGeometry& destination)
{
    return CopyArray(source.positions, destination.positions) && CopyArray(source.influenceRanges, destination.influenceRanges) && CopyArray(source.influences, destination.influences) && CopyArray(source.clusters, destination.clusters) && CopyArray(source.faces, destination.faces) && CopyArray(source.corners, destination.corners) && CopyArray(source.normalGroupRanges, destination.normalGroupRanges) && CopyArray(source.normalFaceIds, destination.normalFaceIds) && CopyArray(source.segments, destination.segments);
}

/**
 * 評価前後のskin入力が同一か調べる。
 */
bool SameGeometry(const FGeometry& first, const FGeometry& second)
{
    return SameArray(first.positions, second.positions) && SameArray(first.influenceRanges, second.influenceRanges) && SameArray(first.influences, second.influences) && SameArray(first.clusters, second.clusters) && SameArray(first.faces, second.faces) && SameArray(first.corners, second.corners) && SameArray(first.normalGroupRanges, second.normalGroupRanges) && SameArray(first.normalFaceIds, second.normalFaceIds) && SameArray(first.segments, second.segments);
}

/**
 * 行ごとの3x4 affine行列を12要素へ設定する。
 */
void SetMatrix(FGeometry::FMatrix& matrix, const double values[12])
{
    memcpy(matrix.value, values, sizeof(matrix.value));
}

/**
 * 既知の単骨・混合weight・fallback入力を作る。
 */
bool MakeFixture(FGeometry& geometry, gk::Array<FGeometry::FMatrix>& matrices)
{
    const FGeometry::FPosition positions[] = { { { 1.0, 2.0, 3.0 } }, { { 2.0, -1.0, 0.5 } }, { { 4.0, 2.0, 1.0 } }, { { 1.0, 1.0, 1.0 } }, { { 1.0, 2.0, 0.0 } } };
    const FGeometry::FInfluenceRange ranges[] = { { 0, 1 }, { 1, 2 }, { 3, 0 }, { 3, 2 }, { 5, 1 } };
    const FGeometry::FInfluence influences[] = { { 0, 1.0 }, { 0, 0.5 }, { 1, 0.5 }, { 0, 0.2 }, { 1, 0.3 }, { 1, 1.0 } };
    // 固定Hipsと回転・移動するClothの変換を混ぜて、50/50 skinを作る。
    // 行ごとにxyz基底とtranslationを続けるFMatrixのproducer形式。
    const double affine0[12] = { 2.0, 0.0, 0.0, 10.0, 0.0, 3.0, 0.0, -2.0, 0.0, 0.0, 4.0, 1.0 };
    // z軸まわりの正回転と平行移動を個別の単骨点でも検証する。
    const double affine1[12] = { 0.0, -1.0, 0.0, 4.0, 1.0, 0.0, 0.0, 5.0, 0.0, 0.0, 1.0, -1.0 };
    const double fallback[12] = { 1.0, 0.0, 0.0, -3.0, 0.0, 2.0, 0.0, 1.0, 0.0, 0.0, 1.0, 2.0 };
    FGeometry::FMatrix first{};
    FGeometry::FMatrix second{};
    FGeometry::FMatrix third{};
    SetMatrix(first, affine0);
    SetMatrix(second, affine1);
    SetMatrix(third, fallback);
    FGeometry::FSegment segment{};
    segment.firstPosition = 0;
    segment.positionCount = 5;
    segment.firstCluster = 0;
    segment.clusterCount = 3;
    return geometry.positions.AppendRange(positions, 5) && geometry.influenceRanges.AppendRange(ranges, 5) && geometry.influences.AppendRange(influences, 6) && geometry.clusters.Append(FGeometry::FCluster{}) && geometry.clusters.Append(FGeometry::FCluster{}) && geometry.clusters.Append(FGeometry::FCluster{}) && geometry.segments.Append(segment) && matrices.Append(first) && matrices.Append(second) && matrices.Append(third);
}

/**
 * 指定点を失敗させ、診断と既存outputの保持を確認する。
 */
bool RejectAtomically(const FGeometry& geometry, const gk::Array<FGeometry::FMatrix>& matrices, const uint32_t* ids, uint32_t count, const char* message)
{
    gk::Array<FVector3d> output;
    gk::String error;
    const FVector3d sentinel = { { -77.0, 88.0, 99.0 } };
    if (!output.Append(sentinel))
        return Fail("could not allocate output sentinel");
    if (gk::model::animation::EvaluateModelSkinPoints(geometry, matrices, ids, count, output, error) || !output.Count() || !Near(output.At(0), -77.0, 88.0, 99.0) || error.Empty())
        return Fail(message);
    return true;
}

/**
 * 要求順、重複ID、affine skin、混合weightとfallbackを検査する。
 */
bool CheckPointEvaluation()
{
    FGeometry geometry;
    gk::Array<FGeometry::FMatrix> matrices;
    FGeometry original;
    gk::Array<FGeometry::FMatrix> originalMatrices;
    if (!MakeFixture(geometry, matrices) || !CopyGeometry(geometry, original) || !CopyArray(matrices, originalMatrices))
        return Fail("could not construct skin point fixture");
    const uint32_t ids[] = { 3, 0, 2, 1, 4, 3 };
    gk::Array<FVector3d> output;
    gk::String error;
    if (!gk::model::animation::EvaluateModelSkinPoints(geometry, matrices, ids, 6, output, error) || output.Count() != 6 || !error.Empty())
        return Fail("skin point evaluation failed for valid mixed input");
    if (!Near(output.At(0), 3.3, 2.0, 1.0) || !Near(output.At(1), 12.0, 4.0, 13.0) || !Near(output.At(2), 1.0, 5.0, 3.0) || !Near(output.At(3), 9.5, 1.0, 1.25) || !Near(output.At(4), 2.0, 6.0, -1.0) || !Near(output.At(5), 3.3, 2.0, 1.0))
        return Fail("single-bone, mixed-weight, affine-offset, fallback, or duplicate result is incorrect");
    if (!SameGeometry(geometry, original) || !SameArray(matrices, originalMatrices))
        return Fail("skin point evaluation modified immutable input geometry or matrices");
    return true;
}

/**
 * 無効入力を診断し、出力を原子的に保つ。
 */
bool CheckInvalidInputs()
{
    FGeometry geometry;
    gk::Array<FGeometry::FMatrix> matrices;
    if (!MakeFixture(geometry, matrices))
        return Fail("could not construct invalid-input fixture");
    const uint32_t validId = 0;
    const uint32_t invalidId = 5;
    if (!RejectAtomically(geometry, matrices, nullptr, 1, "null ids were not rejected atomically") || !RejectAtomically(geometry, matrices, &invalidId, 1, "out-of-range position id was not rejected atomically"))
        return false;

    const uint32_t savedRangeStart = geometry.influenceRanges.At(0).firstInfluence;
    geometry.influenceRanges.At(0).firstInfluence = UINT32_MAX;
    const bool badRangeRejected = RejectAtomically(geometry, matrices, &validId, 1, "invalid influence range was not rejected atomically");
    geometry.influenceRanges.At(0).firstInfluence = savedRangeStart;
    if (!badRangeRejected)
        return false;

    const uint32_t savedCluster = geometry.influences.At(0).clusterIndex;
    geometry.influences.At(0).clusterIndex = 3;
    const bool badClusterRejected = RejectAtomically(geometry, matrices, &validId, 1, "out-of-range cluster index was not rejected atomically");
    geometry.influences.At(0).clusterIndex = savedCluster;
    if (!badClusterRejected)
        return false;

    const double savedPosition = geometry.positions.At(0).value[0];
    geometry.positions.At(0).value[0] = std::numeric_limits<double>::quiet_NaN();
    const bool badPositionRejected = RejectAtomically(geometry, matrices, &validId, 1, "non-finite bind position was not rejected atomically");
    geometry.positions.At(0).value[0] = savedPosition;
    if (!badPositionRejected)
        return false;

    const double savedWeight = geometry.influences.At(0).weight;
    geometry.influences.At(0).weight = -0.25;
    const bool negativeWeightRejected = RejectAtomically(geometry, matrices, &validId, 1, "negative influence weight was not rejected atomically");
    geometry.influences.At(0).weight = savedWeight;
    if (!negativeWeightRejected)
        return false;

    geometry.influences.At(0).weight = std::numeric_limits<double>::quiet_NaN();
    const bool nanWeightRejected = RejectAtomically(geometry, matrices, &validId, 1, "non-finite influence weight was not rejected atomically");
    geometry.influences.At(0).weight = savedWeight;
    if (!nanWeightRejected)
        return false;

    const double savedMatrix = matrices.At(0).value[0];
    matrices.At(0).value[0] = std::numeric_limits<double>::infinity();
    const bool badMatrixRejected = RejectAtomically(geometry, matrices, &validId, 1, "non-finite referenced matrix was not rejected atomically");
    matrices.At(0).value[0] = savedMatrix;
    if (!badMatrixRejected)
        return false;

    gk::Array<FGeometry::FMatrix> shortMatrices;
    if (!shortMatrices.Append(matrices.At(0)) || !shortMatrices.Append(matrices.At(1)) || !RejectAtomically(geometry, shortMatrices, &validId, 1, "matrix array size mismatch was not rejected atomically"))
        return false;

    uint32_t excessiveIds[8193]{};
    if (!RejectAtomically(geometry, matrices, excessiveIds, 8193, "sample count over 8192 was not rejected atomically"))
        return false;
    return true;
}

/**
 * 空の選択を成功扱いし、結果を空にする。
 */
bool CheckEmptySelection()
{
    FGeometry geometry;
    gk::Array<FGeometry::FMatrix> matrices;
    gk::Array<FVector3d> output;
    gk::String error;
    const FVector3d sentinel = { { 1.0, 2.0, 3.0 } };
    if (!output.Append(sentinel) || !gk::model::animation::EvaluateModelSkinPoints(geometry, matrices, nullptr, 0, output, error) || output.Count() != 0 || !error.Empty())
        return Fail("empty skin point selection did not clear output successfully");
    return true;
}

}

int main()
{
    if (!CheckPointEvaluation() || !CheckInvalidInputs() || !CheckEmptySelection())
        return 1;
    return 0;
}
