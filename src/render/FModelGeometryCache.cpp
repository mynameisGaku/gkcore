#include "render/FModelGeometryCache.h"

#include "render/ModelLocalGeometry.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/FSL/defaults.h>
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <float.h>
#include <stdint.h>
#include <string.h>

#ifdef new
#undef new
#endif
#ifdef delete
#undef delete
#endif

/**
 * 静止モデルの展開済み頂点bufferをframe間で再利用する。
 */
namespace gk::render
{
namespace
{

// cacheが保持するentry数の上限。
constexpr uint32_t kMaximumEntries = 256;
// GPUに保持する展開済み頂点のbyte上限。
constexpr uint64_t kMaximumBytes = 128ull * 1024ull * 1024ull;
// 一つの頂点展開に許すframe内上限。
constexpr uint32_t kMaximumVertices = 1u << 20;

/**
 * float値のビット表現が一致するか調べる。
 */
bool SameFloat(float left, float right)
{
    return memcmp(&left, &right, sizeof(float)) == 0;
}

/**
 * 描画頂点に影響するsampler設定が一致するか調べる。
 */
bool SameSampler(const detail::FTextureSampler& left, const detail::FTextureSampler& right)
{
    return left.addressU == right.addressU && left.addressV == right.addressV && left.minFilter == right.minFilter && left.magFilter == right.magFilter && left.mipFilter == right.mipFilter;
}

/**
 * 描画頂点に影響する材質計画の全fieldを比較する。
 */
bool SamePart(const ModelPartPlan& left, const ModelPartPlan& right)
{
    if (left.firstIndex != right.firstIndex || left.indexCount != right.indexCount || left.materialIndex != right.materialIndex || left.textureIndex != right.textureIndex || left.metallicRoughnessTextureIndex != right.metallicRoughnessTextureIndex || left.normalTextureIndex != right.normalTextureIndex || left.emissiveTextureIndex != right.emissiveTextureIndex || left.occlusionTextureIndex != right.occlusionTextureIndex || left.alphaMask != right.alphaMask || left.alphaBlend != right.alphaBlend || !SameFloat(left.alphaCutoff, right.alphaCutoff) || !SameFloat(left.metallicFactor, right.metallicFactor) || !SameFloat(left.roughnessFactor, right.roughnessFactor) || !SameFloat(left.normalScale, right.normalScale) || !SameFloat(left.occlusionStrength, right.occlusionStrength) || !SameSampler(left.baseColorSampler, right.baseColorSampler) || !SameSampler(left.metallicRoughnessSampler, right.metallicRoughnessSampler) || !SameSampler(left.normalSampler, right.normalSampler) || !SameSampler(left.emissiveSampler, right.emissiveSampler) || !SameSampler(left.occlusionSampler, right.occlusionSampler))
        return false;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (!SameFloat(left.baseColorFactor[i], right.baseColorFactor[i]) || !SameFloat(left.emissiveFactorStrength[i], right.emissiveFactorStrength[i]))
            return false;
    }
    return true;
}

/**
 * pose snapshotが保持する元geometryとの構造上の対応を確認する。
 */
bool HasSameGeometryShape(const detail::ModelResource& snapshot, const detail::ModelResource& source, const ModelPartPlan& part)
{
    if (&snapshot == &source || !snapshot.isPoseSnapshot || source.isPoseSnapshot || snapshot.vertices.Count() != source.vertices.Count() || snapshot.indices.Count() != source.indices.Count() || snapshot.primitives.Count() != source.primitives.Count() || part.firstIndex > source.indices.Count() || part.indexCount > source.indices.Count() - part.firstIndex)
        return false;
    for (uint32_t i = 0; i < snapshot.primitives.Count(); ++i)
    {
        const detail::ModelPrimitive& snapshotPrimitive = snapshot.primitives.At(i);
        const detail::ModelPrimitive& sourcePrimitive = source.primitives.At(i);
        if (snapshotPrimitive.firstIndex != sourcePrimitive.firstIndex || snapshotPrimitive.indexCount != sourcePrimitive.indexCount || snapshotPrimitive.materialIndex != sourcePrimitive.materialIndex)
            return false;
    }
    return true;
}

/**
 * cache miss時に対象primitiveのindex並びが元geometryと一致するか調べる。
 */
bool HasSamePartIndices(const detail::ModelResource& snapshot, const detail::ModelResource& source, const ModelPartPlan& part)
{
    for (uint32_t i = 0; i < part.indexCount; ++i)
    {
        if (snapshot.indices.At(part.firstIndex + i) != source.indices.At(part.firstIndex + i))
            return false;
    }
    return true;
}

/**
 * エラー文字列を設定し、失敗を返す。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

/**
 * GPU頂点bufferと対応するmodel参照を保持するentry。
 */
struct FModelGeometryCache::Entry
{
    // cache entryが参照する元model resource。
    detail::ModelResource* model = nullptr;
    // 不変なprimitive材質とindex範囲。
    ModelPartPlan part{};
    // 生成済みGPU bufferと頂点数。
    Buffer* buffer = nullptr;
    uint32_t vertexCount = 0;
    uint64_t byteSize = 0;
    // budget計上済みかを示し、失敗時のcleanupで差引きを防ぐ。
    bool accounted = false;
    // 最後に参照されたframe番号。現在frameと一致するentryは追い出さない。
    uint64_t lastUsedFrame = 0;
};

/**
 * 空のgeometry cacheを作る。
 */
FModelGeometryCache::FModelGeometryCache() : renderer_(nullptr), queue_(nullptr), frameSerial_(0), usedBytes_(0)
{
}

/**
 * 保持中bufferとmodel参照を解放する。
 */
FModelGeometryCache::~FModelGeometryCache()
{
    Shutdown();
}

/**
 * Rendererとgraphics queueを設定する。
 */
bool FModelGeometryCache::Initialize(Renderer* renderer, Queue* queue, String& error)
{
    if (renderer_ || !renderer || !queue)
        return Fail(error, "The model geometry cache initialization is invalid");
    renderer_ = renderer;
    queue_ = queue;
    frameSerial_ = 0;
    usedBytes_ = 0;
    error.Clear();
    return true;
}

/**
 * 新しいframeを開始し、frame番号の周回を安全に処理する。
 */
void FModelGeometryCache::BeginFrame()
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
 * queueの処理完了後にGPU bufferと保持参照を解放する。
 */
void FModelGeometryCache::Shutdown()
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
 * 指定された描画入力が保持entryと完全に一致するか調べる。
 */
bool FModelGeometryCache::Matches(const Entry& entry, const detail::ModelResource& sourceModel, const ModelPartPlan& part) const
{
    return entry.model == &sourceModel && SamePart(entry.part, part);
}

/**
 * byte budgetとentry数を空ける。現在frameで使ったentryは残す。
 */
bool FModelGeometryCache::MakeRoom(uint64_t bytes, String& error)
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
        Entry* victim = entries_.At(victimIndex);
        entries_.RemoveAt(victimIndex);
        DestroyEntry(victim);
    }
    error.Clear();
    return true;
}

