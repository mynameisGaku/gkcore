#pragma once

#include <stdint.h>

/**
 * CPU validation and state selection shared by custom shader recording and tests.
 */
namespace gk::render {

inline constexpr uint32_t kCustomShaderFrameCount = 2;
inline constexpr uint32_t kCustomShaderMaximumDraws = 4096;

/**
 * Pipeline variants created for the scene and display targets.
 */
enum class CustomShaderPipelineVariant : uint8_t {
    SceneOpaqueDepth,
    SceneAlphaDepth,
    SceneOpaque,
    SceneAlpha,
    UiOpaque,
    UiAlpha,
    PostEffect
};

/**
 * Selects one of the six prebuilt variants; invalid layers leave output unchanged.
 */
bool SelectCustomShaderPipelineVariant(uint32_t layer, bool depthTest, bool alphaBlend,
                                      CustomShaderPipelineVariant& output);

/**
 * Selects the depth-free post-effect variant for a valid frame and prepared draw ordinal.
 */
bool SelectPostEffectShaderPipelineVariant(uint32_t frameIndex, uint32_t drawIndex,
                                           CustomShaderPipelineVariant& output);

/**
 * Validates the frame slot and number of custom draw snapshots before arena writes.
 */
bool IsCustomShaderFrameValid(uint32_t frameIndex, uint32_t drawCount);

/**
 * Validates the frame slot, per-frame draw ordinal, and scene/UI layer before binding.
 */
bool IsCustomShaderBindingValid(uint32_t frameIndex, uint32_t drawIndex, uint32_t layer);

} // namespace gk::render
