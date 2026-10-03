#include "../src/render/PostProcess.h"
#include "../src/foundation/String.h"

#include <math.h>

namespace gk::tests {
namespace {
bool Near(float left, float right, float tolerance = 0.0001f) {
    return fabsf(left - right) <= tolerance;
}
}

bool PostProcessMathContract(String& failure) {
    if (!Near(render::SrgbToLinear(0.0f), 0.0f) ||
        !Near(render::SrgbToLinear(0.04045f), 0.0031308f) ||
        !Near(render::SrgbToLinear(0.5f), 0.214041f) ||
        !Near(render::SrgbToLinear(1.0f), 1.0f)) {
        failure.Assign("sRGB conversion does not match the linear-light transfer curve");
        return false;
    }

    float weights[5]{};
    render::GetBloomGaussianWeights(weights);
    const float totalWeight = weights[0] + 2.0f * (weights[1] + weights[2] + weights[3] + weights[4]);
    if (!Near(totalWeight, 1.0f) || weights[0] <= weights[1] || weights[4] <= 0.0f) {
        failure.Assign("bloom blur weights are not a normalized symmetric Gaussian kernel");
        return false;
    }

    const render::LinearColor dark = {0.2f, 0.4f, 0.6f, 1.0f};
    const render::LinearColor noBloom = render::ExtractBloom(dark);
    const render::LinearColor bright = {4.0f, 4.0f, 4.0f, 1.0f};
    const render::LinearColor extracted = render::ExtractBloom(bright);
    if (!Near(noBloom.r, 0.0f) || !Near(noBloom.g, 0.0f) || !Near(noBloom.b, 0.0f) ||
        !(extracted.r > 0.0f && extracted.g > 0.0f && extracted.b > 0.0f)) {
        failure.Assign("bloom extraction does not isolate above-threshold highlights");
        return false;
    }

    render::PostProcessSettings settings = {false, 0.0f, 1.0f, false};
    render::LinearColor output = render::CompositeAndToneMap(dark, {0, 0, 0, 1}, settings);
    if (!Near(output.r, dark.r) || !Near(output.g, dark.g) || !Near(output.b, dark.b)) {
        failure.Assign("disabled post processing changed scene color");
        return false;
    }
    settings.exposure = 2.0f;
    output = render::CompositeAndToneMap(dark, {0, 0, 0, 1}, settings);
    if (!Near(output.r, 0.4f) || !Near(output.g, 0.8f) || !Near(output.b, 1.0f)) {
        failure.Assign("exposure was not applied before output clamping");
        return false;
    }
    settings.bloomEnabled = true;
    settings.bloomIntensity = 0.5f;
    settings.exposure = 1.0f;
    const render::LinearColor bloom = {0.4f, 0.2f, 0.1f, 1.0f};
    output = render::CompositeAndToneMap(dark, bloom, settings);
    if (!Near(output.r, 0.4f) || !Near(output.g, 0.5f) || !Near(output.b, 0.65f)) {
        failure.Assign("bloom intensity was not combined with the scene");
        return false;
    }
    settings.bloomEnabled = false;
    settings.toneMappingEnabled = true;
    output = render::CompositeAndToneMap({1, 1, 1, 1}, {0, 0, 0, 1}, settings);
    if (!Near(output.r, 0.8038f, 0.001f) || output.r >= 1.0f) {
        failure.Assign("ACES tone mapping does not compress highlights");
        return false;
    }

    if (!render::IsPostProcessSettingsValid({true, 0.15f, 1.0f, true}) ||
        render::IsPostProcessSettingsValid({true, -0.1f, 1.0f, true}) ||
        render::IsPostProcessSettingsValid({true, 0.1f, 0.0f, true}) ||
        render::IsPostProcessSettingsValid({true, 0.1f, INFINITY, true})) {
        failure.Assign("invalid post-process settings were accepted");
        return false;
    }
    return true;
}

} // namespace gk::tests
