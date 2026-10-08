#include "../src/resources/Resources.h"

#include <filesystem>
#include <math.h>
#include <stdio.h>
#include <string>

namespace
{

/**
 * float値がfixtureの許容誤差内で一致するか調べる。
 */
bool Near(float left, float right)
{
    return fabsf(left - right) <= 0.0002f;
}

/**
 * 生成接線と明示TANGENT参照の、全頂点属性を比較する。
 */
bool SameVertex(const gk::detail::ModelVertex& left, const gk::detail::ModelVertex& right)
{
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!Near(left.position[axis], right.position[axis]) || !Near(left.normal[axis], right.normal[axis]))
            return false;
    }
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (!Near(left.tangent[component], right.tangent[component]))
            return false;
    }
    for (uint32_t coordinate = 0; coordinate < 2; ++coordinate)
    {
        if (!Near(left.uv[coordinate], right.uv[coordinate]) || !Near(left.metallicRoughnessUv[coordinate], right.metallicRoughnessUv[coordinate]) || !Near(left.normalUv[coordinate], right.normalUv[coordinate]) || !Near(left.emissiveUv[coordinate], right.emissiveUv[coordinate]) || !Near(left.occlusionUv[coordinate], right.occlusionUv[coordinate]))
            return false;
    }
    return true;
}

/**
 * TANGENT欠損GLBを読み、明示接線参照とindexごとの頂点payloadを比べる。
 */
bool CheckGeneratedFixture(const std::filesystem::path& directory, const char* actualName, const char* referenceName, uint32_t expectedActualVertices = 0, uint32_t expectedReferenceVertices = 0, uint32_t expectedIndices = 0)
{
    // loaderがGLB読み込みに失敗した理由。
    gk::String actualError;
    gk::String referenceError;
    // 接線生成を行うGLBと、対応するFLOAT VEC4 TANGENT参照GLBのpath。
    const std::string actualPath = (directory / actualName).u8string();
    const std::string referencePath = (directory / referenceName).u8string();
    const gk::ModelHandle actualHandle = gk::detail::LoadModel(actualPath.c_str(), actualError);
    if (!actualHandle.IsValid())
    {
        fprintf(stderr, "%s was rejected: %s\n", actualName, actualError.CStr());
        return false;
    }
    const gk::ModelHandle referenceHandle = gk::detail::LoadModel(referencePath.c_str(), referenceError);
    if (!referenceHandle.IsValid())
    {
        gk::detail::DeleteModel(actualHandle, actualError);
        fprintf(stderr, "%s reference was rejected: %s\n", referenceName, referenceError.CStr());
        return false;
    }

    // registryから借りる生成payloadと明示TANGENT payload。
    const gk::detail::ModelResource* actual = gk::detail::FindModel(actualHandle);
    const gk::detail::ModelResource* reference = gk::detail::FindModel(referenceHandle);
    bool valid = actual && reference && actual->indices.Count() == reference->indices.Count() && actual->primitives.Count() == reference->primitives.Count() && actual->materials.Count() == reference->materials.Count() && actual->textures.Count() == reference->textures.Count();
    if (valid && ((expectedActualVertices && actual->vertices.Count() != expectedActualVertices) || (expectedReferenceVertices && reference->vertices.Count() != expectedReferenceVertices) || (expectedIndices && actual->indices.Count() != expectedIndices)))
    {
        fprintf(stderr, "%s has an unexpected generated topology\n", actualName);
        valid = false;
    }
    if (!valid)
        fprintf(stderr, "%s has unexpected resource counts\n", actualName);

    if (valid)
    {
        // 材質とprimitive範囲が参照fixtureから変化していないか調べるloop。
        for (uint32_t index = 0; index < actual->materials.Count(); ++index)
        {
            const gk::detail::ModelMaterial& actualMaterial = actual->materials.At(index);
            const gk::detail::ModelMaterial& referenceMaterial = reference->materials.At(index);
            if (actualMaterial.normalTextureIndex != referenceMaterial.normalTextureIndex || !Near(actualMaterial.normalScale, referenceMaterial.normalScale))
            {
                fprintf(stderr, "%s changed normal material settings\n", actualName);
                valid = false;
                break;
            }
        }
        for (uint32_t index = 0; valid && index < actual->primitives.Count(); ++index)
        {
            const gk::detail::ModelPrimitive& actualPrimitive = actual->primitives.At(index);
            const gk::detail::ModelPrimitive& referencePrimitive = reference->primitives.At(index);
            if (actualPrimitive.firstIndex != referencePrimitive.firstIndex || actualPrimitive.indexCount != referencePrimitive.indexCount || actualPrimitive.materialIndex != referencePrimitive.materialIndex)
            {
                fprintf(stderr, "%s changed primitive ranges\n", actualName);
                valid = false;
            }
        }
    }

    if (valid)
    {
        // index順の全triangle cornerで生成payloadが参照payloadと一致するか調べるloop。
        for (uint32_t index = 0; index < actual->indices.Count(); ++index)
        {
            const uint32_t actualVertexIndex = actual->indices.At(index);
            const uint32_t referenceVertexIndex = reference->indices.At(index);
            if (actualVertexIndex >= actual->vertices.Count() || referenceVertexIndex >= reference->vertices.Count() || !SameVertex(actual->vertices.At(actualVertexIndex), reference->vertices.At(referenceVertexIndex)))
            {
                fprintf(stderr, "%s generated vertex payload differs from its reference at index %u\n", actualName, index);
                valid = false;
                break;
            }
        }
    }

    // registryの所有参照を解放し、両fixtureの後始末も成功したか。
    const bool referenceDeleted = gk::detail::DeleteModel(referenceHandle, referenceError);
    const bool actualDeleted = gk::detail::DeleteModel(actualHandle, actualError);
    return valid && referenceDeleted && actualDeleted;
}

