// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELGEOMETRYCACHE_H
#define GKCORE_RENDER_FMODELGEOMETRYCACHE_H

#include "render/ModelDrawPlan.h"
#include "render/ModelGeometry.h"
#include "internal/Backend.hpp"

#if defined(_WIN32) && defined(DIRECT3D12)
#include <Graphics/Interfaces/IGraphics.h>

/**
 * 静止モデルの展開済み頂点bufferをframe間で再利用する。
 */
namespace gk::render
{

/**
 * 静止modelのprimitiveごとにlocal頂点bufferを保持するcache。
 */
class FModelGeometryCache
{
  public:
    /**
     * Rendererとgraphics queueを借用し、空のcacheを初期化する。
     */
    FModelGeometryCache();
    /**
     * 全bufferを解放しcacheの保持領域を破棄する。
     */
    ~FModelGeometryCache();

    FModelGeometryCache(const FModelGeometryCache&) = delete;
    FModelGeometryCache& operator=(const FModelGeometryCache&) = delete;

    /**
     * Rendererとgraphics queueを設定する。失敗時は理由を返す。
     */
    bool Initialize(Renderer* renderer, Queue* queue, String& error);
    /**
     * frame開始を記録し、以前のframeで使ったentryをeviction可能にする。
     */
    void BeginFrame();
    /**
     * queueを待機してからGPU bufferと保持中model参照を解放する。
     */
    void Shutdown();
    /**
     * 元geometryとprimitiveが一致するlocal頂点bufferを探し、なければ一度だけuploadする。
     * pose snapshotは元geometryを再利用し、透明順のindex listは妥当性だけ調べる。
     */
    bool Prepare(const detail::FramePacket& frame, const detail::DrawPacket& draw, const ModelPartPlan& part, const uint32_t* sortedFirstIndices, uint32_t triangleCount, bool& cached, Buffer*& buffer, uint32_t& vertexCount, String& error);

  private:
    struct Entry;

    /**
     * 元model参照とprimitive材質が一致するentryを探す。
     */
    bool Matches(const Entry& entry, const detail::ModelResource& sourceModel, const ModelPartPlan& part) const;
    /**
     * 上限内に収まるまで、未使用frameの古いentryを解放する。
     */
    bool MakeRoom(uint64_t bytes, String& error);
    /**
     * GPU bufferとmodel参照を解放してentryを破棄する。
     */
    void DestroyEntry(Entry* entry);

    // buffer作成に使う借用renderer。
    Renderer* renderer_;
    // resource解放のidle待機に使う借用queue。
    Queue* queue_;
    // 非自明なentryを個別所有するfoundation配列。
    Array<Entry*> entries_;
    // 現在のframe番号。
    uint64_t frameSerial_;
    // cacheが確保中のGPU頂点byte数。
    uint64_t usedBytes_;
};

// namespace gk::render
}

#endif

#endif
