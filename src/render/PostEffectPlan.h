#pragma once

#include "internal/Backend.hpp"
#include "render/Geometry.h"
#include "render/CustomShaderPolicy.h"
#include "render/ShaderAbi.h"
#include "foundation/String.h"

/**
 * CPU draw planning and transactional output-state tracking for custom post effects.
 */
namespace gk::render
{

/**
 * One captured custom post-effect draw appended after regular custom draws.
 */
struct PostEffectPlan
{
    bool enabled;
    ShaderHandle shader;
    uint32_t customDrawIndex;
    uint32_t preparedDrawCount;
    uint32_t constantCount;
    ShaderConstant constants[kShaderConstantSlotCount];
};

/**
 * Validates a frame's captured post shader and constants before native arena writes.
 */
bool BuildPostEffectPlan(const detail::FramePacket& frame, uint32_t customDrawCount, PostEffectPlan& output, String& error);

/**
 * Fills the clip-space fullscreen triangle using the shared position/color/UV vertex ABI.
 */
void MakePostEffectVertices(Vertex (&vertices)[3]);

/**
 * Tracks one render target's committed and pending shader-readable state.
 */
class PostEffectTargetStateTracker
{
  public:
    /**
     * Resets committed and pending state to a newly created render target.
     */
    void Reset();
    /**
     * Stages the state expected after the current command submission.
     */
    bool Stage(bool nextShaderReadable);
    /**
     * Commits staged state after the command buffer is submitted.
     */
    bool Commit();
    /**
     * Drops an unsubmitted transition while preserving committed state.
     */
    void DiscardPending();

    /**
     * Returns the last submitted shader-readable state.
     */
    bool ShaderReadable() const;
    /**
     * Reports whether a command recording has an uncommitted transition.
     */
    bool HasPending() const;
    /**
     * Returns the target state staged for the current recording.
     */
    bool PendingShaderReadable() const;

  private:
    bool shaderReadable_ = false;
    bool hasPending_ = false;
    bool pendingShaderReadable_ = false;
};

} // namespace gk::render
