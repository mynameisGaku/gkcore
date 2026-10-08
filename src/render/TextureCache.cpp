#include "TextureCache.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/FSL/defaults.h>
#include "../../shaders/gkcore_model_textures.srt.h"
#include "../../shaders/gkcore_sprite.srt.h"
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <float.h>
#include <stdint.h>
#include <string.h>

namespace gk::render
{
namespace
{

/**
 * 診断文を設定し、失敗を返す。
 */
bool SetError(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

/**
 * samplerとqueueを登録し、画像texture cacheを初期化する。
 */
bool TextureCache::Initialize(Renderer* renderer, Queue* queue, String& error)
{
    if (renderer_ || !renderer || !queue)
        return SetError(error, "The image texture cache initialization is invalid");
    renderer_ = renderer;
    queue_ = queue;

    SamplerDesc samplerDesc{};
    samplerDesc.mMinFilter = FILTER_LINEAR;
    samplerDesc.mMagFilter = FILTER_LINEAR;
    samplerDesc.mMipMapMode = MIPMAP_MODE_LINEAR;
    samplerDesc.mAddressU = ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.mAddressV = ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.mAddressW = ADDRESS_MODE_CLAMP_TO_EDGE;
    addSampler(renderer_, &samplerDesc, &sampler_);
    if (!sampler_)
    {
        Shutdown();
        return SetError(error, "The Forge could not create the image sampler");
    }
    error.Clear();
    return true;
}

/**
 * GPU作業を待ち、cache内の画像資源とsamplerを解放する。
 */
void TextureCache::Shutdown()
{
    if (queue_)
        waitQueueIdle(queue_);
    String drainError;
    if (!DrainPendingUploads(drainError))
    {
        // GPU資源破棄は避け、CPU側の借用関係だけを切る。
        for (uint32_t i = 0; i < kModelCapacity; ++i)
            modelEntries_[i] = {};
        for (uint32_t i = 0; i < kCapacity; ++i)
        {
            if (entries_[i].image)
                Release(&entries_[i].image->reference);
            delete entries_[i].mipChain;
            entries_[i] = {};
        }
        renderer_ = nullptr;
        queue_ = nullptr;
        sampler_ = nullptr;
        for (uint32_t i = 0; i < kSamplerCapacity; ++i)
            modelSamplers_[i] = nullptr;
        clock_ = 0;
        frame_ = 0;
        cachedBytes_ = 0;
        uploadSubmissionPending_ = false;
        uploadFence_ = nullptr;
        return;
    }
    // model descriptorを物理textureより先に解放する。
    for (uint32_t i = 0; i < kModelCapacity; ++i)
        DestroyModelEntry(modelEntries_[i]);
    for (uint32_t i = 0; i < kCapacity; ++i)
        DestroyEntry(entries_[i]);
    for (uint32_t i = 0; i < kSamplerCapacity; ++i)
    {
        if (renderer_ && modelSamplers_[i])
            removeSampler(renderer_, modelSamplers_[i]);
        modelSamplers_[i] = nullptr;
    }
    if (renderer_ && sampler_)
    {
        removeSampler(renderer_, sampler_);
        sampler_ = nullptr;
    }
    renderer_ = nullptr;
    queue_ = nullptr;
    clock_ = 0;
    frame_ = 0;
    cachedBytes_ = 0;
    uploadSubmissionPending_ = false;
    uploadFence_ = nullptr;
}

/**
 * frame番号を進め、このframeで使うentryを保護する。
 */
void TextureCache::BeginFrame()
{
    ++frame_;
    if (frame_ == 0)
    {
        frame_ = 1;
        for (uint32_t i = 0; i < kCapacity; ++i)
            entries_[i].state.frameUsed = 0;
        for (uint32_t i = 0; i < kModelCapacity; ++i)
            modelEntries_[i].state.frameUsed = 0;
    }
}

/**
 * 画像pointerに対応するcache entryを検索する。
 */
TextureCache::Entry* TextureCache::Find(detail::ImageResource* image, ETextureColorSpace colorSpace, bool fullMipChain)
{
    for (uint32_t i = 0; i < kCapacity; ++i)
    {
        if (entries_[i].image == image && entries_[i].colorSpace == colorSpace && entries_[i].fullMipChain == fullMipChain)
            return &entries_[i];
    }
    return nullptr;
}

/**
 * sRGBの基本色・自己発光と、線形の金属度・粗さ・法線画像の組を検索する。
 */
TextureCache::ModelEntry* TextureCache::FindModel(detail::ImageResource* baseImage, detail::ImageResource* metallicRoughnessImage, detail::ImageResource* normalImage, detail::ImageResource* emissiveImage, const detail::FTextureSampler& baseSampler, const detail::FTextureSampler& metallicRoughnessSampler, const detail::FTextureSampler& normalSampler, const detail::FTextureSampler& emissiveSampler)
{
    for (uint32_t i = 0; i < kModelCapacity; ++i)
    {
        // 検索対象の画像role descriptor。
        ModelEntry& entry = modelEntries_[i];
        if (entry.baseImage == baseImage && entry.metallicRoughnessImage == metallicRoughnessImage && entry.normalImage == normalImage && entry.emissiveImage == emissiveImage && detail::AreTextureSamplersEqual(entry.baseSampler, baseSampler) && detail::AreTextureSamplersEqual(entry.metallicRoughnessSampler, metallicRoughnessSampler) && detail::AreTextureSamplersEqual(entry.normalSampler, normalSampler) && detail::AreTextureSamplersEqual(entry.emissiveSampler, emissiveSampler))
            return &entry;
    }
    return nullptr;
}

/**
 * 上限内でentry slotを確保し、必要なら未使用資源を追い出す。
 */
TextureCache::Entry* TextureCache::AcquireSlot(uint64_t imageBytes, String& error)
{
    if (imageBytes > kByteCapacity)
    {
        error.Assign("The image exceeds the bounded texture cache memory budget");
        return nullptr;
    }
    TextureCacheSlotState states[kCapacity]{};
    bool waitedForGpu = false;
    while (!TextureCacheFitsByteBudget(cachedBytes_, imageBytes, kByteCapacity))
    {
        for (uint32_t i = 0; i < kCapacity; ++i)
            states[i] = entries_[i].state;
        uint32_t victim = 0;
        if (!SelectTextureCacheVictim(states, kCapacity, frame_, victim))
        {
            error.Assign("A frame exceeds the bounded image texture memory budget");
            return nullptr;
        }
        if (!waitedForGpu)
        {
            if (!queue_)
            {
                error.Assign("The image texture cache is not initialized");
                return nullptr;
            }
            waitQueueIdle(queue_);
            if (!DrainPendingUploads(error))
                return nullptr;
            waitedForGpu = true;
        }
        DestroyEntry(entries_[victim]);
    }
    for (uint32_t i = 0; i < kCapacity; ++i)
        states[i] = entries_[i].state;
    uint32_t selected = 0;
    if (!SelectTextureCacheSlot(states, kCapacity, frame_, selected))
    {
        error.Assign("A frame uses more unique images than the bounded texture cache supports");
        return nullptr;
    }
    Entry* slot = &entries_[selected];
    if (!slot->image)
        return slot;
    if (!queue_)
    {
        error.Assign("The image texture cache is not initialized");
        return nullptr;
    }
    if (!waitedForGpu)
        waitQueueIdle(queue_);
    if (!DrainPendingUploads(error))
        return nullptr;
    DestroyEntry(*slot);
    return slot;
}

/**
 * current frameで使っていないmodel descriptor slotを確保する。
 */
TextureCache::ModelEntry* TextureCache::AcquireModelSlot(String& error)
{
    // slot選択へ渡すdescriptor recordの使用状態。
    TextureCacheSlotState states[kModelCapacity]{};
    for (uint32_t i = 0; i < kModelCapacity; ++i)
        states[i] = modelEntries_[i].state;
    // 空きまたは最古の未使用slot番号。
    uint32_t selected = 0;
    if (!SelectTextureCacheSlot(states, kModelCapacity, frame_, selected))
    {
        SetError(error, "A frame uses more unique model texture sets than the bounded cache supports");
        return nullptr;
    }
    // 選択したmodel descriptor slot。
    ModelEntry* slot = &modelEntries_[selected];
    if (!slot->state.occupied)
        return slot;
    if (!queue_)
    {
        SetError(error, "The model texture cache is not initialized");
        return nullptr;
    }
    waitQueueIdle(queue_);
    if (!DrainPendingUploads(error))
        return nullptr;
    DestroyModelEntry(*slot);
    return slot;
}

/**
 * 色空間に合う2D textureを作り、sRGB画像だけ従来shader用descriptorも登録する。
 */
bool TextureCache::CreateTexture(Entry& entry, detail::ImageResource* image, ETextureColorSpace colorSpace, String& error)
{
    const uint64_t pixelCount = static_cast<uint64_t>(image->width) * image->height;
    if (!image->width || !image->height || image->width > 16384 || image->height > 16384 || pixelCount > 64u * 1024u * 1024u)
        return SetError(error, "The image pixel buffer does not match a supported texture size");
    const uint64_t expectedBytes = pixelCount * 4u;
    if (expectedBytes > image->rgba.Count())
        return SetError(error, "The image pixel buffer does not match a supported texture size");

    TextureDesc textureDesc{};
    textureDesc.mWidth = image->width;
    textureDesc.mHeight = image->height;
    textureDesc.mDepth = 1;
    textureDesc.mArraySize = 1;
    textureDesc.mMipLevels = entry.mipLevels;
    textureDesc.mSampleCount = SAMPLE_COUNT_1;
    textureDesc.mFormat = colorSpace == ETextureColorSpace::Srgb ? TinyImageFormat_R8G8B8A8_SRGB : TinyImageFormat_R8G8B8A8_UNORM;
    textureDesc.mStartState = RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
    // Tex2D画像の小さなfallbackも2D資源として作る。
    textureDesc.mFlags = TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT | TEXTURE_CREATION_FLAG_FORCE_2D;
    textureDesc.pName = colorSpace == ETextureColorSpace::Srgb ? "gkcore sRGB image" : "gkcore linear image";

    TextureLoadDesc loadDesc{};
    loadDesc.ppTexture = &entry.texture;
    loadDesc.pDesc = &textureDesc;
    loadDesc.mForceReset = true;
    SyncToken token = 0;
    addResource(&loadDesc, &token);
    if (token)
        waitForToken(&token);
    if (!entry.texture)
        return SetError(error, "The Forge could not allocate the image texture");

    if (colorSpace != ETextureColorSpace::Srgb)
    {
        error.Clear();
        return true;
    }

    DescriptorSetDesc descriptorDesc = SRT_SET_DESC(SpriteResources, Persistent, 1, 0);
    addDescriptorSet(renderer_, &descriptorDesc, &entry.descriptorSet);
    if (!entry.descriptorSet)
        return SetError(error, "The Forge could not allocate an image descriptor set");

    DescriptorData descriptors[2]{};
    descriptors[0].mIndex = SRT_RES_IDX(SpriteResources, Persistent, gImageTexture);
    descriptors[0].ppTextures = &entry.texture;
    descriptors[0].mCount = 1;
    descriptors[1].mIndex = SRT_RES_IDX(SpriteResources, Persistent, gImageSampler);
    descriptors[1].ppSamplers = &sampler_;
    descriptors[1].mCount = 1;
    updateDescriptorSet(renderer_, 0, entry.descriptorSet, 2, descriptors);
    error.Clear();
    return true;
}

/**
 * 画像をcacheへ登録し、未登録ならGPU転送を予約する。
 */
bool TextureCache::Prepare(detail::ImageResource* image, String& error, ETextureColorSpace colorSpace, bool fullMipChain)
{
    if (!renderer_ || !queue_ || !image)
        return SetError(error, "The image texture cache is not ready");
    if (colorSpace != ETextureColorSpace::Srgb && colorSpace != ETextureColorSpace::Linear)
        return SetError(error, "The image texture color space is invalid");
    // 1x1画像はfull-chain指定でも単一mip資源として共有する。
    fullMipChain = fullMipChain && (image->width > 1 || image->height > 1);
    Entry* entry = Find(image, colorSpace, fullMipChain);
    if (entry)
    {
        entry->state.lastUsed = ++clock_;
        entry->state.frameUsed = frame_;
        error.Clear();
        return true;
    }
    const uint64_t pixelCount = static_cast<uint64_t>(image->width) * image->height;
    if (!image->width || !image->height || image->width > 16384 || image->height > 16384 || pixelCount > 64u * 1024u * 1024u || pixelCount * 4u > image->rgba.Count())
        return SetError(error, "The image pixel buffer does not match a supported texture size");
    // GPU cache byte budgetへ加える全mip段の論理byte数。
    uint64_t imageBytes = pixelCount * 4u;
    // texture記述へ渡すmip段数。
    uint32_t mipLevels = 1;
    if (fullMipChain)
    {
        // GPU確保前に上限と全段数を決める配置表。
        Array<FTextureMipLevel> plannedLevels;
        if (!PlanTextureMipChain(image->width, image->height, true, plannedLevels, imageBytes, error))
            return false;
        mipLevels = plannedLevels.Count();
    }
    entry = AcquireSlot(imageBytes, error);
    if (!entry)
        return false;
    // upload stagingへコピーするまで保持するCPU mip画像列。
    FTextureMipChain* mipChain = nullptr;
    if (fullMipChain)
    {
        try
        {
            mipChain = new FTextureMipChain;
        }
        catch (...)
        {
            return SetError(error, "Texture mip chain allocation failed");
        }
        if (!BuildTextureMipChain(*image, colorSpace, true, *mipChain, error))
        {
            delete mipChain;
            return false;
        }
        if (mipChain->levels.Count() != mipLevels || mipChain->rgba.Count() != imageBytes)
        {
            delete mipChain;
            return SetError(error, "Texture mip chain did not match its planned layout");
        }
    }
    entry->mipLevels = mipLevels;
    entry->fullMipChain = fullMipChain;
    entry->mipChain = mipChain;
    if (!CreateTexture(*entry, image, colorSpace, error))
    {
        DestroyEntry(*entry);
        return false;
    }
    Retain(&image->reference);
    entry->image = image;
    entry->colorSpace = colorSpace;
    entry->byteSize = imageBytes;
    cachedBytes_ += imageBytes;
    entry->state.lastUsed = ++clock_;
    entry->state.frameUsed = frame_;
    entry->state.occupied = true;
    entry->uploadPending = true;
    error.Clear();
    return true;
}

/**
 * model画像4 roleを登録し、色空間別textureとdescriptorを用意する。
 */
bool TextureCache::PrepareModel(detail::ImageResource* baseImage, detail::ImageResource* metallicRoughnessImage, String& error, detail::ImageResource* normalImage, const detail::FTextureSampler* baseSampler, const detail::FTextureSampler* metallicRoughnessSampler, const detail::FTextureSampler* normalSampler, detail::ImageResource* emissiveImage, const detail::FTextureSampler* emissiveSampler)
{
    if (!renderer_ || !queue_ || !baseImage || !metallicRoughnessImage)
        return SetError(error, "The model texture set is invalid");
    // 法線画像がなければlinear MR画像をdescriptor用fallbackとして共有する。
    detail::ImageResource* resolvedNormalImage = normalImage ? normalImage : metallicRoughnessImage;
    // 自己発光画像がなければsRGB基本色画像をdescriptor用fallbackとして共有する。
    detail::ImageResource* resolvedEmissiveImage = emissiveImage ? emissiveImage : baseImage;
    // nullptrは既存caller向けのClampLinear設定へ解決する。
    const detail::FTextureSampler resolvedBaseSampler = baseSampler ? *baseSampler : detail::FTextureSampler{};
    const detail::FTextureSampler resolvedMetallicRoughnessSampler = metallicRoughnessSampler ? *metallicRoughnessSampler : detail::FTextureSampler{};
    const detail::FTextureSampler resolvedNormalSampler = normalSampler ? *normalSampler : detail::FTextureSampler{};
    // nullptrは既存roleと同じClampLinear設定へ解決する。
    const detail::FTextureSampler resolvedEmissiveSampler = emissiveSampler ? *emissiveSampler : detail::FTextureSampler{};
    // 全roleを検証してからtextureやsamplerのresourceを作る。
    uint32_t samplerIndex = 0;
    if (!detail::GetTextureSamplerIndex(resolvedBaseSampler, samplerIndex) || !detail::GetTextureSamplerIndex(resolvedMetallicRoughnessSampler, samplerIndex) || !detail::GetTextureSamplerIndex(resolvedNormalSampler, samplerIndex) || !detail::GetTextureSamplerIndex(resolvedEmissiveSampler, samplerIndex))
        return SetError(error, "The model texture sampler settings are invalid");
    // 各画像役割で要求するmip段の有無。
    const bool baseMipChain = resolvedBaseSampler.mipFilter != detail::ETextureMipFilter::None;
    // MR画像用のmip段要求。
    const bool metallicRoughnessMipChain = resolvedMetallicRoughnessSampler.mipFilter != detail::ETextureMipFilter::None;
    // normal画像用のmip段要求。
    const bool normalMipChain = resolvedNormalSampler.mipFilter != detail::ETextureMipFilter::None;
    // 自己発光画像はsRGB roleとして準備する。
    const bool emissiveMipChain = resolvedEmissiveSampler.mipFilter != detail::ETextureMipFilter::None;
    if (!Prepare(baseImage, error, ETextureColorSpace::Srgb, baseMipChain) || !Prepare(metallicRoughnessImage, error, ETextureColorSpace::Linear, metallicRoughnessMipChain) || !Prepare(resolvedNormalImage, error, ETextureColorSpace::Linear, normalMipChain) || !Prepare(resolvedEmissiveImage, error, ETextureColorSpace::Srgb, emissiveMipChain))
        return false;
    // 用途ごとの色空間で登録された4種類のtexture。
    Entry* baseEntry = Find(baseImage, ETextureColorSpace::Srgb, baseMipChain && (baseImage->width > 1 || baseImage->height > 1));
    Entry* metallicRoughnessEntry = Find(metallicRoughnessImage, ETextureColorSpace::Linear, metallicRoughnessMipChain && (metallicRoughnessImage->width > 1 || metallicRoughnessImage->height > 1));
    Entry* normalEntry = Find(resolvedNormalImage, ETextureColorSpace::Linear, normalMipChain && (resolvedNormalImage->width > 1 || resolvedNormalImage->height > 1));
    Entry* emissiveEntry = Find(resolvedEmissiveImage, ETextureColorSpace::Srgb, emissiveMipChain && (resolvedEmissiveImage->width > 1 || resolvedEmissiveImage->height > 1));
    if (!baseEntry || !metallicRoughnessEntry || !normalEntry || !emissiveEntry)
        return SetError(error, "The prepared model texture set is unavailable");

    // 画像4 roleに対応するdescriptor cache entry。
    ModelEntry* modelEntry = FindModel(baseImage, metallicRoughnessImage, resolvedNormalImage, resolvedEmissiveImage, resolvedBaseSampler, resolvedMetallicRoughnessSampler, resolvedNormalSampler, resolvedEmissiveSampler);
    if (modelEntry)
    {
        modelEntry->state.lastUsed = ++clock_;
        modelEntry->state.frameUsed = frame_;
        error.Clear();
        return true;
    }
    // roleごとに固定sampler cacheからdescriptor資源を得る。
    Sampler* baseSamplerResource = nullptr;
    Sampler* metallicRoughnessSamplerResource = nullptr;
    Sampler* normalSamplerResource = nullptr;
    // 自己発光画像のsampler資源。
    Sampler* emissiveSamplerResource = nullptr;
    if (!GetOrCreateModelSampler(resolvedBaseSampler, baseSamplerResource, error) || !GetOrCreateModelSampler(resolvedMetallicRoughnessSampler, metallicRoughnessSamplerResource, error) || !GetOrCreateModelSampler(resolvedNormalSampler, normalSamplerResource, error) || !GetOrCreateModelSampler(resolvedEmissiveSampler, emissiveSamplerResource, error))
        return false;
    modelEntry = AcquireModelSlot(error);
    if (!modelEntry)
        return false;
    modelEntry->baseImage = baseImage;
    modelEntry->metallicRoughnessImage = metallicRoughnessImage;
    modelEntry->normalImage = resolvedNormalImage;
    modelEntry->emissiveImage = resolvedEmissiveImage;
    modelEntry->baseTexture = baseEntry->texture;
    modelEntry->metallicRoughnessTexture = metallicRoughnessEntry->texture;
    modelEntry->normalTexture = normalEntry->texture;
    modelEntry->emissiveTexture = emissiveEntry->texture;
    modelEntry->baseSampler = resolvedBaseSampler;
    modelEntry->metallicRoughnessSampler = resolvedMetallicRoughnessSampler;
    modelEntry->normalSampler = resolvedNormalSampler;
    modelEntry->emissiveSampler = resolvedEmissiveSampler;
    modelEntry->baseSamplerResource = baseSamplerResource;
    modelEntry->metallicRoughnessSamplerResource = metallicRoughnessSamplerResource;
    modelEntry->normalSamplerResource = normalSamplerResource;
    modelEntry->emissiveSamplerResource = emissiveSamplerResource;
    if (!CreateModelDescriptor(*modelEntry, *baseEntry, *metallicRoughnessEntry, *normalEntry, *emissiveEntry, error))
    {
        DestroyModelEntry(*modelEntry);
        return false;
    }
    modelEntry->state.lastUsed = ++clock_;
    modelEntry->state.frameUsed = frame_;
    modelEntry->state.occupied = true;
    error.Clear();
    return true;
}

/**
 * 有効なsampler値に対応する共有GPU resourceを得る。
 */
bool TextureCache::GetOrCreateModelSampler(const detail::FTextureSampler& settings, Sampler*& sampler, String& error)
{
    // 108状態の固定table内でsampler値の位置を得る。
    uint32_t samplerIndex = 0;
    if (!detail::GetTextureSamplerIndex(settings, samplerIndex))
        return SetError(error, "The model texture sampler settings are invalid");
    if (!modelSamplers_[samplerIndex])
    {
        // glTFのfilterと座標範囲をGPU設定へ変換する。
        SamplerDesc samplerDesc{};
        samplerDesc.mMinFilter = settings.minFilter == detail::ETextureFilter::Linear ? FILTER_LINEAR : FILTER_NEAREST;
        samplerDesc.mMagFilter = settings.magFilter == detail::ETextureFilter::Linear ? FILTER_LINEAR : FILTER_NEAREST;
        samplerDesc.mMipMapMode = settings.mipFilter == detail::ETextureMipFilter::Linear ? MIPMAP_MODE_LINEAR : MIPMAP_MODE_NEAREST;
        samplerDesc.mAddressU = settings.addressU == detail::ETextureAddressMode::ClampToEdge ? ADDRESS_MODE_CLAMP_TO_EDGE : settings.addressU == detail::ETextureAddressMode::Repeat ? ADDRESS_MODE_REPEAT : ADDRESS_MODE_MIRROR;
        samplerDesc.mAddressV = settings.addressV == detail::ETextureAddressMode::ClampToEdge ? ADDRESS_MODE_CLAMP_TO_EDGE : settings.addressV == detail::ETextureAddressMode::Repeat ? ADDRESS_MODE_REPEAT : ADDRESS_MODE_MIRROR;
        samplerDesc.mAddressW = ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerDesc.mMipLodBias = 0.0f;
        // 参照段数の上限を0にすると縮小filterが拡大filterとして選ばれるため、sampler側の範囲を開ける。
        // texture側のmip段数に合わせて縮小時の段選択を有効にする。
        samplerDesc.mSetLodRange = true;
        samplerDesc.mMinLod = 0.0f;
        samplerDesc.mMaxLod = FLT_MAX;
        samplerDesc.mMaxAnisotropy = 0.0f;
        samplerDesc.mCompareFunc = CMP_NEVER;
        addSampler(renderer_, &samplerDesc, &modelSamplers_[samplerIndex]);
        if (!modelSamplers_[samplerIndex])
            return SetError(error, "The Forge could not create a model texture sampler");
    }
    sampler = modelSamplers_[samplerIndex];
    error.Clear();
    return true;
}

/**
 * 準備済み4画像をmodel shader用のpersistent descriptorへ登録する。
 */
bool TextureCache::CreateModelDescriptor(ModelEntry& modelEntry, Entry& baseEntry, Entry& metallicRoughnessEntry, Entry& normalEntry, Entry& emissiveEntry, String& error)
{
    // model texture SRTのpersistent descriptor配置。
    DescriptorSetDesc descriptorDesc = SRT_SET_DESC(ModelTextureResources, Persistent, 1, 0);
    addDescriptorSet(renderer_, &descriptorDesc, &modelEntry.descriptorSet);
    if (!modelEntry.descriptorSet)
        return SetError(error, "The Forge could not allocate a model texture descriptor set");

    // 画像4枚とrole別sampler4個のdescriptor値。
    DescriptorData descriptors[8]{};
    descriptors[0].mIndex = SRT_RES_IDX(ModelTextureResources, Persistent, gImageTexture);
    descriptors[0].ppTextures = &baseEntry.texture;
    descriptors[0].mCount = 1;
    descriptors[1].mIndex = SRT_RES_IDX(ModelTextureResources, Persistent, gMetallicRoughnessTexture);
    descriptors[1].ppTextures = &metallicRoughnessEntry.texture;
    descriptors[1].mCount = 1;
    descriptors[2].mIndex = SRT_RES_IDX(ModelTextureResources, Persistent, gNormalTexture);
    descriptors[2].ppTextures = &normalEntry.texture;
    descriptors[2].mCount = 1;
    descriptors[3].mIndex = SRT_RES_IDX(ModelTextureResources, Persistent, gEmissiveTexture);
    descriptors[3].ppTextures = &emissiveEntry.texture;
    descriptors[3].mCount = 1;
    descriptors[4].mIndex = SRT_RES_IDX(ModelTextureResources, Persistent, gImageSampler);
    descriptors[4].ppSamplers = &modelEntry.baseSamplerResource;
    descriptors[4].mCount = 1;
    descriptors[5].mIndex = SRT_RES_IDX(ModelTextureResources, Persistent, gMetallicRoughnessSampler);
    descriptors[5].ppSamplers = &modelEntry.metallicRoughnessSamplerResource;
    descriptors[5].mCount = 1;
    descriptors[6].mIndex = SRT_RES_IDX(ModelTextureResources, Persistent, gNormalSampler);
    descriptors[6].ppSamplers = &modelEntry.normalSamplerResource;
    descriptors[6].mCount = 1;
    descriptors[7].mIndex = SRT_RES_IDX(ModelTextureResources, Persistent, gEmissiveSampler);
    descriptors[7].ppSamplers = &modelEntry.emissiveSamplerResource;
    descriptors[7].mCount = 1;
    updateDescriptorSet(renderer_, 0, modelEntry.descriptorSet, 8, descriptors);
    error.Clear();
    return true;
}

/**
 * このframeで使う画像pixelをresource loaderの転送領域へ書き込む。
 */
bool TextureCache::UploadPending(String& error)
{
    for (uint32_t i = 0; i < kCapacity; ++i)
    {
        Entry& entry = entries_[i];
        if (!entry.image || !entry.uploadPending || entry.state.frameUsed != frame_)
            continue;
        TextureUpdateDesc update{};
        update.pTexture = entry.texture;
        update.mBaseMipLevel = 0;
        update.mMipLevels = entry.mipLevels;
        update.mBaseArrayLayer = 0;
        update.mLayerCount = 1;
        update.mCurrentState = RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        update.pCmd = nullptr;
        beginUpdateResource(&update);
        for (uint32_t mip = 0; mip < entry.mipLevels; ++mip)
        {
            // source mipの寸法と連続byte列内の位置。
            const FTextureMipLevel* level = entry.mipChain ? &entry.mipChain->levels.At(mip) : nullptr;
            const uint32_t sourceWidth = level ? level->width : entry.image->width;
            const uint32_t sourceHeight = level ? level->height : entry.image->height;
            const uint8_t* sourcePixels = level ? entry.mipChain->rgba.Data() + level->offset : entry.image->rgba.Data();
            const uint32_t sourceRowBytes = sourceWidth * 4u;
            TextureSubresourceUpdate subresource = update.getSubresourceUpdateDesc(mip, 0);
            if (subresource.mSrcRowStride != sourceRowBytes || !subresource.pMappedData || subresource.mDstRowStride < sourceRowBytes || subresource.mRowCount != sourceHeight)
            {
                endUpdateResource(&update);
                uploadSubmissionPending_ = true;
                return SetError(error, "The Forge returned an incompatible image mip upload layout");
            }
            for (uint32_t row = 0; row < sourceHeight; ++row)
            {
                memcpy(subresource.pMappedData + row * subresource.mDstRowStride, sourcePixels + row * sourceRowBytes, sourceRowBytes);
            }
        }
        endUpdateResource(&update);
        delete entry.mipChain;
        entry.mipChain = nullptr;
        entry.uploadPending = false;
        uploadSubmissionPending_ = true;
    }
    error.Clear();
    return true;
}

/**
 * 記録済み転送を提出し、graphics queueが待つ同期情報を返す。
 */
bool TextureCache::FlushPendingUploads(FlushResourceUpdateDesc& flush, String& error)
{
    flush = {};
    if (!uploadSubmissionPending_)
    {
        error.Clear();
        return true;
    }
    flush.mNodeIndex = 0;
    flushResourceUpdates(&flush);
    uploadSubmissionPending_ = false;
    uploadFence_ = flush.pOutFence;
    if (!flush.pOutFence || !flush.pOutSubmittedSemaphore)
        return SetError(error, "The Forge could not provide image upload synchronization");
    error.Clear();
    return true;
}

/**
 * 未提出または実行中の転送を完了させ、資源を安全に扱える状態にする。
 */
bool TextureCache::DrainPendingUploads(String& error)
{
    if (uploadSubmissionPending_)
    {
        FlushResourceUpdateDesc flush{};
        if (!FlushPendingUploads(flush, error) && !uploadFence_)
            return false;
    }
    if (uploadFence_)
    {
        waitForFences(renderer_, 1, &uploadFence_);
        uploadFence_ = nullptr;
    }
    error.Clear();
    return true;
}

/**
 * このframeで準備済みの画像descriptorをcommandへbindする。
 */
bool TextureCache::Bind(Cmd* command, detail::ImageResource* image, String& error) const
{
    if (!command || !image)
        return SetError(error, "The image descriptor binding is invalid");
    // 既存画像APIと同じsRGB variant。
    const Entry* entry = nullptr;
    for (uint32_t i = 0; i < kCapacity; ++i)
    {
        if (entries_[i].image == image && entries_[i].colorSpace == ETextureColorSpace::Srgb && !entries_[i].fullMipChain)
        {
            entry = &entries_[i];
            break;
        }
    }
    if (!entry || entry->state.frameUsed != frame_)
        return SetError(error, "The sRGB image was not prepared for this frame");
    if (!entry->descriptorSet)
        return SetError(error, "The image descriptor set is unavailable");
    cmdBindDescriptorSet(command, 0, entry->descriptorSet);
    error.Clear();
    return true;
}

/**
 * frame用に準備済みのmodel画像descriptor setをbindする。
 */
bool TextureCache::BindModel(Cmd* command, detail::ImageResource* baseImage, detail::ImageResource* metallicRoughnessImage, String& error, detail::ImageResource* normalImage, const detail::FTextureSampler* baseSampler, const detail::FTextureSampler* metallicRoughnessSampler, const detail::FTextureSampler* normalSampler, detail::ImageResource* emissiveImage, const detail::FTextureSampler* emissiveSampler) const
{
    if (!command || !baseImage || !metallicRoughnessImage)
        return SetError(error, "The model texture set binding is invalid");
    // 法線画像がなければPrepareModelと同じMR画像を検索keyにする。
    const detail::ImageResource* resolvedNormalImage = normalImage ? normalImage : metallicRoughnessImage;
    // 自己発光画像がなければPrepareModelと同じ基本色画像を検索keyにする。
    const detail::ImageResource* resolvedEmissiveImage = emissiveImage ? emissiveImage : baseImage;
    // nullptrはPrepareModelと同じClampLinear設定へ解決する。
    const detail::FTextureSampler resolvedBaseSampler = baseSampler ? *baseSampler : detail::FTextureSampler{};
    const detail::FTextureSampler resolvedMetallicRoughnessSampler = metallicRoughnessSampler ? *metallicRoughnessSampler : detail::FTextureSampler{};
    const detail::FTextureSampler resolvedNormalSampler = normalSampler ? *normalSampler : detail::FTextureSampler{};
    // nullptrはPrepareModelと同じClampLinear設定へ解決する。
    const detail::FTextureSampler resolvedEmissiveSampler = emissiveSampler ? *emissiveSampler : detail::FTextureSampler{};
    // 不正samplerはcache検索前に拒否する。
    uint32_t samplerIndex = 0;
    if (!detail::GetTextureSamplerIndex(resolvedBaseSampler, samplerIndex) || !detail::GetTextureSamplerIndex(resolvedMetallicRoughnessSampler, samplerIndex) || !detail::GetTextureSamplerIndex(resolvedNormalSampler, samplerIndex) || !detail::GetTextureSamplerIndex(resolvedEmissiveSampler, samplerIndex))
        return SetError(error, "The model texture sampler settings are invalid");
    for (uint32_t i = 0; i < kModelCapacity; ++i)
    {
        // 現frameに準備された画像4 role。
        const ModelEntry& entry = modelEntries_[i];
        if (entry.baseImage != baseImage || entry.metallicRoughnessImage != metallicRoughnessImage || entry.normalImage != resolvedNormalImage || entry.emissiveImage != resolvedEmissiveImage || !detail::AreTextureSamplersEqual(entry.baseSampler, resolvedBaseSampler) || !detail::AreTextureSamplersEqual(entry.metallicRoughnessSampler, resolvedMetallicRoughnessSampler) || !detail::AreTextureSamplersEqual(entry.normalSampler, resolvedNormalSampler) || !detail::AreTextureSamplersEqual(entry.emissiveSampler, resolvedEmissiveSampler) || entry.state.frameUsed != frame_)
            continue;
        if (!entry.descriptorSet)
            return SetError(error, "The model texture descriptor set is unavailable");
        cmdBindDescriptorSet(command, 0, entry.descriptorSet);
        error.Clear();
        return true;
    }
    return SetError(error, "The model texture set was not prepared for this frame");
}

/**
 * entryのdescriptor、texture、保持参照を解放する。
 */
void TextureCache::DestroyEntry(Entry& entry)
{
    InvalidateModelEntries(entry.texture);
    if (renderer_ && entry.descriptorSet)
    {
        removeDescriptorSet(renderer_, entry.descriptorSet);
    }
    if (entry.texture)
        removeResource(entry.texture);
    if (entry.image)
    {
        cachedBytes_ -= entry.byteSize;
        Release(&entry.image->reference);
    }
    delete entry.mipChain;
    entry = {};
}

/**
 * textureを参照するmodel descriptorを解放する。
 */
void TextureCache::InvalidateModelEntries(Texture* texture)
{
    if (!texture)
        return;
    for (uint32_t i = 0; i < kModelCapacity; ++i)
    {
        ModelEntry& entry = modelEntries_[i];
        if (entry.baseTexture == texture || entry.metallicRoughnessTexture == texture || entry.normalTexture == texture || entry.emissiveTexture == texture)
            DestroyModelEntry(entry);
    }
}

/**
 * model descriptorを解放し、借用texture参照を忘れる。
 */
void TextureCache::DestroyModelEntry(ModelEntry& entry)
{
    if (renderer_ && entry.descriptorSet)
        removeDescriptorSet(renderer_, entry.descriptorSet);
    entry = {};
}

}

#endif
