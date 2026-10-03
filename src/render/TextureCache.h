#pragma once

#if defined(_WIN32) && defined(DIRECT3D12)

#include "../resources/Resources.h"
#include "TextureCachePolicy.h"

#include <Graphics/Interfaces/IGraphics.h>
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

/**
 * Bounded Direct3D 12 texture cache for retained decoded framework images.
 */
namespace gk::render {

/**
 * Retains source pixels while their uploaded Forge texture remains cached.
 */
class TextureCache {
public:
    /**
     * Creates the shared sampler used by uploaded framework images.
     */
    bool Initialize(Renderer* renderer, Queue* queue, String& error);
    /**
     * Waits for GPU use and releases every cached image and descriptor.
     */
    void Shutdown();
    /**
     * Starts a frame so descriptor entries used by it cannot be evicted.
     */
    void BeginFrame();
    /**
     * Ensures a retained image has a ready texture and descriptor set.
     */
    bool Prepare(detail::ImageResource* image, String& error);
    /**
     * Records staged image transfers with The Forge resource loader.
     */
    bool UploadPending(String& error);
    /**
     * Reports whether recorded texture copies need a graphics-queue wait.
     */
    bool HasPendingSubmission() const { return uploadSubmissionPending_; }
    /**
     * Flushes staged transfers and retains their fence until graphics submission or drain.
     */
    bool FlushPendingUploads(FlushResourceUpdateDesc& flush, String& error);
    /**
     * Waits for an unsubmitted resource-loader copy batch before releasing its textures.
     */
    bool DrainPendingUploads(String& error);
    /**
     * Clears upload state after its synchronization semaphore is submitted.
     */
    void MarkSubmitted() { uploadSubmissionPending_ = false; uploadFence_ = nullptr; }
    /**
     * Binds the cached descriptor set for an image snapshot.
     */
    bool Bind(Cmd* command, detail::ImageResource* image, String& error) const;

private:
    /**
     * Tracks one image's upload state, GPU objects, and retained byte cost.
     */
    struct Entry {
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
     * Finds the cache record for an image pointer, if one exists.
     */
    Entry* Find(detail::ImageResource* image);
    /**
     * Reserves a slot within entry and byte limits, evicting only safe entries.
     */
    Entry* AcquireSlot(uint64_t imageBytes, String& error);
    /**
     * Creates an uninitialized GPU texture for a validated retained image.
     */
    bool CreateTexture(Entry& entry, detail::ImageResource* image, String& error);
    /**
     * Releases one record's descriptor and texture and resets its cache state.
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
