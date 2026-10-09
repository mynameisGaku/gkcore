#include "render/CustomShaderPolicy.h"

/**
 * Implements the deterministic variant and arena-boundary choices used by native draws.
 */
namespace gk::render
{

bool SelectCustomShaderPipelineVariant(uint32_t layer, bool depthTest, bool alphaBlend, CustomShaderPipelineVariant& output)
{
    if (layer > 1)
        return false;
    CustomShaderPipelineVariant selected;
    if (layer == 1)
    {
        selected = alphaBlend ? CustomShaderPipelineVariant::UiAlpha : CustomShaderPipelineVariant::UiOpaque;
    }
    else if (depthTest)
    {
        selected = alphaBlend ? CustomShaderPipelineVariant::SceneAlphaDepth : CustomShaderPipelineVariant::SceneOpaqueDepth;
    }
    else
    {
        selected = alphaBlend ? CustomShaderPipelineVariant::SceneAlpha : CustomShaderPipelineVariant::SceneOpaque;
    }
    output = selected;
    return true;
}

/**
 * Selects the dedicated HDR variant after validating its constant-snapshot address.
 */
bool SelectPostEffectShaderPipelineVariant(uint32_t frameIndex, uint32_t drawIndex, CustomShaderPipelineVariant& output)
{
    if (frameIndex >= kCustomShaderFrameCount || drawIndex >= kCustomShaderMaximumDraws)
        return false;
    output = CustomShaderPipelineVariant::PostEffect;
    return true;
}

bool IsCustomShaderFrameValid(uint32_t frameIndex, uint32_t drawCount)
{
    return frameIndex < kCustomShaderFrameCount && drawCount <= kCustomShaderMaximumDraws;
}

bool IsCustomShaderBindingValid(uint32_t frameIndex, uint32_t drawIndex, uint32_t layer)
{
    return frameIndex < kCustomShaderFrameCount && drawIndex < kCustomShaderMaximumDraws && layer <= 1;
}

} // namespace gk::render
