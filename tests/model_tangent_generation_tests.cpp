// SPDX-License-Identifier: NOASSERTION
#include "model/ModelTangents.h"
#include "foundation/Memory.h"

#include <math.h>
#include <stdio.h>

namespace
{

/**
 * テスト失敗を一つ記録する。
 */
bool Fail(gk::String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * 接線生成fixture用の頂点を作る。
 */
gk::detail::ModelVertex MakeVertex(float x, float y, float u, float v)
{
    gk::detail::ModelVertex vertex{};
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = 0.0f;
    vertex.normal[0] = 0.0f;
    vertex.normal[1] = 0.0f;
    vertex.normal[2] = 1.0f;
    vertex.uv[0] = u;
    vertex.uv[1] = v;
    vertex.metallicRoughnessUv[0] = 0.25f;
    vertex.metallicRoughnessUv[1] = 0.75f;
    vertex.normalUv[0] = u;
    vertex.normalUv[1] = v;
    vertex.emissiveUv[0] = 0.4f;
    vertex.emissiveUv[1] = 0.6f;
    vertex.occlusionUv[0] = 0.3f;
    vertex.occlusionUv[1] = 0.7f;
    return vertex;
}

/**
 * 平面三角形で標準方向の単位接線を生成できることを確かめる。
 */
bool TestPlanarTriangle(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> vertices;
    gk::Array<uint32_t> indices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    gk::detail::ModelVertex source[3] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 1.0f) };
    // 基本色座標を別向きにし、法線画像座標が選ばれることを区別する。
    source[1].uv[0] = 0.0f;
    source[1].uv[1] = 1.0f;
    source[2].uv[0] = 1.0f;
    source[2].uv[1] = 0.0f;
    const uint32_t sourceIndices[3] = { 0, 1, 2 };
    if (!vertices.AppendRange(source, 3) || !indices.AppendRange(sourceIndices, 3))
        return Fail(failure, "tangent fixture allocation failed");
    if (!gk::model::GenerateModelTangents(vertices, indices, 16, outputVertices, outputIndices, error))
        return Fail(failure, error.CStr());
    if (outputVertices.Count() != 3 || outputIndices.Count() != 3 || outputIndices.At(0) != 0 || outputIndices.At(1) != 1 || outputIndices.At(2) != 2)
        return Fail(failure, "planar tangent output topology changed");
    for (uint32_t i = 0; i < outputVertices.Count(); ++i)
    {
        const gk::detail::ModelVertex& vertex = outputVertices.At(i);
        if (fabsf(vertex.tangent[0] - 1.0f) > 0.0001f || fabsf(vertex.tangent[1]) > 0.0001f || fabsf(vertex.tangent[2]) > 0.0001f || vertex.tangent[3] != 1.0f)
            return Fail(failure, "planar tangent does not match the known frame");
        if (vertex.uv[0] != source[i].uv[0] || vertex.normalUv[1] != source[i].normalUv[1] || vertex.occlusionUv[0] != source[i].occlusionUv[0])
            return Fail(failure, "tangent generation changed another vertex attribute");
    }
    return true;
}

/**
 * 共有頂点で接線方向が分かれるときに頂点を複製する。
 */
bool TestMirroredCornerSplit(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> vertices;
    gk::Array<uint32_t> indices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    const gk::detail::ModelVertex source[4] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 1.0f), MakeVertex(-1.0f, 0.0f, 1.0f, 0.0f) };
    const uint32_t sourceIndices[6] = { 0, 1, 2, 0, 2, 3 };
    if (!vertices.AppendRange(source, 4) || !indices.AppendRange(sourceIndices, 6))
        return Fail(failure, "mirrored tangent fixture allocation failed");
    if (!gk::model::GenerateModelTangents(vertices, indices, 12, outputVertices, outputIndices, error))
        return Fail(failure, error.CStr());
    if (outputVertices.Count() <= vertices.Count() || outputIndices.Count() != indices.Count() || outputIndices.At(0) == outputIndices.At(3))
        return Fail(failure, "mirrored tangent seam was not split");
    for (uint32_t index = 0; index < outputVertices.Count(); ++index)
    {
        const gk::detail::ModelVertex& vertex = outputVertices.At(index);
        if (!isfinite(vertex.tangent[0]) || !isfinite(vertex.tangent[1]) || !isfinite(vertex.tangent[2]) || (vertex.tangent[3] != -1.0f && vertex.tangent[3] != 1.0f))
            return Fail(failure, "mirrored tangent output is invalid");
    }
    return true;
}

