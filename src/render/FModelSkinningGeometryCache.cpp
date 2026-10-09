// SPDX-License-Identifier: NOASSERTION
#include "render/FModelSkinningGeometryCache.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <math.h>
#include <string.h>

namespace gk::render
{
namespace
{

constexpr uint32_t kMaximumEntries = 64;                      // 同時に保持するgeometry数。
constexpr uint64_t kMaximumBytes = 64ull * 1024ull * 1024ull; // GPU_ONLY bufferの上限。

/**
 * エラー理由を設定してfalseを返す。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * uint4 recordを配列へ追加する。
 */
bool Append(Array<FModelSkinningRecord>& records, uint32_t x, uint32_t y = 0, uint32_t z = 0, uint32_t w = 0)
{
    const FModelSkinningRecord record = { { x, y, z, w } };
    return records.Append(record);
}

bool AppendDoublePair(Array<FModelSkinningRecord>& records, double first, double second)
{
    uint64_t firstBits = 0;
    uint64_t secondBits = 0;
    memcpy(&firstBits, &first, sizeof(firstBits));
    memcpy(&secondBits, &second, sizeof(secondBits));
    return Append(records, static_cast<uint32_t>(firstBits), static_cast<uint32_t>(firstBits >> 32), static_cast<uint32_t>(secondBits), static_cast<uint32_t>(secondBits >> 32));
}

/**
 * 不変geometryをGPU入力recordへ変換し、対応範囲をdispatchへ記録する。
 */
bool PackGeometry(const model::animation::FModelGpuSkinningGeometry& geometry, const Array<model::animation::FModelSparseVertexMap>& sparseMap, Array<FModelSkinningRecord>& records, FModelSkinningDispatch& dispatch, String& error)
{
    using FGeometry = model::animation::FModelGpuSkinningGeometry;
    const uint64_t total = static_cast<uint64_t>(geometry.positions.Count()) * 2u + geometry.influenceRanges.Count() + geometry.influences.Count() + geometry.faces.Count() + geometry.corners.Count() + geometry.normalGroupRanges.Count() + geometry.normalFaceIds.Count();
    if (geometry.positions.Count() != geometry.influenceRanges.Count() || sparseMap.Count() == 0 || total > UINT32_MAX || geometry.clusters.Count() > UINT32_MAX / 6u)
        return Fail(error, "FBX GPU skinning geometry dimensions are invalid");
    Array<uint8_t> referencedGroups;
    if (!referencedGroups.Reserve(geometry.normalGroupRanges.Count()))
        return Fail(error, "FBX GPU skinning normal reference allocation failed");
    for (uint32_t group = 0; group < geometry.normalGroupRanges.Count(); ++group)
    {
        if (!referencedGroups.Append(0))
            return Fail(error, "FBX GPU skinning normal reference allocation failed");
    }
    for (uint32_t vertex = 0; vertex < sparseMap.Count(); ++vertex)
    {
        const uint32_t group = sparseMap.At(vertex).normalIndex;
        if (group >= referencedGroups.Count())
            return Fail(error, "FBX GPU skinning sparse normal reference is invalid");
        referencedGroups.At(group) = 1;
    }
    if (!records.Reserve(static_cast<uint32_t>(total)))
        return Fail(error, "FBX GPU skinning geometry allocation failed");

    FModelSkinningDispatch candidate{};
    candidate.positionsOffset = records.Count();
    candidate.positionsCount = geometry.positions.Count();
    for (uint32_t i = 0; i < geometry.positions.Count(); ++i)
    {
        const FGeometry::FPosition& position = geometry.positions.At(i);
        if (!isfinite(position.value[0]) || !isfinite(position.value[1]) || !isfinite(position.value[2]) || !AppendDoublePair(records, position.value[0], position.value[1]) || !AppendDoublePair(records, position.value[2], 0.0))
            return Fail(error, "FBX GPU skinning position is invalid or out of memory");
    }
    candidate.influenceRangesOffset = records.Count();
    candidate.influenceRangesCount = geometry.influenceRanges.Count();
    for (uint32_t i = 0; i < geometry.influenceRanges.Count(); ++i)
    {
        const FGeometry::FInfluenceRange& range = geometry.influenceRanges.At(i);
        if (range.firstInfluence > geometry.influences.Count() || range.influenceCount > geometry.influences.Count() - range.firstInfluence || !Append(records, range.firstInfluence, range.influenceCount))
            return Fail(error, "FBX GPU skinning influence range is invalid or out of memory");
    }
    candidate.influencesOffset = records.Count();
    candidate.influencesCount = geometry.influences.Count();
    for (uint32_t i = 0; i < geometry.influences.Count(); ++i)
    {
        const FGeometry::FInfluence& influence = geometry.influences.At(i);
        uint64_t weightBits = 0;
        memcpy(&weightBits, &influence.weight, sizeof(weightBits));
        if (influence.clusterIndex >= geometry.clusters.Count() || !isfinite(influence.weight) || !Append(records, influence.clusterIndex, static_cast<uint32_t>(weightBits), static_cast<uint32_t>(weightBits >> 32)))
            return Fail(error, "FBX GPU skinning influence is invalid or out of memory");
    }
    candidate.matricesCount = geometry.clusters.Count();
    candidate.facesOffset = records.Count();
    candidate.facesCount = geometry.faces.Count();
    for (uint32_t i = 0; i < geometry.faces.Count(); ++i)
    {
        const FGeometry::FFace& face = geometry.faces.At(i);
        if (face.firstCorner > geometry.corners.Count() || face.cornerCount > geometry.corners.Count() - face.firstCorner || !Append(records, face.firstCorner, face.cornerCount))
            return Fail(error, "FBX GPU skinning face range is invalid or out of memory");
    }
    candidate.cornersOffset = records.Count();
    candidate.cornersCount = geometry.corners.Count();
    for (uint32_t i = 0; i < geometry.corners.Count(); ++i)
    {
        const FGeometry::FCorner& corner = geometry.corners.At(i);
        if (corner.positionIndex >= geometry.positions.Count() || corner.normalGroupIndex >= geometry.normalGroupRanges.Count() || !Append(records, corner.positionIndex))
            return Fail(error, "FBX GPU skinning corner is invalid or out of memory");
    }
    candidate.normalGroupRangesOffset = records.Count();
    candidate.normalGroupRangesCount = geometry.normalGroupRanges.Count();
    for (uint32_t i = 0; i < geometry.normalGroupRanges.Count(); ++i)
    {
        const FGeometry::FNormalGroupRange& range = geometry.normalGroupRanges.At(i);
        if (range.firstFace > geometry.normalFaceIds.Count() || range.faceCount > geometry.normalFaceIds.Count() - range.firstFace || !Append(records, range.firstFace, range.faceCount, referencedGroups.At(i)))
            return Fail(error, "FBX GPU skinning normal range is invalid or out of memory");
    }
    candidate.normalFaceIdsOffset = records.Count();
    candidate.normalFaceIdsCount = geometry.normalFaceIds.Count();
    for (uint32_t i = 0; i < geometry.normalFaceIds.Count(); ++i)
    {
        if (geometry.normalFaceIds.At(i) >= geometry.faces.Count() || !Append(records, geometry.normalFaceIds.At(i)))
            return Fail(error, "FBX GPU skinning normal face reference is invalid or out of memory");
    }
    dispatch = candidate;
    error.Clear();
    return true;
}

}

struct FModelSkinningGeometryCache::Entry
{
    detail::ModelResource* owner = nullptr;                                // geometryの寿命を保つsource model。
    const model::animation::FModelGpuSkinningGeometry* geometry = nullptr; // 不変入力のcache key。
    Buffer* buffer = nullptr;                                              // upload済みGPU_ONLY record buffer。
    FModelSkinningDispatch dispatch{};                                     // static record範囲。
    uint64_t byteSize = 0;                                                 // budgetへ計上するbuffer byte数。
    uint64_t lastUsedFrame = 0;                                            // 現在frameからの追い出しを防ぐ番号。
    bool accounted = false;                                                // budget加算済みか示す。
};

FModelSkinningGeometryCache::FModelSkinningGeometryCache() = default;

FModelSkinningGeometryCache::~FModelSkinningGeometryCache()
{
    Shutdown();
}

bool FModelSkinningGeometryCache::Initialize(Renderer* renderer, Queue* queue, String& error)
{
    if (renderer_ || !renderer || !queue)
        return Fail(error, "The model skinning geometry cache initialization is invalid");
    renderer_ = renderer;
    queue_ = queue;
    frameSerial_ = 0;
    usedBytes_ = 0;
    error.Clear();
    return true;
}

void FModelSkinningGeometryCache::BeginFrame()
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

void FModelSkinningGeometryCache::Shutdown()
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

bool FModelSkinningGeometryCache::Matches(const Entry& entry, const detail::ModelResource& owner, const model::animation::FModelGpuSkinningGeometry& geometry) const
{
    return entry.owner == &owner && entry.geometry == &geometry;
}

bool FModelSkinningGeometryCache::MakeRoom(uint64_t bytes, String& error)
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

void FModelSkinningGeometryCache::DestroyEntry(Entry* entry)
{
    if (!entry)
        return;
    if (entry->buffer)
        removeResource(entry->buffer);
    if (entry->owner)
        Release(&entry->owner->reference);
    if (entry->accounted)
    {
        if (usedBytes_ >= entry->byteSize)
            usedBytes_ -= entry->byteSize;
        else
            usedBytes_ = 0;
    }
    delete entry;
}

bool FModelSkinningGeometryCache::Prepare(const detail::ModelResource& owner, const model::animation::FModelGpuSkinningGeometry& geometry, const Array<model::animation::FModelSparseVertexMap>& sparseMap, Buffer*& buffer, FModelSkinningDispatch& dispatch, bool& cacheHit, String& error)
{
    buffer = nullptr;
    cacheHit = false;
    error.Clear();
    if (!renderer_ || !queue_)
        return Fail(error, "The model skinning geometry cache input is invalid");
    if (geometry.positions.Count() == 0)
    {
        error.Clear();
        return true;
    }
    for (uint32_t i = 0; i < entries_.Count(); ++i)
    {
        Entry* entry = entries_.At(i);
        if (Matches(*entry, owner, geometry))
        {
            entry->lastUsedFrame = frameSerial_;
            buffer = entry->buffer;
            dispatch = entry->dispatch;
            cacheHit = true;
            return true;
        }
    }

    Array<FModelSkinningRecord> records;
    FModelSkinningDispatch packed{};
    if (!PackGeometry(geometry, sparseMap, records, packed, error))
        return false;
    if (records.Count() == 0 || records.Count() > UINT32_MAX / sizeof(FModelSkinningRecord))
        return Fail(error, "The model skinning geometry record size is invalid");
    const uint64_t byteSize = static_cast<uint64_t>(records.Count()) * sizeof(FModelSkinningRecord);
    if (!MakeRoom(byteSize, error))
    {
        error.Clear();
        return true;
    }
    if (!Retain(&const_cast<detail::ModelResource&>(owner).reference))
        return Fail(error, "The model skinning geometry owner could not be retained");

    Entry* entry = nullptr;
    try
    {
        entry = new Entry();
    }
    catch (...)
    {
        Release(&const_cast<detail::ModelResource&>(owner).reference);
        return Fail(error, "The model skinning geometry cache metadata allocation failed");
    }
    entry->owner = const_cast<detail::ModelResource*>(&owner);
    entry->geometry = &geometry;
    entry->dispatch = packed;
    entry->byteSize = byteSize;
    entry->lastUsedFrame = frameSerial_;

    BufferLoadDesc loadDesc{};
    loadDesc.mDesc.mSize = byteSize;
    loadDesc.mDesc.mElementCount = records.Count();
    loadDesc.mDesc.mStructStride = sizeof(FModelSkinningRecord);
    loadDesc.mDesc.mFormat = TinyImageFormat_UNDEFINED;
    loadDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    loadDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
    loadDesc.mDesc.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
    loadDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
    loadDesc.mDesc.pName = "gkcore immutable skinning geometry records";
    loadDesc.pData = records.Data();
    loadDesc.ppBuffer = &entry->buffer;
    SyncToken token = 0;
    addResource(&loadDesc, &token);
    if (token)
        waitForToken(&token);
    if (!entry->buffer)
    {
        DestroyEntry(entry);
        return Fail(error, "The Forge could not upload immutable skinning geometry records");
    }
    if (!entries_.Append(entry))
    {
        waitQueueIdle(queue_);
        DestroyEntry(entry);
        return Fail(error, "The model skinning geometry cache entry allocation failed");
    }
    usedBytes_ += byteSize;
    entry->accounted = true;
    buffer = entry->buffer;
    dispatch = entry->dispatch;
    error.Clear();
    return true;
}

}

#endif
