// SPDX-License-Identifier: NOASSERTION
#include "model/ModelNormals.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

namespace gk::model
{
namespace
{

/**
 * 三角形cornerの元頂点番号と生成面法線を記録する。
 */
struct FCornerNormal
{
    // 入力mesh上の頂点とcorner位置。
    uint32_t sourceIndex;
    uint32_t cornerIndex;
    // 安全に正規化した面法線。
    float normal[3];
};

/**
 * 参照頂点から面法線を作り、有限な単位長へ正規化する。
 */
bool CalculateFaceNormal(const Array<detail::ModelVertex>& vertices, const uint32_t indices[3], float output[3])
{
    const detail::ModelVertex& a = vertices.At(indices[0]);
    const detail::ModelVertex& b = vertices.At(indices[1]);
    const detail::ModelVertex& c = vertices.At(indices[2]);
    double edgeA[3]{};
    double edgeB[3]{};
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(a.position[axis]) || !isfinite(b.position[axis]) || !isfinite(c.position[axis]))
            return false;
        edgeA[axis] = static_cast<double>(b.position[axis]) - static_cast<double>(a.position[axis]);
        edgeB[axis] = static_cast<double>(c.position[axis]) - static_cast<double>(a.position[axis]);
    }
    // double差分から外積を求め、float座標の最大差でも中間値を保持する。
    const double cross[3] = { edgeA[1] * edgeB[2] - edgeA[2] * edgeB[1], edgeA[2] * edgeB[0] - edgeA[0] * edgeB[2], edgeA[0] * edgeB[1] - edgeA[1] * edgeB[0] };
    double largest = 0.0;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(cross[axis]))
            return false;
        const double magnitude = fabs(cross[axis]);
        if (magnitude > largest)
            largest = magnitude;
    }
    if (!(largest > 0.0))
        return false;
    double lengthSquared = 0.0;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        const double scaled = cross[axis] / largest;
        lengthSquared += scaled * scaled;
    }
    const double scaledLength = sqrt(lengthSquared);
    if (!(scaledLength > 0.0) || !isfinite(scaledLength))
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
        output[axis] = static_cast<float>((cross[axis] / largest) / scaledLength);
    return isfinite(output[0]) && isfinite(output[1]) && isfinite(output[2]);
}

/**
 * 元頂点番号と面法線でcornerを決定的に並べる。
 */
int CompareCornerNormals(const void* leftValue, const void* rightValue)
{
    const FCornerNormal& left = *static_cast<const FCornerNormal*>(leftValue);
    const FCornerNormal& right = *static_cast<const FCornerNormal*>(rightValue);
    if (left.sourceIndex != right.sourceIndex)
        return left.sourceIndex < right.sourceIndex ? -1 : 1;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (left.normal[axis] < right.normal[axis])
            return -1;
        if (left.normal[axis] > right.normal[axis])
            return 1;
    }
    return left.cornerIndex < right.cornerIndex ? -1 : left.cornerIndex > right.cornerIndex ? 1 : 0;
}

/**
 * 三成分の面法線が全成分で一致するか調べる。
 */
bool SameNormal(const float left[3], const float right[3])
{
    return left[0] == right[0] && left[1] == right[1] && left[2] == right[2];
}

/**
 * 三角形meshのindex構造と参照範囲を検証する。
 */
bool ValidateNormalInput(const Array<detail::ModelVertex>& vertices, const Array<uint32_t>& indices, String& error)
{
    if (vertices.Count() == 0 || indices.Count() == 0 || indices.Count() % 3 != 0)
    {
        error.Assign("Model normal input does not contain complete triangles");
        return false;
    }
    for (uint32_t corner = 0; corner < indices.Count(); ++corner)
    {
        if (indices.At(corner) >= vertices.Count())
        {
            error.Assign("Model normal input contains an invalid vertex index");
            return false;
        }
    }
    return true;
}

}

/**
 * 面法線を生成し、異なるflat normalが必要な頂点を分割する。
 */
bool GenerateModelNormals(const Array<detail::ModelVertex>& sourceVertices, const Array<uint32_t>& sourceIndices, uint32_t vertexLimit, Array<detail::ModelVertex>& outVertices, Array<uint32_t>& outIndices, String& error)
{
    if (!ValidateNormalInput(sourceVertices, sourceIndices, error))
        return false;
    Array<FCornerNormal> corners;
    if (!corners.Reserve(sourceIndices.Count()))
    {
        error.Assign("Model normal corner allocation failed");
        return false;
    }
    for (uint32_t first = 0; first < sourceIndices.Count(); first += 3)
    {
        // 面のindexとその向きからcornerに共通する平面法線を得る。
        const uint32_t faceIndices[3] = { sourceIndices.At(first), sourceIndices.At(first + 1), sourceIndices.At(first + 2) };
        float normal[3]{};
        if (!CalculateFaceNormal(sourceVertices, faceIndices, normal))
        {
            error.Assign("Model normal input contains a degenerate or non-finite triangle");
            return false;
        }
        for (uint32_t localCorner = 0; localCorner < 3; ++localCorner)
        {
            FCornerNormal corner{};
            corner.sourceIndex = faceIndices[localCorner];
            corner.cornerIndex = first + localCorner;
            for (uint32_t axis = 0; axis < 3; ++axis)
                corner.normal[axis] = normal[axis];
            if (!corners.Append(corner))
            {
                error.Assign("Model normal corner allocation failed");
                return false;
            }
        }
    }
    qsort(corners.Data(), corners.Count(), sizeof(FCornerNormal), CompareCornerNormals);

    Array<detail::ModelVertex> candidateVertices;
    Array<uint32_t> candidateIndices;
    if (!candidateIndices.Reserve(sourceIndices.Count()))
    {
        error.Assign("Model normal result allocation failed");
        return false;
    }
    for (uint32_t corner = 0; corner < sourceIndices.Count(); ++corner)
    {
        const uint32_t initialIndex = 0;
        if (!candidateIndices.Append(initialIndex))
        {
            error.Assign("Model normal result allocation failed");
            return false;
        }
    }
    uint32_t groupStart = 0;
    while (groupStart < corners.Count())
    {
        uint32_t groupEnd = groupStart + 1;
        while (groupEnd < corners.Count() && corners.At(groupEnd).sourceIndex == corners.At(groupStart).sourceIndex && SameNormal(corners.At(groupEnd).normal, corners.At(groupStart).normal))
            ++groupEnd;
        if (candidateVertices.Count() >= vertexLimit)
        {
            error.Assign("Model normal output exceeds the vertex limit");
            return false;
        }
        const uint32_t outputIndex = candidateVertices.Count();
        detail::ModelVertex vertex = sourceVertices.At(corners.At(groupStart).sourceIndex);
        for (uint32_t axis = 0; axis < 3; ++axis)
            vertex.normal[axis] = corners.At(groupStart).normal[axis];
        for (uint32_t component = 0; component < 4; ++component)
            vertex.tangent[component] = 0.0f;
        if (!candidateVertices.Append(vertex))
        {
            error.Assign("Model normal result allocation failed");
            return false;
        }
        for (uint32_t index = groupStart; index < groupEnd; ++index)
            candidateIndices.At(corners.At(index).cornerIndex) = outputIndex;
        groupStart = groupEnd;
    }
    outVertices.MoveFrom(candidateVertices);
    outIndices.MoveFrom(candidateIndices);
    error.Clear();
    return true;
}

}
