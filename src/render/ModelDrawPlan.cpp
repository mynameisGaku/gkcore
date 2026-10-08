#include "ModelDrawPlan.h"

#include <float.h>
#include "../resources/TextureSampler.h"

/**
 * 静的modelのprimitiveを検証し、材質を描画用記録へ変換する。
 */
namespace gk::render
{
/**
 * 描画計画の生成に必要な有限値検査をこのfile内に閉じる。
 */
namespace
{

/**
 * 検証理由を設定し、失敗を返す。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * 材質係数が0から1の有限値か調べる。
 */
bool IsUnitFactor(float value)
{
    return value == value && value >= 0.0f && value <= 1.0f && value <= FLT_MAX;
}

/**
 * アルファ抜きの境界が0以上の有限値か調べる。1を超える値も有効。
 */
bool IsAlphaCutoff(float value)
{
    return value == value && value >= 0.0f && value <= FLT_MAX;
}

/**
 * normal scaleが有限値であることを調べる。glTFで許される負値と0も保持する。
 */
bool IsFiniteNormalScale(float value)
{
    return value == value && value >= -FLT_MAX && value <= FLT_MAX;
}

/**
 * 自己発光強度が有限かつ0以上であることを調べる。
 */
bool IsEmissiveStrength(float value)
{
    return value == value && value >= 0.0f && value <= FLT_MAX;
}

/**
 * 描画計画へ渡す材質の係数とalpha cutoffを検証する。
 */
bool IsMaterialValid(const detail::ModelMaterial& material)
{
    // 基本色RGBAの全成分を確認するloop。
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (!IsUnitFactor(material.baseColorFactor[i]))
            return false;
    }
    // 自己発光の色は線形RGBの0〜1に限る。
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (!IsUnitFactor(material.emissiveFactor[i]))
            return false;
    }
    return IsUnitFactor(material.metallicFactor) && IsUnitFactor(material.roughnessFactor) && IsAlphaCutoff(material.alphaCutoff) && IsFiniteNormalScale(material.normalScale) && IsEmissiveStrength(material.emissiveStrength) && detail::IsTextureSamplerValid(material.baseColorSampler) && detail::IsTextureSamplerValid(material.metallicRoughnessSampler) && detail::IsTextureSamplerValid(material.normalSampler) && detail::IsTextureSamplerValid(material.emissiveSampler);
}

// namespace
}

/**
 * 再利用できる容量を保ちながら、順序付き描画記録を空にする。
 */
void ModelDrawPlan::Reset()
{
    parts.Clear();
}

/**
 * modelと出力先を受け取り、各primitiveの範囲・材質を検証して順序付き計画へ変換する。
 * 不正値や確保失敗では既存の出力を維持し、理由を返す。
 */
