// SPDX-License-Identifier: NOASSERTION
#include "../src/resources/Resources.h"
#include "../src/resources/TextureSampler.h"
#include "../src/render/ModelDrawPlan.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <math.h>
#include <stdio.h>
#include <string>

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
 * float値をfixtureで使う許容誤差内で比べる。
 */
bool Near(float actual, float expected)
{
    return fabsf(actual - expected) <= 0.0001f;
}

/**
 * GLB fixtureの存在とversion 2 headerを読み込み前に検証する。
 */
bool CheckFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // 検査対象GLBの実path。
    const std::filesystem::path path = directory / filename;
    // fixture headerをbinary modeで読むstream。
    std::ifstream file(path, std::ios::binary);
    // magic、version、全体byte数を持つGLB header。
    unsigned char header[12]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != 'g' || header[1] != 'l' || header[2] != 'T' || header[3] != 'F')
    {
        failure.Assign("emissive GLB fixture is missing or has an invalid signature: ");
        failure.Append(filename);
        return false;
    }
    // GLB header内のformat version。
    const uint32_t version = static_cast<uint32_t>(header[4]) | (static_cast<uint32_t>(header[5]) << 8) | (static_cast<uint32_t>(header[6]) << 16) | (static_cast<uint32_t>(header[7]) << 24);
    // GLB header内の宣言byte数。
    const uint32_t declaredSize = static_cast<uint32_t>(header[8]) | (static_cast<uint32_t>(header[9]) << 8) | (static_cast<uint32_t>(header[10]) << 16) | (static_cast<uint32_t>(header[11]) << 24);
    // filesystem上の実byte数。
    std::error_code sizeError;
    const uintmax_t actualSize = std::filesystem::file_size(path, sizeError);
    if (version != 2 || sizeError || actualSize != declaredSize)
    {
        failure.Assign("emissive GLB fixture has an invalid version or declared size: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 1材質分の自己発光設定とsamplerの独立期待値。
 */
struct FEmissiveExpectation
{
    // 自己発光の線形RGB係数。
    float factor[3];
    // emissive factorへ掛ける強度。
    float strength;
    // 自己発光画像slot。未使用は-1。
    int32_t textureIndex;
    // 自己発光画像が使う座標番号。
    int32_t texCoord;
    // alpha maskの有効状態。
    bool alphaMask;
    // 自己発光画像のsampler設定。
    gk::detail::FTextureSampler sampler;
    // 基本色画像slot。未使用は-1。
    int32_t baseTextureIndex = -1;
};

/**
 * 読み込んだGLBの材質、独立UV、画像slot、描画計画を検査する。
 */
bool CheckLoadedFixture(const std::filesystem::path& directory, const char* filename, const FEmissiveExpectation* expected, uint32_t expectedMaterialCount, uint32_t expectedTextureCount, gk::String& failure)
{
    if (!CheckFixture(directory, filename, failure))
        return false;
    // loaderへ渡すfixtureのUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // loaderとplanから返る診断。
    gk::String error;
    // registryへ読み込まれたfixture model handle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        failure.Assign("valid emissive fixture was rejected: ");
        failure.Append(filename);
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }

    // fixtureの頂点、材質、画像を参照する借用pointer。
    gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    bool valid = model && model->vertices.Count() >= 4 && model->materials.Count() == expectedMaterialCount && model->textures.Count() == expectedTextureCount;
    if (!valid)
    {
        failure.Assign("emissive fixture has unexpected geometry or resource counts: ");
        failure.Append(filename);
    }
    if (valid)
    {
        // 材質ごとの発光係数、画像、UV指定、samplerを確認するloop。
        for (uint32_t materialIndex = 0; materialIndex < expectedMaterialCount; ++materialIndex)
        {
            // loaderが保持した自己発光材質。
            const gk::detail::ModelMaterial& material = model->materials.At(materialIndex);
            // fixture名に対応する独立期待値。
            const FEmissiveExpectation& item = expected[materialIndex];
            if (!Near(material.emissiveFactor[0], item.factor[0]) || !Near(material.emissiveFactor[1], item.factor[1]) || !Near(material.emissiveFactor[2], item.factor[2]) || !Near(material.emissiveStrength, item.strength) || material.emissiveTextureIndex != item.textureIndex || material.baseColorTextureIndex != item.baseTextureIndex || material.alphaMask != item.alphaMask || !gk::detail::AreTextureSamplersEqual(material.emissiveSampler, item.sampler))
            {
                failure.Assign("emissive GLB material values were not retained: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid && expectedMaterialCount == 1 && expected[0].texCoord == 1)
    {
        // UV0=.5とは異なる四象限ごとの横反転UV1。
        const float uv1[4][2] = { { 0.75f, 0.25f }, { 0.25f, 0.25f }, { 0.75f, 0.75f }, { 0.25f, 0.75f } };
        if (model->vertices.Count() != 16)
        {
            failure.Assign("emissive UV1 pattern fixture has an unexpected vertex count");
            valid = false;
        }
        // 4 primitiveごとに4頂点の独立UVを確認するloop。
        for (uint32_t vertexIndex = 0; valid && vertexIndex < 16; ++vertexIndex)
        {
            // 四象限と対応するquad頂点位置。
            const uint32_t quadrant = vertexIndex / 4;
            const gk::detail::ModelVertex& vertex = model->vertices.At(vertexIndex);
            if (!Near(vertex.emissiveUv[0], uv1[quadrant][0]) || !Near(vertex.emissiveUv[1], uv1[quadrant][1]))
            {
                failure.Assign("emissive texture coordinates selected the wrong set: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid)
    {
        // 材質情報を描画用へ複写するplan。
        gk::render::ModelDrawPlan plan;
        if (!gk::render::BuildModelDrawPlan(*model, plan, error) || plan.parts.Count() != model->primitives.Count())
        {
            failure.Assign("emissive material did not build a valid draw plan: ");
            failure.Append(filename);
            valid = false;
        }
        if (valid)
        {
            // primitive順にemissive設定のplan複写を確認するloop。
            for (uint32_t primitiveIndex = 0; primitiveIndex < plan.parts.Count(); ++primitiveIndex)
            {
                // primitiveが参照する元材質slot。
                const int32_t materialIndex = model->primitives.At(primitiveIndex).materialIndex;
                if (materialIndex < 0 || static_cast<uint32_t>(materialIndex) >= expectedMaterialCount)
                {
                    failure.Assign("emissive primitive material index is invalid: ");
                    failure.Append(filename);
                    valid = false;
                    break;
                }
                // planへ複写された係数と画像参照。
                const gk::render::ModelPartPlan& part = plan.parts.At(primitiveIndex);
                const FEmissiveExpectation& item = expected[static_cast<uint32_t>(materialIndex)];
                if (part.emissiveTextureIndex != item.textureIndex || !Near(part.emissiveFactorStrength[0], item.factor[0]) || !Near(part.emissiveFactorStrength[1], item.factor[1]) || !Near(part.emissiveFactorStrength[2], item.factor[2]) || !Near(part.emissiveFactorStrength[3], item.strength) || !gk::detail::AreTextureSamplersEqual(part.emissiveSampler, item.sampler))
                {
                    failure.Assign("emissive values were not copied into the draw plan: ");
                    failure.Append(filename);
                    valid = false;
                    break;
                }
            }
        }
    }
    // registryが所有するfixture modelを解放する結果。
    const bool deleted = gk::detail::DeleteModel(handle, error);
    if (!deleted && valid)
    {
        failure.Assign("emissive fixture model could not be deleted: ");
        failure.Append(filename);
        valid = false;
    }
    return valid;
}

/**
 * 不正emissive fixtureが有効modelとして登録されないことを確認する。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    if (!CheckFixture(directory, filename, failure))
        return false;
    // loaderへ渡すfixtureのUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // invalid fixtureの拒否理由。
    gk::String error;
    // 拒否されたmodelがhandleとして登録されないこと。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        failure.Assign("invalid emissive fixture was accepted: ");
        failure.Append(filename);
        return false;
    }
    if (error.Empty())
    {
        failure.Assign("invalid emissive fixture returned no diagnostic: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * GLBに埋め込まれた画像の寸法と選択pixelがdecode後も保たれる。
 */
bool CheckDecodedImage(const std::filesystem::path& directory, const char* filename, uint32_t textureIndex, uint32_t width, uint32_t height, uint32_t x, uint32_t y, const uint8_t expected[4], gk::String& failure)
{
    if (!CheckFixture(directory, filename, failure))
        return false;
    // loaderへ渡すfixtureのUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // model loadとdeleteの診断。
    gk::String error;
    // decoded imageを所有するmodel handle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        failure.Assign("emissive image fixture was rejected: ");
        failure.Append(filename);
        return false;
    }
    // modelが所有するdecoded texture配列。
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    bool valid = model && textureIndex < model->textures.Count();
    if (valid)
    {
        // 指定indexのdecoded image。
        const gk::detail::ImageResource* image = model->textures.At(textureIndex);
        // 寸法内のRGBA byte位置。
        const uint32_t offset = (y * width + x) * 4;
        valid = image && image->width == width && image->height == height && x < width && y < height && image->rgba.Count() == width * height * 4 && image->rgba.At(offset) == expected[0] && image->rgba.At(offset + 1) == expected[1] && image->rgba.At(offset + 2) == expected[2] && image->rgba.At(offset + 3) == expected[3];
    }
    if (!valid)
    {
        failure.Assign("emissive fixture image pixels or dimensions are incorrect: ");
        failure.Append(filename);
    }
    const bool deleted = gk::detail::DeleteModel(handle, error);
    return valid && deleted;
}

/**
 * GLB default、factor、strength、texture、sampler、UV選択を確認する。
 */
bool CheckPositiveFixtures(const std::filesystem::path& directory, gk::String& failure)
{
    // glTFで省略されたemissive factorの期待値。
    const gk::detail::FTextureSampler gltfDefaultSampler = []
    {
        gk::detail::FTextureSampler sampler{};
        sampler.addressU = gk::detail::ETextureAddressMode::Repeat;
        sampler.addressV = gk::detail::ETextureAddressMode::Repeat;
        return sampler;
    }();
    // textureを持たない既存材質の構造体既定値。
    const gk::detail::FTextureSampler noTextureSampler{};
    // 鏡映repeat samplerの期待値。
    gk::detail::FTextureSampler mirrorSampler = gltfDefaultSampler;
    mirrorSampler.addressU = gk::detail::ETextureAddressMode::MirroredRepeat;
    mirrorSampler.addressV = gk::detail::ETextureAddressMode::MirroredRepeat;
    // emissive-default.glb: 既定は黒、強度1、画像なし。
    const FEmissiveExpectation defaultExpectation[] = { { { 0.0f, 0.0f, 0.0f }, 1.0f, -1, -1, false, noTextureSampler } };
    if (!CheckLoadedFixture(directory, "emissive-default.glb", defaultExpectation, 1, 0, failure))
        return false;
    // emissive-factor.glb: emissiveFactorだけを使う。
    const FEmissiveExpectation factorExpectation[] = { { { 0.25f, 0.5f, 0.75f }, 1.0f, -1, -1, false, noTextureSampler } };
    if (!CheckLoadedFixture(directory, "emissive-factor.glb", factorExpectation, 1, 0, failure))
        return false;
    // emissive-zero-strength.glb: 係数があっても強度0を保持する。
    const FEmissiveExpectation zeroStrengthExpectation[] = { { { 1.0f, 0.5f, 0.25f }, 0.0f, -1, -1, false, noTextureSampler } };
    if (!CheckLoadedFixture(directory, "emissive-zero-strength.glb", zeroStrengthExpectation, 1, 0, failure))
        return false;
    // textureを持つfixtureが共通に使う1x1 emissive材質。
    const FEmissiveExpectation textureExpectation[] = { { { 1.0f, 1.0f, 1.0f }, 1.0f, 0, 0, false, gltfDefaultSampler } };
    if (!CheckLoadedFixture(directory, "emissive-uniform-texture-alpha-ignored.glb", textureExpectation, 1, 1, failure))
        return false;
    // strength2とfactorの積を後段へ渡すfixture。
    const FEmissiveExpectation productExpectation[] = { { { 0.5f, 0.25f, 1.0f }, 2.0f, 0, 0, false, gltfDefaultSampler } };
    if (!CheckLoadedFixture(directory, "emissive-factor-product.glb", productExpectation, 1, 1, failure))
        return false;
    // sRGB mid textureを線形色空間で読むfixture。
    if (!CheckLoadedFixture(directory, "emissive-srgb-mid.glb", textureExpectation, 1, 1, failure))
        return false;
    // texCoord1の水平反転を独立emissive UVへ保存するfixture。
    FEmissiveExpectation uv1Expectation[] = { { { 1.0f, 1.0f, 1.0f }, 1.0f, 0, 1, false, gltfDefaultSampler } };
    if (!CheckLoadedFixture(directory, "emissive-uv1-pattern.glb", uv1Expectation, 1, 1, failure))
        return false;
    // 鏡映repeat addressを材質samplerへ保存するfixture。
    const FEmissiveExpectation mirrorExpectation[] = { { { 1.0f, 1.0f, 1.0f }, 1.0f, 0, 0, false, mirrorSampler } };
    if (!CheckLoadedFixture(directory, "emissive-wrap-mirror.glb", mirrorExpectation, 1, 1, failure))
        return false;
    // base colorと自己発光が同じdecoded image slotを共有するfixture。
    const FEmissiveExpectation sharedExpectation[] = { { { 1.0f, 1.0f, 1.0f }, 1.0f, 0, 0, false, gltfDefaultSampler, 0 } };
    if (!CheckLoadedFixture(directory, "emissive-shared-base.glb", sharedExpectation, 1, 1, failure))
        return false;
    // 同一modelで別emissive imageを参照する左右材質。
    // batchの左右emissive textureが持つClampとMirror sampler。
    gk::detail::FTextureSampler clampSampler{};
    gk::detail::FTextureSampler batchMirrorSampler = gltfDefaultSampler;
    batchMirrorSampler.addressU = gk::detail::ETextureAddressMode::MirroredRepeat;
    batchMirrorSampler.addressV = gk::detail::ETextureAddressMode::MirroredRepeat;
    const FEmissiveExpectation batchExpectation[] = { { { 1.0f, 1.0f, 1.0f }, 1.0f, 1, 0, false, clampSampler, 0 }, { { 1.0f, 1.0f, 1.0f }, 1.0f, 2, 0, false, batchMirrorSampler, 0 } };
    if (!CheckLoadedFixture(directory, "emissive-batch-mixed.glb", batchExpectation, 2, 3, failure))
        return false;
    // minFilter 9987を線形mip選択へ分離するfixture。
    gk::detail::FTextureSampler mipLinear = gltfDefaultSampler;
    mipLinear.addressU = gk::detail::ETextureAddressMode::ClampToEdge;
    mipLinear.addressV = gk::detail::ETextureAddressMode::ClampToEdge;
    mipLinear.minFilter = gk::detail::ETextureFilter::Linear;
    mipLinear.magFilter = gk::detail::ETextureFilter::Linear;
    mipLinear.mipFilter = gk::detail::ETextureMipFilter::Linear;
    const FEmissiveExpectation mipExpectation[] = { { { 1.0f, 1.0f, 1.0f }, 1.0f, 0, 0, false, mipLinear } };
    if (!CheckLoadedFixture(directory, "emissive-mip-9987.glb", mipExpectation, 1, 1, failure))
        return false;
    // mip filterなしでは同じ線形画素filterでも段を生成しない。
    gk::detail::FTextureSampler noMip = mipLinear;
    noMip.mipFilter = gk::detail::ETextureMipFilter::None;
    const FEmissiveExpectation noMipExpectation[] = { { { 1.0f, 1.0f, 1.0f }, 1.0f, 0, 0, false, noMip } };
    if (!CheckLoadedFixture(directory, "emissive-mip-9987-no-mip.glb", noMipExpectation, 1, 1, failure))
        return false;
    // maskと自己発光画像を同時に持つfixture。
    const FEmissiveExpectation maskExpectation[] = { { { 1.0f, 1.0f, 1.0f }, 1.0f, 1, 0, true, gltfDefaultSampler, 0 } };
    if (!CheckLoadedFixture(directory, "emissive-mask.glb", maskExpectation, 1, 2, failure))
        return false;

    // decoded sRGB画像のalphaとRGB byteを保持する期待値。
    const uint8_t uniformPixel[4] = { 204, 102, 51, 0 };
    const uint8_t productPixel[4] = { 128, 64, 32, 255 };
    const uint8_t middlePixel[4] = { 128, 128, 128, 255 };
    const uint8_t sharedPixel[4] = { 64, 64, 64, 255 };
    if (!CheckDecodedImage(directory, "emissive-uniform-texture-alpha-ignored.glb", 0, 1, 1, 0, 0, uniformPixel, failure) || !CheckDecodedImage(directory, "emissive-factor-product.glb", 0, 1, 1, 0, 0, productPixel, failure) || !CheckDecodedImage(directory, "emissive-srgb-mid.glb", 0, 1, 1, 0, 0, middlePixel, failure) || !CheckDecodedImage(directory, "emissive-shared-base.glb", 0, 1, 1, 0, 0, sharedPixel, failure))
        return false;
    // batchの基本色と左右emissive image各slotの画素値。
    const uint8_t batchPixels[3][4] = { { 8, 8, 8, 255 }, { 64, 128, 192, 255 }, { 192, 128, 64, 255 } };
    for (uint32_t imageIndex = 0; imageIndex < 3; ++imageIndex)
    {
        if (!CheckDecodedImage(directory, "emissive-batch-mixed.glb", imageIndex, 1, 1, 0, 0, batchPixels[imageIndex], failure))
            return false;
    }
    // mask base imageのalpha差とemissive imageのRGB。
    const uint8_t maskLeft[4] = { 255, 255, 255, 0 };
    const uint8_t maskRight[4] = { 255, 255, 255, 255 };
    const uint8_t maskEmission[4] = { 64, 160, 224, 255 };
    return CheckDecodedImage(directory, "emissive-mask.glb", 0, 2, 1, 0, 0, maskLeft, failure) && CheckDecodedImage(directory, "emissive-mask.glb", 0, 2, 1, 1, 0, maskRight, failure) && CheckDecodedImage(directory, "emissive-mask.glb", 1, 1, 1, 0, 0, maskEmission, failure);
}

/**
 * 不正な材質係数や画像slotで描画計画を置き換えない。
 */
bool CheckInvalidPlanPreservesOutput(const std::filesystem::path& directory, gk::String& failure)
{
    // plan入力に使うvalid material fixture path。
    const std::string path = (directory / "emissive-factor-product.glb").u8string();
    // loaderとplanから返る診断。
    gk::String error;
    // registryへ読み込んだvalid model。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
        return Fail(failure, "valid emissive plan fixture was rejected");
    // loaderが保持する材質とmodel payload。
    gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    // 変更前の正しい描画計画。
    gk::render::ModelDrawPlan output;
    bool valid = model && gk::render::BuildModelDrawPlan(*model, output, error) && output.parts.Count() == 1;
    if (!valid)
    {
        gk::detail::DeleteModel(handle, error);
        return Fail(failure, "valid emissive material did not build its initial draw plan");
    }
    // 成功済みplanへ置く不変性sentinel。
    const float sentinelStrength = output.parts.At(0).emissiveFactorStrength[3];
    model->materials.At(0).emissiveFactor[0] = 1.5f;
    if (gk::render::BuildModelDrawPlan(*model, output, error) || output.parts.Count() != 1 || !Near(output.parts.At(0).emissiveFactorStrength[3], sentinelStrength))
        valid = Fail(failure, "out-of-range emissive factor changed the existing draw plan");
    model->materials.At(0).emissiveFactor[0] = 0.5f;
    model->materials.At(0).emissiveStrength = std::numeric_limits<float>::quiet_NaN();
    if (valid && (gk::render::BuildModelDrawPlan(*model, output, error) || output.parts.Count() != 1 || !Near(output.parts.At(0).emissiveFactorStrength[3], sentinelStrength)))
        valid = Fail(failure, "non-finite emissive strength changed the existing draw plan");
    model->materials.At(0).emissiveStrength = std::numeric_limits<float>::max();
    if (valid && (!gk::render::BuildModelDrawPlan(*model, output, error) || output.parts.Count() != 1 || output.parts.At(0).emissiveFactorStrength[3] != std::numeric_limits<float>::max()))
        valid = Fail(failure, "largest finite emissive strength was not preserved");
    model->materials.At(0).emissiveStrength = 2.0f;
    model->materials.At(0).emissiveTextureIndex = 99;
    const float invalidIndexSentinelStrength = output.parts.At(0).emissiveFactorStrength[3];
    if (valid && (gk::render::BuildModelDrawPlan(*model, output, error) || output.parts.Count() != 1 || !Near(output.parts.At(0).emissiveFactorStrength[3], invalidIndexSentinelStrength)))
        valid = Fail(failure, "invalid emissive image slot changed the existing draw plan");
    // model registryが所有するfixtureを解放する結果。
    const bool deleted = gk::detail::DeleteModel(handle, error);
    return valid && deleted;
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: gkcore_model_emissive_tests <fixture-directory>\n");
        return 2;
    }
    // command lineから受け取るfixture directory。
    const std::filesystem::path directory = std::filesystem::u8path(argv[1]);
    // 全caseの失敗理由を受け取る診断文字列。
    gk::String failure;
    if (!CheckPositiveFixtures(directory, failure) || !CheckRejectedFixture(directory, "emissive-missing-uv.glb", failure) || !CheckRejectedFixture(directory, "emissive-transform.glb", failure) || !CheckRejectedFixture(directory, "emissive-invalid-factor.glb", failure) || !CheckRejectedFixture(directory, "emissive-invalid-strength.glb", failure) || !CheckRejectedFixture(directory, "emissive-invalid-texcoord.glb", failure) || !CheckRejectedFixture(directory, "emissive-invalid-texture-ref.glb", failure) || !CheckRejectedFixture(directory, "emissive-invalid-image-ref.glb", failure) || !CheckRejectedFixture(directory, "emissive-invalid-factor-overflow.glb", failure) || !CheckRejectedFixture(directory, "emissive-invalid-strength-overflow.glb", failure) || !CheckInvalidPlanPreservesOutput(directory, failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    return 0;
}
