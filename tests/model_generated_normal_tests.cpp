// SPDX-License-Identifier: NOASSERTION
#include "resources/Resources.h"

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
 * 生成法線の全頂点属性が明示法線fixtureと一致するか調べる。
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
 * 法線を省略したGLBと明示法線参照の、頂点・材質・primitive payloadを比べる。
 */
bool CheckGeneratedFixture(const std::filesystem::path& directory, const char* actualName, const char* referenceName, uint32_t expectedActualVertices, uint32_t expectedReferenceVertices, uint32_t expectedIndices, bool expectNormalTexture)
{
    // loaderが読み込みに失敗した理由。
    gk::String actualError;
    gk::String referenceError;
    // 生成対象と明示法線参照のUTF-8 path。
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

    // registryから借りる生成結果と明示法線結果。
    const gk::detail::ModelResource* actual = gk::detail::FindModel(actualHandle);
    const gk::detail::ModelResource* reference = gk::detail::FindModel(referenceHandle);
    bool valid = actual && reference && actual->vertices.Count() == expectedActualVertices && reference->vertices.Count() == expectedReferenceVertices && actual->indices.Count() == expectedIndices && reference->indices.Count() == expectedIndices && actual->primitives.Count() == reference->primitives.Count() && actual->materials.Count() == reference->materials.Count() && actual->textures.Count() == reference->textures.Count();
    if (!valid)
        fprintf(stderr, "%s has unexpected resource or topology counts\n", actualName);

    if (valid)
    {
        // 材質のnormal画像slotと描画範囲が参照fixtureと一致するか調べる。
        for (uint32_t index = 0; index < actual->materials.Count(); ++index)
        {
            const gk::detail::ModelMaterial& actualMaterial = actual->materials.At(index);
            const gk::detail::ModelMaterial& referenceMaterial = reference->materials.At(index);
            if (actualMaterial.normalTextureIndex != referenceMaterial.normalTextureIndex || (actualMaterial.normalTextureIndex >= 0) != expectNormalTexture || !Near(actualMaterial.normalScale, referenceMaterial.normalScale))
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
        // 全index cornerで位置、法線、接線と全texture UVを参照payloadと比べる。
        for (uint32_t index = 0; index < actual->indices.Count(); ++index)
        {
            const uint32_t actualVertexIndex = actual->indices.At(index);
            const uint32_t referenceVertexIndex = reference->indices.At(index);
            if (actualVertexIndex >= actual->vertices.Count() || referenceVertexIndex >= reference->vertices.Count() || !SameVertex(actual->vertices.At(actualVertexIndex), reference->vertices.At(referenceVertexIndex)))
            {
                fprintf(stderr, "%s vertex payload differs from its explicit reference at index %u\n", actualName, index);
                valid = false;
                break;
            }
        }
    }

    // 法線mapがないfixtureでは生成法線だけを保持し、接線は未使用のままか調べる。
    if (valid && !expectNormalTexture)
    {
        for (uint32_t index = 0; index < actual->vertices.Count(); ++index)
        {
            const float* tangent = actual->vertices.At(index).tangent;
            if (!Near(tangent[0], 0.0f) || !Near(tangent[1], 0.0f) || !Near(tangent[2], 0.0f) || !Near(tangent[3], 0.0f))
            {
                fprintf(stderr, "%s generated a tangent without a normal texture\n", actualName);
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
 * NORMALが存在する不正データを、欠損扱いのflat法線生成で隠さない。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename)
{
    // loaderが読み込みに失敗した理由。
    gk::String error;
    // 拒否対象GLBのUTF-8 path。
    const std::string path = (directory / filename).u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        fprintf(stderr, "%s was accepted despite invalid normal input\n", filename);
        return false;
    }
    if (error.Empty())
    {
        fprintf(stderr, "%s was rejected without a diagnostic\n", filename);
        return false;
    }
    return true;
}

/**
 * 明示NORMALのsingular node変換が従来どおり読み込めることを確かめる。
 */
bool CheckAuthoredNormalSingularFixture(const std::filesystem::path& directory)
{
    // loaderが読み込みに失敗した理由。
    gk::String error;
    // 明示normalとsingular nodeを持つGLBのUTF-8 path。
    const std::string path = (directory / "generated-normal-authored-singular-no-map.glb").u8string();
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        fprintf(stderr, "authored normal singular fixture was rejected: %s\n", error.CStr());
        return false;
    }
    // registryから借りるfixture payload。
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    bool valid = model && model->vertices.Count() == 4 && model->materials.Count() == 1 && model->materials.At(0).normalTextureIndex == -1;
    for (uint32_t index = 0; valid && index < model->vertices.Count(); ++index)
    {
        const gk::detail::ModelVertex& vertex = model->vertices.At(index);
        if (!Near(vertex.normal[0], 0.0f) || !Near(vertex.normal[1], 0.0f) || !Near(vertex.normal[2], -1.0f) || !Near(vertex.tangent[0], 0.0f) || !Near(vertex.tangent[1], 0.0f) || !Near(vertex.tangent[2], 0.0f) || !Near(vertex.tangent[3], 0.0f))
            valid = false;
    }
    if (!valid)
        fprintf(stderr, "authored normal singular fixture changed legacy fallback data\n");
    const bool deleted = gk::detail::DeleteModel(handle, error);
    return valid && deleted;
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: gkcore_model_generated_normal_tests <fixture-directory>\n");
        return 2;
    }
    // command lineから受け取るfixture directory。
    const std::filesystem::path directory = std::filesystem::u8path(argv[1]);
    // 平面、UV選択、sharp edge、node変換、未参照頂点の各参照fixture。
    struct FFixturePair
    {
        const char* actual;
        const char* reference;
        uint32_t actualVertices;
        uint32_t referenceVertices;
        uint32_t indices;
        bool normalTexture;
    };
    const FFixturePair pairs[] = { { "generated-normal-flat.glb", "generated-normal-flat-reference.glb", 4, 4, 6, true }, { "generated-normal-tangent-ignored.glb", "generated-normal-flat-reference.glb", 4, 4, 6, true }, { "generated-normal-uv1.glb", "generated-normal-uv1-reference.glb", 4, 4, 6, true }, { "generated-normal-coplanar-shared.glb", "generated-normal-coplanar-shared-reference.glb", 4, 4, 6, true }, { "generated-normal-non-indexed.glb", "generated-normal-non-indexed-reference.glb", 6, 6, 6, true }, { "generated-normal-node-transform.glb", "generated-normal-node-transform-reference.glb", 4, 4, 6, true }, { "generated-normal-tiny-node.glb", "generated-normal-tiny-node-reference.glb", 4, 4, 6, true }, { "generated-normal-unused-source-vertex.glb", "generated-normal-unused-source-vertex-reference.glb", 4, 4, 6, true }, { "generated-normal-sharp-fold.glb", "generated-normal-sharp-fold-reference.glb", 6, 6, 6, true }, { "generated-normal-no-map.glb", "generated-normal-no-map-reference.glb", 4, 4, 6, false }, { "generated-normal-no-map-tangent-ignored.glb", "generated-normal-no-map-reference.glb", 4, 4, 6, false }, { "generated-normal-degenerate-uv-no-map.glb", "generated-normal-degenerate-uv-no-map-reference.glb", 4, 4, 6, false } };
    for (const FFixturePair& pair : pairs)
    {
        if (!CheckGeneratedFixture(directory, pair.actual, pair.reference, pair.actualVertices, pair.referenceVertices, pair.indices, pair.normalTexture))
            return 1;
    }
    // NORMALが明示されていて無効な入力と、生成不能な面を拒否する。
    const char* const rejected[] = { "generated-normal-degenerate-position.glb", "generated-normal-degenerate-uv-with-map.glb", "generated-normal-authored-normal-zero.glb", "generated-normal-authored-normal-nonfinite.glb", "generated-normal-singular-no-map.glb" };
    for (const char* filename : rejected)
    {
        if (!CheckRejectedFixture(directory, filename))
            return 1;
    }
    if (!CheckAuthoredNormalSingularFixture(directory))
        return 1;
    return 0;
}
