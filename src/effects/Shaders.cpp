#include "Shaders.h"

#include <float.h>

namespace gk {
namespace {
bool IsFinite(float value) {
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}
}

void ShaderSnapshot::Reset() {
    shaderHandle = ShaderHandle();
    constants.Clear();
}

void ShaderSnapshot::MoveFrom(ShaderSnapshot& source) {
    if (this == &source) return;
    constants.MoveFrom(source.constants);
    shaderHandle = source.shaderHandle;
    source.shaderHandle = ShaderHandle();
}

void ShaderBindings::Reset() {
    shaders_.Clear();
    constants_.Clear();
    active_ = ShaderHandle();
}

bool ShaderBindings::RegisterShader(ShaderHandle handle) {
    if (!handle.IsValid() || HasShader(handle)) return false;
    const ShaderRecord record = {handle.value};
    return shaders_.Append(record);
}

bool ShaderBindings::HasShader(ShaderHandle handle) const {
    if (!handle.IsValid()) return false;
    for (uint32_t i = 0; i < shaders_.Count(); ++i)
        if (shaders_.At(i).handle == handle.value) return true;
    return false;
}

bool ShaderBindings::DeleteShader(ShaderHandle handle) {
    if (!HasShader(handle)) return false;
    for (uint32_t i = 0; i < shaders_.Count(); ++i) {
        if (shaders_.At(i).handle == handle.value) {
            shaders_.RemoveAt(i);
            break;
        }
    }
    for (uint32_t i = 0; i < constants_.Count();) {
        if (constants_.At(i).handle == handle.value) constants_.RemoveAt(i);
        else ++i;
    }
    if (active_ == handle) active_ = ShaderHandle();
    return true;
}

bool ShaderBindings::SetActiveShader(ShaderHandle handle) {
    if (!handle.IsValid()) {
        active_ = ShaderHandle();
        return true;
    }
    if (!HasShader(handle)) return false;
    active_ = handle;
    return true;
}

bool ShaderBindings::SetConstant(ShaderHandle handle, uint32_t registerIndex, Float4 value) {
    if (!HasShader(handle) || registerIndex >= 64 || !IsFinite(value.x) || !IsFinite(value.y) ||
        !IsFinite(value.z) || !IsFinite(value.w)) return false;
    for (uint32_t i = 0; i < constants_.Count(); ++i) {
        ConstantRecord& record = constants_.At(i);
        if (record.handle == handle.value && record.slot == registerIndex) {
            record.value = value;
            return true;
        }
    }
    const ConstantRecord record = {handle.value, registerIndex, value};
    return constants_.Append(record);
}

ShaderHandle ShaderBindings::ActiveHandle() const { return active_; }

bool ShaderBindings::Snapshot(ShaderSnapshot& output) const {
    ShaderSnapshot replacement;
    replacement.shaderHandle = active_;
    if (active_.IsValid()) {
        for (uint32_t slot = 0; slot < 64; ++slot) {
            for (uint32_t i = 0; i < constants_.Count(); ++i) {
                const ConstantRecord& record = constants_.At(i);
                if (record.handle == active_.value && record.slot == slot) {
                    const ShaderConstant copy = {slot, record.value};
                    if (!replacement.constants.Append(copy)) return false;
                    break;
                }
            }
        }
    }
    output.MoveFrom(replacement);
    return true;
}

} // namespace gk
