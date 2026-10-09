#include "render/PostEffectPlan.h"

#include <float.h>

/**
 * Portable validation and fullscreen geometry for custom post-effect draws.
 */
namespace gk::render
{
/**
 * Checks float4 constants before they are copied into a queued render plan.
 */
namespace
{

/**
 * Returns true for finite values representable by the runtime float format.
 */
bool IsFinite(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

/**
 * Stores a failed plan diagnostic without touching the output plan.
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

} // namespace

/**
 * Validates a captured post-effect draw and publishes its complete frame-local plan.
 */
bool BuildPostEffectPlan(const detail::FramePacket& frame, uint32_t customDrawCount, PostEffectPlan& output, String& error)
{
    if (customDrawCount > kCustomShaderMaximumDraws || frame.postEffectConstantCount > kShaderConstantSlotCount)
        return Fail(error, "post-effect draw or constant count exceeds its limit");

    PostEffectPlan replacement{};
    replacement.enabled = frame.postEffectShader.IsValid();
    replacement.shader = frame.postEffectShader;
    replacement.customDrawIndex = customDrawCount;
    replacement.preparedDrawCount = customDrawCount;
    if (replacement.enabled)
    {
        if (customDrawCount == kCustomShaderMaximumDraws)
            return Fail(error, "post-effect draw exceeds the custom draw frame capacity");
        for (uint32_t i = 0; i < frame.postEffectConstantCount; ++i)
        {
            const ShaderConstant& constant = frame.postEffectConstants[i];
            if (constant.registerIndex >= kShaderConstantSlotCount || !IsFinite(constant.value.x) || !IsFinite(constant.value.y) || !IsFinite(constant.value.z) || !IsFinite(constant.value.w))
                return Fail(error, "post-effect constant slot or value is invalid");
            for (uint32_t previous = 0; previous < i; ++previous)
                if (frame.postEffectConstants[previous].registerIndex == constant.registerIndex)
                    return Fail(error, "post-effect constant register is duplicated");
            replacement.constants[i] = constant;
        }
        replacement.constantCount = frame.postEffectConstantCount;
        replacement.preparedDrawCount = customDrawCount + 1;
    }
    else if (frame.postEffectConstantCount != 0)
    {
        return Fail(error, "disabled post-effect has captured constants");
    }

    output = replacement;
    error.Clear();
    return true;
}

/**
 * Creates a single oversized triangle whose interpolated UVs span the viewport.
 */
void MakePostEffectVertices(Vertex (&vertices)[3])
{
    const float positions[3][2] = { { -1.0f, 1.0f }, { 3.0f, 1.0f }, { -1.0f, -3.0f } };
    const float uvs[3][2] = { { 0.0f, 0.0f }, { 2.0f, 0.0f }, { 0.0f, 2.0f } };
    for (uint32_t i = 0; i < 3; ++i)
    {
        vertices[i].position[0] = positions[i][0];
        vertices[i].position[1] = positions[i][1];
        vertices[i].position[2] = 0.0f;
        vertices[i].position[3] = 1.0f;
        vertices[i].color[0] = 1.0f;
        vertices[i].color[1] = 1.0f;
        vertices[i].color[2] = 1.0f;
        vertices[i].color[3] = 1.0f;
        vertices[i].uv[0] = uvs[i][0];
        vertices[i].uv[1] = uvs[i][1];
    }
}

/**
 * Clears submitted and pending state when the associated target is recreated.
 */
void PostEffectTargetStateTracker::Reset()
{
    shaderReadable_ = false;
    hasPending_ = false;
    pendingShaderReadable_ = false;
}

/**
 * Records the state transition that will become visible after submission.
 */
bool PostEffectTargetStateTracker::Stage(bool nextShaderReadable)
{
    if (hasPending_)
        return false;
    hasPending_ = true;
    pendingShaderReadable_ = nextShaderReadable;
    return true;
}

/**
 * Publishes a staged transition after its command buffer has been submitted.
 */
bool PostEffectTargetStateTracker::Commit()
{
    if (!hasPending_)
        return false;
    shaderReadable_ = pendingShaderReadable_;
    hasPending_ = false;
    pendingShaderReadable_ = false;
    return true;
}

/**
 * Drops transitions from an abandoned recording without changing submitted state.
 */
void PostEffectTargetStateTracker::DiscardPending()
{
    hasPending_ = false;
    pendingShaderReadable_ = false;
}

bool PostEffectTargetStateTracker::ShaderReadable() const
{
    return shaderReadable_;
}
bool PostEffectTargetStateTracker::HasPending() const
{
    return hasPending_;
}
bool PostEffectTargetStateTracker::PendingShaderReadable() const
{
    return pendingShaderReadable_;
}

} // namespace gk::render
