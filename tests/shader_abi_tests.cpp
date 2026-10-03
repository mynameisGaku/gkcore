#include "../src/render/ShaderAbi.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace gk::tests {

bool ShaderConstantAbiContract(String& failure) {
    render::ShaderConstantSlice slice{};
    if (!render::MakeShaderConstantSlice(0, 0, slice) ||
        slice.descriptorIndex != 0 || slice.byteOffset != 0 ||
        !render::MakeShaderConstantSlice(1, 0, slice) ||
        slice.descriptorIndex != render::kShaderConstantDrawCapacity || slice.byteOffset != 0 ||
        !render::MakeShaderConstantSlice(1, render::kShaderConstantDrawCapacity - 1, slice) ||
        slice.descriptorIndex != render::kShaderConstantDescriptorCapacity - 1 ||
        slice.byteOffset != (render::kShaderConstantDrawCapacity - 1) * render::kShaderConstantBlockBytes ||
        render::MakeShaderConstantSlice(2, 0, slice) ||
        render::MakeShaderConstantSlice(0, render::kShaderConstantDrawCapacity, slice)) {
        failure.Assign("custom shader draw slices do not stay within two aligned frame arenas");
        return false;
    }

    Float4 block[render::kShaderConstantSlotCount];
    memset(block, 0x5a, sizeof(block));
    const ShaderConstant sparse[2] = {
        {63, {6.0f, 7.0f, 8.0f, 9.0f}},
        {2, {1.0f, 2.0f, 3.0f, 4.0f}}
    };
    if (!render::PackShaderConstantBlock(sparse, 2, block, sizeof(block), failure)) return false;
    for (uint32_t slot = 0; slot < render::kShaderConstantSlotCount; ++slot) {
        const Float4 expected = slot == 63 ? sparse[0].value : (slot == 2 ? sparse[1].value : Float4{});
        const Float4 actual = block[slot];
        if (actual.x != expected.x || actual.y != expected.y ||
            actual.z != expected.z || actual.w != expected.w) {
            failure.Assign("custom shader constants were not zero-filled and placed by register index");
            return false;
        }
    }

    Float4 preserved[render::kShaderConstantSlotCount];
    for (uint32_t i = 0; i < render::kShaderConstantSlotCount; ++i)
        preserved[i] = {11.0f, 12.0f, 13.0f, 14.0f};
    const Float4 before = preserved[0];
    const ShaderConstant invalidSlot = {64, {1, 2, 3, 4}};
    if (render::PackShaderConstantBlock(&invalidSlot, 1, preserved, sizeof(preserved), failure) ||
        preserved[0].x != before.x || preserved[0].y != before.y ||
        preserved[0].z != before.z || preserved[0].w != before.w) {
        failure.Assign("invalid shader constants were accepted or modified the output block");
        return false;
    }
    ShaderConstant tooMany[render::kShaderConstantSlotCount + 1]{};
    if (render::PackShaderConstantBlock(tooMany, render::kShaderConstantSlotCount + 1,
                                        preserved, sizeof(preserved), failure)) {
        failure.Assign("more than 64 custom shader constants were accepted");
        return false;
    }
    if (render::PackShaderConstantBlock(nullptr, 1, preserved, sizeof(preserved), failure) ||
        render::PackShaderConstantBlock(nullptr, 0, preserved, sizeof(preserved) - 1, failure)) {
        failure.Assign("invalid custom shader constant storage was accepted");
        return false;
    }

    union AliasBlock {
        ShaderConstant source;
        uint64_t alignment[render::kShaderConstantBlockBytes / sizeof(uint64_t)];
        uint8_t bytes[render::kShaderConstantBlockBytes];
    } alias{};
    alias.source = {5, {5.0f, 6.0f, 7.0f, 8.0f}};
    if (!render::PackShaderConstantBlock(&alias.source, 1, alias.bytes, sizeof(alias.bytes), failure))
        return false;
    Float4 aliasedSlot[render::kShaderConstantSlotCount];
    memcpy(aliasedSlot, alias.bytes, sizeof(aliasedSlot));
    if (aliasedSlot[5].x != 5.0f || aliasedSlot[5].y != 6.0f ||
        aliasedSlot[5].z != 7.0f || aliasedSlot[5].w != 8.0f) {
        failure.Assign("packing an aliased shader constant erased its source before copying");
        return false;
    }

    uint8_t unalignedStorage[render::kShaderConstantBlockBytes + 1]{};
    Float4 alignedSlot[render::kShaderConstantSlotCount];
    if (!render::PackShaderConstantBlock(sparse, 2, unalignedStorage + 1,
                                         render::kShaderConstantBlockBytes, failure))
        return false;
    memcpy(alignedSlot, unalignedStorage + 1, sizeof(alignedSlot));
    if (alignedSlot[63].x != 6.0f || alignedSlot[2].w != 4.0f) {
        failure.Assign("packing into an unaligned constant range produced invalid values");
        return false;
    }
    return true;
}

} // namespace gk::tests

int main() {
    gk::String failure;
    return gk::tests::ShaderConstantAbiContract(failure) ? 0 : 1;
}
