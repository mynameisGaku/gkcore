#pragma once

#include <gkcore.h>

/**
 * Persistent scene effects and draw-layer settings.
 */
namespace gk::effects
{

/**
 * Values copied by BeginFrame so settings apply consistently to one frame.
 */
struct Settings
{
    bool bloomEnabled;
    float bloomIntensity;
    float exposure;
    bool toneMappingEnabled;
    DrawLayer layer;
    float saturation = 1.0f;
    float contrast = 1.0f;
    bool fxaaEnabled = true;
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
 * Sets saturation to a finite factor in [0, 2], where 1 preserves saturation.
 */
bool SetSaturation(float factor);
/**
 * Sets contrast to a finite factor in [0, 2], where 1 preserves contrast.
 */
bool SetContrast(float factor);
/**
 * Enables or disables FXAA for subsequently captured frames.
 */
bool SetFxaaEnabled(bool enabled);
/**
 * Enables or disables scene tone mapping.
 */
bool SetToneMappingEnabled(bool enabled);
/**
 * Selects whether later draw commands belong to the scene or UI layer.
 */
bool SetLayer(DrawLayer layer);

} // namespace gk::effects
