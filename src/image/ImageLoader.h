#pragma once

#include "Image.h"
#include "../foundation/String.h"
#include <stdint.h>

namespace gk::detail {

/**
 * Decodes one supported image file into RGBA resource storage.
 */
ImageResource* LoadImagePayload(const char* path, String& error);
/**
 * Decodes encoded image memory into RGBA resource storage.
 */
ImageResource* DecodeImagePayload(const uint8_t* bytes, uint32_t size, String& error);

}
