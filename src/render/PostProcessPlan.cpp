#include "render/PostProcessPlan.h"

/**
 * Portable construction and submission-state policy for post-process frames.
 */
namespace gk::render
{
/**
 * Helpers private to pass-plan and target-state policy implementation.
 */
namespace
{

/**
 * Appends one bounded pass to a temporary plan.
 */
bool Append(PostProcessPlan& plan, PostProcessStepKind kind, PostProcessTarget target)
{
    if (plan.count >= sizeof(plan.steps) / sizeof(plan.steps[0]))
        return false;
    plan.steps[plan.count++] = { kind, target };
    return true;
}

}

bool BuildPostProcessPlan(const PostProcessSettings& settings, PostProcessPlan& output)
{
    if (!IsPostProcessSettingsValid(settings))
        return false;

    PostProcessPlan replacement{};
    if (settings.bloomEnabled && (!Append(replacement, PostProcessStepKind::BloomExtract, PostProcessTarget::BloomA) || !Append(replacement, PostProcessStepKind::BloomBlurHorizontal, PostProcessTarget::BloomB) || !Append(replacement, PostProcessStepKind::BloomBlurVertical, PostProcessTarget::BloomA)))
        return false;

    const PostProcessTarget compositeTarget = settings.fxaaEnabled ? PostProcessTarget::LinearLdr : PostProcessTarget::Output;
    if (!Append(replacement, PostProcessStepKind::Composite, compositeTarget))
        return false;
    if (settings.fxaaEnabled && !Append(replacement, PostProcessStepKind::Fxaa, PostProcessTarget::Output))
        return false;

    output = replacement;
    return true;
}

PostProcessTargetStateTracker::PostProcessTargetStateTracker() : committed_{}, pending_{}, hasPending_(false)
{
}

const PostProcessTargetStates& PostProcessTargetStateTracker::Committed() const
{
    return committed_;
}

void PostProcessTargetStateTracker::Stage(const PostProcessTargetStates& states)
{
    pending_ = states;
    hasPending_ = true;
}

void PostProcessTargetStateTracker::Commit()
{
    if (!hasPending_)
        return;
    committed_ = pending_;
    hasPending_ = false;
}

void PostProcessTargetStateTracker::DiscardPending()
{
    hasPending_ = false;
}

void PostProcessTargetStateTracker::Reset()
{
    committed_ = {};
    pending_ = {};
    hasPending_ = false;
}

}
