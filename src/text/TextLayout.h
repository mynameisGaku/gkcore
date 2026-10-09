#pragma once

#include "foundation/String.h"
#include <stdint.h>

/**
 * Bounded text normalization shared by platform font adapters.
 */
namespace gk::detail
{

/**
 * Expands each tab to four spaces in at most 4096 bytes and commits output
 * only on success. UTF-8 validation remains the caller's responsibility.
 */
bool ExpandTextTabs(const char* utf8Text, String& expandedText, String& error);

} // namespace gk::detail
