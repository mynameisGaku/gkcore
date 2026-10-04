#include "../src/render/CustomShaderPolicy.h"
#include "../src/foundation/String.h"

namespace gk::tests {

bool CustomShaderPolicyContract(String& failure) {
    render::CustomShaderPipelineVariant variant = render::CustomShaderPipelineVariant::UiAlpha;
    if (!render::SelectCustomShaderPipelineVariant(0, true, false, variant) ||
        variant != render::CustomShaderPipelineVariant::SceneOpaqueDepth ||
        !render::SelectCustomShaderPipelineVariant(0, true, true, variant) ||
        variant != render::CustomShaderPipelineVariant::SceneAlphaDepth ||
        !render::SelectCustomShaderPipelineVariant(0, false, false, variant) ||
        variant != render::CustomShaderPipelineVariant::SceneOpaque ||
        !render::SelectCustomShaderPipelineVariant(0, false, true, variant) ||
        variant != render::CustomShaderPipelineVariant::SceneAlpha) {
        failure.Assign("scene custom shader pipeline selection ignored depth or alpha state");
        return false;
    }
    if (!render::SelectCustomShaderPipelineVariant(1, false, false, variant) ||
        variant != render::CustomShaderPipelineVariant::UiOpaque ||
        !render::SelectCustomShaderPipelineVariant(1, true, false, variant) ||
        variant != render::CustomShaderPipelineVariant::UiOpaque ||
        !render::SelectCustomShaderPipelineVariant(1, false, true, variant) ||
        variant != render::CustomShaderPipelineVariant::UiAlpha ||
        !render::SelectCustomShaderPipelineVariant(1, true, true, variant) ||
        variant != render::CustomShaderPipelineVariant::UiAlpha) {
        failure.Assign("UI custom shader pipeline selection did not ignore depth state");
        return false;
    }
    const render::CustomShaderPipelineVariant before = variant;
    if (render::SelectCustomShaderPipelineVariant(2, false, false, variant) || variant != before) {
        failure.Assign("invalid custom shader layer was accepted or changed pipeline output");
        return false;
    }
    if (!render::IsCustomShaderFrameValid(0, 0) ||
        !render::IsCustomShaderFrameValid(1, render::kCustomShaderMaximumDraws) ||
        render::IsCustomShaderFrameValid(2, 0) ||
        render::IsCustomShaderFrameValid(0, render::kCustomShaderMaximumDraws + 1) ||
        !render::IsCustomShaderBindingValid(1, render::kCustomShaderMaximumDraws - 1, 1) ||
        render::IsCustomShaderBindingValid(2, 0, 0) ||
        render::IsCustomShaderBindingValid(0, render::kCustomShaderMaximumDraws, 0) ||
        render::IsCustomShaderBindingValid(0, 0, 2)) {
        failure.Assign("custom shader frame, draw, or layer bounds do not match the arena");
        return false;
    }
    return true;
}

}

int main() {
    gk::String failure;
    return gk::tests::CustomShaderPolicyContract(failure) ? 0 : 1;
}
