#pragma once

#include <stdint.h>

/**
 * Reference math shared by post-process tests and the Direct3D 12 shader passes.
 */
namespace gk::render {

inline constexpr float kBloomThreshold = 1.0f;
inline constexpr float kBloomKnee = 0.5f;

/**
 * One linear-light color used by bloom and tone-mapping math.
 */
struct LinearColor {
    float r;
    float g;
    float b;
    float a;
};

/**
 * Settings sampled once for a frame's post-process pass.
 */
struct PostProcessSettings {
    bool bloomEnabled = true;
    float bloomIntensity = 0.15f;
    float exposure = 1.0f;
    bool toneMappingEnabled = true;
    float saturation = 1.0f;
    float contrast = 1.0f;
    bool fxaaEnabled = true;
};

/**
 * Converts one normalized sRGB channel to linear light.
 */
float SrgbToLinear(float value);
/**
 * Converts one normalized linear-light channel to sRGB for perceptual edge detection.
 */
float LinearToSrgb(float value);
/**
 * Validates all post-process values against their public API limits.
 */
bool IsPostProcessSettingsValid(const PostProcessSettings& settings);
/**
 * Extracts bright linear-light pixels with a soft threshold at luminance 1.
 */
LinearColor ExtractBloom(LinearColor color);
/**
 * Writes the center and four positive-offset weights for a symmetric 9-tap blur.
 */
void GetBloomGaussianWeights(float* weights);
/**
 * Composites bloom, then applies exposure, tone mapping, saturation, contrast, and display clamping.
 */
LinearColor CompositeAndToneMap(LinearColor scene, LinearColor bloom,
                                const PostProcessSettings& settings);

} // namespace gk::render