bool BuildModelDrawPlan(const detail::ModelResource& model, ModelDrawPlan& output, String& error)
{
    error.Clear();
    // 検証がすべて通った後に出力へ移す一時計画。
    ModelDrawPlan candidate;
    // model primitiveを元の順序で描画情報へ変換するloop。
    for (uint32_t primitiveIndex = 0; primitiveIndex < model.primitives.Count(); ++primitiveIndex)
    {
        // 検証対象のindex範囲と材質参照。
        const detail::ModelPrimitive& primitive = model.primitives.At(primitiveIndex);
        if (primitive.indexCount == 0 || primitive.indexCount % 3 != 0 || primitive.firstIndex > model.indices.Count() || primitive.indexCount > model.indices.Count() - primitive.firstIndex)
            return Fail(error, "The model primitive index range is invalid");
        // primitiveが参照する全頂点indexを検証するloop。
        for (uint32_t i = 0; i < primitive.indexCount; ++i)
        {
            if (model.indices.At(primitive.firstIndex + i) >= model.vertices.Count())
                return Fail(error, "The model primitive contains an invalid vertex index");
        }

        // 現在のprimitiveに対応する描画計画。
        ModelPartPlan part{};
        part.firstIndex = primitive.firstIndex;
        part.indexCount = primitive.indexCount;
        part.materialIndex = primitive.materialIndex;
        part.textureIndex = -1;
        part.metallicRoughnessTextureIndex = -1;
        part.normalTextureIndex = -1;
        part.emissiveTextureIndex = -1;
        part.baseColorFactor[0] = 1.0f;
        part.baseColorFactor[1] = 1.0f;
        part.baseColorFactor[2] = 1.0f;
        part.baseColorFactor[3] = 1.0f;
        part.metallicFactor = 0.0f;
        part.roughnessFactor = 1.0f;
        part.normalScale = 1.0f;

        if (primitive.materialIndex != -1)
        {
            if (primitive.materialIndex < 0 || static_cast<uint32_t>(primitive.materialIndex) >= model.materials.Count())
                return Fail(error, "The model primitive material index is invalid");
            // primitiveが使う材質値。
            const detail::ModelMaterial& material = model.materials.At(static_cast<uint32_t>(primitive.materialIndex));
            if (!IsMaterialValid(material))
                return Fail(error, "The model material contains an invalid factor");
            // 基本色RGBAを計画へ複写するloop。
            for (uint32_t component = 0; component < 4; ++component)
                part.baseColorFactor[component] = material.baseColorFactor[component];
            part.metallicFactor = material.metallicFactor;
            part.roughnessFactor = material.roughnessFactor;
            part.alphaMask = material.alphaMask;
            part.alphaCutoff = material.alphaCutoff;
            part.normalScale = material.normalScale;
            part.baseColorSampler = material.baseColorSampler;
            part.metallicRoughnessSampler = material.metallicRoughnessSampler;
            part.normalSampler = material.normalSampler;
            // 色の係数とHDR強度を分けて保持する。
            for (uint32_t component = 0; component < 3; ++component)
                part.emissiveFactorStrength[component] = material.emissiveFactor[component];
            part.emissiveFactorStrength[3] = material.emissiveStrength;
            part.emissiveSampler = material.emissiveSampler;
            if (material.metallicRoughnessTextureIndex != -1)
            {
                if (material.metallicRoughnessTextureIndex < 0 || static_cast<uint32_t>(material.metallicRoughnessTextureIndex) >= model.textures.Count() || !model.textures.At(static_cast<uint32_t>(material.metallicRoughnessTextureIndex)))
                    return Fail(error, "The model material metallic-roughness texture index is invalid");
                part.metallicRoughnessTextureIndex = material.metallicRoughnessTextureIndex;
            }
            if (material.baseColorTextureIndex != -1)
            {
                if (material.baseColorTextureIndex < 0 || static_cast<uint32_t>(material.baseColorTextureIndex) >= model.textures.Count() || !model.textures.At(static_cast<uint32_t>(material.baseColorTextureIndex)))
                    return Fail(error, "The model material texture index is invalid");
                part.textureIndex = material.baseColorTextureIndex;
            }
            if (material.normalTextureIndex != -1)
            {
                if (material.normalTextureIndex < 0 || static_cast<uint32_t>(material.normalTextureIndex) >= model.textures.Count() || !model.textures.At(static_cast<uint32_t>(material.normalTextureIndex)))
                    return Fail(error, "The model material normal texture index is invalid");
                part.normalTextureIndex = material.normalTextureIndex;
            }
            if (material.emissiveTextureIndex != -1)
            {
                if (material.emissiveTextureIndex < 0 || static_cast<uint32_t>(material.emissiveTextureIndex) >= model.textures.Count() || !model.textures.At(static_cast<uint32_t>(material.emissiveTextureIndex)))
                    return Fail(error, "The model material emissive texture index is invalid");
                part.emissiveTextureIndex = material.emissiveTextureIndex;
            }
        }
        if (!candidate.parts.Append(part))
            return Fail(error, "The model draw plan allocation failed");
    }
    output.parts.MoveFrom(candidate.parts);
    error.Clear();
    return true;
}

// namespace gk::render
}
