// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELSKINNINGGEOMETRYCACHE_H
#define GKCORE_RENDER_FMODELSKINNINGGEOMETRYCACHE_H

#include "render/FModelSkinningDispatch.h"
#include "model/animation/FModelGpuSkinningGeometry.h"
#include "model/animation/FModelSparseVertexMap.h"
#include "resources/Resources.h"
#include <stdint.h>

#if defined(_WIN32) && defined(DIRECT3D12)
#include <Graphics/Interfaces/IGraphics.h>

namespace gk::render
{

/**
 * 不変なskin入力recordをGPU_ONLY bufferへ保持する。
 */
class FModelSkinningGeometryCache
{
  public:
    /**
     * GPU resourceを持たないcacheを作る。
     */
    FModelSkinningGeometryCache();
    /**
     * GPU bufferと保持中model参照を解放する。
     */
    ~FModelSkinningGeometryCache();

    FModelSkinningGeometryCache(const FModelSkinningGeometryCache&) = delete;
    FModelSkinningGeometryCache& operator=(const FModelSkinningGeometryCache&) = delete;

    /**
     * rendererとqueueを借用し、空のcacheを初期化する。
     */
    bool Initialize(Renderer* renderer, Queue* queue, String& error);
    /**
     * 新しいframeを開始し、古いentryを追い出し可能にする。
     */
    void BeginFrame();
    /**
     * queue完了を待ってbufferと保持参照を解放する。
     */
    void Shutdown();
    /**
     * 不変geometryを一度だけuploadし、GPU bufferとrecord範囲を返す。
     * budgetを空けられない場合は成功扱いでbufferをnullにし、呼び出し側が別経路へ戻る。
     * 入力・resourceの失敗時はfalseを返し、dispatchを変更しない。
     */
    bool Prepare(const detail::ModelResource& owner, const model::animation::FModelGpuSkinningGeometry& geometry, const Array<model::animation::FModelSparseVertexMap>& sparseMap, Buffer*& buffer, FModelSkinningDispatch& dispatch, bool& cacheHit, String& error);

  private:
    struct Entry;

    /**
     * 同じmodel resourceとimmutable geometryのentryを探す。
     */
    bool Matches(const Entry& entry, const detail::ModelResource& owner, const model::animation::FModelGpuSkinningGeometry& geometry) const;
    /**
     * byte budget内に収まるよう、現在frameで使っていないentryを解放する。
     */
    bool MakeRoom(uint64_t bytes, String& error);
    /**
     * entryのbuffer、model参照、budgetを解放する。
     */
    void DestroyEntry(Entry* entry);

    Renderer* renderer_ = nullptr; // buffer作成に使う借用renderer。
    Queue* queue_ = nullptr;       // resource解放前の待機に使う借用queue。
    Array<Entry*> entries_;        // cache entryを個別所有する配列。
    uint64_t frameSerial_ = 0;     // 現在のframe番号。
    uint64_t usedBytes_ = 0;       // cacheが保持するGPU bufferのbyte数。
};

}

#endif

#endif
