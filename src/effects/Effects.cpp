#include "effects/Effects.h"

#include <float.h>

namespace gk::effects
{
namespace
{
Settings settings = { true, 0.15f, 1.0f, true, DrawLayer::Scene };

bool IsFinite(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}
}

const Settings& Current()
{
    return settings;
}
void Reset()
{
    settings = { true, 0.15f, 1.0f, true, DrawLayer::Scene };
}
bool SetBloomEnabled(bool enabled)
{
    settings.bloomEnabled = enabled;
    return true;
}

bool SetBloomIntensity(float intensity)
{
    if (!IsFinite(intensity) || intensity < 0.0f || intensity > 4.0f)
        return false;
    settings.bloomIntensity = intensity;
    return true;
}

bool SetExposure(float exposure)
{
    if (!IsFinite(exposure) || exposure <= 0.0f || exposure > 16.0f)
        return false;
    settings.exposure = exposure;
    return true;
}

bool SetSaturation(float factor)
{
    if (!IsFinite(factor) || factor < 0.0f || factor > 2.0f)
        return false;
    settings.saturation = factor;
    return true;
}

bool SetContrast(float factor)
{
    if (!IsFinite(factor) || factor < 0.0f || factor > 2.0f)
        return false;
    settings.contrast = factor;
    return true;
}

bool SetFxaaEnabled(bool enabled)
{
    settings.fxaaEnabled = enabled;
    return true;
}

bool SetToneMappingEnabled(bool enabled)
{
    settings.toneMappingEnabled = enabled;
    return true;
}

bool SetLayer(DrawLayer layer)
{
    if (layer != DrawLayer::Scene && layer != DrawLayer::UI)
        return false;
    settings.layer = layer;
    return true;
}

} // namespace gk::effects