/**
 * 面の登録順を変えても共有平面の接線が変わらないことを確かめる。
 */
bool TestFaceOrderInvariance(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> vertices;
    gk::Array<uint32_t> firstIndices;
    gk::Array<uint32_t> reversedIndices;
    gk::Array<gk::detail::ModelVertex> firstVertices;
    gk::Array<gk::detail::ModelVertex> reversedVertices;
    gk::Array<uint32_t> firstOutputIndices;
    gk::Array<uint32_t> reversedOutputIndices;
    gk::String error;
    const gk::detail::ModelVertex source[4] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 1.0f, 0.0f), MakeVertex(1.0f, 1.0f, 1.0f, 1.0f), MakeVertex(0.0f, 1.0f, 0.0f, 1.0f) };
    const uint32_t ordered[6] = { 0, 1, 2, 0, 2, 3 };
    const uint32_t reversed[6] = { 0, 2, 3, 0, 1, 2 };
    if (!vertices.AppendRange(source, 4) || !firstIndices.AppendRange(ordered, 6) || !reversedIndices.AppendRange(reversed, 6))
        return Fail(failure, "face order fixture allocation failed");
    if (!gk::model::GenerateModelTangents(vertices, firstIndices, 8, firstVertices, firstOutputIndices, error) || !gk::model::GenerateModelTangents(vertices, reversedIndices, 8, reversedVertices, reversedOutputIndices, error))
        return Fail(failure, error.CStr());
    if (firstVertices.Count() != reversedVertices.Count() || firstVertices.Count() != 4)
        return Fail(failure, "face order changed the planar vertex groups");
    for (uint32_t vertexIndex = 0; vertexIndex < firstVertices.Count(); ++vertexIndex)
    {
        const gk::detail::ModelVertex& first = firstVertices.At(vertexIndex);
        const gk::detail::ModelVertex& second = reversedVertices.At(vertexIndex);
        for (uint32_t component = 0; component < 4; ++component)
        {
            if (first.tangent[component] != second.tangent[component])
                return Fail(failure, "face order changed a generated tangent");
        }
    }
    return true;
}

/**
 * 共有辺で角度加重された接線と面順序独立性を確かめる。
 */
bool TestWeightedSharedEdge(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> vertices;
    gk::Array<uint32_t> firstIndices;
    gk::Array<uint32_t> reversedIndices;
    gk::Array<gk::detail::ModelVertex> firstVertices;
    gk::Array<gk::detail::ModelVertex> reversedVertices;
    gk::Array<uint32_t> firstOutputIndices;
    gk::Array<uint32_t> reversedOutputIndices;
    gk::String error;
    gk::detail::ModelVertex source[4] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(-1.0f, 1.0f, -1.0f, 1.0f), MakeVertex(1.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, -1.0f, -1.0f, 0.0f) };
    const uint32_t ordered[6] = { 0, 1, 2, 0, 3, 1 };
    const uint32_t reversed[6] = { 0, 3, 1, 0, 1, 2 };
    for (uint32_t index = 0; index < 4; ++index)
        source[index].normal[2] = -1.0f;
    if (!vertices.AppendRange(source, 4) || !firstIndices.AppendRange(ordered, 6) || !reversedIndices.AppendRange(reversed, 6))
        return Fail(failure, "weighted tangent fixture allocation failed");
    if (!gk::model::GenerateModelTangents(vertices, firstIndices, 8, firstVertices, firstOutputIndices, error) || !gk::model::GenerateModelTangents(vertices, reversedIndices, 8, reversedVertices, reversedOutputIndices, error))
        return Fail(failure, error.CStr());
    if (firstVertices.Count() != 4 || reversedVertices.Count() != 4 || firstOutputIndices.Count() != 6 || reversedOutputIndices.Count() != 6)
        return Fail(failure, "smooth shared edge produced an unexpected vertex split");
    const float expectedDiagonal = sqrtf(0.5f);
    for (uint32_t index = 0; index < 2; ++index)
    {
        const gk::detail::ModelVertex& first = firstVertices.At(index);
        const gk::detail::ModelVertex& reversedVertex = reversedVertices.At(index);
        if (fabsf(first.tangent[0] - expectedDiagonal) > 0.0002f || fabsf(first.tangent[1] - expectedDiagonal) > 0.0002f || fabsf(first.tangent[2]) > 0.0002f || first.tangent[3] != -1.0f)
            return Fail(failure, "shared edge tangent does not match its angle-weighted analytic frame");
        for (uint32_t component = 0; component < 4; ++component)
        {
            if (first.tangent[component] != reversedVertex.tangent[component])
                return Fail(failure, "shared edge tangent depends on face order");
        }
    }
    return true;
}

