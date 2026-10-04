#include "TextureCache.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/FSL/defaults.h>
#include "../../shaders/gkcore_sprite.srt.h"
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

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
        for (uint32_t i = 0; i < kCapacity; ++i)
        {
            if (entries_[i].image)
                Release(&entries_[i].image->reference);
            entries_[i] = {};
        }
        renderer_ = nullptr;
        queue_ = nullptr;
        sampler_ = nullptr;
        clock_ = 0;
        frame_ = 0;
        cachedBytes_ = 0;
        uploadSubmissionPending_ = false;
        uploadFence_ = nullptr;
        return;
    }
    for (uint32_t i = 0; i < kCapacity; ++i)
        DestroyEntry(entries_[i]);
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
    }
}

/**
 * 画像pointerに対応するcache entryを検索する。
 */
TextureCache::Entry* TextureCache::Find(detail::ImageResource* image)
{
    for (uint32_t i = 0; i < kCapacity; ++i)
    {
        if (entries_[i].image == image)
            return &entries_[i];
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
 * 画像に合う2D textureを作成し、textureとsamplerのdescriptorを登録する。
 */
bool TextureCache::CreateTexture(Entry& entry, detail::ImageResource* image, String& error)
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
    textureDesc.mMipLevels = 1;
    textureDesc.mSampleCount = SAMPLE_COUNT_1;
    textureDesc.mFormat = TinyImageFormat_R8G8B8A8_SRGB;
    textureDesc.mStartState = RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
    // Tex2D画像の小さなfallbackも2D資源として作る。
    textureDesc.mFlags = TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT | TEXTURE_CREATION_FLAG_FORCE_2D;
    textureDesc.pName = "gkcore image";

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
bool TextureCache::Prepare(detail::ImageResource* image, String& error)
{
    if (!renderer_ || !queue_ || !image)
        return SetError(error, "The image texture cache is not ready");
    Entry* entry = Find(image);
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
    const uint64_t imageBytes = pixelCount * 4u;
    entry = AcquireSlot(imageBytes, error);
    if (!entry)
        return false;
    if (!CreateTexture(*entry, image, error))
    {
        DestroyEntry(*entry);
        return false;
    }
    Retain(&image->reference);
    entry->image = image;
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
        update.mMipLevels = 1;
        update.mBaseArrayLayer = 0;
        update.mLayerCount = 1;
        update.mCurrentState = RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        update.pCmd = nullptr;
        beginUpdateResource(&update);
        TextureSubresourceUpdate subresource = update.getSubresourceUpdateDesc(0, 0);
        const uint32_t sourceRowBytes = entry.image->width * 4u;
        if (subresource.mSrcRowStride != sourceRowBytes || !subresource.pMappedData || subresource.mDstRowStride < sourceRowBytes || subresource.mRowCount != entry.image->height)
        {
            endUpdateResource(&update);
            uploadSubmissionPending_ = true;
            return SetError(error, "The Forge returned an incompatible image upload layout");
        }
        for (uint32_t row = 0; row < entry.image->height; ++row)
        {
            memcpy(subresource.pMappedData + row * subresource.mDstRowStride, entry.image->rgba.Data() + row * sourceRowBytes, sourceRowBytes);
        }
        endUpdateResource(&update);
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
    for (uint32_t i = 0; i < kCapacity; ++i)
    {
        const Entry& entry = entries_[i];
        if (entry.image != image || entry.state.frameUsed != frame_)
            continue;
        if (!entry.descriptorSet)
            return SetError(error, "The image descriptor set is unavailable");
        cmdBindDescriptorSet(command, 0, entry.descriptorSet);
        error.Clear();
        return true;
    }
    return SetError(error, "The image was not prepared for this frame");
}

/**
 * entryのdescriptor、texture、保持参照を解放する。
 */
void TextureCache::DestroyEntry(Entry& entry)
{
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
    entry = {};
}

}

#endif
