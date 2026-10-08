// SPDX-License-Identifier: NOASSERTION
#include "../src/resources/Resources.h"
#include "../src/resources/TextureSampler.h"
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
 * 検査に失敗した理由を保存する。
 */
bool Fail(gk::String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * fixture値を比較する小さな許容幅。
 */
bool Near(float actual, float expected)
{
    return fabsf(actual - expected) <= 0.0001f;
}

/**
 * 入力fixtureがGLB 2.0形式で実在することを先に確かめる。
 */
bool CheckFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // 読み込むfixtureのpath。
    const std::filesystem::path path = directory / filename;
    // 先頭GLB headerを読むbinary stream。
    std::ifstream file(path, std::ios::binary);
    // magic、version、全byte数を持つheader。
    unsigned char header[12]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != 'g' || header[1] != 'l' || header[2] != 'T' || header[3] != 'F')
    {
        failure.Assign("occlusion GLB fixture is missing or invalid: ");
        failure.Append(filename);
        return false;
    }
    // headerに記録されたGLB version。
    const uint32_t version = static_cast<uint32_t>(header[4]) | (static_cast<uint32_t>(header[5]) << 8) | (static_cast<uint32_t>(header[6]) << 16) | (static_cast<uint32_t>(header[7]) << 24);
    // headerに記録されたGLB全体byte数。
    const uint32_t declaredSize = static_cast<uint32_t>(header[8]) | (static_cast<uint32_t>(header[9]) << 8) | (static_cast<uint32_t>(header[10]) << 16) | (static_cast<uint32_t>(header[11]) << 24);
    // filesystem上の実byte数。
    std::error_code sizeError;
    const uintmax_t actualSize = std::filesystem::file_size(path, sizeError);
    if (version != 2 || sizeError || actualSize != declaredSize)
    {
        failure.Assign("occlusion GLB fixture has an invalid version or size: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 1材質へ適用するocclusionと基本色画像の独立期待値。
 */
struct FOcclusionExpectation
{
    // strengthを明示省略した場合も含む期待値。
    float strength;
    // model texture配列でのAO image番号。
    int32_t textureIndex;
    // base color image番号。
    int32_t baseTextureIndex;
    // AOが選ぶUV番号。
    int32_t texCoord;
    // AO imageを参照するsampler値。
    gk::detail::FTextureSampler sampler;
};

/**
 * GLB材質・画像共有・planへのocclusion値の保持を検査する。
 */
bool CheckLoadedFixture(const std::filesystem::path& directory, const char* filename, const FOcclusionExpectation* expected, uint32_t expectedMaterialCount, uint32_t expectedTextureCount, gk::String& failure)
{
    if (!CheckFixture(directory, filename, failure))
        return false;
    // loaderへ渡すfixtureのUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // loaderと描画計画の診断。
    gk::String error;
    // registryへ読み込むGLB model handle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        failure.Assign("valid occlusion fixture was rejected: ");
        failure.Append(filename);
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }

    // 読み込み済みmodel payloadを借りるpointer。
    gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    // fixtureの頂点・primitive・material・texture数が期待どおりか。
    bool valid = model && model->vertices.Count() >= 4 && model->primitives.Count() >= 1 && model->materials.Count() == expectedMaterialCount && model->textures.Count() == expectedTextureCount;
    if (!valid)
    {
        failure.Assign("occlusion fixture has unexpected geometry or resource counts: ");
        failure.Append(filename);
    }
    if (valid)
    {
        // loaderが保持した材質ごとのAO設定を確認するloop。
        for (uint32_t materialIndex = 0; materialIndex < expectedMaterialCount; ++materialIndex)
        {
            // 現在検査する読み込み済み材質。
            const gk::detail::ModelMaterial& material = model->materials.At(materialIndex);
            // fixtureに対する独立した期待値。
            const FOcclusionExpectation& item = expected[materialIndex];
            if (!Near(material.occlusionStrength, item.strength) || material.occlusionTextureIndex != item.textureIndex || material.baseColorTextureIndex != item.baseTextureIndex || !gk::detail::AreTextureSamplersEqual(material.occlusionSampler, item.sampler))
            {
                failure.Assign("occlusion material values were not retained: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid && strcmp(filename, "occlusion-shared-roles.glb") == 0)
    {
        // 3材質roleが同じsource image slotを参照し、resourceを重複しない。
        const gk::detail::ModelMaterial& material = model->materials.At(0);
        if (model->textures.Count() != 1 || material.baseColorTextureIndex != 0 || material.metallicRoughnessTextureIndex != 0 || material.occlusionTextureIndex != 0)
        {
            failure.Assign("shared base, MR, and occlusion roles did not retain one image slot");
            valid = false;
        }
    }
    if (valid && expectedMaterialCount == 1 && expected[0].texCoord == 1)
    {
        // 四象限UV1の期待座標。
        const float uv1[4][2] = { { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
        if (model->vertices.Count() != 16)
        {
            failure.Assign("occlusion UV1 fixture has an unexpected vertex count");
            valid = false;
        }
        // 四象限ごとに4頂点のAO用UVを調べるloop。
        for (uint32_t vertexIndex = 0; valid && vertexIndex < 16; ++vertexIndex)
        {
            // 現在頂点と対応する四象限番号。
            const uint32_t quadrant = vertexIndex / 4;
            const gk::detail::ModelVertex& vertex = model->vertices.At(vertexIndex);
            if (!Near(vertex.occlusionUv[0], uv1[quadrant][0]) || !Near(vertex.occlusionUv[1], uv1[quadrant][1]))
            {
                failure.Assign("occlusion texture coordinates selected the wrong set: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid && expectedMaterialCount == 1 && expected[0].texCoord == 0 && expected[0].textureIndex >= 0)
    {
        // AO UV0が既存基本色UVと独立に同じ値を保持するloop。
        for (uint32_t vertexIndex = 0; vertexIndex < model->vertices.Count(); ++vertexIndex)
        {
            // 現在調べるmodel頂点。
            const gk::detail::ModelVertex& vertex = model->vertices.At(vertexIndex);
            if (!Near(vertex.occlusionUv[0], vertex.uv[0]) || !Near(vertex.occlusionUv[1], vertex.uv[1]))
            {
                failure.Assign("occlusion UV0 was not retained: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid)
    {
        // loader値を描画向けに複写したplan。
        gk::render::ModelDrawPlan plan;
        if (!gk::render::BuildModelDrawPlan(*model, plan, error) || plan.parts.Count() != model->primitives.Count())
        {
            failure.Assign("occlusion values did not build a draw plan: ");
            failure.Append(filename);
            valid = false;
        }
        // primitive順にplanが材質値を保つことを調べるloop。
        for (uint32_t primitiveIndex = 0; valid && primitiveIndex < plan.parts.Count(); ++primitiveIndex)
        {
            // primitiveが参照する元材質番号。
            const int32_t materialIndex = model->primitives.At(primitiveIndex).materialIndex;
            if (materialIndex < 0 || static_cast<uint32_t>(materialIndex) >= expectedMaterialCount)
            {
                failure.Assign("occlusion primitive material index is invalid: ");
                failure.Append(filename);
                valid = false;
                break;
            }
            // 描画planへ複写されたAO値。
            const gk::render::ModelPartPlan& part = plan.parts.At(primitiveIndex);
            // 対応材質に対する期待値。
            const FOcclusionExpectation& item = expected[static_cast<uint32_t>(materialIndex)];
            if (part.occlusionTextureIndex != item.textureIndex || !Near(part.occlusionStrength, item.strength) || !gk::detail::AreTextureSamplersEqual(part.occlusionSampler, item.sampler))
            {
                failure.Assign("occlusion values were not copied into the draw plan: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    // registryが所有するfixture modelを解放する結果。
    const bool deleted = gk::detail::DeleteModel(handle, error);
    if (!deleted && valid)
    {
        failure.Assign("occlusion fixture model could not be deleted: ");
        failure.Append(filename);
        valid = false;
    }
    return valid;
}

/**
 * 不正なocclusion GLBがhandleとして登録されないことを検査する。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    if (!CheckFixture(directory, filename, failure))
        return false;
    // loaderへ渡すfixtureのUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // loaderが返す拒否理由。
    gk::String error;
    // 不正GLBを読み込む結果handle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        failure.Assign("invalid occlusion fixture was accepted: ");
        failure.Append(filename);
        return false;
    }
    if (error.Empty())
    {
        failure.Assign("invalid occlusion fixture returned no diagnostic: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 描画planの全材質値が変更前と一致することを調べる。
 */
bool SamePart(const gk::render::ModelPartPlan& left, const gk::render::ModelPartPlan& right)
{
    if (left.firstIndex != right.firstIndex || left.indexCount != right.indexCount || left.materialIndex != right.materialIndex || left.textureIndex != right.textureIndex || left.metallicFactor != right.metallicFactor || left.roughnessFactor != right.roughnessFactor || left.alphaMask != right.alphaMask || left.alphaCutoff != right.alphaCutoff || left.metallicRoughnessTextureIndex != right.metallicRoughnessTextureIndex || left.normalTextureIndex != right.normalTextureIndex || left.normalScale != right.normalScale || left.emissiveTextureIndex != right.emissiveTextureIndex || left.occlusionStrength != right.occlusionStrength || left.occlusionTextureIndex != right.occlusionTextureIndex)
        return false;
    // factor配列とsampler値を比較するloop。
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (left.baseColorFactor[i] != right.baseColorFactor[i] || left.emissiveFactorStrength[i] != right.emissiveFactorStrength[i])
            return false;
    }
    return gk::detail::AreTextureSamplersEqual(left.baseColorSampler, right.baseColorSampler) && gk::detail::AreTextureSamplersEqual(left.metallicRoughnessSampler, right.metallicRoughnessSampler) && gk::detail::AreTextureSamplersEqual(left.normalSampler, right.normalSampler) && gk::detail::AreTextureSamplersEqual(left.emissiveSampler, right.emissiveSampler) && gk::detail::AreTextureSamplersEqual(left.occlusionSampler, right.occlusionSampler);
}

/**
 * 不正occlusion設定による失敗時に既存plan全体が維持される。
 */
bool CheckInvalidPlanPreservesOutput(const std::filesystem::path& directory, gk::String& failure)
{
    // 材質値を読み出す成功fixture path。
    const std::string path = (directory / "occlusion-strength-half.glb").u8string();
    // loaderとplanから返る診断。
    gk::String error;
    // 元fixtureをregistryに保持するhandle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
        return Fail(failure, "valid occlusion plan fixture was rejected");
    // plan検証へ渡すmodel payload。
    gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    // 成功済み描画planの退避領域。
    gk::render::ModelDrawPlan output;
    bool valid = model && gk::render::BuildModelDrawPlan(*model, output, error) && output.parts.Count() == 1;
    if (!valid)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(failure, "valid occlusion fixture did not build an initial plan");
    }
    // 失敗前のplan全材質値。
    const gk::render::ModelPartPlan original = output.parts.At(0);
    // 1を超える不正値で変更前のplan保持を試す。
    model->materials.At(0).occlusionStrength = 1.1f;
    if (gk::render::BuildModelDrawPlan(*model, output, error) || output.parts.Count() != 1 || !SamePart(output.parts.At(0), original))
        valid = Fail(failure, "invalid occlusion strength changed the existing draw plan");
    // 有効strengthへ戻し、範囲外image番号の拒否を試す。
    model->materials.At(0).occlusionStrength = 0.5f;
    model->materials.At(0).occlusionTextureIndex = 99;
    if (valid && (gk::render::BuildModelDrawPlan(*model, output, error) || output.parts.Count() != 1 || !SamePart(output.parts.At(0), original)))
        valid = Fail(failure, "invalid occlusion image slot changed the existing draw plan");
    // 有効image番号へ戻し、sampler enumの範囲検査を試す。
    model->materials.At(0).occlusionTextureIndex = 0;
    model->materials.At(0).occlusionSampler.addressU = static_cast<gk::detail::ETextureAddressMode>(99);
    if (valid && (gk::render::BuildModelDrawPlan(*model, output, error) || output.parts.Count() != 1 || !SamePart(output.parts.At(0), original)))
        valid = Fail(failure, "invalid occlusion sampler changed the existing draw plan");
    const bool deleted = gk::detail::DeleteModel(handle, error);
    return valid && deleted;
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: gkcore_model_occlusion_tests <fixture-directory>\n");
        return 2;
    }
    // command lineから受け取るfixture directory。
    const std::filesystem::path directory = std::filesystem::u8path(argv[1]);
    // case失敗時の診断文。
    gk::String failure;
    // GLBで省略されたsamplerのRepeat/Linear既定値。
    gk::detail::FTextureSampler defaultSampler{};
    defaultSampler.addressU = gk::detail::ETextureAddressMode::Repeat;
    defaultSampler.addressV = gk::detail::ETextureAddressMode::Repeat;
    // 画像範囲外を繰り返し、最も近い画素を選ぶsampler。
    const gk::detail::FTextureSampler nearestRepeat{ gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureMipFilter::None };
    // 画像端へ固定し、最も近い画素を選ぶsampler。
    const gk::detail::FTextureSampler nearestClamp{ gk::detail::ETextureAddressMode::ClampToEdge, gk::detail::ETextureAddressMode::ClampToEdge, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureMipFilter::None };
    // clamp座標とlinear mip階層を持つsampler。
    const gk::detail::FTextureSampler linearMipClamp{ gk::detail::ETextureAddressMode::ClampToEdge, gk::detail::ETextureAddressMode::ClampToEdge, gk::detail::ETextureFilter::Linear, gk::detail::ETextureFilter::Linear, gk::detail::ETextureMipFilter::Linear };
    // clamp座標とmipなしlinear補間を持つsampler。
    const gk::detail::FTextureSampler linearClamp{ gk::detail::ETextureAddressMode::ClampToEdge, gk::detail::ETextureAddressMode::ClampToEdge, gk::detail::ETextureFilter::Linear, gk::detail::ETextureFilter::Linear, gk::detail::ETextureMipFilter::None };
    // strengthやimage indexなど、positive fixtureそれぞれのloader期待値。
    const FOcclusionExpectation defaultAo[] = { { 1.0f, 0, -1, 0, defaultSampler } };
    const FOcclusionExpectation noTexture[] = { { 1.0f, -1, -1, -1, gk::detail::FTextureSampler{} } };
    const FOcclusionExpectation rValues[] = { { 1.0f, 0, -1, 0, nearestRepeat } };
    const FOcclusionExpectation strength0[] = { { 0.0f, 0, -1, 0, defaultSampler } };
    const FOcclusionExpectation strengthHalf[] = { { 0.5f, 0, -1, 0, defaultSampler } };
    const FOcclusionExpectation strength1[] = { { 1.0f, 0, -1, 0, defaultSampler } };
    const FOcclusionExpectation uv1[] = { { 1.0f, 0, -1, 1, defaultSampler } };
    const FOcclusionExpectation sharedRoles[] = { { 1.0f, 0, 0, 0, defaultSampler } };
    const FOcclusionExpectation samplerMixed[] = { { 1.0f, 0, -1, 0, nearestRepeat }, { 1.0f, 0, -1, 0, nearestClamp } };
    const FOcclusionExpectation strengthMixed[] = { { 0.0f, 0, -1, 0, defaultSampler }, { 1.0f, 0, -1, 0, defaultSampler } };
    const FOcclusionExpectation mip[] = { { 1.0f, 0, -1, 0, linearMipClamp } };
    const FOcclusionExpectation mipNoMip[] = { { 1.0f, 0, -1, 0, linearClamp } };
    const FOcclusionExpectation mask[] = { { 1.0f, 1, 0, 0, defaultSampler } };
    const FOcclusionExpectation directOnly[] = { { 1.0f, 0, -1, 0, defaultSampler } };
    const FOcclusionExpectation emissionOnly[] = { { 1.0f, 0, -1, 0, defaultSampler } };
    const FOcclusionExpectation mixedLighting[] = { { 0.5f, 0, -1, 0, defaultSampler } };
    const FOcclusionExpectation stress[] = { { 1.0f, 0, -1, 0, defaultSampler } };
    // 省略値、複数画像role、各strength、UV、sampler、mip、lighting分離を含むpositive fixture群。
    const bool positives = CheckLoadedFixture(directory, "occlusion-no-texture.glb", noTexture, 1, 0, failure) && CheckLoadedFixture(directory, "occlusion-default.glb", defaultAo, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-default-repeat.glb", defaultAo, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-r-values.glb", rValues, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-strength-0.glb", strength0, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-strength-half.glb", strengthHalf, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-strength-1.glb", strength1, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-uv1-pattern.glb", uv1, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-shared-roles.glb", sharedRoles, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-sampler-mixed.glb", samplerMixed, 2, 1, failure) && CheckLoadedFixture(directory, "occlusion-strength-mixed.glb", strengthMixed, 2, 1, failure) && CheckLoadedFixture(directory, "occlusion-mip-9987.glb", mip, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-mip-9987-no-mip.glb", mipNoMip, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-mask.glb", mask, 1, 2, failure) && CheckLoadedFixture(directory, "occlusion-direct-only.glb", directOnly, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-emission-only.glb", emissionOnly, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-mixed-lighting.glb", mixedLighting, 1, 1, failure) && CheckLoadedFixture(directory, "occlusion-stress.glb", stress, 1, 1, failure);
    // 不正UV、変換、strength、texture参照を持つnegative fixture群。
    const bool negatives = positives && CheckRejectedFixture(directory, "occlusion-missing-uv.glb", failure) && CheckRejectedFixture(directory, "occlusion-transform.glb", failure) && CheckRejectedFixture(directory, "occlusion-invalid-strength-low.glb", failure) && CheckRejectedFixture(directory, "occlusion-invalid-strength-high.glb", failure) && CheckRejectedFixture(directory, "occlusion-invalid-strength-overflow.glb", failure) && CheckRejectedFixture(directory, "occlusion-invalid-texture-ref.glb", failure) && CheckRejectedFixture(directory, "occlusion-invalid-image-ref.glb", failure) && CheckInvalidPlanPreservesOutput(directory, failure);
    if (!negatives)
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    return 0;
}
