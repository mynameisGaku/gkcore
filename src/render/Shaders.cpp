#include "Shaders.h"

#include "../foundation/Memory.h"
#include "../resources/ResourceIO.h"

#include <limits.h>
#include <string.h>

namespace gk::render {
namespace {
const uint32_t kFslHeaderSize = 36;
const uint32_t kFslDerivativeSize = 24;
const uint32_t kDxbcHeaderSize = 32;
const uint32_t kMaximumEntries = 256;
const uint32_t kMaximumShaderArtifactBytes = 64u * 1024u * 1024u;

/**
 * Reads a little-endian field after the container validator has checked its range.
 */
uint32_t Read32(const uint8_t* bytes, uint32_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

/**
 * Combines the two little-endian words of a validated artifact field.
 */
uint64_t Read64(const uint8_t* bytes, uint32_t offset) {
    return static_cast<uint64_t>(Read32(bytes, offset)) |
           (static_cast<uint64_t>(Read32(bytes, offset + 4)) << 32);
}

/**
 * Records the artifact path and rejection reason without publishing partial bytecode.
 */
bool Fail(String& error, const char* source, const char* reason) {
    error.Clear();
    if (source && source[0]) {
        if (!error.Append(source) || !error.Append(": ")) return false;
    }
    if (!error.Append(reason)) return false;
    return false;
}

/**
 * Checks container bounds, part overlap, program stage, and the embedded bitcode range.
 */
bool ValidateDxil(const uint8_t* bytes, uint32_t size, CompiledShaderStage expectedStage,
                  String& error) {
    if (size < kDxbcHeaderSize + 4)
        return Fail(error, nullptr, "truncated DXBC container header");
    if (memcmp(bytes, "DXBC", 4) != 0)
        return Fail(error, nullptr, "expected DXIL/DXBC container magic 'DXBC'");
    if (Read32(bytes, 20) != 1)
        return Fail(error, nullptr, "unsupported DXBC container version");
    if (Read32(bytes, 24) != size)
        return Fail(error, nullptr, "DXBC declared size does not match derivative size");

    const uint32_t partCount = Read32(bytes, 28);
    if (partCount == 0 || partCount > kMaximumEntries || partCount > (size - kDxbcHeaderSize) / 4)
        return Fail(error, nullptr, "invalid DXBC part count or truncated offset table");
    const uint32_t offsetsEnd = kDxbcHeaderSize + partCount * 4;
    uint32_t dxilProgramCount = 0;
    for (uint32_t i = 0; i < partCount; ++i) {
        const uint32_t offset = Read32(bytes, kDxbcHeaderSize + i * 4);
        if (offset < offsetsEnd || offset > size || size - offset < 8)
            return Fail(error, nullptr, "DXBC part offset is outside its container");
        const uint32_t payloadSize = Read32(bytes, offset + 4);
        if (payloadSize > size - offset - 8)
            return Fail(error, nullptr, "DXBC part payload exceeds its container");
        const uint32_t end = offset + 8 + payloadSize;
        for (uint32_t j = 0; j < i; ++j) {
            const uint32_t otherOffset = Read32(bytes, kDxbcHeaderSize + j * 4);
            const uint32_t otherSize = Read32(bytes, otherOffset + 4);
            const uint32_t otherEnd = otherOffset + 8 + otherSize;
            if (offset < otherEnd && otherOffset < end)
                return Fail(error, nullptr, "DXBC container parts overlap");
        }
        if (memcmp(bytes + offset, "DXIL", 4) == 0) {
            if (++dxilProgramCount != 1)
                return Fail(error, nullptr, "DXBC container contains multiple DXIL programs");
            if (payloadSize < 28 || payloadSize % 4 != 0)
                return Fail(error, nullptr, "DXIL program header or bitcode is truncated");
            const uint8_t* program = bytes + offset + 8;
            const uint32_t programVersion = Read32(program, 0);
            const uint32_t actualStage = programVersion >> 16;
            const uint32_t wantedStage = expectedStage == CompiledShaderStage::Pixel ? 0u : 1u;
            if (actualStage != wantedStage)
                return Fail(error, nullptr, expectedStage == CompiledShaderStage::Pixel
                    ? "DXIL program is not a pixel shader" : "DXIL program is not a vertex shader");
            if (Read32(program + 4, 0) != payloadSize / 4)
                return Fail(error, nullptr, "DXIL SizeInUint32 does not match program size");

            const uint8_t* bitcodeHeader = program + 8;
            if (memcmp(bitcodeHeader, "DXIL", 4) != 0)
                return Fail(error, nullptr, "invalid DXIL bitcode header magic");
            const uint32_t bitcodeOffset = Read32(bitcodeHeader, 8);
            const uint32_t bitcodeSize = Read32(bitcodeHeader, 12);
            if (bitcodeOffset < 16 || bitcodeOffset > payloadSize - 8 ||
                bitcodeSize < 4 || bitcodeSize > payloadSize - 8 - bitcodeOffset)
                return Fail(error, nullptr, "DXIL bitcode range is outside the program");
            const uint8_t* bitcode = program + 8 + bitcodeOffset;
            if (memcmp(bitcode, "BC\xc0\xde", 4) != 0)
                return Fail(error, nullptr, "DXIL bitcode is missing LLVM bitcode magic");
        }
    }
    if (dxilProgramCount == 0)
        return Fail(error, nullptr, "DXBC container has no DXIL program part");
    return true;
}
}

CompiledShader::CompiledShader() : derivativeHash(0) {}

void CompiledShader::Reset() {
    bytecode.Reset();
    derivativeHash = 0;
}

bool ParseCompiledPixelShader(const void* input, uint32_t size, const char* source,
                              CompiledShader& output, String& error) {
    return ParseCompiledShader(input, size, source, CompiledShaderStage::Pixel, output, error);
}

bool ParseCompiledShader(const void* input, uint32_t size, const char* source,
                         CompiledShaderStage stage, CompiledShader& output, String& error) {
    error.Clear();
    if (stage != CompiledShaderStage::Pixel && stage != CompiledShaderStage::Vertex)
        return Fail(error, source, "compiled shader stage is invalid");
    if (!input) return Fail(error, source, "shader artifact data is null");
    if (size > kMaximumShaderArtifactBytes)
        return Fail(error, source, "shader artifact exceeds the 64 MiB size limit");
    if (size < kFslHeaderSize) return Fail(error, source, "truncated @FSL header");
    const uint8_t* bytes = static_cast<const uint8_t*>(input);
    if (memcmp(bytes, "@FSL", 4) != 0)
        return Fail(error, source, "expected The Forge FSL artifact magic '@FSL'");

    const uint32_t derivativeCount = Read32(bytes, 4);
    if (derivativeCount == 0 || derivativeCount > kMaximumEntries)
        return Fail(error, source, "invalid FSL derivative count");
    const uint32_t tableBytes = derivativeCount * kFslDerivativeSize;
    if (tableBytes > UINT32_MAX - kFslHeaderSize || size < kFslHeaderSize + tableBytes)
        return Fail(error, source, "truncated FSL derivative table");
    const uint32_t tableEnd = kFslHeaderSize + tableBytes;

    uint32_t selected = UINT32_MAX;
    for (uint32_t i = 0; i < derivativeCount; ++i) {
        const uint32_t descriptor = kFslHeaderSize + i * kFslDerivativeSize;
        const uint64_t hash = Read64(bytes, descriptor);
        const uint64_t offset = Read64(bytes, descriptor + 8);
        const uint64_t bytecodeSize = Read64(bytes, descriptor + 16);
        if (bytecodeSize == 0 || offset < tableEnd || offset > size || bytecodeSize > size - offset ||
            bytecodeSize > UINT32_MAX)
            return Fail(error, source, "FSL derivative range is outside the artifact");

        for (uint32_t j = 0; j < i; ++j) {
            const uint32_t other = kFslHeaderSize + j * kFslDerivativeSize;
            if (hash == Read64(bytes, other))
                return Fail(error, source, "FSL artifact contains duplicate derivative hashes");
            const uint64_t otherOffset = Read64(bytes, other + 8);
            const uint64_t otherSize = Read64(bytes, other + 16);
            if (offset < otherOffset + otherSize && otherOffset < offset + bytecodeSize)
                return Fail(error, source, "FSL derivative bytecode ranges overlap");
        }
        if (!ValidateDxil(bytes + static_cast<uint32_t>(offset), static_cast<uint32_t>(bytecodeSize),
                          stage, error)) {
            String contextual;
            if ((!source || contextual.Assign(source)) && contextual.Append(": derivative ") &&
                contextual.AppendUnsigned(i) && contextual.Append(": ") && contextual.Append(error.CStr()))
                error.MoveFrom(contextual);
            return false;
        }
        if (hash == 0 && selected == UINT32_MAX) selected = i;
    }
    if (selected == UINT32_MAX && derivativeCount == 1) selected = 0;
    if (selected == UINT32_MAX)
        return Fail(error, source, "FSL artifact has no D3D12-compatible derivative");

    const uint32_t descriptor = kFslHeaderSize + selected * kFslDerivativeSize;
    const uint32_t offset = static_cast<uint32_t>(Read64(bytes, descriptor + 8));
    const uint32_t bytecodeSize = static_cast<uint32_t>(Read64(bytes, descriptor + 16));
    CompiledShader replacement;
    if (!replacement.bytecode.Reserve(bytecodeSize))
        return Fail(error, source, "not enough memory for shader bytecode");
    const uint8_t* bytecode = bytes + offset;
    if (!replacement.bytecode.AppendRange(bytecode, bytecodeSize))
        return Fail(error, source, "not enough memory for shader bytecode");
    replacement.derivativeHash = Read64(bytes, descriptor);
    output.Reset();
    output.bytecode.MoveFrom(replacement.bytecode);
    output.derivativeHash = replacement.derivativeHash;
    return true;
}

bool LoadCompiledPixelShader(const char* path, CompiledShader& output, String& error) {
    return LoadCompiledShader(path, CompiledShaderStage::Pixel, output, error);
}

bool LoadCompiledVertexShader(const char* path, CompiledShader& output, String& error) {
    return LoadCompiledShader(path, CompiledShaderStage::Vertex, output, error);
}

bool LoadCompiledShader(const char* path, CompiledShaderStage stage,
                        CompiledShader& output, String& error) {
    error.Clear();
    if (!path || !path[0]) return Fail(error, path, "shader path is empty");
    uint8_t* bytes = nullptr;
    uint32_t size = 0;
    if (!detail::ReadResourceFile(path, kMaximumShaderArtifactBytes, bytes, size, error)) return false;
    const bool result = ParseCompiledShader(bytes, size, path, stage, output, error);
    Deallocate(bytes);
    return result;
}

} // namespace gk::render