/**
 * 未参照頂点を除き、縮退UVを出力原子的に拒否する。
 */
/**
 * 未参照頂点を除き、縮退UVを出力原子的に拒否する。
 */
bool TestUnusedAndDegenerateInput(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> vertices;
    gk::Array<uint32_t> indices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    gk::detail::ModelVertex source[4] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 1.0f), MakeVertex(1000000000.0f, -1000000000.0f, 1000000000.0f, -1000000000.0f) };
    // 未参照頂点の極端な座標や零法線は接線生成へ参加しない。
    source[3].normal[0] = 0.0f;
    source[3].normal[1] = 0.0f;
    source[3].normal[2] = 0.0f;
    const uint32_t sourceIndices[3] = { 0, 1, 2 };
    if (!vertices.AppendRange(source, 4) || !indices.AppendRange(sourceIndices, 3))
        return Fail(failure, "unused vertex fixture allocation failed");
    if (!gk::model::GenerateModelTangents(vertices, indices, 8, outputVertices, outputIndices, error))
        return Fail(failure, error.CStr());
    if (outputVertices.Count() != 3 || outputIndices.Count() != 3 || outputIndices.At(0) != 0 || outputIndices.At(1) != 1 || outputIndices.At(2) != 2)
        return Fail(failure, "unused source vertex was not omitted and indices were not remapped");
    if (vertices.Count() != 4 || vertices.At(3).position[0] != source[3].position[0] || indices.At(2) != 2)
        return Fail(failure, "tangent generation changed its source arrays");

    gk::Array<gk::detail::ModelVertex> degenerateVertices;
    gk::Array<uint32_t> degenerateIndices;
    gk::Array<gk::detail::ModelVertex> unchangedVertices;
    gk::Array<uint32_t> unchangedIndices;
    const gk::detail::ModelVertex degenerate[3] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 0.0f, 0.0f), MakeVertex(2.0f, 0.0f, 1.0f, 0.0f) };
    const uint32_t degenerateSourceIndices[3] = { 0, 1, 2 };
    const gk::detail::ModelVertex sentinel = MakeVertex(9.0f, 8.0f, 0.2f, 0.8f);
    const uint32_t sentinelIndex = 11;
    if (!degenerateVertices.AppendRange(degenerate, 3) || !degenerateIndices.AppendRange(degenerateSourceIndices, 3) || !unchangedVertices.Append(sentinel) || !unchangedIndices.Append(sentinelIndex))
        return Fail(failure, "degenerate vertex fixture allocation failed");
    if (gk::model::GenerateModelTangents(degenerateVertices, degenerateIndices, 8, unchangedVertices, unchangedIndices, error))
        return Fail(failure, "degenerate UV mapping unexpectedly succeeded");
    if (unchangedVertices.Count() != 1 || unchangedVertices.At(0).position[0] != sentinel.position[0] || unchangedIndices.Count() != 1 || unchangedIndices.At(0) != sentinelIndex || !error.Length())
        return Fail(failure, "degenerate input changed output or omitted its diagnostic");
    return true;
}

/**
 * 既存出力を失敗時に変更しないことを確かめる。
 */
