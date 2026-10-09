// SPDX-License-Identifier: NOASSERTION
#include "render/FModelSparseMapCache.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"

#include <Graphics/FSL/defaults.h>
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <stdint.h>

#ifdef new
#undef new
#endif
#ifdef delete
#undef delete
#endif

/**
 * animation sourceの不変sparse mapをGPU bufferへ保持するcache。
 */
namespace gk::render
{
namespace
{

// 同時に保持するsource mapの上限。
constexpr uint32_t kMaximumEntries = 64;
// GPUに保持するsource mapのbyte上限。
constexpr uint64_t kMaximumBytes = 16ull * 1024ull * 1024ull;
static_assert(sizeof(model::animation::FModelSparseVertexMap) == sizeof(uint32_t) * 2, "sparse map must contain two uint32 indices");

/**
 * errorを設定して失敗を返す。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

/**
 * cache key、GPU buffer、保持中の元model参照を所有するentry。
 */
struct FModelSparseMapCache::Entry
{
    // map元となるanimation dataを保持するmodel。
    detail::ModelResource* model = nullptr;
    // GPUが参照するimmutable map buffer。
    Buffer* buffer = nullptr;
    // cache budgetから差し引くbuffer byte数。
    uint64_t byteSize = 0;
    // budget計上済みか示す。
    bool accounted = false;
    // 現在frameで使われたか判定する最終frame番号。
    uint64_t lastUsedFrame = 0;
};

/**
 * 空のsparse map cacheを作る。
 */
FModelSparseMapCache::FModelSparseMapCache() : renderer_(nullptr), queue_(nullptr), frameSerial_(0), usedBytes_(0)
{
}

/**
 * queue完了後に保持bufferと元model参照を解放する。
 */
FModelSparseMapCache::~FModelSparseMapCache()
{
    Shutdown();
}

/**
 * Rendererとgraphics queueを設定する。
 */
bool FModelSparseMapCache::Initialize(Renderer* renderer, Queue* queue, String& error)
{
    if (renderer_ || !renderer || !queue)
        return Fail(error, "The sparse map cache initialization is invalid");
    if (!entries_.Reserve(kMaximumEntries))
        return Fail(error, "Not enough memory to initialize the sparse map cache");
    renderer_ = renderer;
    queue_ = queue;
    frameSerial_ = 0;
    usedBytes_ = 0;
    error.Clear();
    return true;
}

/**
 * 次のframeを始め、frame番号の周回を安全に処理する。
 */
void FModelSparseMapCache::BeginFrame()
{
    if (frameSerial_ == UINT64_MAX)
    {
        frameSerial_ = 1;
        for (uint32_t i = 0; i < entries_.Count(); ++i)
            entries_.At(i)->lastUsedFrame = 0;
        return;
    }
    ++frameSerial_;
}

/**
 * queue処理完了後にcache bufferと元model参照を解放する。
 */
void FModelSparseMapCache::Shutdown()
{
    if (queue_ && entries_.Count())
        waitQueueIdle(queue_);
    for (uint32_t i = 0; i < entries_.Count(); ++i)
        DestroyEntry(entries_.At(i));
    entries_.Clear();
    entries_.Reset();
    renderer_ = nullptr;
    queue_ = nullptr;
    frameSerial_ = 0;
    usedBytes_ = 0;
}

/**
 * byte budgetとentry数を空ける。現在frameで使ったentryは残す。
 */
bool FModelSparseMapCache::MakeRoom(uint64_t bytes, String& error)
{
    if (bytes > kMaximumBytes)
        return false;
    bool waited = false;
    while (entries_.Count() >= kMaximumEntries || usedBytes_ > kMaximumBytes - bytes)
    {
        uint32_t victimIndex = UINT32_MAX;
        uint64_t oldestFrame = UINT64_MAX;
        for (uint32_t i = 0; i < entries_.Count(); ++i)
        {
            // 現在frameで固定されていない最古entry候補。
            const Entry* entry = entries_.At(i);
            if (entry->lastUsedFrame != frameSerial_ && entry->lastUsedFrame < oldestFrame)
            {
                oldestFrame = entry->lastUsedFrame;
                victimIndex = i;
            }
        }
        if (victimIndex == UINT32_MAX)
            return false;
        if (!waited && queue_)
        {
            waitQueueIdle(queue_);
            waited = true;
        }
        // GPU完了待ち後に破棄するentry。
        Entry* victim = entries_.At(victimIndex);
        entries_.RemoveAt(victimIndex);
        DestroyEntry(victim);
    }
    error.Clear();
    return true;
}

/**
 * entryのGPU bufferと保持model参照を解放する。
 */
void FModelSparseMapCache::DestroyEntry(Entry* entry)
{
    if (!entry)
        return;
    if (entry->buffer)
    {
        removeResource(entry->buffer);
        entry->buffer = nullptr;
    }
    if (entry->model)
    {
        Release(&entry->model->reference);
        entry->model = nullptr;
    }
    if (entry->accounted)
    {
        if (usedBytes_ >= entry->byteSize)
            usedBytes_ -= entry->byteSize;
        else
            usedBytes_ = 0;
    }
    delete entry;
}

/**
 * 元modelのsparse map bufferを返し、cache miss時だけGPUへuploadする。
 */
bool FModelSparseMapCache::Prepare(const detail::ModelResource& baseModel, Buffer*& map, String& error)
{
    map = nullptr;
    error.Clear();
    if (!renderer_ || !queue_ || baseModel.isPoseSnapshot || !baseModel.animation || !baseModel.animation->source)
        return Fail(error, "The model has no eligible sparse animation source");
    // animation sourceが所有するcorner対応表の借用参照。
    const auto* sparseMap = baseModel.animation->source->SparseVertexMap();
    if (!sparseMap || sparseMap->Count() == 0 || sparseMap->Count() != baseModel.vertices.Count() || !sparseMap->Data())
        return Fail(error, "The model sparse vertex map does not match its source geometry");

    // structured bufferへ複製するmap payloadのbyte数。
    const uint64_t byteSize = static_cast<uint64_t>(sparseMap->Count()) * sizeof(model::animation::FModelSparseVertexMap);
    if (byteSize == 0 || byteSize > kMaximumBytes)
        return Fail(error, "The model sparse vertex map exceeds the cache budget");
    for (uint32_t i = 0; i < entries_.Count(); ++i)
    {
        // cache内で同じ元modelを保持するentry。
        Entry* entry = entries_.At(i);
        if (entry->model == &baseModel)
        {
            entry->lastUsedFrame = frameSerial_;
            map = entry->buffer;
            return map ? true : Fail(error, "The cached model sparse map is unavailable");
        }
    }

    if (!MakeRoom(byteSize, error))
        return Fail(error, "The sparse map cache has no evictable capacity");
    // GPU uploadとmodel参照をまとめて所有するcache entry。
    Entry* entry = nullptr;
    try
    {
        entry = new Entry();
    }
    catch (...)
    {
        return Fail(error, "Not enough memory to retain sparse map cache metadata");
    }
    // intrusive参照countを増やすためのmodel非const view。
    auto* retainedModel = const_cast<detail::ModelResource*>(&baseModel);
    if (!Retain(&retainedModel->reference))
    {
        delete entry;
        return Fail(error, "The model resource could not be retained by the sparse map cache");
    }
    entry->model = retainedModel;
    entry->byteSize = byteSize;
    entry->lastUsedFrame = frameSerial_;

    // immutable source mapをGPU-only structured bufferへuploadする設定。
    BufferLoadDesc loadDesc{};
    loadDesc.mDesc.mSize = byteSize;
    loadDesc.mDesc.mElementCount = sparseMap->Count();
    loadDesc.mDesc.mStructStride = static_cast<uint32_t>(sizeof(model::animation::FModelSparseVertexMap));
    loadDesc.mDesc.mFormat = TinyImageFormat_UNDEFINED;
    loadDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    loadDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
    loadDesc.mDesc.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
    loadDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
    loadDesc.mDesc.pName = "gkcore model sparse vertex map";
    loadDesc.pData = const_cast<model::animation::FModelSparseVertexMap*>(sparseMap->Data());
    loadDesc.ppBuffer = &entry->buffer;
    // map uploadがGPUから参照可能になるまで待つtoken。
    SyncToken token = 0;
    addResource(&loadDesc, &token);
    if (token)
        waitForToken(&token);
    if (!entry->buffer)
    {
        Release(&entry->model->reference);
        delete entry;
        return Fail(error, "The Forge could not upload the model sparse vertex map");
    }

    if (!entries_.Append(entry))
    {
        waitQueueIdle(queue_);
        DestroyEntry(entry);
        return Fail(error, "Not enough memory to retain the sparse map cache entry");
    }
    entry->accounted = true;
    usedBytes_ += byteSize;
    map = entry->buffer;
    error.Clear();
    return true;
}

// namespace gk::render
}

#endif
