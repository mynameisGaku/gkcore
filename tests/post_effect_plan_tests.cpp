#include "render/PostEffectPlan.h"
#include "render/CustomShaderPolicy.h"
#include "render/ShaderAbi.h"

#include <math.h>
#include <stdio.h>

/**
 * Portable regression checks for post-effect draw planning and target state.
 */
namespace gk::tests
{
using namespace gk::render;
/**
 * Keeps assertion helpers local to this standalone contract test.
 */
namespace
{

/**
 * Reports one post-effect contract failure.
 */
bool Require(bool condition, const char* message)
{
    if (condition)
        return true;
    fprintf(stderr, "%s\n", message);
    return false;
}

/**
 * Compares planned vertex values without requiring bit-identical float arithmetic.
 */
bool Near(float left, float right)
{
    return fabsf(left - right) <= 0.00001f;
}

/**
 * Builds a valid frame with one sparse per-frame custom post-effect constant.
 */
void MakeFrame(detail::FramePacket& frame)
{
    frame.postEffectShader = ShaderHandle(17);
    frame.postEffectConstantCount = 1;
    frame.postEffectConstants[0] = { 3, Float4{ 0.25f, 0.5f, 0.75f, 1.0f } };
}

/**
 * Tests enabled and disabled post-effect draw capacity at the exact frame limit.
 */
bool CheckPlanCapacity()
{
    String error;
    PostEffectPlan plan{};
    detail::FramePacket frame{};
    MakeFrame(frame);
    if (!Require(BuildPostEffectPlan(frame, kCustomShaderMaximumDraws - 1, plan, error), "post-effect did not fit as the last draw slot"))
        return false;
    if (!Require(plan.enabled && plan.shader == frame.postEffectShader && plan.customDrawIndex == kCustomShaderMaximumDraws - 1 && plan.preparedDrawCount == kCustomShaderMaximumDraws && plan.constantCount == 1 && plan.constants[0].registerIndex == 3 && Near(plan.constants[0].value.z, 0.75f), "enabled post-effect draw snapshot was not copied to its ordinal"))
        return false;

    PostEffectPlan preserved = plan;
    if (!Require(!BuildPostEffectPlan(frame, kCustomShaderMaximumDraws, plan, error) && plan.enabled == preserved.enabled && plan.shader == preserved.shader && plan.customDrawIndex == preserved.customDrawIndex && plan.preparedDrawCount == preserved.preparedDrawCount && plan.constantCount == preserved.constantCount && plan.constants[0].value.z == preserved.constants[0].value.z, "overflowing enabled plan modified output"))
        return false;

    frame.postEffectShader = ShaderHandle();
    frame.postEffectConstantCount = 0;
    if (!Require(BuildPostEffectPlan(frame, kCustomShaderMaximumDraws, plan, error) && !plan.enabled && !plan.shader.IsValid() && plan.constantCount == 0 && plan.preparedDrawCount == kCustomShaderMaximumDraws, "disabled effect consumed an extra custom draw slot"))
        return false;
    return true;
}

/**
 * Tests invalid counts, duplicate slots, and non-finite constants transactionally.
 */
bool CheckInvalidSnapshotsPreserveOutput()
{
    String error;
    detail::FramePacket frame{};
    MakeFrame(frame);
    PostEffectPlan output{};
    output.enabled = true;
    output.shader = ShaderHandle(91);
    output.customDrawIndex = 8;
    output.preparedDrawCount = 9;
    output.constantCount = 1;
    output.constants[0] = { 11, Float4{ 9, 8, 7, 6 } };
    const PostEffectPlan original = output;

    frame.postEffectConstantCount = kShaderConstantSlotCount + 1;
    if (!Require(!BuildPostEffectPlan(frame, 0, output, error) && output.shader == original.shader && output.customDrawIndex == original.customDrawIndex && output.preparedDrawCount == original.preparedDrawCount && output.constantCount == original.constantCount && output.constants[0].value.x == original.constants[0].value.x, "oversized effect constant snapshot was accepted or modified output"))
        return false;

    MakeFrame(frame);
    frame.postEffectConstantCount = 2;
    frame.postEffectConstants[1] = frame.postEffectConstants[0];
    if (!Require(!BuildPostEffectPlan(frame, 0, output, error) && output.shader == original.shader && output.constants[0].value.x == original.constants[0].value.x, "duplicate effect constant slots were accepted or modified output"))
        return false;

    MakeFrame(frame);
    frame.postEffectConstants[0].value.y = INFINITY;
    if (!Require(!BuildPostEffectPlan(frame, 0, output, error) && output.shader == original.shader && output.constants[0].value.x == original.constants[0].value.x, "non-finite effect constants were accepted or modified output"))
        return false;

    MakeFrame(frame);
    if (!Require(!BuildPostEffectPlan(frame, kCustomShaderMaximumDraws + 1, output, error) && output.shader == original.shader && output.constants[0].value.x == original.constants[0].value.x, "oversized custom draw count was accepted or modified output"))
        return false;
    return true;
}

/**
 * Tests fullscreen triangle clipping coverage, UV span, and winding contract.
 */
bool CheckFullscreenTriangle()
{
    Vertex vertices[3]{};
    MakePostEffectVertices(vertices);
    const float expectedPositions[3][2] = { { -1, 1 }, { 3, 1 }, { -1, -3 } };
    const float expectedUvs[3][2] = { { 0, 0 }, { 2, 0 }, { 0, 2 } };
    for (uint32_t i = 0; i < 3; ++i)
    {
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            if (!Require(Near(vertices[i].position[axis], expectedPositions[i][axis]) && Near(vertices[i].uv[axis], expectedUvs[i][axis]), "fullscreen triangle position or UV ABI changed"))
                return false;
        }
        if (!Require(Near(vertices[i].position[2], 0) && Near(vertices[i].position[3], 1) && Near(vertices[i].color[0], 1) && Near(vertices[i].color[1], 1) && Near(vertices[i].color[2], 1) && Near(vertices[i].color[3], 1), "fullscreen triangle depth, homogeneous W, or white tint changed"))
            return false;
    }
    const float area = (vertices[1].position[0] - vertices[0].position[0]) * (vertices[2].position[1] - vertices[0].position[1]) - (vertices[1].position[1] - vertices[0].position[1]) * (vertices[2].position[0] - vertices[0].position[0]);
    return Require(area < 0.0f, "fullscreen triangle winding changed");
}

/**
 * Tests target-state staging, commit, discard, and initialization semantics.
 */
bool CheckTargetStateTransitions()
{
    PostEffectTargetStateTracker state;
    if (!Require(!state.ShaderReadable() && !state.HasPending(), "default post-effect target state is not initialized"))
        return false;
    state.Reset();
    if (!Require(!state.ShaderReadable() && !state.HasPending(), "reset post-effect target state is not initialized"))
        return false;
    if (!Require(state.Stage(true) && state.HasPending() && !state.ShaderReadable() && state.PendingShaderReadable(), "post-effect transition was not staged"))
        return false;
    state.DiscardPending();
    detail::FramePacket disabledFrame{};
    PostEffectPlan disabledPlan{};
    String error;
    if (!Require(BuildPostEffectPlan(disabledFrame, 0, disabledPlan, error) && !disabledPlan.enabled && !state.HasPending() && !state.ShaderReadable() && !state.Commit(), "aborted frame state changed when the following effect was disabled"))
        return false;
    if (!Require(state.Stage(true) && state.Commit() && state.ShaderReadable() && !state.HasPending(), "submitted post-effect transition was not committed"))
        return false;
    if (!Require(state.Stage(false), "could not stage abort regression after committed frame"))
        return false;
    state.DiscardPending();
    if (!Require(state.ShaderReadable() && !state.HasPending() && !state.Commit(), "aborted next frame replaced the committed shader-readable state"))
        return false;
    state.Reset();
    if (!Require(!state.ShaderReadable() && !state.HasPending(), "post-effect state reset retained old frame state"))
        return false;
    return true;
}

}
}

/**
 * Runs the post-effect plan contract checks and reports failure through status.
 */
int main()
{
    using namespace gk::tests;
    return CheckPlanCapacity() && CheckInvalidSnapshotsPreserveOutput() && CheckFullscreenTriangle() && CheckTargetStateTransitions() ? 0 : 1;
}
