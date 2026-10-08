// SPDX-License-Identifier: NOASSERTION
#include "../src/model/ModelNormals.h"
#include "../src/foundation/Memory.h"

#include <math.h>
#include <stdio.h>

namespace
{

/**
 * テスト失敗の理由を保存する。
 */
bool Fail(gk::String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * 法線生成入力用の属性付き頂点を作る。
 */
gk::detail::ModelVertex MakeVertex(float x, float y, float z, float u, float v)
{
    gk::detail::ModelVertex vertex{};
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = z;
    vertex.uv[0] = u;
    vertex.uv[1] = v;
    vertex.metallicRoughnessUv[0] = 0.2f + u;
    vertex.metallicRoughnessUv[1] = 0.3f + v;
    vertex.normalUv[0] = 0.4f + u;
    vertex.normalUv[1] = 0.5f + v;
    vertex.emissiveUv[0] = 0.6f + u;
    vertex.emissiveUv[1] = 0.7f + v;
    vertex.occlusionUv[0] = 0.8f + u;
    vertex.occlusionUv[1] = 0.9f + v;
    vertex.tangent[0] = 1.0f;
    vertex.tangent[1] = 2.0f;
    vertex.tangent[2] = 3.0f;
    vertex.tangent[3] = -1.0f;
    return vertex;
}

/**
 * 平面の向きに合う単位法線、UV保持、接線初期化を調べる。
 */
bool TestFlatTriangle(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> sourceVertices;
    gk::Array<uint32_t> sourceIndices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    const gk::detail::ModelVertex source[3] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f) };
    const uint32_t indices[3] = { 0, 2, 1 };
    if (!sourceVertices.AppendRange(source, 3) || !sourceIndices.AppendRange(indices, 3))
        return Fail(failure, "flat normal fixture allocation failed");
    if (!gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 8, outputVertices, outputIndices, error))
        return Fail(failure, error.CStr());
    if (outputVertices.Count() != 3 || outputIndices.Count() != 3)
        return Fail(failure, "flat normal output changed triangle counts");
    for (uint32_t corner = 0; corner < outputIndices.Count(); ++corner)
    {
        const gk::detail::ModelVertex& vertex = outputVertices.At(outputIndices.At(corner));
        const gk::detail::ModelVertex& original = source[sourceIndices.At(corner)];
        if (fabsf(vertex.normal[0]) > 0.0001f || fabsf(vertex.normal[1]) > 0.0001f || fabsf(vertex.normal[2] + 1.0f) > 0.0001f)
            return Fail(failure, "flat triangle normal has the wrong orientation");
        for (uint32_t coordinate = 0; coordinate < 2; ++coordinate)
        {
            if (vertex.uv[coordinate] != original.uv[coordinate] || vertex.metallicRoughnessUv[coordinate] != original.metallicRoughnessUv[coordinate] || vertex.normalUv[coordinate] != original.normalUv[coordinate] || vertex.emissiveUv[coordinate] != original.emissiveUv[coordinate] || vertex.occlusionUv[coordinate] != original.occlusionUv[coordinate])
                return Fail(failure, "flat normal generation changed a texture coordinate");
        }
        if (vertex.tangent[0] != 0.0f || vertex.tangent[1] != 0.0f || vertex.tangent[2] != 0.0f || vertex.tangent[3] != 0.0f)
            return Fail(failure, "generated flat-normal vertex retained an authored tangent");
    }
    return true;
}

/**
 * 折れ目の共有頂点を面法線ごとに分割する。
 */
bool TestSharpEdgeSplit(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> sourceVertices;
    gk::Array<uint32_t> sourceIndices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    const gk::detail::ModelVertex source[4] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f), MakeVertex(0.0f, 0.0f, 1.0f, 1.0f, 1.0f) };
    const uint32_t indices[6] = { 0, 1, 2, 0, 1, 3 };
    if (!sourceVertices.AppendRange(source, 4) || !sourceIndices.AppendRange(indices, 6))
        return Fail(failure, "sharp edge fixture allocation failed");
    if (!gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 8, outputVertices, outputIndices, error))
        return Fail(failure, error.CStr());
    if (outputVertices.Count() != 6 || outputIndices.Count() != 6 || outputIndices.At(0) == outputIndices.At(3) || outputIndices.At(1) == outputIndices.At(4))
        return Fail(failure, "sharp edge vertices were not split");
    for (uint32_t corner = 0; corner < 3; ++corner)
    {
        const gk::detail::ModelVertex& firstFace = outputVertices.At(outputIndices.At(corner));
        const gk::detail::ModelVertex& secondFace = outputVertices.At(outputIndices.At(corner + 3));
        if (fabsf(firstFace.normal[2] - 1.0f) > 0.0001f || fabsf(secondFace.normal[1] + 1.0f) > 0.0001f)
            return Fail(failure, "sharp edge split did not preserve both flat normals");
    }
    return true;
}

