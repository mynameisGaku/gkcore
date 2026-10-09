#pragma once

#include "render/PostProcess.h"
#include "foundation/String.h"

#include <stdint.h>

/**
 * Portable FXAA reference pass for post-tonemap linear display colors.
 */
namespace gk::render
{

inline constexpr float kFxaaMaximumSpan = 8.0f;

/**
 * Applies edge-aware directional filtering into a separate or overlapping output image.
 */
bool ApplyFxaaReference(const LinearColor* source, uint32_t sourceCount, uint32_t width, uint32_t height, LinearColor* destination, uint32_t destinationCount, String& error);

} // namespace gk::render
