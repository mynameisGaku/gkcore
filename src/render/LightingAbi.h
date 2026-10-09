#pragma once

#include "effects/Lighting.h"
#include "foundation/String.h"

/**
 * CPU data contract shared with directional-light shader constants.
 */
namespace gk::render
{

/**
 * A 32-byte pair of four-float shader vectors for directional and ambient light.
 */
struct LightingConstants
{
    float directionIntensity[4];
    float ambient[4];
};

static_assert(sizeof(LightingConstants) == 32, "lighting shader constants must occupy 32 bytes");

/**
 * Packs validated settings while preserving output on invalid input.
 */
bool PackLightingConstants(const effects::LightingSettings& settings, LightingConstants& output, String& error);

} // namespace gk::render