/**
 * 同じ平面上の共有頂点を再利用し、反転wind順で法線を反転する。
 */
bool TestCoplanarReuseAndWinding(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> sourceVertices;
    gk::Array<uint32_t> sourceIndices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    const gk::detail::ModelVertex source[4] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f), MakeVertex(1.0f, 1.0f, 0.0f, 1.0f, 1.0f), MakeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f) };
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    if (!sourceVertices.AppendRange(source, 4) || !sourceIndices.AppendRange(indices, 6))
        return Fail(failure, "coplanar fixture allocation failed");
    if (!gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 8, outputVertices, outputIndices, error))
        return Fail(failure, error.CStr());
    if (outputVertices.Count() != 4 || outputIndices.At(0) != outputIndices.At(3) || outputIndices.At(2) != outputIndices.At(4))
        return Fail(failure, "coplanar shared vertices were unnecessarily split");
    gk::Array<uint32_t> reorderedSourceIndices;
    gk::Array<gk::detail::ModelVertex> reorderedVertices;
    gk::Array<uint32_t> reorderedOutputIndices;
    const uint32_t reordered[6] = { 0, 2, 3, 0, 1, 2 };
    if (!reorderedSourceIndices.AppendRange(reordered, 6) || !gk::model::GenerateModelNormals(sourceVertices, reorderedSourceIndices, 8, reorderedVertices, reorderedOutputIndices, error))
        return Fail(failure, error.CStr());
    if (reorderedVertices.Count() != outputVertices.Count() || reorderedOutputIndices.Count() != outputIndices.Count())
        return Fail(failure, "coplanar face order changed output counts");
    for (uint32_t index = 0; index < outputVertices.Count(); ++index)
    {
        if (outputVertices.At(index).normal[0] != reorderedVertices.At(index).normal[0] || outputVertices.At(index).normal[1] != reorderedVertices.At(index).normal[1] || outputVertices.At(index).normal[2] != reorderedVertices.At(index).normal[2])
            return Fail(failure, "coplanar face order changed a generated normal");
    }
    sourceIndices.Clear();
    const uint32_t reversed[3] = { 0, 2, 1 };
    if (!sourceIndices.AppendRange(reversed, 3))
        return Fail(failure, "winding fixture allocation failed");
    gk::Array<gk::detail::ModelVertex> reversedVertices;
    gk::Array<uint32_t> reversedOutputIndices;
    if (!gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 8, reversedVertices, reversedOutputIndices, error))
        return Fail(failure, error.CStr());
    if (fabsf(reversedVertices.At(0).normal[2] + 1.0f) > 0.0001f)
        return Fail(failure, "reversed triangle winding did not reverse its normal");
    return true;
}

/**
 * 大小の極端な座標でも面法線を有限な単位長にする。
 */
bool TestExtremeScales(gk::String& failure)
{
    const float scales[2] = { 1.0e30f, 1.0e-30f };
    for (uint32_t scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    {
        const float scale = scales[scaleIndex];
        gk::Array<gk::detail::ModelVertex> sourceVertices;
        gk::Array<uint32_t> sourceIndices;
        gk::Array<gk::detail::ModelVertex> outputVertices;
        gk::Array<uint32_t> outputIndices;
        gk::String error;
        const gk::detail::ModelVertex source[3] = { MakeVertex(-scale, -scale, 0.0f, 0.0f, 0.0f), MakeVertex(scale, -scale, 0.0f, 1.0f, 0.0f), MakeVertex(-scale, scale, 0.0f, 0.0f, 1.0f) };
        const uint32_t indices[3] = { 0, 1, 2 };
        if (!sourceVertices.AppendRange(source, 3) || !sourceIndices.AppendRange(indices, 3))
            return Fail(failure, "extreme scale fixture allocation failed");
        if (!gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 4, outputVertices, outputIndices, error))
            return Fail(failure, error.CStr());
        for (uint32_t index = 0; index < outputVertices.Count(); ++index)
        {
            const gk::detail::ModelVertex& vertex = outputVertices.At(index);
            if (!isfinite(vertex.normal[0]) || !isfinite(vertex.normal[1]) || !isfinite(vertex.normal[2]) || fabsf(vertex.normal[2] - 1.0f) > 0.0001f)
                return Fail(failure, "extreme scale produced an invalid face normal");
        }
    }
    return true;
}

