// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_TEXTURECACHE_H
#define GKCORE_RENDER_TEXTURECACHE_H

#if defined(_WIN32) && defined(DIRECT3D12)

#include "../resources/Resources.h"
#include "ETextureColorSpace.h"
#include "TextureCachePolicy.h"

#include <Graphics/Interfaces/IGraphics.h>
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

/**
 * 読み込んだ画像を保持し、Direct3D 12用textureを上限付きで管理する。
 */
namespace gk::render
{

/**
 * GPUへ転送したtextureとdescriptorを色空間別に管理する。
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
     * 指定色空間の画像textureを用意する。既定はsRGBで、未初期化・不正画像・容量超過では失敗する。
     */
    bool Prepare(detail::ImageResource* image, String& error, ETextureColorSpace colorSpace = ETextureColorSpace::Srgb);
    /**
     * modelの画像tripleを用意する。法線省略時はMR画像を共有し、無効画像・容量超過では失敗する。
     */
    bool PrepareModel(detail::ImageResource* baseImage, detail::ImageResource* metallicRoughnessImage, String& error, detail::ImageResource* normalImage = nullptr);
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
     * frame用に準備済みのsRGB画像descriptor setをbindする。
     */
    bool Bind(Cmd* command, detail::ImageResource* image, String& error) const;
    /**
     * frame用に準備済みのモデル画像triple descriptor setをbindし、未準備または無効な指定では失敗する。
     */
    bool BindModel(Cmd* command, detail::ImageResource* baseImage, detail::ImageResource* metallicRoughnessImage, String& error, detail::ImageResource* normalImage = nullptr) const;

  private:
    /**
     * 画像と色空間ごとの転送状態、GPU資源、保持byte数を記録する。
     */
    struct Entry
    {
        // LRU順と現frameの使用状態。
        TextureCacheSlotState state;
        // 保持参照を持つ元画像。
        detail::ImageResource* image;
        // GPU上でのRGB解釈。
        ETextureColorSpace colorSpace;
        // 色空間別のGPU画像。
        Texture* texture;
        // 従来画像shader用descriptor。linearでは空。
        DescriptorSet* descriptorSet;
        // byte上限へ加算する物理画像サイズ。
        uint64_t byteSize;
        // resource loaderへ転送が必要な状態。
        bool uploadPending;
    };

    /**
     * 3枚のmodel textureを参照するdescriptorとframe使用状態を記録する。
     */
    struct ModelEntry
    {
        // LRU順と現frameの使用状態。
        TextureCacheSlotState state;
        // sRGB基本色画像のtriple key。
        detail::ImageResource* baseImage;
        // linear金属度・粗さ画像のtriple key。
        detail::ImageResource* metallicRoughnessImage;
        // 物理cacheが所有するsRGB texture。
        Texture* baseTexture;
        // 物理cacheが所有するlinear texture。
        Texture* metallicRoughnessTexture;
        // model shader用の3画像descriptor。
        DescriptorSet* descriptorSet;
        // linear法線画像のtriple key。未指定時はMR画像。
        detail::ImageResource* normalImage;
        // 物理cacheが所有するlinear texture。
        Texture* normalTexture;
    };

    static constexpr uint32_t kCapacity = 128;
    static constexpr uint32_t kModelCapacity = 128;
    static constexpr uint64_t kByteCapacity = 256u * 1024u * 1024u;
    /**
     * 指定画像と色空間のcache記録があれば返す。
     */
    Entry* Find(detail::ImageResource* image, ETextureColorSpace colorSpace);
    /**
     * 指定画像tripleのmodel descriptor記録があれば返す。
     */
    ModelEntry* FindModel(detail::ImageResource* baseImage, detail::ImageResource* metallicRoughnessImage, detail::ImageResource* normalImage);
    /**
     * entry数とbyte上限を守ってslotを確保し、安全な記録だけを追い出す。
     */
    Entry* AcquireSlot(uint64_t imageBytes, String& error);
    /**
     * model descriptor数を守ってslotを確保し、未使用tripleだけを追い出す。
     */
    ModelEntry* AcquireModelSlot(String& error);
    /**
     * 検証済み画像に対応する色空間別GPU textureを作る。
     */
    bool CreateTexture(Entry& entry, detail::ImageResource* image, ETextureColorSpace colorSpace, String& error);
    /**
     * 3つの準備済みtextureをmodel shader用descriptorへ登録する。
     */
    bool CreateModelDescriptor(ModelEntry& modelEntry, Entry& baseEntry, Entry& metallicRoughnessEntry, Entry& normalEntry, String& error);
    /**
     * 物理textureを参照するmodel descriptorを先に解放する。
     */
    void InvalidateModelEntries(Texture* texture);
    /**
     * 1件分のdescriptorとtextureを解放してcache記録を初期化する。
     */
    void DestroyEntry(Entry& entry);
    /**
     * model descriptorを解放してtriple記録を初期化する。
     */
    void DestroyModelEntry(ModelEntry& entry);

    Renderer* renderer_ = nullptr;
    Queue* queue_ = nullptr;
    Sampler* sampler_ = nullptr;
    Fence* uploadFence_ = nullptr;
    uint64_t clock_ = 0;
    uint64_t frame_ = 0;
    uint64_t cachedBytes_ = 0;
    bool uploadSubmissionPending_ = false;
    Entry entries_[kCapacity]{};
    ModelEntry modelEntries_[kModelCapacity]{};
};

}

#endif

#endif
