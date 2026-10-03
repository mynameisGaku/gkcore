#pragma once

#include <gkcore.h>

/**
 * UTF-8 validation and public text drawing implementation.
 */
namespace gk {

/**
 * Queues a UTF-8 string using the renderer's default system font.
 */
int DrawString(float x, float y, const char* utf8Text, uint32_t color, uint32_t pixelSize);

} // namespace gk

/**
 * Portable text checks shared with API contract tests.
 */
namespace gk::detail {

/**
 * Validates UTF-8 and conservative raster bounds before invoking a platform font.
 */
bool ValidateText(const char* utf8Text, uint32_t pixelSize, uint32_t& byteLength);

} // namespace gk::detail