/**
 * 未参照異常頂点を除き、不正入力時に既存出力を保つ。
 */
bool TestUnusedAndAtomicFailure(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> sourceVertices;
    gk::Array<uint32_t> sourceIndices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    gk::detail::ModelVertex source[4] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f), MakeVertex(NAN, INFINITY, 0.0f, NAN, INFINITY) };
    source[3].normal[0] = NAN;
    const uint32_t indices[3] = { 0, 1, 2 };
    if (!sourceVertices.AppendRange(source, 4) || !sourceIndices.AppendRange(indices, 3))
        return Fail(failure, "unused vertex fixture allocation failed");
    if (!gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 3, outputVertices, outputIndices, error))
        return Fail(failure, error.CStr());
    if (outputVertices.Count() != 3 || outputIndices.Count() != 3 || sourceVertices.Count() != 4 || !isnan(sourceVertices.At(3).position[0]))
        return Fail(failure, "unused source vertex was not pruned or input was changed");

    gk::Array<gk::detail::ModelVertex> invalidVertices;
    gk::Array<uint32_t> invalidIndices;
    gk::Array<gk::detail::ModelVertex> sentinelVertices;
    gk::Array<uint32_t> sentinelIndices;
    const gk::detail::ModelVertex degenerate[3] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f), MakeVertex(2.0f, 0.0f, 0.0f, 0.0f, 1.0f) };
    const uint32_t degenerateIndices[3] = { 0, 1, 2 };
    const gk::detail::ModelVertex sentinel = MakeVertex(9.0f, 8.0f, 7.0f, 0.1f, 0.2f);
    const uint32_t sentinelIndex = 19;
    if (!invalidVertices.AppendRange(degenerate, 3) || !invalidIndices.AppendRange(degenerateIndices, 3) || !sentinelVertices.Append(sentinel) || !sentinelIndices.Append(sentinelIndex))
        return Fail(failure, "atomic failure fixture allocation failed");
    if (!error.Assign("preallocated diagnostic storage for injected failures"))
        return Fail(failure, "atomic failure diagnostic setup failed");
    if (gk::model::GenerateModelNormals(invalidVertices, invalidIndices, 8, sentinelVertices, sentinelIndices, error))
        return Fail(failure, "zero-area triangle unexpectedly generated a normal");
    if (sentinelVertices.Count() != 1 || sentinelVertices.At(0).position[0] != sentinel.position[0] || sentinelIndices.Count() != 1 || sentinelIndices.At(0) != sentinelIndex || error.Empty())
        return Fail(failure, "invalid normal input changed output or omitted its diagnostic");
    return true;
}

/**
 * 頂点上限と不正indexを拒否し、入力と既存出力を保つ。
 */
