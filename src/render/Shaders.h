#pragma once

#include "foundation/Array.h"
#include "foundation/String.h"

/**
 * Compiled vertex and pixel artifact loading for the native renderer.
 */
namespace gk::render
{

/**
 * DXIL stage expected in one Forge FSL artifact.
 */
enum class CompiledShaderStage : uint8_t
{
    Vertex,
    Pixel
};

/**
 * Owns raw bytecode selected from a validated Forge FSL artifact.
 */
struct CompiledShader
{
    Array<uint8_t> bytecode;
    uint64_t derivativeHash;

    /**
     * Creates an empty bytecode result.
     */
    CompiledShader();
    /**
     * Releases bytecode and clears the selected derivative hash.
     */
    void Reset();
};

/**
 * Parses an FSL artifact of at most 64 MiB, validates its derivative table and
 * DXIL pixel program, then replaces output with the D3D12-compatible derivative.
 */
bool ParseCompiledPixelShader(const void* bytes, uint32_t size, const char* source, CompiledShader& output, String& error);

/**
 * Parses one D3D12 vertex or pixel artifact while enforcing its DXIL program stage.
 */
bool ParseCompiledShader(const void* bytes, uint32_t size, const char* source, CompiledShaderStage stage, CompiledShader& output, String& error);

/**
 * Reads and parses an FSL shader artifact of at most 64 MiB from the supplied path.
 */
bool LoadCompiledPixelShader(const char* path, CompiledShader& output, String& error);

/**
 * Reads and parses a Forge FSL vertex artifact from the supplied path.
 */
bool LoadCompiledVertexShader(const char* path, CompiledShader& output, String& error);

/**
 * Reads a Forge FSL vertex or pixel artifact from the supplied path.
 */
bool LoadCompiledShader(const char* path, CompiledShaderStage stage, CompiledShader& output, String& error);

} // namespace gk::render
