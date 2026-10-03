#pragma once

#include "../foundation/String.h"
#include "../resources/Resources.h"

/**
 * Windows system-font rasterization used by the native renderer adapter.
 */
namespace gk::detail {

/**
 * Converts UTF-8 text to an owned top-left RGBA image using Windows GDI.
 */
ImageResource* RasterizeWindowsText(const char* utf8Text, uint32_t pixelSize,
                                    uint32_t packedColor, String& error);

} // namespace gk::detail
