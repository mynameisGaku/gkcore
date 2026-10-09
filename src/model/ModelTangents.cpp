// SPDX-License-Identifier: NOASSERTION
#include "model/ModelTangents.h"

#include <math.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

extern "C"
{
#include <mikktspace/mikktspace.h>
}

namespace gk::model
{
namespace
{

/**
 * MikkTSpaceへ渡す、数値範囲を整えた頂点情報。
 */
struct FMikkVertex
{
    // MikkTSpace用に平行移動・縮尺を整えた位置。
    float position[3];
    // 単位長へ正規化した入力法線。
    float normal[3];
    // 法線画像用UVの共通縮尺版。
    float uv[2];
};

/**
 * 1つの三角形cornerへMikkTSpaceが返す接線。
 */
struct FMikkTangent
{
    // 直交化済み接線方向とhandedness。
    float value[4];
    // callbackが結果を設定したか。
    bool written;
};

/**
 * 生成後にsource vertex単位で接線をまとめるcorner記録。
 */
struct FCornerTangent
{
    // 元頂点番号と出力corner番号。
    uint32_t sourceIndex;
    uint32_t cornerIndex;
    // このcornerが使う接線frame。
    float tangent[4];
};

/**
 * MikkTSpace callbackが読む入力配列と書くcorner結果。
 */
struct FMikkContext
{
    // 元meshのindex列。
    const Array<uint32_t>* indices;
    // MikkTSpace用に整えた入力。
    const Array<FMikkVertex>* vertices;
    // 三角形cornerごとの接線出力。
    Array<FMikkTangent>* tangents;
};

/**
 * MikkTSpace入力で実際に参照される元頂点を記録する。
 */
struct FSourceVertexUse
{
    // 少なくとも一つのtriangle cornerから参照される場合はtrue。
    uint8_t used;
};

/**
 * finiteな三成分を、double精度で単位長へ整える。
 */
bool Normalize(const float source[3], float output[3])
{
    double largest = 0.0;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(source[axis]))
            return false;
        const double component = fabs(static_cast<double>(source[axis]));
        if (component > largest)
            largest = component;
    }
    if (largest == 0.0)
        return false;
    double squared = 0.0;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        const double component = static_cast<double>(source[axis]) / largest;
        squared += component * component;
    }
    const double inverseLength = 1.0 / sqrt(squared);
    for (uint32_t axis = 0; axis < 3; ++axis)
        output[axis] = static_cast<float>((static_cast<double>(source[axis]) / largest) * inverseLength);
    return true;
}

/**
 * 面数をMikkTSpaceのint引数で表せる範囲に保つ。
 */
bool ValidateTriangles(const Array<detail::ModelVertex>& vertices, const Array<uint32_t>& indices, String& error)
{
    if (vertices.Count() == 0 || indices.Count() == 0 || indices.Count() % 3 != 0 || vertices.Count() > static_cast<uint32_t>(INT_MAX) || indices.Count() / 3 > static_cast<uint32_t>(INT_MAX / 3))
    {
        error.Assign("Model tangent input has an unsupported triangle count");
        return false;
    }
    for (uint32_t corner = 0; corner < indices.Count(); ++corner)
    {
        if (indices.At(corner) >= vertices.Count())
        {
            error.Assign("Model tangent input contains an invalid vertex index");
            return false;
        }
    }
    return true;
}

/**
 * 正規化した三角形の面積とUV面積が0でないことを確認する。
 */
bool ValidateNondegenerateTriangles(const Array<FMikkVertex>& vertices, const Array<uint32_t>& indices, String& error)
{
    for (uint32_t first = 0; first < indices.Count(); first += 3)
    {
        const FMikkVertex& a = vertices.At(indices.At(first));
        const FMikkVertex& b = vertices.At(indices.At(first + 1));
        const FMikkVertex& c = vertices.At(indices.At(first + 2));
        const double e1[3] = { static_cast<double>(b.position[0]) - a.position[0], static_cast<double>(b.position[1]) - a.position[1], static_cast<double>(b.position[2]) - a.position[2] };
        const double e2[3] = { static_cast<double>(c.position[0]) - a.position[0], static_cast<double>(c.position[1]) - a.position[1], static_cast<double>(c.position[2]) - a.position[2] };
        const double cross[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        const double areaSquared = cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2];
        const double du1 = static_cast<double>(b.uv[0]) - a.uv[0];
        const double dv1 = static_cast<double>(b.uv[1]) - a.uv[1];
        const double du2 = static_cast<double>(c.uv[0]) - a.uv[0];
        const double dv2 = static_cast<double>(c.uv[1]) - a.uv[1];
        const double uvDeterminant = du1 * dv2 - dv1 * du2;
        if (!(areaSquared > 0.0) || !isfinite(areaSquared) || uvDeterminant == 0.0 || !isfinite(uvDeterminant))
        {
            error.Assign("Model tangent input contains a degenerate triangle or UV mapping");
            return false;
        }
    }
    return true;
}

