#include "Lighting.h"

#include <float.h>
#include <math.h>

/**
 * Validates and stores process-wide built-in scene lighting settings.
 */
namespace gk::effects {
namespace {
const LightingSettings kDefaultLighting{};
LightingSettings settings{};

/**
 * Checks whether a scalar can safely be used by renderer math.
 */
bool IsFinite(float value) {
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}
}

const LightingSettings& CurrentLighting() { return settings; }

void ResetLighting() { settings = kDefaultLighting; }

bool SetAmbientLight(float intensity) {
    if (!IsFinite(intensity) || intensity < 0.0f || intensity > 4.0f) return false;
    settings.ambientIntensity = intensity;
    return true;
}

bool SetDirectionalLight(Vec3 direction, float intensity) {
    if (!IsFinite(direction.x) || !IsFinite(direction.y) || !IsFinite(direction.z) ||
        !IsFinite(intensity) || intensity < 0.0f || intensity > 16.0f)
        return false;
    const double x = direction.x;
    const double y = direction.y;
    const double z = direction.z;
    const double length = sqrt(x * x + y * y + z * z);
    if (!(length > 0.0) || !isfinite(length)) return false;
    const Vec3 normalized = {
        static_cast<float>(x / length),
        static_cast<float>(y / length),
        static_cast<float>(z / length)
    };
    settings.direction = normalized;
    settings.directionalIntensity = intensity;
    return true;
}

} // namespace gk::effects
