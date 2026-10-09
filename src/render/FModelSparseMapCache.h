// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELSPARSEMAPCACHE_H
#define GKCORE_RENDER_FMODELSPARSEMAPCACHE_H

#include "internal/Backend.hpp"

#if defined(_WIN32) && defined(DIRECT3D12)
#include <Graphics/Interfaces/IGraphics.h>

/**
 * animation sourceの不変sparse mapをGPU bufferへ保持するcache。
 */
namespace gk::render
{

/**
 * pose snapshotから変形頂点を引くsource mapを、元model単位で再利用するservice。
 */
class FModelSparseMapCache
{
  public:
    /**
     * Rendererとgraphics queueを借用する空のcacheを作る。
     */
    FModelSparseMapCache();
    /**
     * queue完了後にbufferと保持参照を解放する。
     */
    ~FModelSparseMapCache();

    FModelSparseMapCache(const FModelSparseMapCache&) = delete;
    FModelSparseMapCache& operator=(const FModelSparseMapCache&) = delete;

    /**
     * Rendererとgraphics queueを設定する。失敗時は理由を返す。
     */
    bool Initialize(Renderer* renderer, Queue* queue, String& error);
    /**
     * 次のframeを始め、以前に使ったentryをeviction可能にする。
     */
    void BeginFrame();
    /**
     * queueを待機し、cache bufferと元model参照をすべて解放する。
     */
    void Shutdown();
    /**
     * 元modelのsparse mapを探し、必要ならGPU_ONLY structured bufferへuploadする。
     * cache容量不足や未対応sourceでは失敗し、呼出側がCPU変形へ戻れる。
     */
    bool Prepare(const detail::ModelResource& baseModel, Buffer*& map, String& error);

  private:
    struct Entry;

    /**
     * 未使用entryを追い出し、指定byte数を確保する。
     */
    bool MakeRoom(uint64_t bytes, String& error);
    /**
     * GPU bufferと保持中の元model参照を解放する。
     */
    void DestroyEntry(Entry* entry);

    // buffer生成と解放に使う借用renderer。
    Renderer* renderer_;
    // uploadとevictionの完了を待つ借用queue。
    Queue* queue_;
    // 非自明entryを個別に所有する配列。
    Array<Entry*> entries_;
    // 現在のframe番号。
    uint64_t frameSerial_;
    // cacheが確保中のGPU map byte数。
    uint64_t usedBytes_;
};

// namespace gk::render
}

#endif

#endif
