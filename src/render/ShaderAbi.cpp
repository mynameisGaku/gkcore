#include "ShaderAbi.h"

#include <float.h>
#include <string.h>

namespace gk::render {
namespace {
/**
 * Rejects nonfinite values before they enter a GPU constant block.
 */
bool IsFinite(float value) {
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

/**
 * Validates all four components of one shader constant.
 */
bool IsFinite(Float4 value) {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}
}

bool MakeShaderConstantSlice(uint32_t frameIndex, uint32_t drawIndex,
                             ShaderConstantSlice& output) {
    if (frameIndex >= kShaderConstantFrameCount || drawIndex >= kShaderConstantDrawCapacity)
        return false;
    const ShaderConstantSlice replacement = {
        frameIndex * kShaderConstantDrawCapacity + drawIndex,
        drawIndex * kShaderConstantBlockBytes
    };
    output = replacement;
    return true;
}

bool PackShaderConstantBlock(const ShaderConstant* constants, uint32_t count,
                             void* destination, uint32_t destinationBytes,
                             String& error) {
    if (!destination || destinationBytes < kShaderConstantBlockBytes ||
        (count && !constants) || count > kShaderConstantSlotCount) {
        error.Assign("custom shader constant storage is invalid");
        return false;
    }
    uint64_t usedSlots = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const ShaderConstant& constant = constants[i];
        if (constant.registerIndex >= kShaderConstantSlotCount || !IsFinite(constant.value)) {
            error.Assign("custom shader constant slot or value is invalid");
            return false;
        }
        const uint64_t bit = static_cast<uint64_t>(1) << constant.registerIndex;
        if (usedSlots & bit) {
            error.Assign("custom shader constant slots must be unique");
            return false;
        }
        usedSlots |= bit;
    }

    Float4 slots[kShaderConstantSlotCount] = {};
    for (uint32_t i = 0; i < count; ++i)
        slots[constants[i].registerIndex] = constants[i].value;
    memcpy(destination, slots, sizeof(slots));
    error.Clear();
    return true;
}

} // namespace gk::render