bool TestInputAndLimitFailures(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> sourceVertices;
    gk::Array<uint32_t> sourceIndices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    const gk::detail::ModelVertex source[4] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f), MakeVertex(0.0f, 0.0f, 1.0f, 1.0f, 1.0f) };
    const uint32_t sharpIndices[6] = { 0, 1, 2, 0, 1, 3 };
    const gk::detail::ModelVertex sentinel = MakeVertex(9.0f, 8.0f, 7.0f, 0.1f, 0.2f);
    const uint32_t sentinelIndex = 19;
    if (!sourceVertices.AppendRange(source, 4) || !sourceIndices.AppendRange(sharpIndices, 6) || !outputVertices.Append(sentinel) || !outputIndices.Append(sentinelIndex))
        return Fail(failure, "normal limit fixture allocation failed");

    // この折れ面には面法線ごとに6頂点が必要となる。
    if (gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 5, outputVertices, outputIndices, error))
        return Fail(failure, "normal generation exceeded its vertex limit");
    if (outputVertices.Count() != 1 || outputVertices.At(0).position[0] != sentinel.position[0] || outputVertices.At(0).normal[0] != sentinel.normal[0] || outputIndices.Count() != 1 || outputIndices.At(0) != sentinelIndex || error.Empty() || sourceVertices.Count() != 4 || sourceVertices.At(3).position[2] != 1.0f)
        return Fail(failure, "vertex-limit rejection changed source or existing outputs");

    sourceIndices.Clear();
    error.Clear();
    if (gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 8, outputVertices, outputIndices, error))
        return Fail(failure, "empty index input unexpectedly generated normals");
    if (outputVertices.Count() != 1 || outputVertices.At(0).normal[0] != sentinel.normal[0] || outputIndices.Count() != 1 || outputIndices.At(0) != sentinelIndex || error.Empty())
        return Fail(failure, "empty-index rejection changed existing outputs");

    const uint32_t outOfRangeIndices[3] = { 0, 1, 4 };
    if (!sourceIndices.AppendRange(outOfRangeIndices, 3))
        return Fail(failure, "invalid-index fixture allocation failed");
    error.Clear();
    if (gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 8, outputVertices, outputIndices, error))
        return Fail(failure, "out-of-range source index unexpectedly generated normals");
    if (outputVertices.Count() != 1 || outputVertices.At(0).normal[0] != sentinel.normal[0] || outputIndices.Count() != 1 || outputIndices.At(0) != sentinelIndex || error.Empty())
        return Fail(failure, "invalid-index rejection changed existing outputs");
    return true;
}

/**
 * foundation割当を失敗させても出力を保ち、成功境界まで到達する。
 */
bool TestAllocationFailures(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> sourceVertices;
    gk::Array<uint32_t> sourceIndices;
    const gk::detail::ModelVertex source[3] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f) };
    const uint32_t indices[3] = { 0, 1, 2 };
    if (!sourceVertices.AppendRange(source, 3) || !sourceIndices.AppendRange(indices, 3))
        return Fail(failure, "allocation failure source setup failed");
    uint32_t failures = 0;
    bool completed = false;
    for (uint32_t allowed = 0; allowed < 64; ++allowed)
    {
        gk::Array<gk::detail::ModelVertex> outputVertices;
        gk::Array<uint32_t> outputIndices;
        gk::String error;
        gk::detail::ModelVertex sentinel = MakeVertex(9.0f, 8.0f, 7.0f, 0.2f, 0.3f);
        sentinel.normal[0] = 13.0f;
        const uint32_t sentinelIndex = 23;
        if (!outputVertices.Append(sentinel) || !outputIndices.Append(sentinelIndex) || !error.Assign("preallocated diagnostic capacity for injected foundation allocation failures"))
            return Fail(failure, "allocation failure sentinel setup failed");
        gk::SetAllocationFailureAfterForTesting(allowed);
        const bool succeeded = gk::model::GenerateModelNormals(sourceVertices, sourceIndices, 8, outputVertices, outputIndices, error);
        gk::ResetAllocationFailureForTesting();
        if (succeeded)
        {
            if (outputVertices.Count() != 3 || outputIndices.Count() != 3 || !error.Empty())
                return Fail(failure, "allocation success boundary returned invalid output");
            completed = true;
            break;
        }
        ++failures;
        if (outputVertices.Count() != 1 || outputVertices.At(0).normal[0] != 13.0f || outputIndices.Count() != 1 || outputIndices.At(0) != sentinelIndex || error.Empty())
            return Fail(failure, "allocation failure changed output or omitted its diagnostic");
    }
    gk::ResetAllocationFailureForTesting();
    if (!completed || failures < 3)
        return Fail(failure, "allocation failure sweep did not reach failure and success boundaries");
    return true;
}

}

int main()
{
    gk::String failure;
    if (!TestFlatTriangle(failure) || !TestSharpEdgeSplit(failure) || !TestCoplanarReuseAndWinding(failure) || !TestExtremeScales(failure) || !TestUnusedAndAtomicFailure(failure) || !TestInputAndLimitFailures(failure) || !TestAllocationFailures(failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    return 0;
}
