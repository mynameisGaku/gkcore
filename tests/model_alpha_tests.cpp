#include "resources/Resources.h"
#include "render/ModelDrawPlan.h"
#include "render/ModelGeometry.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <math.h>
#include <stdio.h>
#include <string>

namespace
{

/**
 * 検査失敗の理由を記録してfalseを返す。
 */
bool Fail(gk::String& failure, const char* message)
{
    failure.Assign(message);
    return false;
}

/**
 * GLB読み込みで許容する浮動小数差を比較する。
 */
bool Near(float actual, float expected)
{
    return fabsf(actual - expected) <= 0.0001f;
}

/**
 * 読み込み前にfixtureの実在とGLB 2.0 headerを確認する。
 */
bool CheckFixtureFile(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // 検査対象GLBのpath。
    const std::filesystem::path path = directory / filename;
    // GLB headerを読むbinary stream。
    std::ifstream file(path, std::ios::binary);
    // magic、version、file sizeを含む固定header。
    unsigned char header[12]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != 'g' || header[1] != 'l' || header[2] != 'T' || header[3] != 'F')
    {
        failure.Assign("GLB alpha fixture is missing or has an invalid signature: ");
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
        failure.Assign("GLB alpha fixture has an invalid version or declared size: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 正常GLBのalpha mode、cutoff、material共有、埋込画像を確認する。
 */
bool CheckLoadedFixture(const std::filesystem::path& directory, const char* filename, const bool* expectedMasks, const bool* expectedBlends, const float* expectedCutoffs, uint32_t expectedMaterialCount, float expectedBaseAlpha, bool expectTexture, gk::String& failure)
{
    // model loaderへ渡すUTF-8 path。
    const std::string path = (directory / filename).u8string();
    // parse拒否理由を受け取る文字列。
    gk::String error;
    // resource tableへ登録された読み込み結果。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (!handle.IsValid())
    {
        failure.Assign("valid GLB alpha fixture was rejected: ");
        failure.Append(filename);
        failure.Append(" (");
        failure.Append(error.CStr());
        failure.Append(")");
        return false;
    }

    // fixtureの頂点・材質・画像を参照する借用pointer。
    gk::detail::ModelResource* model = gk::detail::FindModel(handle);
    // 共通quadと指定数の材質が読み込まれたか。
    bool valid = model && model->vertices.Count() == 4 * expectedMaterialCount && model->indices.Count() == 6 * expectedMaterialCount && model->primitives.Count() == expectedMaterialCount && model->materials.Count() == expectedMaterialCount && model->textures.Count() == (expectTexture ? 1u : 0u);
    if (!valid)
    {
        failure.Assign("GLB alpha fixture has incomplete geometry or material resources: ");
        failure.Append(filename);
    }
    if (valid)
    {
        // 画像に埋め込んだ4段階alpha値。
        const uint8_t expectedAlpha[4] = { 0, 64, 128, 255 };
        if (expectTexture)
        {
            // 埋込PNGをdecodeした画像resource。
            gk::detail::ImageResource* image = model->textures.At(0);
            if (!image || image->width != 64 || image->height != 32 || image->rgba.Count() != 64u * 32u * 4u)
            {
                failure.Assign("GLB alpha fixture embedded image is invalid: ");
                failure.Append(filename);
                valid = false;
            }
            if (valid)
            {
                // 各alpha stripeの中央pixel位置。
                for (uint32_t stripe = 0; stripe < 4; ++stripe)
                {
                    // 行8、各stripeの中央列にあるalpha byte offset。
                    const uint32_t pixelOffset = (8u * 64u + stripe * 16u + 8u) * 4u;
                    if (image->rgba.At(pixelOffset) != 255 || image->rgba.At(pixelOffset + 1) != 0 || image->rgba.At(pixelOffset + 2) != 0 || image->rgba.At(pixelOffset + 3) != expectedAlpha[stripe])
                    {
                        failure.Assign("GLB alpha fixture stripe values are incorrect: ");
                        failure.Append(filename);
                        valid = false;
                        break;
                    }
                }
            }
        }
    }
    if (valid)
    {
        // fixtureごとに設定した全materialを比較するloop。
        for (uint32_t index = 0; index < expectedMaterialCount; ++index)
        {
            // 読み込まれた材質alpha設定。
            const gk::detail::ModelMaterial& material = model->materials.At(index);
            if (material.alphaMask != expectedMasks[index] || material.alphaBlend != expectedBlends[index] || !Near(material.alphaCutoff, expectedCutoffs[index]) || !Near(material.baseColorFactor[3], expectedBaseAlpha))
            {
                failure.Assign("GLB alpha mode or cutoff was not retained: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid && expectedMaterialCount > 1)
    {
        // primitive順に割り当てられるmaterial slot。
        for (uint32_t index = 0; index < expectedMaterialCount; ++index)
        {
            // 各quad primitiveの材質参照。
            const gk::detail::ModelPrimitive& primitive = model->primitives.At(index);
            if (primitive.materialIndex != static_cast<int32_t>(index) || primitive.firstIndex != index * 6 || primitive.indexCount != 6)
            {
                failure.Assign("GLB alpha material slots were incorrectly merged or reordered: ");
                failure.Append(filename);
                valid = false;
                break;
            }
        }
    }
    if (valid)
    {
        // 材質設定から作る描画計画。
        gk::render::ModelDrawPlan plan;
        if (!gk::render::BuildModelDrawPlan(*model, plan, error) || plan.parts.Count() != expectedMaterialCount)
        {
            failure.Assign("GLB alpha material was not mapped to the draw plan: ");
            failure.Append(filename);
            valid = false;
        }
        if (valid)
        {
            // fixtureのquadを投影する640x480 frame。
            gk::detail::FramePacket frame{};
            frame.width = 640;
            frame.height = 480;
            // 読み込んだmodelと正面向きcameraを結ぶdraw packet。
            gk::detail::DrawPacket draw{};
            draw.kind = gk::detail::DrawKind::Model;
            draw.model = model;
            draw.cameraPosition = { 0.0f, 0.0f, -3.0f };
            draw.cameraTarget = { 0.0f, 0.0f, 0.0f };
            draw.modelScale = { 1.0f, 1.0f, 1.0f };
            // 各primitiveの計画と生成頂点payloadを確認するloop。
            for (uint32_t index = 0; index < expectedMaterialCount; ++index)
            {
                // 描画部へ渡す材質設定。
                const gk::render::ModelPartPlan& part = plan.parts.At(index);
                // modeにかかわらずGLB指定cutoffをplanへ保持する。
                const float expectedPlanCutoff = expectedCutoffs[index];
                if (part.alphaMask != expectedMasks[index] || part.alphaBlend != expectedBlends[index] || !Near(part.alphaCutoff, expectedPlanCutoff))
                {
                    failure.Assign("GLB alpha mode or cutoff did not reach the draw plan: ");
                    failure.Append(filename);
                    valid = false;
                    break;
                }
                // primitiveから投影した頂点payload。
                gk::Array<gk::render::ModelRenderVertex> vertices;
                if (!gk::render::AppendLitModelPart(frame, draw, part, vertices, 64, error) || !vertices.Count())
                {
                    failure.Assign("GLB alpha primitive could not produce model vertices: ");
                    failure.Append(filename);
                    valid = false;
                    break;
                }
                // 生成された全頂点へalpha mask値が複写されたか。
                for (uint32_t vertexIndex = 0; vertexIndex < vertices.Count(); ++vertexIndex)
                {
                    // 描画に使う現在の頂点。
                    const gk::render::ModelRenderVertex& vertex = vertices.At(vertexIndex);
                    const float expectedAlphaMode = expectedBlends[index] ? 2.0f : (expectedMasks[index] ? 1.0f : 0.0f);
                    if (!Near(vertex.alphaMaskCutoff[0], expectedAlphaMode) || !Near(vertex.alphaMaskCutoff[1], expectedPlanCutoff))
                    {
                        failure.Assign("GLB alpha mask payload did not reach model vertices: ");
                        failure.Append(filename);
                        valid = false;
                        break;
                    }
                }
                if (!valid)
                    break;
            }
        }
    }
    // registry所有のfixture modelを解放する結果。
    const bool deleted = gk::detail::DeleteModel(handle, error);
    if (!deleted && valid)
    {
        failure.Assign("GLB alpha fixture model could not be deleted: ");
        failure.Append(filename);
        valid = false;
    }
    return valid;
}

/**
 * unsupported alpha modeまたは不正cutoffを診断付きで拒否する。
 */
bool CheckRejectedFixture(const std::filesystem::path& directory, const char* filename, gk::String& failure)
{
    // loaderへ渡すUTF-8 fixture path。
    const std::string path = (directory / filename).u8string();
    // GLB拒否理由を受け取る文字列。
    gk::String error;
    // 不正fixtureでhandleが発行されないことを調べる結果。
    const gk::ModelHandle handle = gk::detail::LoadModel(path.c_str(), error);
    if (handle.IsValid())
    {
        gk::detail::DeleteModel(handle, error);
        failure.Assign("invalid GLB alpha fixture was accepted: ");
        failure.Append(filename);
        return false;
    }
    if (error.Empty())
    {
        failure.Assign("rejected GLB alpha fixture had no diagnostic: ");
        failure.Append(filename);
        return false;
    }
    return true;
}

/**
 * 不正cutoffを描画計画と頂点配列へ適用せず、既存出力を保つ。
 */
bool CheckInvalidAlphaPreservesOutputs(float cutoff, const char* label)
{
    // 不正alpha値を持つ一時モデル。
    gk::detail::ModelResource model{};
    // 1枚の三角形を構成する頂点。
    const float positions[3][3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
    for (uint32_t index = 0; index < 3; ++index)
    {
        // 現在追加するモデル頂点。
        gk::detail::ModelVertex vertex{};
        vertex.position[0] = positions[index][0];
        vertex.position[1] = positions[index][1];
        vertex.position[2] = positions[index][2];
        vertex.normal[2] = -1.0f;
        model.vertices.Append(vertex);
        model.indices.Append(index);
    }
    // 既定factorと不正cutoffを持つ材質。
    gk::detail::ModelMaterial material{};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
    material.baseColorTextureIndex = -1;
    material.alphaMask = true;
    material.alphaCutoff = cutoff;
    model.materials.Append(material);
    model.primitives.Append({ 0, 3, 0 });

    // 失敗時に維持する既存描画計画。
    gk::render::ModelDrawPlan plan;
    gk::render::ModelPartPlan planSentinel{};
    planSentinel.firstIndex = 17;
    plan.parts.Append(planSentinel);
    // 不正材質を拒否し、以前の計画を保持する結果。
    gk::String error;
    if (gk::render::BuildModelDrawPlan(model, plan, error) || plan.parts.Count() != 1 || plan.parts.At(0).firstIndex != 17)
    {
        fprintf(stderr, "invalid alpha material changed the existing draw plan: %s\n", label);
        return false;
    }

    // 失敗時に維持する既存頂点配列。
    gk::Array<gk::render::ModelRenderVertex> vertices;
    gk::render::ModelRenderVertex vertexSentinel{};
    vertexSentinel.surface.position[0] = 91.0f;
    vertices.Append(vertexSentinel);
    // 三角形を投影するframeとcamera。
    gk::detail::FramePacket frame{};
    frame.width = 640;
    frame.height = 480;
    gk::detail::DrawPacket draw{};
    draw.kind = gk::detail::DrawKind::Model;
    draw.model = &model;
    draw.cameraPosition = { 0.0f, 0.0f, -3.0f };
    draw.cameraTarget = { 0.0f, 0.0f, 0.0f };
    draw.modelScale = { 1.0f, 1.0f, 1.0f };
    // loaderを経由しない不正planを拒否する材質設定。
    gk::render::ModelPartPlan part{};
    part.firstIndex = 0;
    part.indexCount = 3;
    part.materialIndex = 0;
    part.textureIndex = -1;
    part.baseColorFactor[0] = 1.0f;
    part.baseColorFactor[1] = 1.0f;
    part.baseColorFactor[2] = 1.0f;
    part.baseColorFactor[3] = 1.0f;
    part.metallicFactor = 0.0f;
    part.roughnessFactor = 1.0f;
    part.alphaMask = true;
    part.alphaCutoff = cutoff;
    if (gk::render::AppendLitModelPart(frame, draw, part, vertices, 32, error) || vertices.Count() != 1 || vertices.At(0).surface.position[0] != 91.0f)
    {
        fprintf(stderr, "invalid alpha cutoff changed the existing vertex output: %s\n", label);
        return false;
    }
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: model_alpha_tests <fixture-directory>\n");
        return 2;
    }
    // fixture generatorが出力したGLB directory。
    const std::filesystem::path directory(argv[1]);
    // 各検査から返る診断。
    gk::String failure;
    // 読み込み前に全15件のGLB fileを確認する一覧。
    const char* fixtureNames[] = { "opaque-default.glb", "opaque.glb", "opaque-cutoff-two.glb", "mask-default.glb", "mask-zero.glb", "mask-one.glb", "mask-two.glb", "mask-factor-half.glb", "mask-factor-quarter.glb", "mask-equality.glb", "mask-no-texture.glb", "mixed-materials.glb", "blend.glb", "negative-cutoff.glb", "nonfinite-cutoff.glb" };
    for (const char* filename : fixtureNames)
    {
        if (!CheckFixtureFile(directory, filename, failure))
        {
            fprintf(stderr, "%s\n", failure.CStr());
            return 1;
        }
    }
    // alpha maskを無効にするOPAQUE材質の期待値。
    const bool opaqueMask[1] = { false };
    const bool opaqueBlend[1] = { false };
    // MASK材質で保持するcutoffの期待値。
    const bool maskEnabled[1] = { true };
    const bool maskBlend[1] = { false };
    const float cutoffDefault[1] = { 0.5f };
    const float cutoffZero[1] = { 0.0f };
    const float cutoffOne[1] = { 1.0f };
    const float cutoffTwo[1] = { 2.0f };
    const float cutoffEquality[1] = { 128.0f / 255.0f };
    // mixed fixtureはopaque、既定MASK、cutoff 1の順。
    const bool mixedMasks[4] = { false, true, true, false };
    const bool mixedBlends[4] = { false, false, false, true };
    const float mixedCutoffs[4] = { 0.5f, 0.5f, 1.0f, 0.5f };
    const bool blendEnabled[1] = { true };
    if (!CheckLoadedFixture(directory, "opaque-default.glb", opaqueMask, opaqueBlend, cutoffDefault, 1, 1.0f, true, failure) || !CheckLoadedFixture(directory, "opaque.glb", opaqueMask, opaqueBlend, cutoffDefault, 1, 1.0f, true, failure) || !CheckLoadedFixture(directory, "opaque-cutoff-two.glb", opaqueMask, opaqueBlend, cutoffTwo, 1, 1.0f, true, failure) || !CheckLoadedFixture(directory, "mask-default.glb", maskEnabled, maskBlend, cutoffDefault, 1, 1.0f, true, failure) || !CheckLoadedFixture(directory, "mask-zero.glb", maskEnabled, maskBlend, cutoffZero, 1, 1.0f, true, failure) || !CheckLoadedFixture(directory, "mask-one.glb", maskEnabled, maskBlend, cutoffOne, 1, 1.0f, true, failure) || !CheckLoadedFixture(directory, "mask-two.glb", maskEnabled, maskBlend, cutoffTwo, 1, 1.0f, true, failure) || !CheckLoadedFixture(directory, "mask-factor-half.glb", maskEnabled, maskBlend, cutoffDefault, 1, 0.5f, true, failure) || !CheckLoadedFixture(directory, "mask-factor-quarter.glb", maskEnabled, maskBlend, cutoffDefault, 1, 0.25f, true, failure) || !CheckLoadedFixture(directory, "mask-equality.glb", maskEnabled, maskBlend, cutoffEquality, 1, 1.0f, true, failure) || !CheckLoadedFixture(directory, "mask-no-texture.glb", maskEnabled, maskBlend, cutoffDefault, 1, 0.25f, false, failure) || !CheckLoadedFixture(directory, "mixed-materials.glb", mixedMasks, mixedBlends, mixedCutoffs, 4, 1.0f, true, failure) || !CheckLoadedFixture(directory, "blend.glb", opaqueMask, blendEnabled, cutoffDefault, 1, 0.5f, true, failure) || !CheckRejectedFixture(directory, "negative-cutoff.glb", failure) || !CheckRejectedFixture(directory, "nonfinite-cutoff.glb", failure))
    {
        fprintf(stderr, "%s\n", failure.CStr());
        return 1;
    }
    if (!CheckInvalidAlphaPreservesOutputs(-0.1f, "negative cutoff") || !CheckInvalidAlphaPreservesOutputs(std::numeric_limits<float>::quiet_NaN(), "NaN cutoff"))
    {
        return 1;
    }
    return 0;
}
