#include "TextCache.h"

#include "../foundation/Memory.h"

#include <string.h>

namespace gk::detail {
namespace {
const uint32_t kMaximumEntries = 64;
const uint64_t kMaximumBytes = 16ull * 1024ull * 1024ull;

/**
 * Owns the lookup key and one reference for a cached RGBA image.
 */
struct TextCacheEntry {
    char* text;
    uint32_t length;
    uint32_t pixelSize;
    uint32_t color;
    uint64_t rgbaBytes;
    uint64_t lastUsed;
    ImageResource* image;
};

/**
 * Main-thread cache accounting retained image bytes separately from queued refs.
 */
struct TextCache {
    Array<TextCacheEntry> entries;
    uint64_t bytes = 0;
    uint64_t clock = 0;
};

/**
 * Returns the one cache shared by the process-wide drawing API.
 */
TextCache& Cache() {
    static TextCache cache;
    return cache;
}

/**
 * Counts the validated UTF-8 bytes used as a cache lookup key.
 */
uint32_t TextLength(const char* text) {
    uint32_t length = 0;
    while (text[length]) ++length;
    return length;
}

/**
 * Compares every field that determines the rasterized text image.
 */
bool Matches(const TextCacheEntry& entry, const char* text, uint32_t length,
             uint32_t pixelSize, uint32_t color) {
    return entry.length == length && entry.pixelSize == pixelSize && entry.color == color &&
           memcmp(entry.text, text, length) == 0;
}

/**
 * Releases the cache key and retained image reference for one entry.
 */
void ReleaseEntry(TextCacheEntry& entry) {
    Deallocate(entry.text);
    if (entry.image) Release(&entry.image->reference);
    entry.text = nullptr;
    entry.image = nullptr;
}

/**
 * Removes one least-recently-used entry and releases its key and image owner.
 */
void RemoveEntry(TextCache& cache, uint32_t index) {
    TextCacheEntry entry = cache.entries.At(index);
    if (cache.bytes >= entry.rgbaBytes) cache.bytes -= entry.rgbaBytes;
    ReleaseEntry(entry);
    cache.entries.RemoveAt(index);
}

/**
 * Advances LRU order while renumbering entries before the counter wraps.
 */
void Touch(TextCache& cache, TextCacheEntry& entry) {
    if (cache.clock == UINT64_MAX) {
        uint64_t age = 1;
        for (uint32_t i = 0; i < cache.entries.Count(); ++i)
            cache.entries.At(i).lastUsed = age++;
        cache.clock = age;
    }
    entry.lastUsed = ++cache.clock;
}

/**
 * Validates a renderer payload before it can be retained or queued.
 */
bool ValidImage(const ImageResource* image, uint64_t& byteCount) {
    if (!image || !image->width || !image->height || image->width > 8192 || image->height > 4096)
        return false;
    const uint64_t pixels = static_cast<uint64_t>(image->width) * image->height;
    byteCount = pixels * 4;
    return pixels <= 16ull * 1024ull * 1024ull && image->rgba.Count() == byteCount;
}
} // namespace

ImageResource* GetCachedTextImage(Backend& backend, const char* text, uint32_t pixelSize,
                                  uint32_t color, String& error) {
    TextCache& cache = Cache();
    const uint32_t length = TextLength(text);
    for (uint32_t i = 0; i < cache.entries.Count(); ++i) {
        TextCacheEntry& entry = cache.entries.At(i);
        if (!Matches(entry, text, length, pixelSize, color)) continue;
        if (!Retain(&entry.image->reference)) {
            error.Assign("cached text image is no longer available");
            return nullptr;
        }
        Touch(cache, entry);
        return entry.image;
    }

    ImageResource* image = backend.RasterizeText(text, pixelSize, color, error);
    uint64_t rgbaBytes = 0;
    if (!image) return nullptr;
    if (!ValidImage(image, rgbaBytes)) {
        Release(&image->reference);
        error.Assign("text rasterizer returned invalid image dimensions or pixels");
        return nullptr;
    }
    if (length == UINT32_MAX || rgbaBytes > kMaximumBytes || !cache.entries.Reserve(kMaximumEntries))
        return image;

    while (cache.entries.Count() >= kMaximumEntries || cache.bytes > kMaximumBytes - rgbaBytes) {
        if (!cache.entries.Count()) return image;
        uint32_t oldest = 0;
        for (uint32_t i = 1; i < cache.entries.Count(); ++i)
            if (cache.entries.At(i).lastUsed < cache.entries.At(oldest).lastUsed) oldest = i;
        RemoveEntry(cache, oldest);
    }

    char* key = static_cast<char*>(Allocate(static_cast<size_t>(length) + 1));
    if (!key) return image;
    memcpy(key, text, static_cast<size_t>(length) + 1);
    if (!Retain(&image->reference)) {
        Deallocate(key);
        return image;
    }
    TextCacheEntry entry{};
    entry.text = key;
    entry.length = length;
    entry.pixelSize = pixelSize;
    entry.color = color;
    entry.rgbaBytes = rgbaBytes;
    entry.image = image;
    Touch(cache, entry);
    if (!cache.entries.Append(entry)) {
        ReleaseEntry(entry);
        return image;
    }
    cache.bytes += rgbaBytes;
    return image;
}

void ClearTextImageCache() {
    TextCache& cache = Cache();
    for (uint32_t i = 0; i < cache.entries.Count(); ++i) ReleaseEntry(cache.entries.At(i));
    cache.entries.Reset();
    cache.bytes = 0;
    cache.clock = 0;
}

uint32_t TextImageCacheEntryCount() { return Cache().entries.Count(); }
uint64_t TextImageCacheByteCount() { return Cache().bytes; }

} // namespace gk::detail
