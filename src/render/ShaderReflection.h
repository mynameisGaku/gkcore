#pragma once

#include "render/ShaderInterface.h"

/**
 * Direct3D 12 shader reflection checks for resource bindings consumed by the renderer.
 */
namespace gk::render
{

/**
 * Validates a DXIL container of at most 64 MiB, stage, signatures, constant-buffer ABI,
 * and supported resource bindings.
 * The output is assigned only when all reflection checks succeed.
 */
bool ValidatePixelShaderReflection(const void* bytecode, uint32_t bytes, ShaderBindingUsage& output, String& error);

} // namespace gk::render
