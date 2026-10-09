#pragma once

#include "internal/Backend.hpp"

/**
 * Bounded cache of retained system-font rasterizations used by DrawString.
 */
namespace gk::detail
{

/**
 * Returns an owned image reference, reusing an identical UTF-8/color/size entry.
 */
ImageResource* GetCachedTextImage(Backend& backend, const char* text, uint32_t pixelSize, uint32_t color, String& error);
/**
 * Drops cache ownership; queued draws retain their own references independently.
 */
void ClearTextImageCache();
/**
 * Returns the number of retained text images, for contract tests.
 */
uint32_t TextImageCacheEntryCount();
/**
 * Returns retained RGBA bytes, for contract tests.
 */
uint64_t TextImageCacheByteCount();

} // namespace gk::detail
