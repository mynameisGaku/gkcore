// SPDX-License-Identifier: NOASSERTION
#include "../src/resources/Resources.h"
#include "../src/resources/TextureSampler.h"
#include "../src/render/ModelDrawPlan.h"

#include <filesystem>
#include <fstream>
#include <stdio.h>
#include <string>
#include <string.h>

namespace
{

/**
 * sampler値とfixture読み込みの契約失敗を記録する。
 */
bool Fail(gk::String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * GLB fixtureの存在、version、宣言byte数を先に確かめる。
 */
bool CheckFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // 読み込み対象GLBのpath。
    const std::filesystem::path path = directory / filename;
    // GLB headerをbinaryで読むstream。
    std::ifstream file(path, std::ios::binary);
    // magic、version、総byte数を含むheader。
    unsigned char header[12]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || memcmp(header, "glTF", 4) != 0)
    {
        failure.Assign("sampler fixture is missing or has an invalid GLB header: ");
        failure.Append(filename);
        return false;
    }
    // little-endian headerから読むGLB versionと宣言byte数。
    const uint32_t version = static_cast<uint32_t>(header[4]) | (static_cast<uint32_t>(header[5]) << 8) | (static_cast<uint32_t>(header[6]) << 16) | (static_cast<uint32_t>(header[7]) << 24);
    const uint32_t declaredSize = static_cast<uint32_t>(header[8]) | (static_cast<uint32_t>(header[9]) << 8) | (static_cast<uint32_t>(header[10]) << 16) | (static_cast<uint32_t>(header[11]) << 24);
    // 実file size取得時のerror code。
    std::error_code sizeError;
    const uintmax_t actualSize = std::filesystem::file_size(path, sizeError);
    if (version != 2 || sizeError || actualSize != declaredSize)
    {
        failure.Assign("sampler fixture has an invalid GLB version or size: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 全108状態を重複なく固定sampler indexへ対応付けられるか調べる。
 */
bool CheckSamplerIndexTable(gk::String& failure)
{
    // 108個のsampler状態が一度ずつ使われたかを記録する表。
    bool used[108]{};
    // U/Vのaddress、min、mag、mipの組み合わせを列挙するloop。
    for (uint32_t addressU = 0; addressU < 3; ++addressU)
    {
        for (uint32_t addressV = 0; addressV < 3; ++addressV)
        {
            for (uint32_t minFilter = 0; minFilter < 2; ++minFilter)
            {
                for (uint32_t magFilter = 0; magFilter < 2; ++magFilter)
                {
                    for (uint32_t mipFilter = 0; mipFilter < 3; ++mipFilter)
                    {
                        // 今回の固定sampler状態。
                        gk::detail::FTextureSampler sampler{};
                        sampler.addressU = static_cast<gk::detail::ETextureAddressMode>(addressU);
                        sampler.addressV = static_cast<gk::detail::ETextureAddressMode>(addressV);
                        sampler.minFilter = static_cast<gk::detail::ETextureFilter>(minFilter);
                        sampler.magFilter = static_cast<gk::detail::ETextureFilter>(magFilter);
                        sampler.mipFilter = static_cast<gk::detail::ETextureMipFilter>(mipFilter);
                        // helperから返る状態番号。
                        uint32_t index = 0xffffffffu;
                        if (!gk::detail::IsTextureSamplerValid(sampler) || !gk::detail::GetTextureSamplerIndex(sampler, index) || index >= 108 || used[index])
                            return Fail(failure, "sampler states do not map uniquely into the 108-entry table");
                        // コピーしたsamplerと全fieldが一致するか。
                        const gk::detail::FTextureSampler copy = sampler;
                        if (!gk::detail::AreTextureSamplersEqual(sampler, copy))
                            return Fail(failure, "equal sampler values were reported as different");
                        used[index] = true;
                    }
                }
            }
        }
    }
    // 全indexが一度ずつ埋まったか調べるloop。
    for (uint32_t index = 0; index < 108; ++index)
    {
        if (!used[index])
            return Fail(failure, "sampler state table has an unused entry");
    }
    // mip設定だけ異なるsamplerを同一扱いしないか確認する。
    gk::detail::FTextureSampler noMip{};
    gk::detail::FTextureSampler nearestMip = noMip;
    nearestMip.mipFilter = gk::detail::ETextureMipFilter::Nearest;
    if (gk::detail::AreTextureSamplersEqual(noMip, nearestMip))
        return Fail(failure, "different mip filters were reported as equal");
    return true;
}

/**
 * 無効enumを拒否し、失敗時にindex出力を維持するか調べる。
 */
bool CheckInvalidSamplerValues(gk::String& failure)
{
    // 不正enumを順番に試すsampler値。
    gk::detail::FTextureSampler sampler{};
    // 無効値判定で変化してはならないindex。
    uint32_t index = 0x12345678u;
    sampler.addressU = static_cast<gk::detail::ETextureAddressMode>(99);
    if (gk::detail::IsTextureSamplerValid(sampler) || gk::detail::GetTextureSamplerIndex(sampler, index) || index != 0x12345678u)
        return Fail(failure, "invalid U address mode changed sampler output");
    sampler = {};
    sampler.addressV = static_cast<gk::detail::ETextureAddressMode>(99);
    if (gk::detail::IsTextureSamplerValid(sampler) || gk::detail::GetTextureSamplerIndex(sampler, index) || index != 0x12345678u)
        return Fail(failure, "invalid V address mode changed sampler output");
    sampler = {};
    sampler.minFilter = static_cast<gk::detail::ETextureFilter>(99);
    if (gk::detail::IsTextureSamplerValid(sampler) || gk::detail::GetTextureSamplerIndex(sampler, index) || index != 0x12345678u)
        return Fail(failure, "invalid minification filter changed sampler output");
    sampler = {};
    sampler.magFilter = static_cast<gk::detail::ETextureFilter>(99);
    if (gk::detail::IsTextureSamplerValid(sampler) || gk::detail::GetTextureSamplerIndex(sampler, index) || index != 0x12345678u)
        return Fail(failure, "invalid magnification filter changed sampler output");
    sampler = {};
    sampler.mipFilter = static_cast<gk::detail::ETextureMipFilter>(99);
    if (gk::detail::IsTextureSamplerValid(sampler) || gk::detail::GetTextureSamplerIndex(sampler, index) || index != 0x12345678u)
        return Fail(failure, "invalid mip filter changed sampler output");
    return true;
}

/**
 * fixtureごとの役割別sampler期待値。
 */
struct FSamplerExpectation
{
    // 読み込むGLB名。
    const char* filename;
    // base color viewに期待するsampler。
    gk::detail::FTextureSampler baseColor;
    // metallic-roughness viewに期待するsampler。
    gk::detail::FTextureSampler metallicRoughness;
    // normal viewに期待するsampler。
    gk::detail::FTextureSampler normal;
};

/**
 * 有効GLBのview別samplerと描画計画への伝播を確認する。
 */
bool CheckLoadedFixture(const std::filesystem::path& directory, const FSamplerExpectation& expected, gk::String& failure)
{
    // loaderへ渡すUTF-8 path。
    const std::string path = (directory / expected.filename).u8string();
    // loaderから返る診断。
    gk::String error;
    // 登録されたmodel handle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        failure.Assign("valid sampler fixture was rejected: ");
        failure.Append(expected.filename);
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }
    // fixture resourceを借用するmodel pointer。
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    bool valid = model && model->textures.Count() == 1 && model->materials.Count() == 1 && model->primitives.Count() == 1;
    if (valid)
    {
        // 3つのtexture viewが保持するsamplerと画像slot。
        const gk::detail::ModelMaterial& material = model->materials.At(0);
        valid = material.baseColorTextureIndex == 0 && material.metallicRoughnessTextureIndex == 0 && material.normalTextureIndex == 0 && gk::detail::AreTextureSamplersEqual(material.baseColorSampler, expected.baseColor) && gk::detail::AreTextureSamplersEqual(material.metallicRoughnessSampler, expected.metallicRoughness) && gk::detail::AreTextureSamplersEqual(material.normalSampler, expected.normal);
    }
    if (valid)
    {
        // 検証済み材質が描画計画へ渡すsampler値。
        gk::render::ModelDrawPlan plan;
        valid = gk::render::BuildModelDrawPlan(*model, plan, error) && plan.parts.Count() == 1 && gk::detail::AreTextureSamplersEqual(plan.parts.At(0).baseColorSampler, expected.baseColor) && gk::detail::AreTextureSamplersEqual(plan.parts.At(0).metallicRoughnessSampler, expected.metallicRoughness) && gk::detail::AreTextureSamplersEqual(plan.parts.At(0).normalSampler, expected.normal);
    }
    const bool deleted = gk::detail::DeleteModel(handle, error);
    if (!valid || !deleted)
    {
        failure.Assign("sampler settings or image sharing were not preserved: ");
        failure.Append(expected.filename);
        return false;
    }
    return true;
}

/**
 * 同じimageを異なるsamplerで参照する材質を重複統合しないことを確かめる。
 */
bool CheckSamplerKeepsMaterialsDistinct(const std::filesystem::path& directory, gk::String& failure)
{
    // 2つの材質が同じimageと異なるwrap設定を使うfixture path。
    const std::filesystem::path fixturePath = directory / "sampler-batch-mixed.glb";
    if (!CheckFixture(directory, "sampler-batch-mixed.glb", failure))
        return false;
    // loaderへ渡すUTF-8 path。
    const std::string path = fixturePath.u8string();
    // loaderから返るdiagnosticとmodel handle。
    gk::String error;
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
        return Fail(failure, "valid mixed sampler fixture was rejected");
    // fixtureが所有するmodel resource。
    const gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    // image共有とmaterial分離、primitive数が期待どおりか。
    bool valid = model && model->textures.Count() == 1 && model->materials.Count() == 2 && model->primitives.Count() == 2;
    if (valid)
    {
        // 同じ画像slotを異なるsamplerで読む左右の材質。
        const gk::detail::ModelMaterial& repeated = model->materials.At(0);
        const gk::detail::ModelMaterial& clamped = model->materials.At(1);
        valid = repeated.baseColorTextureIndex == 0 && clamped.baseColorTextureIndex == 0 && repeated.baseColorSampler.addressU == gk::detail::ETextureAddressMode::Repeat && repeated.baseColorSampler.addressV == gk::detail::ETextureAddressMode::Repeat && clamped.baseColorSampler.addressU == gk::detail::ETextureAddressMode::ClampToEdge && clamped.baseColorSampler.addressV == gk::detail::ETextureAddressMode::ClampToEdge;
    }
    if (valid)
    {
        // 描画計画も左右の異なるaddress modeを保つか確認する。
        gk::render::ModelDrawPlan plan;
        valid = gk::render::BuildModelDrawPlan(*model, plan, error) && plan.parts.Count() == 2 && plan.parts.At(0).baseColorSampler.addressU == gk::detail::ETextureAddressMode::Repeat && plan.parts.At(1).baseColorSampler.addressU == gk::detail::ETextureAddressMode::ClampToEdge;
    }
    // 検査後にfixtureのmodel所有権を解放できたか。
    const bool deleted = gk::detail::DeleteModel(handle, error);
    if (!valid || !deleted)
        return Fail(failure, "different samplers were merged or lost during model planning");
    return true;
}

/**
 * 不正samplerをGLB loaderが診断付きで拒否する。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // loaderへ渡すUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // reject理由を受け取る文字列。
    gk::String error;
    // invalid sampler fixtureの結果handle。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        failure.Assign("invalid sampler fixture was accepted: ");
        failure.Append(filename);
        return false;
    }
    if (error.Empty())
    {
        failure.Assign("rejected sampler fixture had no diagnostic: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 無効samplerで既存描画計画を置き換えないことを確認する。
 */
bool CheckInvalidPlanPreservesOutput(gk::String& failure)
{
    // 有効な三角形を持つ検査model。
    gk::detail::ModelResource model{};
    for (uint32_t vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
    {
        // 描画計画の範囲検証に使う頂点。
        gk::detail::ModelVertex vertex{};
        vertex.position[0] = static_cast<float>(vertexIndex);
        vertex.normal[2] = -1.0f;
        model.vertices.Append(vertex);
        model.indices.Append(vertexIndex);
    }
    // invalid samplerを持つ材質値。
    gk::detail::ModelMaterial material{};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
    material.baseColorSampler.addressU = static_cast<gk::detail::ETextureAddressMode>(99);
    model.materials.Append(material);
    model.primitives.Append({ 0, 3, 0 });
    // failure後も残る既存計画のsentinel。
    gk::render::ModelDrawPlan plan;
    gk::render::ModelPartPlan sentinel{};
    sentinel.firstIndex = 17;
    plan.parts.Append(sentinel);
    if (gk::render::BuildModelDrawPlan(model, plan, failure) || plan.parts.Count() != 1 || plan.parts.At(0).firstIndex != 17)
        return Fail(failure, "invalid sampler changed the existing draw plan");
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--values-only") == 0)
    {
        // 純sampler関数だけを切り離して検証するfailure診断。
        gk::String failure;
        if (!CheckSamplerIndexTable(failure) || !CheckInvalidSamplerValues(failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
        return 0;
    }
    if (argc != 2)
    {
        fprintf(stderr, "usage: model_sampler_tests <fixture-directory>\n");
        return 2;
    }
    // generator出力を格納したGLB directory。
    const std::filesystem::path directory(argv[1]);
    // fixture検査から返るfailure診断。
    gk::String failure;
    // loader実行前に存在とGLB headerを確かめる全8 fixture。
    const char* fixtures[] = { "sampler-defaults.glb", "sampler-explicit.glb", "sampler-min-fallbacks-a.glb", "sampler-min-fallbacks-b.glb", "bad-sampler-wrap.glb", "bad-sampler-mag.glb", "bad-sampler-min.glb", "bad-sampler-reference.glb" };
    for (const char* filename : fixtures)
    {
        if (!CheckFixture(directory, filename, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }
    if (!CheckSamplerIndexTable(failure) || !CheckInvalidSamplerValues(failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    // glTF samplerの省略値とfilter fallbackを含む正常fixture。
    const gk::detail::FTextureSampler repeatLinear = { gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureFilter::Linear, gk::detail::ETextureFilter::Linear };
    const gk::detail::FTextureSampler clampNearest = { gk::detail::ETextureAddressMode::ClampToEdge, gk::detail::ETextureAddressMode::ClampToEdge, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureFilter::Nearest };
    const gk::detail::FTextureSampler mirrorLinearNearestMag = { gk::detail::ETextureAddressMode::MirroredRepeat, gk::detail::ETextureAddressMode::MirroredRepeat, gk::detail::ETextureFilter::Linear, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureMipFilter::Linear };
    const gk::detail::FTextureSampler repeatNearestMipNearest = { gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureFilter::Linear, gk::detail::ETextureMipFilter::Nearest };
    const gk::detail::FTextureSampler repeatNearestMipLinear = { gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureFilter::Linear, gk::detail::ETextureMipFilter::Linear };
    const gk::detail::FTextureSampler repeatLinearNearestMagMipNearest = { gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureFilter::Linear, gk::detail::ETextureFilter::Nearest, gk::detail::ETextureMipFilter::Nearest };
    const gk::detail::FTextureSampler repeatLinearMipLinear = { gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureAddressMode::Repeat, gk::detail::ETextureFilter::Linear, gk::detail::ETextureFilter::Linear, gk::detail::ETextureMipFilter::Linear };
    const FSamplerExpectation positives[] = {
        { "sampler-defaults.glb", repeatLinear, repeatLinear, repeatLinear },
        { "sampler-explicit.glb", clampNearest, repeatLinear, mirrorLinearNearestMag },
        { "sampler-min-fallbacks-a.glb", repeatNearestMipNearest, repeatLinearNearestMagMipNearest, repeatNearestMipLinear },
        { "sampler-min-fallbacks-b.glb", repeatLinearMipLinear, repeatLinearMipLinear, repeatLinearMipLinear },
    };
    for (const FSamplerExpectation& expected : positives)
    {
        if (!CheckLoadedFixture(directory, expected, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }
    // 不正samplerをGLB読み込み前に用意した正常headerから拒否するloop。
    const char* rejected[] = { "bad-sampler-wrap.glb", "bad-sampler-mag.glb", "bad-sampler-min.glb", "bad-sampler-reference.glb" };
    for (const char* filename : rejected)
    {
        if (!CheckRejectedFixture(directory, filename, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }
    if (!CheckInvalidPlanPreservesOutput(failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    if (!CheckSamplerKeepsMaterialsDistinct(directory, failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    return 0;
}
