#pragma once

#include "PostProcess.h"

#include <stdint.h>

/**
 * Immutable full-screen pass ordering used by the native post-process renderer.
 */
namespace gk::render {

/**
 * One render pass in the frame's post-process chain.
 */
enum class PostProcessStepKind : uint8_t {
    BloomExtract,
    BloomBlurHorizontal,
    BloomBlurVertical,
    Composite,
    Fxaa
};

/**
 * Destination class for one post-process step.
 */
enum class PostProcessTarget : uint8_t { BloomA, BloomB, LinearLdr, Output };

/**
 * One ordered operation and its destination class.
 */
struct PostProcessStep {
    PostProcessStepKind kind;
    PostProcessTarget target;
};

/**
 * Bounded ordered pass sequence for one frame.
 */
struct PostProcessPlan {
    PostProcessStep steps[5];
    uint32_t count;
};

/**
 * Shader-readable states of the reusable bloom and graded-color targets.
 */
struct PostProcessTargetStates {
    bool bloomShaderReadable[2];
    bool linearLdrShaderReadable;
};

/**
 * Separates recorded target barriers from target states accepted by queue submission.
 */
class PostProcessTargetStateTracker {
public:
    /**
     * Starts with resources in their render-target initial state.
     */
    PostProcessTargetStateTracker();
    /**
     * Returns the last queue-submitted target states.
     */
    const PostProcessTargetStates& Committed() const;
    /**
     * Stores the target states reached by a recorded command, awaiting submission.
     */
    void Stage(const PostProcessTargetStates& states);
    /**
     * Publishes the staged states after queue submission accepts the command.
     */
    void Commit();
    /**
     * Drops a recorded state update when its command buffer is not submitted.
     */
    void DiscardPending();
    /**
     * Resets states after targets are created or resized.
     */
    void Reset();

private:
    PostProcessTargetStates committed_;
    PostProcessTargetStates pending_;
    bool hasPending_;
};

/**
 * Builds a frame's bloom/composite/FXAA pass sequence transactionally.
 */
bool BuildPostProcessPlan(const PostProcessSettings& settings, PostProcessPlan& output);

}
