#include "../src/render/Shaders.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef GKCORE_TEST_SOURCE_DIR
#define GKCORE_TEST_SOURCE_DIR "."
#endif

namespace gk::tests {
namespace {
void Write32(uint8_t* bytes, uint32_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void Write64(uint8_t* bytes, uint32_t offset, uint64_t value) {
    Write32(bytes, offset, static_cast<uint32_t>(value));
    Write32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
}

uint32_t MakeDxil(uint8_t* bytes, const char* part = "DXIL", uint32_t programVersion = 0x60,
                  uint32_t programWords = 7, uint32_t bitcodeOffset = 16,
                  uint32_t bitcodeSize = 4, const char* bitcodeMagic = "BC\xc0\xde") {
    const uint32_t partOffset = 36;
    memcpy(bytes, "DXBC", 4);
    Write32(bytes, 20, 1);
    Write32(bytes, 24, 72);
    Write32(bytes, 28, 1);
    Write32(bytes, 32, partOffset);
    memcpy(bytes + partOffset, part, 4);
    Write32(bytes, partOffset + 4, 28);
    Write32(bytes, partOffset + 8, programVersion);
    Write32(bytes, partOffset + 12, programWords);
    memcpy(bytes + partOffset + 16, "DXIL", 4);
    Write32(bytes, partOffset + 20, 0x100);
    Write32(bytes, partOffset + 24, bitcodeOffset);
    Write32(bytes, partOffset + 28, bitcodeSize);
    if (bitcodeOffset == 16) memcpy(bytes + partOffset + 16 + bitcodeOffset, bitcodeMagic, 4);
    return 72;
}

uint32_t MakeArtifact(uint8_t* bytes, uint64_t derivativeOffset = 60,
                      uint64_t derivativeSize = 72, const char* part = "DXIL",
                      uint32_t programVersion = 0x60, uint32_t programWords = 7,
                      uint32_t bitcodeOffset = 16, uint32_t bitcodeSize = 4,
                      const char* bitcodeMagic = "BC\xc0\xde") {
    memset(bytes, 0, 160);
    memcpy(bytes, "@FSL", 4);
    Write32(bytes, 4, 1);
    Write64(bytes, 36, 0);
    Write64(bytes, 44, derivativeOffset);
    Write64(bytes, 52, derivativeSize);
    MakeDxil(bytes + 60, part, programVersion, programWords, bitcodeOffset, bitcodeSize, bitcodeMagic);
    return 132;
}

uint32_t MakeDuplicateHashArtifact(uint8_t* bytes) {
    memset(bytes, 0, 256);
    memcpy(bytes, "@FSL", 4);
    Write32(bytes, 4, 2);
    Write64(bytes, 36, 0);
    Write64(bytes, 44, 84);
    Write64(bytes, 52, 72);
    Write64(bytes, 60, 0);
    Write64(bytes, 68, 156);
    Write64(bytes, 76, 72);
    MakeDxil(bytes + 84);
    MakeDxil(bytes + 156);
    return 228;
}

uint32_t MakeEmptyDxilPartArtifact(uint8_t* bytes) {
    memset(bytes, 0, 128);
    memcpy(bytes, "@FSL", 4);
    Write32(bytes, 4, 1);
    Write64(bytes, 36, 0);
    Write64(bytes, 44, 60);
    Write64(bytes, 52, 44);
    memcpy(bytes + 60, "DXBC", 4);
    Write32(bytes, 80, 1);
    Write32(bytes, 84, 44);
    Write32(bytes, 88, 1);
    Write32(bytes, 92, 36);
    memcpy(bytes + 96, "DXIL", 4);
    Write32(bytes, 100, 0);
    return 104;
}
}

bool ShaderArtifactContract(String& failure) {
    char customPostShaderPath[1024];
    const int customPostShaderPathLength = snprintf(
        customPostShaderPath, sizeof(customPostShaderPath),
        "%s/tests/assets/shaders/post_effect_tint.frag", GKCORE_TEST_SOURCE_DIR);
    if (customPostShaderPathLength <= 0 ||
        static_cast<size_t>(customPostShaderPathLength) >= sizeof(customPostShaderPath)) {
        failure.Assign("custom post-effect shader fixture path is too long");
        return false;
    }
    render::CompiledShader customPostShader;
    if (!render::LoadCompiledPixelShader(customPostShaderPath, customPostShader, failure) ||
        customPostShader.bytecode.Count() == 0) {
        failure.Assign("custom post-effect pixel shader fixture could not be loaded");
        return false;
    }

    uint8_t artifact[256];
    const uint32_t size = MakeArtifact(artifact);
    render::CompiledShader parsed;
    if (!render::ParseCompiledPixelShader(artifact, size, "valid.bin", parsed, failure)) return false;
    if (parsed.bytecode.Count() != 72 || memcmp(parsed.bytecode.Data(), "DXBC", 4) != 0 ||
        parsed.derivativeHash != 0) {
        failure.Assign("valid FSL shader did not yield raw default DXIL derivative");
        return false;
    }

    parsed.Reset();
    if (render::ParseCompiledPixelShader(artifact, 10, "short.bin", parsed, failure) ||
        parsed.bytecode.Count() != 0) {
        failure.Assign("truncated FSL header was accepted");
        return false;
    }
    if (render::ParseCompiledPixelShader(artifact, 59, "table.bin", parsed, failure)) {
        failure.Assign("truncated FSL derivative table was accepted");
        return false;
    }

    MakeArtifact(artifact, UINT64_MAX, 48);
    if (render::ParseCompiledPixelShader(artifact, size, "overflow.bin", parsed, failure)) {
        failure.Assign("overflowing FSL derivative offset was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "DXBC");
    if (render::ParseCompiledPixelShader(artifact, size, "no-pixel.bin", parsed, failure)) {
        failure.Assign("DXBC container without a DXIL pixel program was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "BAD!");
    if (render::ParseCompiledPixelShader(artifact, size, "wrong-dxil.bin", parsed, failure)) {
        failure.Assign("non-DXBC derivative was accepted");
        return false;
    }
    MakeArtifact(artifact);
    artifact[60] = 'N';
    if (render::ParseCompiledPixelShader(artifact, size, "wrong-container.bin", parsed, failure)) {
        failure.Assign("derivative with invalid DXBC magic was accepted");
        return false;
    }
    const uint32_t emptyDxilSize = MakeEmptyDxilPartArtifact(artifact);
    if (render::ParseCompiledPixelShader(artifact, emptyDxilSize, "empty-dxil-part.bin", parsed, failure)) {
        failure.Assign("empty DXIL container part was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "DXIL", 0x10060);
    if (render::ParseCompiledShader(artifact, size, "invalid-stage.bin",
                                    static_cast<render::CompiledShaderStage>(99), parsed, failure)) {
        failure.Assign("invalid compiled shader stage enum was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "DXIL", 0x60, 7, 16, 0);
    if (render::ParseCompiledPixelShader(artifact, size, "empty-dxil.bin", parsed, failure)) {
        failure.Assign("empty DXIL program was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "DXIL", 0x60, 8);
    if (render::ParseCompiledPixelShader(artifact, size, "bad-program-size.bin", parsed, failure)) {
        failure.Assign("DXIL program SizeInUint32 mismatch was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "DXIL", 0x60, 7, UINT32_MAX);
    if (render::ParseCompiledPixelShader(artifact, size, "overflow-bitcode-offset.bin", parsed, failure)) {
        failure.Assign("overflowing DXIL bitcode offset was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "DXIL", 0x60, 7, 16, UINT32_MAX);
    if (render::ParseCompiledPixelShader(artifact, size, "overflow-bitcode-size.bin", parsed, failure)) {
        failure.Assign("overflowing DXIL bitcode size was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "DXIL", 0x60, 7, 16, 4, "NOPE");
    if (render::ParseCompiledPixelShader(artifact, size, "wrong-bitcode-magic.bin", parsed, failure)) {
        failure.Assign("non-LLVM bitcode magic was accepted");
        return false;
    }
    MakeArtifact(artifact, 60, 72, "DXIL", 0x10060);
    if (render::ParseCompiledPixelShader(artifact, size, "vertex-shader.bin", parsed, failure)) {
        failure.Assign("vertex-stage DXIL was accepted as a pixel shader");
        return false;
    }
    const uint32_t duplicateSize = MakeDuplicateHashArtifact(artifact);
    if (render::ParseCompiledPixelShader(artifact, duplicateSize, "duplicate-hash.bin", parsed, failure)) {
        failure.Assign("ambiguous duplicate derivative hashes were accepted");
        return false;
    }
    if (render::ParseCompiledPixelShader(artifact, 64u * 1024u * 1024u + 1u,
                                         "oversized.bin", parsed, failure)) {
        failure.Assign("shader artifacts larger than 64 MiB were accepted");
        return false;
    }
    MakeArtifact(artifact);
    Write32(artifact, 60 + 24, UINT32_MAX);
    if (render::ParseCompiledPixelShader(artifact, size, "overflow-dxil.bin", parsed, failure)) {
        failure.Assign("DXBC declared size overflow was accepted");
        return false;
    }

    MakeArtifact(artifact);
    if (!render::ParseCompiledPixelShader(artifact, size, "valid.bin", parsed, failure)) return false;
    const uint8_t* previous = parsed.bytecode.Data();
    const uint32_t previousSize = parsed.bytecode.Count();
#ifdef GKCORE_TESTING
    SetAllocationFailureAfterForTesting(0);
    const bool allocationSucceeded = render::ParseCompiledPixelShader(artifact, size, "oom.bin", parsed, failure);
    ResetAllocationFailureForTesting();
    if (allocationSucceeded || parsed.bytecode.Data() != previous || parsed.bytecode.Count() != previousSize) {
        failure.Assign("allocation failure replaced the previously loaded shader");
        return false;
    }
#endif
    MakeArtifact(artifact, UINT64_MAX, 48);
    if (render::ParseCompiledPixelShader(artifact, size, "preserve.bin", parsed, failure) ||
        parsed.bytecode.Data() != previous || parsed.bytecode.Count() != previousSize) {
        failure.Assign("failed parse replaced the previously loaded shader");
        return false;
    }

    MakeArtifact(artifact);
    const char* path = "gkcore_shader_artifact_fixture.bin";
    FILE* file = fopen(path, "wb");
    if (!file) {
        failure.Assign("could not create custom shader path fixture");
        return false;
    }
    const bool wrote = fwrite(artifact, 1, size, file) == size;
    const bool closed = fclose(file) == 0;
    if (!wrote || !closed) {
        remove(path);
        failure.Assign("could not write custom shader path fixture");
        return false;
    }
    render::CompiledShader loadedFromPath;
    const bool loaded = render::LoadCompiledPixelShader(path, loadedFromPath, failure);
    remove(path);
    if (!loaded || loadedFromPath.bytecode.Count() != 72) {
        failure.Assign("compiled shader loader did not read the caller-supplied path");
        return false;
    }

    char pixelPath[1024];
    char vertexPath[1024];
    const int pixelPathLength = snprintf(pixelPath, sizeof(pixelPath),
                                         "%s/tests/assets/shaders/gkcore_color.frag", GKCORE_TEST_SOURCE_DIR);
    const int vertexPathLength = snprintf(vertexPath, sizeof(vertexPath),
                                          "%s/tests/assets/shaders/gkcore_color.vert", GKCORE_TEST_SOURCE_DIR);
    if (pixelPathLength <= 0 || static_cast<size_t>(pixelPathLength) >= sizeof(pixelPath) ||
        vertexPathLength <= 0 || static_cast<size_t>(vertexPathLength) >= sizeof(vertexPath)) {
        failure.Assign("shader fixture asset path is too long");
        return false;
    }
    render::CompiledShader actualPixel;
    if (!render::LoadCompiledPixelShader(pixelPath, actualPixel, failure) || actualPixel.bytecode.Count() == 0) {
        failure.Assign("pinned DXC pixel shader artifact could not be loaded");
        return false;
    }
    const uint8_t* actualPixelBytes = actualPixel.bytecode.Data();
    if (render::LoadCompiledPixelShader(vertexPath, actualPixel, failure) ||
        actualPixel.bytecode.Data() != actualPixelBytes) {
        failure.Assign("pinned DXC vertex shader was accepted or replaced the existing pixel shader");
        return false;
    }
    render::CompiledShader actualVertex;
    if (!render::LoadCompiledVertexShader(vertexPath, actualVertex, failure) ||
        actualVertex.bytecode.Count() == 0) {
        failure.Assign("pinned DXC vertex shader artifact could not be loaded for the universal shader ABI");
        return false;
    }
    const uint8_t* actualVertexBytes = actualVertex.bytecode.Data();
    if (render::LoadCompiledVertexShader(pixelPath, actualVertex, failure) ||
        actualVertex.bytecode.Data() != actualVertexBytes) {
        failure.Assign("pinned DXC pixel shader was accepted as a vertex shader or replaced existing bytecode");
        return false;
    }
    const char* compiledPixelShaderNames[] = {
        "gkcore_bloom_extract.frag",
        "gkcore_bloom_blur.frag",
        "gkcore_post_composite.frag",
        "gkcore_fxaa.frag",
        "gkcore_user_tint.frag",
        "post_effect_tint.frag"
    };
    for (uint32_t i = 0; i < sizeof(compiledPixelShaderNames) / sizeof(compiledPixelShaderNames[0]); ++i) {
        char postShaderPath[1024];
        const int pathLength = snprintf(postShaderPath, sizeof(postShaderPath),
                                        "%s/tests/assets/shaders/%s",
                                        GKCORE_TEST_SOURCE_DIR, compiledPixelShaderNames[i]);
        if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(postShaderPath)) {
            failure.Assign("compiled pixel shader fixture asset path is too long");
            return false;
        }
        render::CompiledShader postShader;
        if (!render::LoadCompiledPixelShader(postShaderPath, postShader, failure) ||
            postShader.bytecode.Count() == 0) {
            failure.Assign("pinned DXC pixel shader artifact could not be loaded");
            return false;
        }
    }
    return true;
}

} // namespace gk::tests
