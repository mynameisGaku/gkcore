#include "Context.h"
#include <math.h>

namespace gk::detail {

Context& GetContext() {
    static Context context;
    return context;
}

int SetError(const char* message) {
    Context& context = GetContext();
    if (!context.error.Assign(message)) {
        context.emergencyError = "out of memory while recording an error";
    } else {
        context.emergencyError = nullptr;
    }
    return -1;
}

void ClearError() {
    Context& context = GetContext();
    context.error.Clear();
    context.emergencyError = nullptr;
}

bool IsFinite(Vec3 value) {
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

ModelTransform* FindModelTransform(ModelHandle handle) {
    Context& context = GetContext();
    for (uint32_t i = 0; i < context.modelTransforms.Count(); ++i) {
        ModelTransform& transform = context.modelTransforms.At(i);
        if (transform.handle == handle) return &transform;
    }
    return nullptr;
}

ShaderHandle FindBackendShader(ShaderHandle handle) {
    Context& context = GetContext();
    for (uint32_t i = 0; i < context.nativeShaders.Count(); ++i) {
        const ShaderNativeRecord& record = context.nativeShaders.At(i);
        if (record.publicHandle == handle) return record.backendHandle;
    }
    return ShaderHandle();
}

void ClearModelTransforms() {
    GetContext().modelTransforms.Clear();
}

void ClearFrameDraws() {
    Context& context = GetContext();
    for (uint32_t i = 0; i < context.frame.draws.Count(); ++i) {
        DrawPacket& packet = context.frame.draws.At(i);
        if (packet.image) Release(&packet.image->reference);
        if (packet.model) Release(&packet.model->reference);
        packet.image = nullptr;
        packet.model = nullptr;
    }
    context.frame.draws.Clear();
}

} // namespace gk::detail
