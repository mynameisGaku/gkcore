// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSkinPoints.h"

#include <math.h>

namespace gk::model::animation
{
namespace
{

/**
 * affine行列の全要素が有限か調べる。
 */
bool IsFiniteMatrix(const FModelGpuSkinningGeometry::FMatrix& matrix)
{
    // affine行列の各係数を検査する。
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
 * 頂点を含むsegmentのfallback clusterを返す。
 */
bool FindFallbackCluster(const FModelGpuSkinningGeometry& geometry, uint32_t positionId, uint32_t& clusterIndex)
{
    // 該当segmentが一つだけ見つかったか記録する。
    bool found = false;
    // 全segmentから頂点の所属先を探す。
    for (uint32_t segmentIndex = 0; segmentIndex < geometry.segments.Count(); ++segmentIndex)
    {
        // 位置・cluster範囲を持つ現在のsegment。
        const FModelGpuSkinningGeometry::FSegment& segment = geometry.segments.At(segmentIndex);
        if (positionId < segment.firstPosition || positionId - segment.firstPosition >= segment.positionCount)
        {
            continue;
        }
        if (found || segment.clusterCount == 0 || segment.firstCluster > geometry.clusters.Count() || segment.clusterCount > geometry.clusters.Count() - segment.firstCluster)
        {
            return false;
        }
        clusterIndex = segment.firstCluster + segment.clusterCount - 1;
        found = true;
    }
    return found;
}

}

bool EvaluateModelSkinPoints(const FModelGpuSkinningGeometry& geometry, const gk::Array<FModelGpuSkinningGeometry::FMatrix>& matrices, const uint32_t* ids, uint32_t count, gk::Array<gk::FVector3d>& output, gk::String& error)
{
    error.Clear();
    if (count > 8192u || (count != 0 && !ids) || geometry.influenceRanges.Count() != geometry.positions.Count() || matrices.Count() != geometry.clusters.Count())
    {
        error.Assign("model skin point input counts or ids are invalid");
        return false;
    }
    for (uint32_t segmentIndex = 0; segmentIndex < geometry.segments.Count(); ++segmentIndex)
    {
        // 位置とclusterの参照範囲が入力配列内か調べる。
        const FModelGpuSkinningGeometry::FSegment& segment = geometry.segments.At(segmentIndex);
        if (segment.firstPosition > geometry.positions.Count() || segment.positionCount > geometry.positions.Count() - segment.firstPosition || segment.firstCluster > geometry.clusters.Count() || segment.clusterCount > geometry.clusters.Count() - segment.firstCluster)
        {
            error.Assign("model skin point segment range is invalid");
            return false;
        }
    }

    // 全点の検査完了までoutputを変更しない仮配列。
    gk::Array<gk::FVector3d> candidate;
    if (!candidate.Reserve(count))
    {
        error.Assign("model skin point output allocation failed");
        return false;
    }
    for (uint32_t requestIndex = 0; requestIndex < count; ++requestIndex)
    {
        // 要求順で評価する位置番号。
        const uint32_t positionId = ids[requestIndex];
        if (positionId >= geometry.positions.Count())
        {
            error.Assign("model skin point id is out of range");
            return false;
        }
        // bind poseに記録された位置。
        const FModelGpuSkinningGeometry::FPosition& position = geometry.positions.At(positionId);
        // XYZ各成分の有限性を調べる。
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(position.value[axis]))
            {
                error.Assign("model skin point bind position is non-finite");
                return false;
            }
        }

        // 位置に対応する連続influence範囲。
        const FModelGpuSkinningGeometry::FInfluenceRange& range = geometry.influenceRanges.At(positionId);
        if (range.firstInfluence > geometry.influences.Count() || range.influenceCount > geometry.influences.Count() - range.firstInfluence)
        {
            error.Assign("model skin point influence range is invalid");
            return false;
        }

        // 全influenceの寄与を足し合わせるskin後位置。
        gk::FVector3d result{};
        // fallback判定に使うweight合計。
        double totalWeight = 0.0;
        // influence配列から位置の重みを順に読む。
        for (uint32_t influenceOffset = 0; influenceOffset < range.influenceCount; ++influenceOffset)
        {
            // 現在のbone clusterとweight。
            const FModelGpuSkinningGeometry::FInfluence& influence = geometry.influences.At(range.firstInfluence + influenceOffset);
            if (!isfinite(influence.weight) || influence.weight < 0.0 || influence.clusterIndex >= matrices.Count())
            {
                error.Assign("model skin point influence is invalid");
                return false;
            }
            // clusterに対応するpose別affine行列。
            const FModelGpuSkinningGeometry::FMatrix& matrix = matrices.At(influence.clusterIndex);
            if (!IsFiniteMatrix(matrix))
            {
                error.Assign("model skin point referenced matrix is non-finite");
                return false;
            }
            // influence weightを合計し、overflowも検出する。
            totalWeight += influence.weight;
            if (!isfinite(totalWeight))
            {
                error.Assign("model skin point total weight is non-finite");
                return false;
            }
            // affine行列の各出力軸へ位置を変換する。
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                // 行順3x4行列で現在の出力軸が始まる要素。
                const uint32_t row = axis * 4u;
                // weightを掛ける前の変換後座標。
                const double transformed = matrix.value[row] * position.value[0] + matrix.value[row + 1u] * position.value[1] + matrix.value[row + 2u] * position.value[2] + matrix.value[row + 3u];
                result.value[axis] += transformed * influence.weight;
                if (!isfinite(transformed) || !isfinite(result.value[axis]))
                {
                    error.Assign("model skin point result is non-finite");
                    return false;
                }
            }
        }
        if (!(totalWeight > 0.0))
        {
            // unweighted頂点用のmesh node変換cluster。
            uint32_t fallbackCluster = 0;
            if (!FindFallbackCluster(geometry, positionId, fallbackCluster) || fallbackCluster >= matrices.Count())
            {
                error.Assign("model skin point has no usable fallback cluster");
                return false;
            }
            // segmentに記録されたfallback affine行列。
            const FModelGpuSkinningGeometry::FMatrix& matrix = matrices.At(fallbackCluster);
            if (!IsFiniteMatrix(matrix))
            {
                error.Assign("model skin point fallback matrix is non-finite");
                return false;
            }
            // fallback行列でbind位置をmodel空間へ変換する。
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                // 行順3x4行列で現在の出力軸が始まる要素。
                const uint32_t row = axis * 4u;
                result.value[axis] = matrix.value[row] * position.value[0] + matrix.value[row + 1u] * position.value[1] + matrix.value[row + 2u] * position.value[2] + matrix.value[row + 3u];
                if (!isfinite(result.value[axis]))
                {
                    error.Assign("model skin point fallback result is non-finite");
                    return false;
                }
            }
        }
        if (!candidate.Append(result))
        {
            error.Assign("model skin point output allocation failed");
            return false;
        }
    }
    output.MoveFrom(candidate);
    error.Clear();
    return true;
}

}
