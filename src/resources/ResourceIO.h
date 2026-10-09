#pragma once

#include "foundation/String.h"
#include <stdint.h>

namespace gk::detail
{

/**
 * Reads a UTF-8 resource path into gk-owned memory within the supplied limit.
 */
bool ReadResourceFile(const char* path, uint32_t limit, uint8_t*& bytes, uint32_t& size, String& error);

}
