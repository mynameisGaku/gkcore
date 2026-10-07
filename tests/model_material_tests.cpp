#include "../src/resources/Resources.h"
#include "../src/render/ModelDrawPlan.h"

#include <filesystem>
#include <fstream>
#include <math.h>
#include <stdio.h>
#include <string>
#include <string.h>

namespace
{

/**
 * GLB読み込みと描画計画の失敗理由を記録する。
 */
bool Fail(gk::String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * float係数をGLB読み込み誤差内で比較する。
 */
bool Near(float actual, float expected)
{
    return fabsf(actual - expected) <= 0.0001f;
}

/**
 * 期待する材質係数、画像slot、alpha設定。
 */
struct MaterialExpectation
{
    // 検証するmetallic factor。
    float metallic;
    // 検証するroughness factor。
    float roughness;
    // 基本色のRGBA factor。
    float baseColor[4];
    // 基本色画像slot。未使用は-1。
    int32_t baseTexture;
    // MR画像slot。未使用は-1。
    int32_t metallicRoughnessTexture;
    // alpha maskの有効状態。
    bool alphaMask;
    // alpha maskの境界値。
    float alphaCutoff;
};

/**
 * texture画像から指定pixelのRGBA値を検査する。
 */
bool CheckPixel(const gk::detail::ImageResource& image, uint32_t x, uint32_t y, const uint8_t expected[4])
{
    // 画像サイズ内のpixel byte位置。
    const uint32_t offset = (y * image.width + x) * 4;
    return image.rgba.At(offset) == expected[0] && image.rgba.At(offset + 1) == expected[1] && image.rgba.At(offset + 2) == expected[2] && image.rgba.At(offset + 3) == expected[3];
}

/**
 * fixtureの実在、GLB magic、version、宣言file sizeを読み込み前に確認する。
 */
bool CheckFixtureFile(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // 検査対象GLBのpath。
    const std::filesystem::path path = directory / filename;
    // fixtureをbinary modeで読むstream。
    std::ifstream file(path, std::ios::binary);
    // magic、version、全体byte数を持つGLB header。
    unsigned char header[12]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != 'g' || header[1] != 'l' || header[2] != 'T' || header[3] != 'F')
    {
        failure.Assign("GLB material fixture is missing or has an invalid signature: ");
        failure.Append(filename);
        return false;
    }
    // headerに記録されたGLB format version。
    const uint32_t version = static_cast<uint32_t>(header[4]) | (static_cast<uint32_t>(header[5]) << 8) | (static_cast<uint32_t>(header[6]) << 16) | (static_cast<uint32_t>(header[7]) << 24);
    // headerに記録された全byte数。
    const uint32_t declaredSize = static_cast<uint32_t>(header[8]) | (static_cast<uint32_t>(header[9]) << 8) | (static_cast<uint32_t>(header[10]) << 16) | (static_cast<uint32_t>(header[11]) << 24);
    // filesystemから取得した実file byte数。
    std::error_code fileSizeError;
    const uintmax_t actualSize = std::filesystem::file_size(path, fileSizeError);
    if (version != 2 || fileSizeError || actualSize != declaredSize)
    {
        failure.Assign("GLB material fixture has an invalid version or declared size: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 正常GLBの材質係数、texture参照、画像payload、UVと描画計画を確認する。
 */
bool CheckLoadedFixture(const std::filesystem::path& directory, const char* filename, const MaterialExpectation* expectedMaterials, uint32_t expectedMaterialCount, uint32_t expectedTextureCount, uint32_t expectedVertexCount, bool expectUv1, gk::String& failure)
{
    // model loaderへ渡すUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // parse拒否理由を受け取る文字列。
    gk::String error;
    // registryへ登録された読み込み結果。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        failure.Assign("valid GLB material fixture was rejected: ");
        failure.Append(filename);
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }

    // fixtureの頂点・材質・画像を参照する借用pointer。
    gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    // quad数と期待されたresource数が一致するか。
    const uint32_t expectedPrimitiveCount = expectedMaterialCount == 4 ? 4 : (expectedMaterialCount == 2 ? 2 : 1);
    bool valid = model && model->vertices.Count() == expectedVertexCount && model->indices.Count() == expectedPrimitiveCount * 6 && model->primitives.Count() == expectedPrimitiveCount && model->materials.Count() == expectedMaterialCount && model->textures.Count() == expectedTextureCount;
    if (!valid)
    {
        failure.Assign("GLB material fixture has incomplete geometry or resources: ");
        failure.Append(filename);
    }
    if (valid)
    {
        // 全materialの基本色、係数、画像slotを検査するloop。
        for (uint32_t index = 0; index < expectedMaterialCount; ++index)
        {
            // loaderが格納した材質と独立した期待値。
            const gk::detail::ModelMaterial& material = model->materials.At(index);
            const MaterialExpectation& expected = expectedMaterials[index];
            if (!Near(material.baseColorFactor[0], expected.baseColor[0]) || !Near(material.baseColorFactor[1], expected.baseColor[1]) || !Near(material.baseColorFactor[2], expected.baseColor[2]) || !Near(material.baseColorFactor[3], expected.baseColor[3]) || !Near(material.metallicFactor, expected.metallic) || !Near(material.roughnessFactor, expected.roughness) || material.baseColorTextureIndex != expected.baseTexture || material.metallicRoughnessTextureIndex != expected.metallicRoughnessTexture || material.alphaMask != expected.alphaMask || !Near(material.alphaCutoff, expected.alphaCutoff))
            {
                failure.Assign("GLB material factors or texture slots were not retained: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid && expectUv1)
    {
        // base UV0と水平反転したMR UV1の期待値。
        const float expectedUv0[4][2] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
        const float expectedUv1[4][2] = { { 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f } };
        // UVを検査する単一quadの頂点loop。
        for (uint32_t index = 0; index < 4; ++index)
        {
            // loaderが保持した基本色用とMR用の独立UV。
            const gk::detail::ModelVertex& vertex = model->vertices.At(index);
            if (!Near(vertex.uv[0], expectedUv0[index][0]) || !Near(vertex.uv[1], expectedUv0[index][1]) || !Near(vertex.metallicRoughnessUv[0], expectedUv1[index][0]) || !Near(vertex.metallicRoughnessUv[1], expectedUv1[index][1]))
            {
                failure.Assign("GLB metallic-roughness texCoord did not select UV1 independently: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid)
    {
        // 材質係数とMR画像slotを描画計画へ複写するloop。
        gk::render::ModelDrawPlan plan;
        if (!gk::render::BuildModelDrawPlan(*model, plan, error) || plan.parts.Count() != model->primitives.Count())
        {
            failure.Assign("GLB material data did not build a valid draw plan: ");
            failure.Append(filename);
            valid = false;
        }
        if (valid)
        {
            // primitive順に対応する描画材質情報。
            for (uint32_t index = 0; index < plan.parts.Count(); ++index)
            {
                // primitiveが参照する元材質slot。
                const int32_t materialIndex = model->primitives.At(index).materialIndex;
                if (materialIndex < 0 || static_cast<uint32_t>(materialIndex) >= expectedMaterialCount)
                {
                    failure.Assign("GLB material primitive index is invalid: ");
                    failure.Append(filename);
                    valid = false;
                    break;
                }
                // planへ複写された画像slotと係数。
                const gk::render::ModelPartPlan& part = plan.parts.At(index);
                const MaterialExpectation& expected = expectedMaterials[materialIndex];
                if (part.metallicRoughnessTextureIndex != expected.metallicRoughnessTexture || !Near(part.metallicFactor, expected.metallic) || !Near(part.roughnessFactor, expected.roughness))
                {
                    failure.Assign("GLB metallic-roughness mapping did not reach the draw plan: ");
                    failure.Append(filename);
                    valid = false;
                    break;
                }
            }
        }
    }
    // registry所有のfixture modelを解放する結果。
    const bool deleted = gk::detail::DeleteModel(handle, error);
    if (!deleted && valid)
    {
        failure.Assign("GLB material fixture model could not be deleted: ");
        failure.Append(filename);
        valid = false;
    }
    return valid;
}

/**
 * MR画像の正規化pixel値を1x1 textureで検証する。
 */
bool CheckSinglePixel(const gk::detail::ModelResource& model, uint32_t textureIndex, const uint8_t expected[4], gk::String& failure)
{
    if (textureIndex >= model.textures.Count())
        return Fail(failure, "GLB material texture index exceeds the decoded image array");
    // texture tableから借用した画像payload。
    const gk::detail::ImageResource* image = model.textures.At(textureIndex);
    if (!image || image->width != 1 || image->height != 1 || image->rgba.Count() != 4 || !CheckPixel(*image, 0, 0, expected))
        return Fail(failure, "GLB material texture pixel or dimensions are incorrect");
    return true;
}

/**
 * MR pattern PNGの四象限とサイズをdecode結果から調べる。
 */
bool CheckPatternImage(const gk::detail::ModelResource& model, uint32_t textureIndex, gk::String& failure)
{
    if (textureIndex >= model.textures.Count())
        return Fail(failure, "GLB pattern texture index exceeds the decoded image array");
    // 四象限のMR値を保持するdecode済み画像。
    const gk::detail::ImageResource* image = model.textures.At(textureIndex);
    if (!image || image->width != 64 || image->height != 32 || image->rgba.Count() != 64u * 32u * 4u)
        return Fail(failure, "GLB MR pattern dimensions are incorrect");
    // texture上の四象限RGBA値。
    const uint8_t expected[4][4] = { { 0, 64, 0, 255 }, { 0, 192, 255, 255 }, { 0, 128, 64, 255 }, { 0, 255, 192, 255 } };
    // 各象限の内側pixelを調べるloop。
    for (uint32_t quadrant = 0; quadrant < 4; ++quadrant)
    {
        // 上段と下段で異なるsample位置。
        const uint32_t x = (quadrant % 2) * 32 + 8;
        const uint32_t y = (quadrant / 2) * 16 + 8;
        if (!CheckPixel(*image, x, y, expected[quadrant]))
            return Fail(failure, "GLB MR pattern pixel values are incorrect");
    }
    return true;
}

/**
 * 不正texture参照を拒否し、error diagnosticを返す。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // model loaderへ渡すUTF-8 fixture path。
    const std::string path = (directory / filename).u8string();
    // 拒否理由を受け取る文字列。
    gk::String error;
    // invalid fixtureが誤って登録されないことを確認するhandle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        failure.Assign("invalid GLB metallic-roughness fixture was accepted: ");
        failure.Append(filename);
        return false;
    }
    if (error.Empty())
    {
        failure.Assign("rejected GLB metallic-roughness fixture had no diagnostic: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 不正MR画像slotで失敗したときに以前の描画計画を保つ。
 */
bool CheckInvalidTexturePlanPreservesOutput()
{
    // MR画像参照を不正値にした小さなmodel。
    gk::detail::ModelResource model{};
    // plan検査に必要な三角形頂点を用意するloop。
    for (uint32_t index = 0; index < 3; ++index)
    {
        // 各頂点の有効な位置と法線。
        gk::detail::ModelVertex vertex{};
        vertex.position[0] = static_cast<float>(index);
        vertex.normal[2] = -1.0f;
        model.vertices.Append(vertex);
        model.indices.Append(index);
    }
    // MR画像slotだけを範囲外にする材質。
    gk::detail::ModelMaterial material{};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 1.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
    material.metallicRoughnessTextureIndex = 99;
    model.materials.Append(material);
    model.primitives.Append({ 0, 3, 0 });

    // 失敗時に維持する既存描画計画。
    gk::render::ModelDrawPlan plan;
    gk::render::ModelPartPlan sentinel{};
    sentinel.firstIndex = 17;
    plan.parts.Append(sentinel);
    // 範囲外MR slotを拒否し、既存計画を維持したか。
    gk::String error;
    if (gk::render::BuildModelDrawPlan(model, plan, error) || plan.parts.Count() != 1 || plan.parts.At(0).firstIndex != 17)
    {
        fprintf(stderr, "invalid MR texture index changed the existing draw plan\n");
        return false;
    }
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: model_material_tests <fixture-directory>\n");
        return 2;
    }
    // fixture generatorが出力したGLB directory。
    const std::filesystem::path directory(argv[1]);
    // 各検査から返る診断。
    gk::String failure;
    // 読み込み前に確認する19件のGLB fixture。
    const char* fixtureNames[] = { "uniform-mr.glb", "factor-reference.glb", "no-mr-default.glb", "ignored-ra.glb", "scaled-mr.glb", "scaled-reference.glb", "shared-image.glb", "shared-reference.glb", "mr-only.glb", "mr-only-reference.glb", "uv1-pattern.glb", "uv1-reference.glb", "mixed-pairs.glb", "mixed-reference.glb", "masked-mr.glb", "missing-mr-uv.glb", "transform-mr.glb", "bad-mr-image.glb", "bad-mr-index.glb" };
    for (const char* filename : fixtureNames)
    {
        if (!CheckFixtureFile(directory, filename, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }

    const MaterialExpectation uniformMr[1] = { { 1.0f, 1.0f, { 1, 1, 1, 1 }, 0, 1, false, 0.5f } };
    const MaterialExpectation factorReference[1] = { { 64.0f / 255.0f, 128.0f / 255.0f, { 1, 1, 1, 1 }, 0, -1, false, 0.5f } };
    const MaterialExpectation noMrDefault[1] = { { 1, 1, { 1, 1, 1, 1 }, 0, -1, false, 0.5f } };
    const MaterialExpectation ignoredRa[1] = { { 1.0f, 1.0f, { 1, 1, 1, 1 }, 0, 1, false, 0.5f } };
    const MaterialExpectation scaledMr[1] = { { 0.5f, 0.5f, { 1, 1, 1, 1 }, 0, 1, false, 0.5f } };
    const MaterialExpectation scaledReference[1] = { { 32.0f / 255.0f, 64.0f / 255.0f, { 1, 1, 1, 1 }, 0, -1, false, 0.5f } };
    const MaterialExpectation sharedImage[1] = { { 1.0f, 1.0f, { 1, 1, 1, 1 }, 0, 0, false, 0.5f } };
    const MaterialExpectation sharedReference[1] = { { 40.0f / 255.0f, 100.0f / 255.0f, { 1, 1, 1, 1 }, 0, -1, false, 0.5f } };
    const MaterialExpectation mrOnly[1] = { { 1.0f, 1.0f, { 0.4f, 0.2f, 0.1f, 1 }, -1, 0, false, 0.5f } };
    const MaterialExpectation mrOnlyReference[1] = { { 64.0f / 255.0f, 128.0f / 255.0f, { 0.4f, 0.2f, 0.1f, 1 }, -1, -1, false, 0.5f } };
    const MaterialExpectation uv1Pattern[1] = { { 1, 1, { 1, 1, 1, 1 }, 0, 1, false, 0.5f } };
    const MaterialExpectation uv1Reference[4] = {
        { 1, 192.0f / 255.0f, { 1, 1, 1, 1 }, 0, -1, false, 0.5f },
        { 0, 64.0f / 255.0f, { 1, 1, 1, 1 }, 0, -1, false, 0.5f },
        { 192.0f / 255.0f, 1, { 1, 1, 1, 1 }, 0, -1, false, 0.5f },
        { 64.0f / 255.0f, 128.0f / 255.0f, { 1, 1, 1, 1 }, 0, -1, false, 0.5f },
    };
    const MaterialExpectation mixedPairs[2] = {
        { 1.0f, 1.0f, { 1, 1, 1, 1 }, 0, 1, false, 0.5f },
        { 1.0f, 1.0f, { 1, 1, 1, 1 }, 0, 2, false, 0.5f },
    };
    const MaterialExpectation mixedReference[2] = {
        { 64.0f / 255.0f, 128.0f / 255.0f, { 1, 1, 1, 1 }, 0, -1, false, 0.5f },
        { 192.0f / 255.0f, 64.0f / 255.0f, { 1, 1, 1, 1 }, 0, -1, false, 0.5f },
    };
    const MaterialExpectation maskedMr[1] = { { 1.0f, 1.0f, { 1, 1, 1, 1 }, 0, 1, true, 0.5f } };

    if (!CheckLoadedFixture(directory, "uniform-mr.glb", uniformMr, 1, 2, 4, false, failure) || !CheckLoadedFixture(directory, "factor-reference.glb", factorReference, 1, 1, 4, false, failure) || !CheckLoadedFixture(directory, "no-mr-default.glb", noMrDefault, 1, 1, 4, false, failure) || !CheckLoadedFixture(directory, "ignored-ra.glb", ignoredRa, 1, 2, 4, false, failure) || !CheckLoadedFixture(directory, "scaled-mr.glb", scaledMr, 1, 2, 4, false, failure) || !CheckLoadedFixture(directory, "scaled-reference.glb", scaledReference, 1, 1, 4, false, failure) || !CheckLoadedFixture(directory, "shared-image.glb", sharedImage, 1, 1, 4, false, failure) || !CheckLoadedFixture(directory, "shared-reference.glb", sharedReference, 1, 1, 4, false, failure) || !CheckLoadedFixture(directory, "mr-only.glb", mrOnly, 1, 1, 4, false, failure) || !CheckLoadedFixture(directory, "mr-only-reference.glb", mrOnlyReference, 1, 0, 4, false, failure) || !CheckLoadedFixture(directory, "uv1-pattern.glb", uv1Pattern, 1, 2, 4, true, failure) || !CheckLoadedFixture(directory, "uv1-reference.glb", uv1Reference, 4, 1, 64, false, failure) || !CheckLoadedFixture(directory, "mixed-pairs.glb", mixedPairs, 2, 3, 16, false, failure) || !CheckLoadedFixture(directory, "mixed-reference.glb", mixedReference, 2, 1, 16, false, failure) || !CheckLoadedFixture(directory, "masked-mr.glb", maskedMr, 1, 2, 4, false, failure) || !CheckRejectedFixture(directory, "missing-mr-uv.glb", failure) || !CheckRejectedFixture(directory, "transform-mr.glb", failure) || !CheckRejectedFixture(directory, "bad-mr-image.glb", failure) || !CheckRejectedFixture(directory, "bad-mr-index.glb", failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }

    // 基本色画像とMR画像が同じglTF textureを共有する場合のdecoded image。
    // shared-image fixtureを読み込むUTF-8 path。
    const std::string sharedPath = (directory / "shared-image.glb").u8string();
    // shared textureの読み込み結果を保持するhandle。
    gk::ModelHandle sharedHandle = gk::detail::LoadModel(sharedPath.c_str(), failure);
    if (!sharedHandle.IsValid())
    {
        fprintf(stderr, "shared texture fixture could not be loaded: %s\n", failure.CStr());
        return 1;
    }
    // registryから借用するshared texture model。
    gk::detail::ModelResource* loadedShared = gk::detail::FindModel(sharedHandle);
    const uint8_t basePixel[4] = { 128, 100, 40, 255 };
    const uint8_t uniformPixel[4] = { 0, 128, 64, 0 };
    const uint8_t ignoredPixel[4] = { 255, 128, 64, 255 };
    const uint8_t mixedPixel[4] = { 0, 64, 192, 255 };
    if (!loadedShared || loadedShared->textures.Count() != 1 || !CheckSinglePixel(*loadedShared, 0, basePixel, failure))
    {
        fprintf(stderr, "shared image was decoded more than once or changed\n");
        return 1;
    }
    if (!gk::detail::DeleteModel(sharedHandle, failure))
    {
        fprintf(stderr, "shared texture fixture could not be deleted\n");
        return 1;
    }

    // 代表的な基本色/MR image pixelとmapping tableを確認するcase一覧。
    const char* imageCases[] = { "uniform-mr.glb", "ignored-ra.glb", "mixed-pairs.glb", "uv1-pattern.glb", "mr-only.glb" };
    for (const char* filename : imageCases)
    {
        // image test fixtureのUTF-8 path。
        const std::string imagePath = (directory / filename).u8string();
        // texture image check用のmodel handle。
        const gk::ModelHandle imageHandle = gk::detail::LoadModel(imagePath.c_str(), failure);
        if (!imageHandle.IsValid())
        {
            fprintf(stderr, "image fixture could not be loaded: %s\n", filename);
            return 1;
        }
        // registryから借用したtexture payload一覧。
        gk::detail::ModelResource* imageModel = gk::detail::FindModel(imageHandle);
        bool imageValid = imageModel != nullptr;
        if (imageValid && strcmp(filename, "uniform-mr.glb") == 0)
            imageValid = CheckSinglePixel(*imageModel, 0, basePixel, failure) && CheckSinglePixel(*imageModel, 1, uniformPixel, failure);
        if (imageValid && strcmp(filename, "ignored-ra.glb") == 0)
            imageValid = CheckSinglePixel(*imageModel, 1, ignoredPixel, failure);
        if (imageValid && strcmp(filename, "mixed-pairs.glb") == 0)
            imageValid = CheckSinglePixel(*imageModel, 2, mixedPixel, failure);
        if (imageValid && strcmp(filename, "uv1-pattern.glb") == 0)
            imageValid = CheckPatternImage(*imageModel, 1, failure);
        if (imageValid && strcmp(filename, "mr-only.glb") == 0)
            imageValid = CheckSinglePixel(*imageModel, 0, uniformPixel, failure);
        const bool deleted = gk::detail::DeleteModel(imageHandle, failure);
        if (!imageValid || !deleted)
        {
            fprintf(stderr, "decoded metallic-roughness image check failed: %s\n", filename);
            return 1;
        }
    }

    if (!CheckInvalidTexturePlanPreservesOutput())
        return 1;
    return 0;
}
