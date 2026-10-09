#include "render/ShaderReflection.h"
#include "render/Shaders.h"

#include <stdio.h>

#ifndef GKCORE_TEST_SOURCE_DIR
#define GKCORE_TEST_SOURCE_DIR "."
#endif

namespace
{

bool CheckShader(const char* filename, bool expectedValid, bool constants, bool texture, bool sampler, bool preserveOutput)
{
    char path[1024];
    const int pathLength = snprintf(path, sizeof(path), "%s/tests/assets/shaders/%s", GKCORE_TEST_SOURCE_DIR, filename);
    if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(path))
    {
        fprintf(stderr, "shader reflection fixture path is invalid: %s\n", filename);
        return false;
    }
    gk::String error;
    gk::render::CompiledShader shader;
    if (!gk::render::LoadCompiledPixelShader(path, shader, error))
    {
        fprintf(stderr, "could not load checked-in pixel fixture %s: %s\n", filename, error.CStr());
        return false;
    }
    const gk::render::ShaderBindingUsage sentinel = { true, true, true };
    gk::render::ShaderBindingUsage usage = preserveOutput ? sentinel : gk::render::ShaderBindingUsage{ false, false, false };
    const bool valid = gk::render::ValidatePixelShaderReflection(shader.bytecode.Data(), shader.bytecode.Count(), usage, error);
    if (valid != expectedValid || usage.constants != (preserveOutput ? sentinel.constants : constants) || usage.texture != (preserveOutput ? sentinel.texture : texture) || usage.sampler != (preserveOutput ? sentinel.sampler : sampler))
    {
        fprintf(stderr, "unexpected reflection result for %s: %s\n", filename, error.CStr());
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!CheckShader("gkcore_color.frag", true, false, false, false, false))
        return 1;
    if (!CheckShader("gkcore_user_tint.frag", true, true, false, false, false))
        return 1;
    if (!CheckShader("gkcore_user_textured.frag", true, true, true, true, false))
        return 1;
    if (!CheckShader("gkcore_bad_constant_count.frag", false, false, false, false, true))
        return 1;
    if (!CheckShader("gkcore_bad_texture_type.frag", false, false, false, false, true))
        return 1;

    char vertexPath[1024];
    const int vertexLength = snprintf(vertexPath, sizeof(vertexPath), "%s/tests/assets/shaders/gkcore_color.vert", GKCORE_TEST_SOURCE_DIR);
    if (vertexLength <= 0 || static_cast<size_t>(vertexLength) >= sizeof(vertexPath))
    {
        fprintf(stderr, "shader reflection vertex fixture path is invalid\n");
        return 1;
    }
    gk::render::CompiledShader vertex;
    gk::String error;
    if (!gk::render::LoadCompiledVertexShader(vertexPath, vertex, error))
    {
        fprintf(stderr, "could not load checked-in vertex fixture: %s\n", error.CStr());
        return 1;
    }
    gk::render::ShaderBindingUsage usage = { true, true, true };
    if (gk::render::ValidatePixelShaderReflection(vertex.bytecode.Data(), vertex.bytecode.Count(), usage, error) || !usage.constants || !usage.texture || !usage.sampler)
    {
        fprintf(stderr, "vertex fixture was accepted or modified output on rejection\n");
        return 1;
    }

    usage = { true, true, true };
    if (gk::render::ValidatePixelShaderReflection(nullptr, 0, usage, error) || !usage.constants || !usage.texture || !usage.sampler)
    {
        fprintf(stderr, "empty bytecode was accepted or modified output\n");
        return 1;
    }
    const uint8_t marker = 0;
    usage = { true, true, true };
    if (gk::render::ValidatePixelShaderReflection(&marker, 64u * 1024u * 1024u + 1u, usage, error) || !usage.constants || !usage.texture || !usage.sampler)
    {
        fprintf(stderr, "oversized bytecode was accepted or modified output\n");
        return 1;
    }
    return 0;
}
