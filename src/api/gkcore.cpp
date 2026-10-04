#include <gkcore.h>

#include "../core/Context.h"
#include "../effects/Effects.h"
#include "../resources/Resources.h"

namespace gk {
namespace {
int ResourceFailure(const String& error, const char* fallback) {
    return detail::SetError(error.Empty() ? fallback : error.CStr());
}
}

uint32_t ColorRGB(int red, int green, int blue) {
    const auto clamp = [](int value) -> uint32_t {
        if (value < 0) return 0;
        if (value > 255) return 255;
        return static_cast<uint32_t>(value);
    };
    detail::ClearError();
    return (clamp(red) << 16) | (clamp(green) << 8) | clamp(blue);
}

ImageHandle LoadImage(const char* utf8Path) {
    String error;
    const ImageHandle handle = detail::LoadImage(utf8Path, error);
    if (!handle.IsValid()) {
        ResourceFailure(error, "image load failed");
        return {};
    }
    detail::ClearError();
    return handle;
}

int DeleteImage(ImageHandle image) {
    String error;
    if (!detail::DeleteImage(image, error)) return ResourceFailure(error, "invalid image handle");
    detail::ClearError();
    return 0;
}

ModelHandle LoadModel(const char* utf8Path) {
    String error;
    const ModelHandle handle = detail::LoadModel(utf8Path, error);
    if (!handle.IsValid()) {
        ResourceFailure(error, "model load failed");
        return {};
    }
    detail::ModelTransform initial{};
    initial.handle = handle;
    initial.scale = {1.0f, 1.0f, 1.0f};
    if (!detail::GetContext().modelTransforms.Append(initial)) {
        detail::DeleteModel(handle, error);
        detail::SetError("not enough memory to store model transform state");
        return {};
    }
    detail::ClearError();
    return handle;
}

int DeleteModel(ModelHandle model) {
    String error;
    if (!detail::DeleteModel(model, error)) return ResourceFailure(error, "invalid model handle");
    detail::Context& context = detail::GetContext();
    for (uint32_t i = 0; i < context.modelTransforms.Count(); ++i) {
        if (context.modelTransforms.At(i).handle == model) {
            context.modelTransforms.RemoveAt(i);
            break;
        }
    }
    detail::ClearError();
    return 0;
}

int SetBloomEnabled(bool enabled) {
    effects::SetBloomEnabled(enabled);
    detail::ClearError();
    return 0;
}

int SetBloomIntensity(float intensity) {
    if (!effects::SetBloomIntensity(intensity)) return detail::SetError("bloom intensity must be finite and in [0, 4]");
    detail::ClearError();
    return 0;
}

int SetExposure(float exposure) {
    if (!effects::SetExposure(exposure)) return detail::SetError("exposure must be finite and in (0, 16]");
    detail::ClearError();
    return 0;
}

int SetSaturation(float factor) {
    if (!effects::SetSaturation(factor)) return detail::SetError("saturation must be finite and in [0, 2]");
    detail::ClearError();
    return 0;
}

int SetContrast(float factor) {
    if (!effects::SetContrast(factor)) return detail::SetError("contrast must be finite and in [0, 2]");
    detail::ClearError();
    return 0;
}

int SetFxaaEnabled(bool enabled) {
    effects::SetFxaaEnabled(enabled);
    detail::ClearError();
    return 0;
}

int SetToneMappingEnabled(bool enabled) {
    effects::SetToneMappingEnabled(enabled);
    detail::ClearError();
    return 0;
}

int SetDrawLayer(DrawLayer layer) {
    if (!effects::SetLayer(layer)) return detail::SetError("invalid draw layer");
    detail::ClearError();
    return 0;
}

ShaderHandle LoadPixelShader(const char* compiledPath) {
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) {
        detail::SetError("framework is not initialized");
        return {};
    }
    if (!compiledPath || !*compiledPath) {
        detail::SetError("compiled shader path is empty");
        return {};
    }
    if (context.nextShaderHandle == 0) {
        detail::SetError("pixel shader handle space exhausted");
        return {};
    }
    String error;
    const ShaderHandle native = context.backend->LoadPixelShader(compiledPath, error);
    if (!native.IsValid()) {
        ResourceFailure(error, "pixel shader load failed");
        return {};
    }
    const ShaderHandle publicHandle(context.nextShaderHandle);
    detail::ShaderNativeRecord record{publicHandle, native};
    if (!context.nativeShaders.Append(record)) {
        String ignored;
        context.backend->ReleasePixelShader(native, ignored);
        detail::SetError("not enough memory to record the pixel shader");
        return {};
    }
    if (!context.shaders.RegisterShader(publicHandle)) {
        context.nativeShaders.RemoveAt(context.nativeShaders.Count() - 1);
        String ignored;
        context.backend->ReleasePixelShader(native, ignored);
        detail::SetError("not enough memory to record the pixel shader");
        return {};
    }
    ++context.nextShaderHandle;
    detail::ClearError();
    return publicHandle;
}

int SetPixelShader(ShaderHandle shader) {
    detail::Context& context = detail::GetContext();
    if (!context.shaders.SetActiveShader(shader)) return detail::SetError("invalid pixel shader handle");
    detail::ClearError();
    return 0;
}

int DeleteShader(ShaderHandle shader) {
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) return detail::SetError("framework is not initialized");
    if (!context.shaders.HasShader(shader)) return detail::SetError("invalid pixel shader handle");
    const ShaderHandle native = detail::FindBackendShader(shader);
    for (uint32_t i = 0; i < context.frame.draws.Count(); ++i) {
        if (context.frame.draws.At(i).shader == native && native.IsValid())
            return detail::SetError("cannot delete a shader used by the open frame");
    }
    String error;
    if (!context.backend->ReleasePixelShader(native, error))
        return ResourceFailure(error, "pixel shader deletion failed");
    context.shaders.DeleteShader(shader);
    for (uint32_t i = 0; i < context.nativeShaders.Count(); ++i) {
        if (context.nativeShaders.At(i).publicHandle == shader) {
            context.nativeShaders.RemoveAt(i);
            break;
        }
    }
    detail::ClearError();
    return 0;
}

int SetShaderFloat4(ShaderHandle shader, uint32_t slot, Float4 value) {
    detail::Context& context = detail::GetContext();
    if (!context.shaders.SetConstant(shader, slot, value))
        return detail::SetError("invalid shader handle, slot, or non-finite constant");
    detail::ClearError();
    return 0;
}

} // namespace gk
