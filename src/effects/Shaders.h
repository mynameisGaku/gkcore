#pragma once

#include "../foundation/Array.h"
#include "gkcore.h"

/**
 * Custom shader handles, constants, and per-draw snapshots.
 */
namespace gk {

/**
 * One float4 constant value addressed by shader register slot.
 */
struct ShaderConstant {
    uint32_t registerIndex;
    Float4 value;
};

/**
 * Shader selection and constants copied into a queued draw command.
 */
struct ShaderSnapshot {
    ShaderHandle shaderHandle;
    Array<ShaderConstant> constants;

    /**
     * Resets the handle and removes the copied constants.
     */
    void Reset();
    /**
     * Replaces this snapshot with another snapshot's owned constants.
     */
    void MoveFrom(ShaderSnapshot& source);
};

/**
 * Tracks loaded shader IDs and constant values without owning backend objects.
 */
class ShaderBindings {
public:
    /**
     * Releases all tracked IDs and resets selection to the built-in shader.
     */
    void Reset();
    /**
     * Adds a valid, previously unseen backend-independent shader ID.
     */
    bool RegisterShader(ShaderHandle handle);
    /**
     * Returns whether a shader ID is still loaded.
     */
    bool HasShader(ShaderHandle handle) const;
    /**
     * Removes a shader and its constants; selected removal restores the built-in shader.
     */
    bool DeleteShader(ShaderHandle handle);
    /**
     * Selects a loaded shader or the built-in shader when handle is invalid.
     */
    bool SetActiveShader(ShaderHandle handle);
    /**
     * Sets one finite float4 constant for a loaded shader and slot 0 through 63.
     */
    bool SetConstant(ShaderHandle handle, uint32_t registerIndex, Float4 value);
    /**
     * Returns the selected shader, or an invalid handle for the built-in shader.
     */
    ShaderHandle ActiveHandle() const;
    /**
     * Replaces output with a sorted snapshot of the selected shader and constants.
     */
    bool Snapshot(ShaderSnapshot& output) const;

private:
    struct ShaderRecord { uint32_t handle; };
    struct ConstantRecord { uint32_t handle; uint32_t slot; Float4 value; };
    Array<ShaderRecord> shaders_;
    Array<ConstantRecord> constants_;
    ShaderHandle active_;
};

} // namespace gk