/**
 * entryが所有するGPU bufferとmodel参照を解放する。
 */
void FModelGeometryCache::DestroyEntry(Entry* entry)
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
 * 一致entryを返し、なければ静止geometryを展開してGPU_ONLYへuploadする。
 */
bool FModelGeometryCache::Prepare(const detail::FramePacket& frame, const detail::DrawPacket& draw, const ModelPartPlan& part, const uint32_t* sortedFirstIndices, uint32_t triangleCount, bool& cached, Buffer*& buffer, uint32_t& vertexCount, String& error)
{
    cached = false;
    buffer = nullptr;
    vertexCount = 0;
    error.Clear();
    if (!renderer_ || !queue_ || draw.kind != detail::DrawKind::Model || !draw.model || frame.width == 0 || frame.height == 0 || triangleCount == 0 || part.indexCount == 0 || part.indexCount % 3 != 0 || triangleCount > part.indexCount / 3 || part.firstIndex > draw.model->indices.Count() || part.indexCount > draw.model->indices.Count() - part.firstIndex)
        return Fail(error, "The model geometry cache input is invalid");

    // pose snapshotの頂点値はframeごとに変わるため、保持された元geometryをcache keyと展開元に使う。
    detail::ModelResource* sourceModel = draw.model;
    if (draw.model->isPoseSnapshot)
    {
        sourceModel = draw.model->geometrySource;
        if (!sourceModel || !HasSameGeometryShape(*draw.model, *sourceModel, part))
            return true;
    }

    // CPU経路と同じ有限値・camera条件を確認し、扱えない変換だけ従来経路へ戻す。
    FWorldGeometryContext transformValidation{};
    if (!BuildWorldGeometryContext(draw, true, transformValidation, error))
    {
        error.Clear();
        return true;
    }

    // 透明描画順のindex範囲がprimitive内にあるか確認する。
    for (uint32_t i = 0; sortedFirstIndices && i < triangleCount; ++i)
    {
        const uint32_t firstIndex = sortedFirstIndices[i];
        if (firstIndex < part.firstIndex || firstIndex - part.firstIndex > part.indexCount - 3 || (firstIndex - part.firstIndex) % 3 != 0)
            return Fail(error, "The sorted model triangle is outside its part range");
    }

    for (uint32_t i = 0; i < entries_.Count(); ++i)
    {
        Entry* entry = entries_.At(i);
        if (Matches(*entry, *sourceModel, part))
        {
            entry->lastUsedFrame = frameSerial_;
            cached = true;
            buffer = entry->buffer;
            vertexCount = entry->vertexCount;
            return true;
        }
    }

    Array<ModelRenderVertex> vertices;
    if (draw.model != sourceModel && !HasSamePartIndices(*draw.model, *sourceModel, part))
        return true;
    if (!AppendLocalModelPart(*sourceModel, part, vertices, kMaximumVertices, error) || vertices.Count() != part.indexCount)
    {
        // local頂点として安全に扱えない材質や属性は、従来のCPU経路へ戻す。
        error.Clear();
        return true;
    }

    const uint64_t byteSize = static_cast<uint64_t>(vertices.Count()) * sizeof(ModelRenderVertex);
    if (!MakeRoom(byteSize, error))
    {
        error.Clear();
        return true;
    }

    Entry* entry = nullptr;
    try
    {
        entry = new Entry();
    }
    catch (...)
    {
        return Fail(error, "Not enough memory to retain model geometry cache metadata");
    }
    if (!Retain(&sourceModel->reference))
    {
        delete entry;
        return Fail(error, "The model resource could not be retained by the geometry cache");
    }
    entry->model = const_cast<detail::ModelResource*>(sourceModel);
    entry->part = part;
    entry->vertexCount = part.indexCount;
    entry->byteSize = byteSize;
    entry->lastUsedFrame = frameSerial_;

    if (entry->vertexCount)
    {
        BufferLoadDesc loadDesc{};
        loadDesc.mDesc.mSize = byteSize;
        loadDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
        loadDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
        loadDesc.mDesc.mElementCount = vertices.Count();
        loadDesc.mDesc.mStructStride = sizeof(ModelRenderVertex);
        loadDesc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        loadDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        loadDesc.mDesc.pName = "gkcore cached source model geometry";
        loadDesc.pData = vertices.Data();
        loadDesc.ppBuffer = &entry->buffer;
        SyncToken token = 0;
        addResource(&loadDesc, &token);
        if (token)
            waitForToken(&token);
        if (!entry->buffer)
        {
            if (entry->model)
                Release(&entry->model->reference);
            delete entry;
            return Fail(error, "The Forge could not upload cached model geometry");
        }
    }

    if (!entries_.Append(entry))
    {
        if (queue_ && entry->buffer)
            waitQueueIdle(queue_);
        DestroyEntry(entry);
        return Fail(error, "Not enough memory to retain the model geometry cache entry");
    }
    entry->accounted = true;
    usedBytes_ += byteSize;
    cached = true;
    buffer = entry->buffer;
    vertexCount = entry->vertexCount;
    return true;
}

// namespace gk::render
}

#endif
