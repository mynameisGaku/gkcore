#include "LightingAbi.h"

#include <float.h>
#include <math.h>

/**
 * Validates lighting values and packs the GPU constant-buffer layout.
 */
namespace gk::render {
namespace {
/**
 * Checks whether a scalar can safely be stored in the shader constants.
 */
bool IsFinite(float value) {
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}
}

bool PackLightingConstants(const effects::LightingSettings& settings,
                           LightingConstants& output, String& error) {
    const double x = settings.direction.x;
    const double y = settings.direction.y;
    const double z = settings.direction.z;
    const double length = sqrt(x * x + y * y + z * z);
    if (!IsFinite(settings.ambientIntensity) || settings.ambientIntensity < 0.0f ||
        settings.ambientIntensity > 4.0f || !IsFinite(settings.directionalIntensity) ||
        settings.directionalIntensity < 0.0f || settings.directionalIntensity > 16.0f ||
        !IsFinite(settings.direction.x) || !IsFinite(settings.direction.y) ||
        !IsFinite(settings.direction.z) || !(length > 0.0) || !isfinite(length)) {
        error.Assign("lighting settings are outside the supported range");
        return false;
    }
    LightingConstants replacement{};
    replacement.directionIntensity[0] = static_cast<float>(x / length);
    replacement.directionIntensity[1] = static_cast<float>(y / length);
    replacement.directionIntensity[2] = static_cast<float>(z / length);
    replacement.directionIntensity[3] = settings.directionalIntensity;
    replacement.ambient[0] = settings.ambientIntensity;
    output = replacement;
    error.Clear();
    return true;
}

} // namespace gk::render
