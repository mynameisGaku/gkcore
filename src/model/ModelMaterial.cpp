// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include "model/ModelMaterial.h"
#include "model/animation/ModelSnapshot.h"
#include "model/animation/FModelAnimationAsset.h"
#include "core/Context.h"
#include "foundation/Memory.h"
#include "resources/Resources.h"
#include <math.h>

/**
 * モデル材質を公開APIから操作する処理。
 */
namespace gk
{
namespace
{
/**
 * API失敗を最後の診断へ設定する。
 */
int Failure(const char* message)
{
    return detail::SetError(message);
}

bool IsTextureSlotReferenced(const detail::ModelResource& model, uint32_t textureIndex)
{
    // 各材質役割のうちslotを使うものを探す。
    for (uint32_t i = 0; i < model.materials.Count(); ++i)
    {
        const auto& material = model.materials.At(i);
        if (material.baseColorTextureIndex == static_cast<int32_t>(textureIndex) || material.metallicRoughnessTextureIndex == static_cast<int32_t>(textureIndex) || material.normalTextureIndex == static_cast<int32_t>(textureIndex) || material.emissiveTextureIndex == static_cast<int32_t>(textureIndex) || material.occlusionTextureIndex == static_cast<int32_t>(textureIndex))
            return true;
    }
    return false;
}
}

detail::ModelResource* model::CreateMaterialSnapshot(const detail::ModelResource& source, uint32_t materialIndex, const float baseColorFactor[4], int32_t baseColorTextureIndex, bool alphaMask, bool alphaBlend, float alphaCutoff, String& error)
{
    // 入力材質を保持する独立snapshotを作成する。
    if (materialIndex >= source.materials.Count() || !baseColorFactor)
    {
        error.Assign("model material snapshot request is invalid");
        return nullptr;
    }
    // 頂点・index・材質と画像参照を複製したresource。
    auto* result = model::CloneModelSnapshot(source, error);
    if (!result)
        return nullptr;
    if (source.animation)
    {
        // 複製後も元のanimation sourceを保持する。
        if (!Retain(&source.animation->reference))
        {
            Release(&result->reference);
            error.Assign("model animation reference limit exceeded");
            return nullptr;
        }
        result->animation = source.animation;
    }
    // 呼び出し側が選んだ材質record。
    auto& material = result->materials.At(materialIndex);
    for (uint32_t channel = 0; channel < 4; ++channel)
        material.baseColorFactor[channel] = baseColorFactor[channel];
    material.baseColorTextureIndex = baseColorTextureIndex;
    material.alphaMask = alphaMask;
    material.alphaBlend = alphaBlend;
    material.alphaCutoff = alphaCutoff;
    error.Clear();
    return result;
}

uint32_t GetModelMaterialCount(ModelHandle handle)
{
    auto* model = detail::FindModel(handle);
    if (!model)
    {
        detail::SetError("invalid model handle");
        return 0;
    }
    detail::ClearError();
    return model->materials.Count();
}

int SetModelMaterial(ModelHandle handle, uint32_t materialIndex, const FModelMaterialSettings& settings)
{
    // 更新元としてhandleが現在指す材質snapshot。
    auto* source = detail::FindModel(handle);
    if (!source)
        return Failure("invalid model handle");
    if (materialIndex >= source->materials.Count())
        return Failure("model material index is out of range");
    if (settings.alphaMode != EModelAlphaMode::Opaque && settings.alphaMode != EModelAlphaMode::Mask && settings.alphaMode != EModelAlphaMode::Blend)
        return Failure("invalid model alpha mode");
    if (!isfinite(settings.alphaCutoff) || settings.alphaCutoff < 0.0f)
        return Failure("model alpha cutoff must be finite and non-negative");
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        // このRGBA成分へ適用する値。
        const float value = settings.baseColorFactor[channel];
        if (!isfinite(value) || value < 0.0f || value > 1.0f)
            return Failure("model base color factor must be finite and in [0, 1]");
    }
    // 新しい基本色画像。無効handleならnullのままにする。
    detail::ImageResource* image = nullptr;
    if (settings.baseColorImage.IsValid())
    {
        image = detail::FindImage(settings.baseColorImage);
        if (!image)
            return Failure("invalid base color image handle");
    }
    // snapshot作成や画像保持が失敗した理由。
    String error;
    // 変更前の描画・他instanceから独立させた材質resource。
    auto* replacement = model::CreateMaterialSnapshot(*source, materialIndex, settings.baseColorFactor, -1, settings.alphaMode == EModelAlphaMode::Mask, settings.alphaMode == EModelAlphaMode::Blend, settings.alphaCutoff, error);
    if (!replacement)
        return Failure(error.Empty() ? "model material snapshot creation failed" : error.CStr());
    // 複製上で基本色画像slotだけを差し替える材質。
    auto& material = replacement->materials.At(materialIndex);
    material.baseColorSampler.minFilter = detail::ETextureFilter::Linear;
    material.baseColorSampler.mipFilter = settings.generateMipmaps ? detail::ETextureMipFilter::Linear : detail::ETextureMipFilter::None;
    if (image)
    {
        // 同じ画像を既に保持しているslotがあれば共有する。
        uint32_t textureIndex = replacement->textures.Count();
        for (uint32_t i = 0; i < replacement->textures.Count(); ++i)
        {
            if (replacement->textures.At(i) == image)
            {
                textureIndex = i;
                break;
            }
        }
        if (textureIndex == replacement->textures.Count())
        {
            // まず旧基本色slotが他の役割にも使われていないか調べる。
            const int32_t oldSlot = source->materials.At(materialIndex).baseColorTextureIndex;
            // 未使用slotを上書きできる場合はtexture配列を増やさない。
            bool reused = false;
            if (oldSlot >= 0 && static_cast<uint32_t>(oldSlot) < replacement->textures.Count() && !IsTextureSlotReferenced(*replacement, static_cast<uint32_t>(oldSlot)))
            {
                textureIndex = static_cast<uint32_t>(oldSlot);
                reused = true;
            }
            if (!reused)
            {
                // 空slotまたはどの材質役割からも参照されないslotを再利用する。
                for (uint32_t i = 0; i < replacement->textures.Count(); ++i)
                {
                    if (!replacement->textures.At(i) || !IsTextureSlotReferenced(*replacement, i))
                    {
                        textureIndex = i;
                        reused = true;
                        break;
                    }
                }
            }
            // 新しいmodel snapshotが画像を所有できるよう参照を増やす。
            if (!Retain(&image->reference))
            {
                Release(&replacement->reference);
                return Failure("base color image retention failed");
            }
            if (reused)
            {
                // 置き換えるslotが持っていた参照を複製から解放する。
                auto* previousTexture = replacement->textures.At(textureIndex);
                replacement->textures.At(textureIndex) = image;
                if (previousTexture)
                    Release(&previousTexture->reference);
            }
            // 新しいslotを追加できない場合は複製全体を破棄する。
            else if (!replacement->textures.Append(image))
            {
                Release(&image->reference);
                Release(&replacement->reference);
                return Failure("base color image retention failed");
            }
        }
        material.baseColorTextureIndex = static_cast<int32_t>(textureIndex);
    }
    // 新しい参照先を確定した後、未使用になった旧基本色slotを解放する。
    const int32_t oldSlot = source->materials.At(materialIndex).baseColorTextureIndex;
    if (oldSlot >= 0 && oldSlot != material.baseColorTextureIndex && static_cast<uint32_t>(oldSlot) < replacement->textures.Count() && !IsTextureSlotReferenced(*replacement, static_cast<uint32_t>(oldSlot)))
    {
        auto*& oldTexture = replacement->textures.At(static_cast<uint32_t>(oldSlot));
        if (oldTexture)
        {
            Release(&oldTexture->reference);
            oldTexture = nullptr;
        }
    }
    // handleとregistryを新snapshotへ切り替える前のresource。
    detail::ModelResource* previous = nullptr;
    if (!detail::ReplaceModelResource(handle, replacement, previous, error))
    {
        Release(&replacement->reference);
        return Failure(error.Empty() ? "model resource replacement failed" : error.CStr());
    }
    if (previous)
        Release(&previous->reference);
    detail::ClearError();
    return 0;
}
}
