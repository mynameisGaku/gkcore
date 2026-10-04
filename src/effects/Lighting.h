#pragma once

#include <gkcore.h>

/**
 * Scene lighting settings captured with each submitted frame.
 */
namespace gk::effects {

/**
 * One ambient contribution and one directional light in world space.
 */
struct LightingSettings {
    float ambientIntensity = 0.2f;
    Vec3 direction{-0.4082483f, -0.8164966f, 0.4082483f};
    float directionalIntensity = 3.0f;
};

/**
 * Returns current persistent lighting settings.
 */
const LightingSettings& CurrentLighting();

/**
 * Restores beginner-friendly lighting defaults.
 */
void ResetLighting();

/**
 * Sets finite ambient intensity in [0, 4].
 */
bool SetAmbientLight(float intensity);

/**
 * Sets a finite, nonzero world-space travel direction and intensity in [0, 16].
 */
bool SetDirectionalLight(Vec3 direction, float intensity);

} // namespace gk::effects
