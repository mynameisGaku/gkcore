#pragma once

#if defined(_WIN32) && defined(DIRECT3D12)

#include "../resources/Resources.h"
#include "TextureCachePolicy.h"

#include <Graphics/Interfaces/IGraphics.h>
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

/**
 * 読み込んだ画像を保持し、Direct3D 12用textureを上限付きで管理する。
 */
namespace gk::render
{

/**
 * GPUへ転送したtextureがcacheにある間、元画像のpixelを保持する。
 */
class TextureCache
{
  public:
    /**
     * 画像textureで共用するsamplerを作成する。
     */
    bool Initialize(Renderer* renderer, Queue* queue, String& error);
    /**
     * GPUの使用完了を待ち、cache画像とdescriptorを解放する。
     */
    void Shutdown();
    /**
     * frameを開始し、そのframeで使うdescriptorを追い出し対象から外す。
     */
    void BeginFrame();
    /**
     * 保持中の画像に対応するtextureとdescriptor setを用意する。
     */
    bool Prepare(detail::ImageResource* image, String& error);
    /**
     * The Forgeのresource loaderへ画像転送を記録する。
     */
    bool UploadPending(String& error);
    /**
     * 記録済みtexture転送をgraphics queueで待つ必要があるか返す。
     */
    bool HasPendingSubmission() const
    {
        return uploadSubmissionPending_;
    }
    /**
     * 転送を提出し、graphics queueへの提出または完了待ちまでfenceを保持する。
     */
    bool FlushPendingUploads(FlushResourceUpdateDesc& flush, String& error);
    /**
     * 未提出の転送batchを待ってからtextureを解放する。
     */
    bool DrainPendingUploads(String& error);
    /**
     * 同期semaphoreの提出後に転送状態を消去する。
     */
    void MarkSubmitted()
    {
        uploadSubmissionPending_ = false;
        uploadFence_ = nullptr;
    }
    /**
     * frame用に準備済みの画像descriptor setをbindする。
     */
    bool Bind(Cmd* command, detail::ImageResource* image, String& error) const;

  private:
    /**
     * 画像ごとの転送状態、GPU資源、保持byte数を記録する。
     */
    struct Entry
    {
        TextureCacheSlotState state;
        detail::ImageResource* image;
        Texture* texture;
        DescriptorSet* descriptorSet;
        uint64_t byteSize;
        bool uploadPending;
    };

    static constexpr uint32_t kCapacity = 128;
    static constexpr uint64_t kByteCapacity = 256u * 1024u * 1024u;
    /**
     * 指定画像のcache記録があれば返す。
     */
    Entry* Find(detail::ImageResource* image);
    /**
     * entry数とbyte上限を守ってslotを確保し、安全な記録だけを追い出す。
     */
    Entry* AcquireSlot(uint64_t imageBytes, String& error);
    /**
     * 検証済み画像に対応するGPU textureとdescriptorを作る。
     */
    bool CreateTexture(Entry& entry, detail::ImageResource* image, String& error);
    /**
     * 1件分のdescriptorとtextureを解放してcache記録を初期化する。
     */
    void DestroyEntry(Entry& entry);

    Renderer* renderer_ = nullptr;
    Queue* queue_ = nullptr;
    Sampler* sampler_ = nullptr;
    Fence* uploadFence_ = nullptr;
    uint64_t clock_ = 0;
    uint64_t frame_ = 0;
    uint64_t cachedBytes_ = 0;
    bool uploadSubmissionPending_ = false;
    Entry entries_[kCapacity]{};
};

}

#endif
