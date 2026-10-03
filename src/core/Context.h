#pragma once

#include "../internal/Backend.hpp"
#include "../foundation/Array.h"
#include "../foundation/String.h"
#include "../effects/Shaders.h"
#include <gkcore.h>

/**
 * Shared mutable state and internal helpers for the process-wide API.
 */
namespace gk::detail {

/**
 * Per-model values copied into each submitted model draw.
 */
struct ModelTransform {
    ModelHandle handle;
    Vec3 position;
    Vec3 rotation;
    Vec3 scale;
};

/**
 * Maps a stable API shader handle to the backend's current identifier.
 */
struct ShaderNativeRecord {
    ShaderHandle publicHandle;
    ShaderHandle backendHandle;
};

/**
 * Process-wide API state; the public API deliberately has no context object.
 */
struct Context {
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t colorDepth = 32;
    bool windowConfigured = true;
    bool initialized = false;
    bool frameOpen = false;
    Backend* backend = nullptr;
    String error;
    const char* emergencyError = nullptr;
    FramePacket frame;
    Vec3 cameraPosition{0.0f, 0.0f, -5.0f};
    Vec3 cameraTarget{0.0f, 0.0f, 0.0f};
    Array<ModelTransform> modelTransforms;
    ShaderBindings shaders;
    Array<ShaderNativeRecord> nativeShaders;
    uint32_t nextShaderHandle = 1;
};

/**
 * Returns the single process-wide API state.
 */
Context& GetContext();

/**
 * Stores a diagnostic and returns the public failure status.
 */
int SetError(const char* message);

/**
 * Clears the current diagnostic after a successful API call.
 */
void ClearError();

/**
 * Checks all vector components before they reach graphics math.
 */
bool IsFinite(Vec3 value);

/**
 * Returns the transform entry for a live model handle, or null.
 */
ModelTransform* FindModelTransform(ModelHandle handle);

/**
 * Finds the backend shader identifier associated with a public handle.
 */
ShaderHandle FindBackendShader(ShaderHandle handle);

/**
 * Removes all per-model values at shutdown or test backend replacement.
 */
void ClearModelTransforms();

/**
 * Releases intrusive references held by the current draw list.
 */
void ClearFrameDraws();

} // namespace gk::detail