/**
 * MikkTSpaceに渡す面数を返す。
 */
int GetFaceCount(const SMikkTSpaceContext* context)
{
    const FMikkContext* data = static_cast<const FMikkContext*>(context->m_pUserData);
    return static_cast<int>(data->indices->Count() / 3);
}

/**
 * 三角形の頂点数を返す。
 */
int GetFaceVertexCount(const SMikkTSpaceContext*, const int)
{
    return 3;
}

/**
 * face cornerの正規化位置を返す。
 */
void GetPosition(const SMikkTSpaceContext* context, float output[], const int face, const int vertex)
{
    const FMikkContext* data = static_cast<const FMikkContext*>(context->m_pUserData);
    const uint32_t corner = static_cast<uint32_t>(face) * 3u + static_cast<uint32_t>(vertex);
    const FMikkVertex& source = data->vertices->At(data->indices->At(corner));
    for (uint32_t axis = 0; axis < 3; ++axis)
        output[axis] = source.position[axis];
}

/**
 * face cornerの単位法線を返す。
 */
void GetNormal(const SMikkTSpaceContext* context, float output[], const int face, const int vertex)
{
    const FMikkContext* data = static_cast<const FMikkContext*>(context->m_pUserData);
    const uint32_t corner = static_cast<uint32_t>(face) * 3u + static_cast<uint32_t>(vertex);
    const FMikkVertex& source = data->vertices->At(data->indices->At(corner));
    for (uint32_t axis = 0; axis < 3; ++axis)
        output[axis] = source.normal[axis];
}

/**
 * face cornerの法線画像UVを返す。
 */
void GetTexCoord(const SMikkTSpaceContext* context, float output[], const int face, const int vertex)
{
    const FMikkContext* data = static_cast<const FMikkContext*>(context->m_pUserData);
    const uint32_t corner = static_cast<uint32_t>(face) * 3u + static_cast<uint32_t>(vertex);
    const FMikkVertex& source = data->vertices->At(data->indices->At(corner));
    output[0] = source.uv[0];
    output[1] = source.uv[1];
}

/**
 * MikkTSpaceの接線と符号をcorner結果へ記録する。
 */
void SetTangent(const SMikkTSpaceContext* context, const float tangent[], const float sign, const int face, const int vertex)
{
    FMikkContext* data = static_cast<FMikkContext*>(context->m_pUserData);
    const uint32_t corner = static_cast<uint32_t>(face) * 3u + static_cast<uint32_t>(vertex);
    FMikkTangent& output = data->tangents->At(corner);
    for (uint32_t axis = 0; axis < 3; ++axis)
        output.value[axis] = tangent[axis];
    output.value[3] = sign;
    output.written = true;
}

/**
 * 同じsource頂点の接線variantを決定的な順で並べる。
 */
int CompareCornerTangents(const void* leftValue, const void* rightValue)
{
    const FCornerTangent& left = *static_cast<const FCornerTangent*>(leftValue);
    const FCornerTangent& right = *static_cast<const FCornerTangent*>(rightValue);
    if (left.sourceIndex != right.sourceIndex)
        return left.sourceIndex < right.sourceIndex ? -1 : 1;
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (left.tangent[component] < right.tangent[component])
            return -1;
        if (left.tangent[component] > right.tangent[component])
            return 1;
    }
    return left.cornerIndex < right.cornerIndex ? -1 : left.cornerIndex > right.cornerIndex ? 1 : 0;
}

/**
 * 二つの接線frameが全成分で一致するか調べる。
 */
bool SameTangent(const float left[4], const float right[4])
{
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (left[component] != right[component])
            return false;
    }
    return true;
}

}

/**
 * MikkTSpace接線を生成し、異なるframeを必要とする頂点だけ分割する。
 */
