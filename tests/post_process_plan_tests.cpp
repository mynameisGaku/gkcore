#include "../src/render/PostProcessPlan.h"

#include <stdio.h>

namespace {

bool Expect(bool condition, const char* message) {
    if (condition) return true;
    fprintf(stderr, "%s\n", message);
    return false;
}

gk::render::PostProcessSettings Settings(bool bloom, bool fxaa) {
    gk::render::PostProcessSettings settings{};
    settings.bloomEnabled = bloom;
    settings.bloomIntensity = 0.15f;
    settings.exposure = 1.0f;
    settings.toneMappingEnabled = true;
    settings.saturation = 1.0f;
    settings.contrast = 1.0f;
    settings.fxaaEnabled = fxaa;
    return settings;
}

bool FullPlanHasFiveOrderedPasses() {
    using namespace gk::render;
    PostProcessPlan plan{};
    if (!BuildPostProcessPlan(Settings(true, true), plan)) return false;
    return Expect(plan.count == 5, "bloom + FXAA should produce five full-screen passes") &&
           Expect(plan.steps[0].kind == PostProcessStepKind::BloomExtract &&
                  plan.steps[0].target == PostProcessTarget::BloomA, "bloom extract target/order is invalid") &&
           Expect(plan.steps[1].kind == PostProcessStepKind::BloomBlurHorizontal &&
                  plan.steps[1].target == PostProcessTarget::BloomB, "horizontal blur target/order is invalid") &&
           Expect(plan.steps[2].kind == PostProcessStepKind::BloomBlurVertical &&
                  plan.steps[2].target == PostProcessTarget::BloomA, "vertical blur target/order is invalid") &&
           Expect(plan.steps[3].kind == PostProcessStepKind::Composite &&
                  plan.steps[3].target == PostProcessTarget::LinearLdr, "composite must grade into the linear FXAA input") &&
           Expect(plan.steps[4].kind == PostProcessStepKind::Fxaa &&
                  plan.steps[4].target == PostProcessTarget::Output, "FXAA must be the final pass before UI");
}

bool FxaaWithoutBloomUsesTwoPasses() {
    using namespace gk::render;
    PostProcessPlan plan{};
    if (!BuildPostProcessPlan(Settings(false, true), plan)) return false;
    return Expect(plan.count == 2, "FXAA without bloom should use composite and FXAA") &&
           Expect(plan.steps[0].kind == PostProcessStepKind::Composite &&
                  plan.steps[0].target == PostProcessTarget::LinearLdr, "FXAA input must remain linear LDR") &&
           Expect(plan.steps[1].kind == PostProcessStepKind::Fxaa &&
                  plan.steps[1].target == PostProcessTarget::Output, "FXAA output target is invalid");
}

bool FxaaDisabledCompositesDirectly() {
    using namespace gk::render;
    PostProcessPlan plan{};
    if (!BuildPostProcessPlan(Settings(true, false), plan)) return false;
    return Expect(plan.count == 4, "bloom without FXAA should use bloom and direct composite") &&
           Expect(plan.steps[3].kind == PostProcessStepKind::Composite &&
                  plan.steps[3].target == PostProcessTarget::Output, "FXAA-off composite must target the swapchain");
}

bool DisabledEffectsKeepDirectComposite() {
    using namespace gk::render;
    PostProcessPlan plan{};
    if (!BuildPostProcessPlan(Settings(false, false), plan)) return false;
    return Expect(plan.count == 1, "disabled bloom and FXAA should use one direct composite pass") &&
           Expect(plan.steps[0].kind == PostProcessStepKind::Composite &&
                  plan.steps[0].target == PostProcessTarget::Output, "direct composite target is invalid");
}

bool AbandonedRecordingDoesNotChangeCommittedTargetStates() {
    using namespace gk::render;
    PostProcessTargetStateTracker tracker;
    PostProcessTargetStates submitted{};
    submitted.bloomShaderReadable[0] = true;
    submitted.bloomShaderReadable[1] = true;
    tracker.Stage(submitted);
    tracker.Commit();

    PostProcessTargetStates abandoned = tracker.Committed();
    abandoned.bloomShaderReadable[0] = false;
    abandoned.linearLdrShaderReadable = true;
    tracker.Stage(abandoned);
    tracker.DiscardPending();

    const PostProcessTargetStates& nextFrame = tracker.Committed();
    return Expect(nextFrame.bloomShaderReadable[0] && nextFrame.bloomShaderReadable[1],
                  "an abandoned command changed submitted bloom target states") &&
           Expect(!nextFrame.linearLdrShaderReadable,
                  "an abandoned FXAA command marked the graded target shader-readable");
}

}

int main() {
    return FullPlanHasFiveOrderedPasses() && FxaaWithoutBloomUsesTwoPasses() &&
           FxaaDisabledCompositesDirectly() && DisabledEffectsKeepDirectComposite() &&
           AbandonedRecordingDoesNotChangeCommittedTargetStates() ? 0 : 1;
}