/**
 * normalTextureがないGLBでは接線生成を行わず、既存の零値payloadを保つ。
 */
bool CheckNoNormalMapFixture(const std::filesystem::path& directory)
{
    // 読み込みに失敗した理由。
    gk::String error;
    // normalTextureとTANGENTがないpositive fixtureのpath。
    const std::string path = (directory / "tangent-no-normal-map.glb").u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        fprintf(stderr, "normal-map-free tangent fixture was rejected: %s\n", error.CStr());
        return false;
    }
    // loaderが作成したmodel payload。
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    bool valid = model && model->materials.Count() == 1 && model->materials.At(0).normalTextureIndex == -1;
    if (valid)
    {
        for (uint32_t index = 0; index < model->vertices.Count(); ++index)
        {
            const gk::detail::ModelVertex& vertex = model->vertices.At(index);
            if (!Near(vertex.tangent[0], 0.0f) || !Near(vertex.tangent[1], 0.0f) || !Near(vertex.tangent[2], 0.0f) || !Near(vertex.tangent[3], 0.0f))
            {
                fprintf(stderr, "normal-map-free fixture unexpectedly generated TANGENT\n");
                valid = false;
                break;
            }
        }
    }
    const bool deleted = gk::detail::DeleteModel(handle, error);
    return valid && deleted;
}

/**
 * 不正な接線生成条件または壊れた明示TANGENTを拒否する。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename)
{
    // loaderがGLB読み込みに失敗した理由。
    gk::String error;
    // 接線生成条件を検査するGLBのpath。
    const std::string path = (directory / filename).u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        fprintf(stderr, "%s was accepted despite invalid normal tangent input\n", filename);
        return false;
    }
    if (error.Empty())
    {
        fprintf(stderr, "%s was rejected without a diagnostic\n", filename);
        return false;
    }
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: gkcore_model_tangent_tests <fixture-directory>\n");
        return 2;
    }
    // command lineから受け取るfixture directory。
    const std::filesystem::path directory = std::filesystem::u8path(argv[1]);
    // 接線向き、UV選択、node変換、index生成を比較するfixture対。
    struct FFixturePair
    {
        const char* actual;
        const char* reference;
        uint32_t expectedActualVertices;
        uint32_t expectedReferenceVertices;
        uint32_t expectedIndices;
    };
    // 生成接線を明示参照と比べ、未参照頂点の除去数も固定する。
    const FFixturePair fixturePairs[] = { { "tangent-canonical.glb", "tangent-canonical-reference.glb", 0, 0, 0 }, { "tangent-mirror-u.glb", "tangent-mirror-u-reference.glb", 0, 0, 0 }, { "tangent-rotated-uv.glb", "tangent-rotated-uv-reference.glb", 0, 0, 0 }, { "tangent-uv1.glb", "tangent-uv1-reference.glb", 0, 0, 0 }, { "tangent-node-transform.glb", "tangent-node-transform-reference.glb", 0, 0, 0 }, { "tangent-tiny-node-scale.glb", "tangent-tiny-node-scale-reference.glb", 0, 0, 0 }, { "tangent-non-indexed.glb", "tangent-non-indexed-reference.glb", 0, 0, 0 }, { "tangent-shared-seam.glb", "tangent-shared-seam-reference.glb", 0, 0, 0 }, { "tangent-weighted-source.glb", "tangent-weighted-source-reference.glb", 0, 0, 0 }, { "tangent-weighted-node.glb", "tangent-weighted-node-reference.glb", 0, 0, 0 }, { "tangent-unused-source-vertex.glb", "tangent-unused-source-vertex-reference.glb", 4, 4, 6 } };
    for (const auto& pair : fixturePairs)
    {
        if (!CheckGeneratedFixture(directory, pair.actual, pair.reference, pair.expectedActualVertices, pair.expectedReferenceVertices, pair.expectedIndices))
            return 1;
    }
    if (!CheckNoNormalMapFixture(directory))
        return 1;
    const char* const rejectedFixtures[] = { "tangent-missing-normal-uv.glb", "tangent-degenerate-uv.glb", "tangent-zero-area.glb", "tangent-nonfinite-normal.glb", "tangent-malformed-authored.glb" };
    for (const char* fixture : rejectedFixtures)
    {
        if (!CheckRejectedFixture(directory, fixture))
            return 1;
    }
    return 0;
}