bool GenerateModelTangents(const Array<detail::ModelVertex>& sourceVertices, const Array<uint32_t>& sourceIndices, uint32_t vertexLimit, Array<detail::ModelVertex>& outputVertices, Array<uint32_t>& outputIndices, String& error)
{
    if (!ValidateTriangles(sourceVertices, sourceIndices, error))
        return false;
    Array<FMikkVertex> normalizedVertices;
    Array<FSourceVertexUse> sourceUse;
    double positionMinimum[3] = { DBL_MAX, DBL_MAX, DBL_MAX };
    double positionScale = 0.0;
    double uvMinimum[2] = { DBL_MAX, DBL_MAX };
    double uvScale = 0.0;
    if (!sourceUse.Reserve(sourceVertices.Count()))
    {
        error.Assign("Model tangent input allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < sourceVertices.Count(); ++index)
    {
        const FSourceVertexUse unused{};
        if (!sourceUse.Append(unused))
        {
            error.Assign("Model tangent input allocation failed");
            return false;
        }
    }
    for (uint32_t corner = 0; corner < sourceIndices.Count(); ++corner)
    {
        const uint32_t sourceIndex = sourceIndices.At(corner);
        sourceUse.At(sourceIndex).used = 1;
        const detail::ModelVertex& source = sourceVertices.At(sourceIndex);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(source.position[axis]))
            {
                error.Assign("Model tangent input contains a non-finite position");
                return false;
            }
            const double value = source.position[axis];
            if (value < positionMinimum[axis])
                positionMinimum[axis] = value;
        }
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            if (!isfinite(source.normalUv[axis]))
            {
                error.Assign("Model tangent input contains a non-finite normal UV");
                return false;
            }
            const double value = source.normalUv[axis];
            if (value < uvMinimum[axis])
                uvMinimum[axis] = value;
        }
    }
    double positionMaximum[3] = { -DBL_MAX, -DBL_MAX, -DBL_MAX };
    double uvMaximum[2] = { -DBL_MAX, -DBL_MAX };
    for (uint32_t index = 0; index < sourceVertices.Count(); ++index)
    {
        if (!sourceUse.At(index).used)
            continue;
        const detail::ModelVertex& source = sourceVertices.At(index);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const double value = source.position[axis];
            if (value > positionMaximum[axis])
                positionMaximum[axis] = value;
        }
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            const double value = source.normalUv[axis];
            if (value > uvMaximum[axis])
                uvMaximum[axis] = value;
        }
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        const double extent = positionMaximum[axis] - positionMinimum[axis];
        if (extent > positionScale)
            positionScale = extent;
    }
    for (uint32_t axis = 0; axis < 2; ++axis)
    {
        const double extent = uvMaximum[axis] - uvMinimum[axis];
        if (extent > uvScale)
            uvScale = extent;
    }
    if (!(positionScale > 0.0) || !(uvScale > 0.0) || !isfinite(positionScale) || !isfinite(uvScale))
    {
        error.Assign("Model tangent input has a degenerate position or UV range");
        return false;
    }
    if (!normalizedVertices.Reserve(sourceVertices.Count()))
    {
        error.Assign("Model tangent input allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < sourceVertices.Count(); ++index)
    {
        const detail::ModelVertex& source = sourceVertices.At(index);
        FMikkVertex normalized{};
        if (!sourceUse.At(index).used)
        {
            if (!normalizedVertices.Append(normalized))
            {
                error.Assign("Model tangent input allocation failed");
                return false;
            }
            continue;
        }
        if (!Normalize(source.normal, normalized.normal))
        {
            error.Assign("Model tangent input contains a zero or invalid normal");
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(source.position[axis]))
            {
                error.Assign("Model tangent input contains a non-finite position");
                return false;
            }
            const double value = (static_cast<double>(source.position[axis]) - positionMinimum[axis]) / positionScale;
            if (!isfinite(value) || fabs(value) > 1.0)
            {
                error.Assign("Model tangent input position normalization failed");
                return false;
            }
            normalized.position[axis] = static_cast<float>(value);
        }
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            if (!isfinite(source.normalUv[axis]))
            {
                error.Assign("Model tangent input contains a non-finite normal UV");
                return false;
            }
            const double value = (static_cast<double>(source.normalUv[axis]) - uvMinimum[axis]) / uvScale;
            if (!isfinite(value) || fabs(value) > 1.0)
            {
                error.Assign("Model tangent input UV normalization failed");
                return false;
            }
            normalized.uv[axis] = static_cast<float>(value);
        }
        if (!normalizedVertices.Append(normalized))
        {
            error.Assign("Model tangent input allocation failed");
            return false;
        }
    }
    if (!ValidateNondegenerateTriangles(normalizedVertices, sourceIndices, error))
        return false;

    Array<FMikkTangent> tangents;
    if (!tangents.Reserve(sourceIndices.Count()))
    {
        error.Assign("Model tangent output allocation failed");
        return false;
    }
    for (uint32_t corner = 0; corner < sourceIndices.Count(); ++corner)
    {
        const FMikkTangent empty{};
        if (!tangents.Append(empty))
        {
            error.Assign("Model tangent output allocation failed");
            return false;
        }
    }
    FMikkContext mikkData{ &sourceIndices, &normalizedVertices, &tangents };
    SMikkTSpaceInterface interface{};
    interface.m_getNumFaces = GetFaceCount;
    interface.m_getNumVerticesOfFace = GetFaceVertexCount;
    interface.m_getPosition = GetPosition;
    interface.m_getNormal = GetNormal;
    interface.m_getTexCoord = GetTexCoord;
    interface.m_setTSpaceBasic = SetTangent;
    SMikkTSpaceContext context{ &interface, &mikkData };
    if (!genTangSpaceDefault(&context))
    {
        error.Assign("MikkTSpace could not generate model tangents");
        return false;
    }

    Array<FCornerTangent> corners;
    if (!corners.Reserve(sourceIndices.Count()))
    {
        error.Assign("Model tangent corner allocation failed");
        return false;
    }
    for (uint32_t corner = 0; corner < sourceIndices.Count(); ++corner)
    {
        const FMikkTangent& tangent = tangents.At(corner);
        const uint32_t sourceIndex = sourceIndices.At(corner);
        if (!tangent.written || !isfinite(tangent.value[0]) || !isfinite(tangent.value[1]) || !isfinite(tangent.value[2]) || (tangent.value[3] != -1.0f && tangent.value[3] != 1.0f))
        {
            error.Assign("MikkTSpace returned an invalid model tangent");
            return false;
        }
        const FMikkVertex& source = normalizedVertices.At(sourceIndex);
        double dot = 0.0;
        double lengthSquared = 0.0;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            dot += static_cast<double>(source.normal[axis]) * tangent.value[axis];
            lengthSquared += static_cast<double>(tangent.value[axis]) * tangent.value[axis];
        }
        if (!isfinite(dot) || !isfinite(lengthSquared) || fabs(dot) > 0.001 || fabs(lengthSquared - 1.0) > 0.002)
        {
            error.Assign("MikkTSpace returned a non-orthonormal model tangent");
            return false;
        }
        FCornerTangent record{};
        record.sourceIndex = sourceIndex;
        record.cornerIndex = corner;
        for (uint32_t axis = 0; axis < 4; ++axis)
            record.tangent[axis] = tangent.value[axis];
        if (!corners.Append(record))
        {
            error.Assign("Model tangent corner allocation failed");
            return false;
        }
    }
    qsort(corners.Data(), corners.Count(), sizeof(FCornerTangent), CompareCornerTangents);

    Array<detail::ModelVertex> candidateVertices;
    Array<uint32_t> candidateIndices;
    if (!candidateIndices.Reserve(sourceIndices.Count()))
    {
        error.Assign("Model tangent result allocation failed");
        return false;
    }
    for (uint32_t corner = 0; corner < sourceIndices.Count(); ++corner)
    {
        const uint32_t emptyIndex = 0;
        if (!candidateIndices.Append(emptyIndex))
        {
            error.Assign("Model tangent result allocation failed");
            return false;
        }
    }
    uint32_t groupStart = 0;
    while (groupStart < corners.Count())
    {
        uint32_t groupEnd = groupStart + 1;
        while (groupEnd < corners.Count() && corners.At(groupEnd).sourceIndex == corners.At(groupStart).sourceIndex && SameTangent(corners.At(groupEnd).tangent, corners.At(groupStart).tangent))
            ++groupEnd;
        const uint32_t sourceIndex = corners.At(groupStart).sourceIndex;
        uint32_t outputIndex = candidateVertices.Count();
        detail::ModelVertex copy = sourceVertices.At(sourceIndex);
        for (uint32_t component = 0; component < 4; ++component)
            copy.tangent[component] = corners.At(groupStart).tangent[component];
        if (candidateVertices.Count() >= vertexLimit)
        {
            error.Assign("Model tangent output exceeds the vertex limit");
            return false;
        }
        if (!candidateVertices.Append(copy))
        {
            error.Assign("Model tangent result allocation failed");
            return false;
        }
        for (uint32_t i = groupStart; i < groupEnd; ++i)
            candidateIndices.At(corners.At(i).cornerIndex) = outputIndex;
        groupStart = groupEnd;
    }
    if (candidateVertices.Count() > vertexLimit)
    {
        error.Assign("Model tangent output exceeds the vertex limit");
        return false;
    }
    outputVertices.MoveFrom(candidateVertices);
    outputIndices.MoveFrom(candidateIndices);
    error.Clear();
    return true;
}

}
