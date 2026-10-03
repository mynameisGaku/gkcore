#pragma once

#include "gkcore.h"

/**
 * Persistent scene effects and draw-layer settings.
 */
namespace gk::effects {

/**
 * Values copied by BeginFrame so settings apply consistently to one frame.
 */
struct Settings {
    bool bloomEnabled;
    float bloomIntensity;
    float exposure;
    bool toneMappingEnabled;
    DrawLayer layer;
};

/**
 * Returns the active effect settings.
 */
const Settings& Current();
/**
 * Restores beginner-friendly effect defaults.
 */
void Reset();
/**
 * Enables or disables scene bloom.
 */
bool SetBloomEnabled(bool enabled);
/**
 * Sets bloom strength to a finite value in [0, 4].
 */
bool SetBloomIntensity(float intensity);
/**
 * Sets exposure to a finite value in (0, 16].
 */
bool SetExposure(float exposure);
/**
 * Enables or disables scene tone mapping.
 */
bool SetToneMappingEnabled(bool enabled);
/**
 * Selects whether later draw commands belong to the scene or UI layer.
 */
bool SetLayer(DrawLayer layer);

} // namespace gk::effects
