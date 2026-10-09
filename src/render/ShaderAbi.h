#pragma once

#include "effects/Shaders.h"
#include "foundation/String.h"

/**
 * Uniform-buffer layout shared by draw snapshots and custom shader allocation.
 */
namespace gk::render
{

inline constexpr uint32_t kShaderConstantSlotCount = 64;
inline constexpr uint32_t kShaderConstantBlockBytes = kShaderConstantSlotCount * sizeof(Float4);
inline constexpr uint32_t kShaderConstantFrameCount = 2;
inline constexpr uint32_t kShaderConstantDrawCapacity = 4096;
inline constexpr uint32_t kShaderConstantDescriptorCapacity = kShaderConstantFrameCount * kShaderConstantDrawCapacity;

/**
 * Descriptor-array index and byte range for one custom draw's constant block.
 */
struct ShaderConstantSlice
{
    uint32_t descriptorIndex;
    uint32_t byteOffset;
};

/**
 * Maps a frame and custom draw ordinal to its unique, aligned descriptor slice.
 */
bool MakeShaderConstantSlice(uint32_t frameIndex, uint32_t drawIndex, ShaderConstantSlice& output);

/**
 * Validates and zero-fills a 64-slot float4 block before copying sparse constants by slot.
 */
bool PackShaderConstantBlock(const ShaderConstant* constants, uint32_t count, void* destination, uint32_t destinationBytes, String& error);

} // namespace gk::render