bool TestAtomicFailure(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> vertices;
    gk::Array<uint32_t> indices;
    gk::Array<gk::detail::ModelVertex> outputVertices;
    gk::Array<uint32_t> outputIndices;
    gk::String error;
    const gk::detail::ModelVertex source[3] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 1.0f) };
    const uint32_t sourceIndices[3] = { 0, 1, 2 };
    gk::detail::ModelVertex oldVertex = MakeVertex(7.0f, 8.0f, 0.2f, 0.8f);
    oldVertex.tangent[0] = 9.0f;
    const uint32_t oldIndex = 12;
    if (!vertices.AppendRange(source, 3) || !indices.AppendRange(sourceIndices, 3) || !outputVertices.Append(oldVertex) || !outputIndices.Append(oldIndex))
        return Fail(failure, "atomicity fixture allocation failed");
    if (gk::model::GenerateModelTangents(vertices, indices, 2, outputVertices, outputIndices, error))
        return Fail(failure, "vertex limit failure unexpectedly succeeded");
    if (outputVertices.Count() != 1 || outputVertices.At(0).tangent[0] != 9.0f || outputIndices.Count() != 1 || outputIndices.At(0) != oldIndex || !error.Length())
        return Fail(failure, "failed tangent generation changed output or omitted its diagnostic");
    return true;
}

/**
 * foundationとMikkTSpace内部の割当失敗でも既存出力を保つ。
 */
bool TestAllocationFailures(gk::String& failure)
{
    gk::Array<gk::detail::ModelVertex> vertices;
    gk::Array<uint32_t> indices;
    const gk::detail::ModelVertex source[3] = { MakeVertex(0.0f, 0.0f, 0.0f, 0.0f), MakeVertex(1.0f, 0.0f, 1.0f, 0.0f), MakeVertex(0.0f, 1.0f, 0.0f, 1.0f) };
    const uint32_t sourceIndices[3] = { 0, 1, 2 };
    if (!vertices.AppendRange(source, 3) || !indices.AppendRange(sourceIndices, 3))
        return Fail(failure, "allocation failure fixture setup failed");
    uint32_t failures = 0;
    bool completed = false;
    for (uint32_t allowed = 0; allowed < 80; ++allowed)
    {
        gk::Array<gk::detail::ModelVertex> outputVertices;
        gk::Array<uint32_t> outputIndices;
        gk::String error;
        gk::detail::ModelVertex sentinel = MakeVertex(9.0f, 8.0f, 0.2f, 0.8f);
        sentinel.tangent[0] = 13.0f;
        const uint32_t sentinelIndex = 17;
        if (!outputVertices.Append(sentinel) || !outputIndices.Append(sentinelIndex))
            return Fail(failure, "allocation sentinel setup failed");
        if (!error.Assign("preallocated diagnostic storage to survive injected allocation failures and preserve error text without allocation"))
            return Fail(failure, "allocation diagnostic setup failed");
        gk::SetAllocationFailureAfterForTesting(allowed);
        const bool succeeded = gk::model::GenerateModelTangents(vertices, indices, 16, outputVertices, outputIndices, error);
        gk::ResetAllocationFailureForTesting();
        if (succeeded)
        {
            if (outputVertices.Count() != 3 || outputIndices.Count() != 3 || error.Length() != 0)
                return Fail(failure, "successful allocation boundary returned invalid output");
            for (uint32_t vertexIndex = 0; vertexIndex < outputVertices.Count(); ++vertexIndex)
            {
                const gk::detail::ModelVertex& vertex = outputVertices.At(vertexIndex);
                if (fabsf(vertex.tangent[0] - 1.0f) > 0.0001f || fabsf(vertex.tangent[1]) > 0.0001f || fabsf(vertex.tangent[2]) > 0.0001f || vertex.tangent[3] != 1.0f)
                    return Fail(failure, "successful allocation boundary changed the known tangent frame");
            }
            completed = true;
            break;
        }
        ++failures;
        if (outputVertices.Count() != 1 || outputVertices.At(0).tangent[0] != 13.0f)
        {
            fprintf(stderr, "allocation injection at %u changed output vertex count or value\n", allowed);
            return Fail(failure, "allocation failure changed output vertex");
        }
        if (outputIndices.Count() != 1 || outputIndices.At(0) != sentinelIndex)
        {
            fprintf(stderr, "allocation injection at %u changed output index count or value\n", allowed);
            return Fail(failure, "allocation failure changed output index");
        }
        if (!error.Length())
        {
            fprintf(stderr, "allocation injection at %u omitted its error diagnostic\n", allowed);
            return Fail(failure, "allocation failure omitted its diagnostic");
        }
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
    if (!TestPlanarTriangle(failure) || !TestMirroredCornerSplit(failure) || !TestFaceOrderInvariance(failure) || !TestWeightedSharedEdge(failure) || !TestUnusedAndDegenerateInput(failure) || !TestAtomicFailure(failure) || !TestAllocationFailures(failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    return 0;
}
