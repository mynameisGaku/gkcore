#include "render/PostProcess.h"

#include <float.h>
#include <math.h>

namespace gk::render
{
namespace
{
const float kLuminanceWeights[3] = { 0.2126f, 0.7152f, 0.0722f };
const float kBloomWeights[5] = { 0.2270270270f, 0.1945945946f, 0.1216216216f, 0.0540540541f, 0.0162162162f };

float Clamp(float value, float minimum, float maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

float Max(float left, float right)
{
    return left > right ? left : right;
}

float ToneMapAces(float value)
{
    value = Max(value, 0.0f);
    const float numerator = value * (2.51f * value + 0.03f);
    const float denominator = value * (2.43f * value + 0.59f) + 0.14f;
    if (denominator <= FLT_MIN)
        return 0.0f;
    return Clamp(numerator / denominator, 0.0f, 1.0f);
}
}

float SrgbToLinear(float value)
{
    if (value <= 0.04045f)
        return value / 12.92f;
    return powf((value + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float value)
{
    value = Clamp(value, 0.0f, 1.0f);
    if (value <= 0.0031308f)
        return value * 12.92f;
    return 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
}

bool IsPostProcessSettingsValid(const PostProcessSettings& settings)
{
    return settings.bloomIntensity == settings.bloomIntensity && settings.bloomIntensity >= 0.0f && settings.bloomIntensity <= 4.0f && settings.exposure == settings.exposure && settings.exposure > 0.0f && settings.exposure <= 16.0f && settings.bloomIntensity <= FLT_MAX && settings.exposure <= FLT_MAX && settings.saturation == settings.saturation && settings.saturation >= 0.0f && settings.saturation <= 2.0f && settings.saturation <= FLT_MAX && settings.contrast == settings.contrast && settings.contrast >= 0.0f && settings.contrast <= 2.0f && settings.contrast <= FLT_MAX;
}

LinearColor ExtractBloom(LinearColor color)
{
    const float luminance = Max(color.r * kLuminanceWeights[0] + color.g * kLuminanceWeights[1] + color.b * kLuminanceWeights[2], 0.0f);
    const float softLimit = Clamp(luminance - kBloomThreshold + kBloomKnee, 0.0f, 2.0f * kBloomKnee);
    const float softContribution = softLimit * softLimit / (4.0f * kBloomKnee);
    const float contribution = Max(luminance - kBloomThreshold, softContribution) / Max(luminance, 0.0001f);
    return { color.r * contribution, color.g * contribution, color.b * contribution, 1.0f };
}

void GetBloomGaussianWeights(float* weights)
{
    if (!weights)
        return;
    for (uint32_t i = 0; i < 5; ++i)
        weights[i] = kBloomWeights[i];
}

LinearColor CompositeAndToneMap(LinearColor scene, LinearColor bloom, const PostProcessSettings& settings)
{
    const float bloomScale = settings.bloomEnabled ? settings.bloomIntensity : 0.0f;
    float red = Max(scene.r + bloom.r * bloomScale, 0.0f) * settings.exposure;
    float green = Max(scene.g + bloom.g * bloomScale, 0.0f) * settings.exposure;
    float blue = Max(scene.b + bloom.b * bloomScale, 0.0f) * settings.exposure;
    if (settings.toneMappingEnabled)
    {
        red = ToneMapAces(red);
        green = ToneMapAces(green);
        blue = ToneMapAces(blue);
    }
    else
    {
        red = Clamp(red, 0.0f, 1.0f);
        green = Clamp(green, 0.0f, 1.0f);
        blue = Clamp(blue, 0.0f, 1.0f);
    }
    const float luminance = red * kLuminanceWeights[0] + green * kLuminanceWeights[1] + blue * kLuminanceWeights[2];
    red = luminance + (red - luminance) * settings.saturation;
    green = luminance + (green - luminance) * settings.saturation;
    blue = luminance + (blue - luminance) * settings.saturation;
    red = Clamp((red - 0.5f) * settings.contrast + 0.5f, 0.0f, 1.0f);
    green = Clamp((green - 0.5f) * settings.contrast + 0.5f, 0.0f, 1.0f);
    blue = Clamp((blue - 0.5f) * settings.contrast + 0.5f, 0.0f, 1.0f);
    return { red, green, blue, Clamp(scene.a, 0.0f, 1.0f) };
}

} // namespace gk::render
