#include "Frame.h"
#include "Context.h"
#include "../resources/Resources.h"
#include "../effects/Effects.h"
#include "../text/TextCache.h"

namespace gk {
namespace {
/**
 * Releases draw-packet resource references before frame or runtime reset.
 */
void releaseFrameResources() {
    detail::ClearFrameDraws();
}
}

int SetWindowSize(uint32_t width, uint32_t height) {
    detail::Context& context = detail::GetContext();
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (context.initialized) return detail::SetError("SetWindowSize must be called before Init");
    if (width == 0 || height == 0 || width > 16384 || height > 16384 || pixels > 64ull * 1024 * 1024)
        return detail::SetError("window dimensions exceed the supported framebuffer limit");
    context.width = width;
    context.height = height;
    context.windowConfigured = true;
    detail::ClearError();
    return 0;
}

int Init() {
    detail::Context& context = detail::GetContext();
    if (context.initialized) return 0;
    if (!context.windowConfigured) return detail::SetError("window size has not been configured");
    if (!context.backend) {
#if defined(GKCORE_TESTING)
        return detail::SetError("a test backend must be installed before Init");
#else
        context.backend = detail::CreateNativeBackend();
#endif
    }
    if (!context.backend) return detail::SetError("native renderer is unavailable");

    String error;
    if (!context.backend->Initialize(context.width, context.height, context.colorDepth, error)) {
        context.backend->Shutdown();
        delete context.backend;
        context.backend = nullptr;
        return detail::SetError(error.Empty() ? "renderer initialization failed" : error.CStr());
    }
    detail::ClearFrameDraws();
    context.initialized = true;
    context.frameOpen = false;
    detail::ClearError();
    return 0;
}

void Shutdown() {
    detail::Context& context = detail::GetContext();
    releaseFrameResources();
    detail::ClearTextImageCache();
    if (context.backend) {
        for (uint32_t i = 0; i < context.nativeShaders.Count(); ++i) {
            String ignored;
            context.backend->ReleasePixelShader(context.nativeShaders.At(i).backendHandle, ignored);
        }
        context.backend->Shutdown();
        delete context.backend;
        context.backend = nullptr;
    }
    detail::ClearResources();
    detail::ClearModelTransforms();
    context.shaders.Reset();
    context.nativeShaders.Clear();
    context.initialized = false;
    context.frameOpen = false;
    effects::Reset();
    context.cameraPosition = {0.0f, 0.0f, -5.0f};
    context.cameraTarget = {0.0f, 0.0f, 0.0f};
    detail::ClearError();
}

bool ProcessEvents() {
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) {
        detail::SetError("framework is not initialized");
        return false;
    }
    const int result = context.backend->ProcessMessage();
    if (result == -1) {
        detail::ClearError();
        return false;
    }
    if (result < -1) {
        detail::SetError("window event processing failed");
        return false;
    }
    uint32_t width = 0;
    uint32_t height = 0;
    if (context.backend->GetClientSize(width, height) && width && height &&
        width <= 16384 && height <= 16384 &&
        static_cast<uint64_t>(width) * height <= 64ull * 1024 * 1024) {
        context.width = width;
        context.height = height;
    }
    detail::ClearError();
    return true;
}

int BeginFrame() {
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) return detail::SetError("framework is not initialized");
    if (context.frameOpen) return detail::SetError("the previous frame has not been presented");
    detail::ClearFrameDraws();
    context.frame.width = context.width;
    context.frame.height = context.height;
    context.frame.cameraPosition = context.cameraPosition;
    context.frame.cameraTarget = context.cameraTarget;
    const effects::Settings& settings = effects::Current();
    context.frame.bloomEnabled = settings.bloomEnabled;
    context.frame.bloomIntensity = settings.bloomIntensity;
    context.frame.exposure = settings.exposure;
    context.frame.toneMappingEnabled = settings.toneMappingEnabled;
    context.frameOpen = true;
    detail::ClearError();
    return 0;
}

int Present() {
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) return detail::SetError("framework is not initialized");
    if (!context.frameOpen) return detail::SetError("BeginFrame must be called before Present");
    String error;
    const bool result = context.backend->Present(context.frame, error);
    context.frameOpen = false;
    detail::ClearFrameDraws();
    if (!result) return detail::SetError(error.Empty() ? "frame presentation failed" : error.CStr());
    detail::ClearError();
    return 0;
}

const char* GetLastErrorMessage() {
    const detail::Context& context = detail::GetContext();
    return context.emergencyError ? context.emergencyError : context.error.CStr();
}

namespace detail {
#ifdef GKCORE_TESTING
void SetBackendForTesting(Backend* backend) {
    Context& context = GetContext();
    if (context.backend) {
        context.backend->Shutdown();
        delete context.backend;
    }
    releaseFrameResources();
    ClearTextImageCache();
    ClearResources();
    ClearModelTransforms();
    context.shaders.Reset();
    context.nativeShaders.Clear();
    context.backend = backend;
    context.initialized = false;
    context.frameOpen = false;
    effects::Reset();
    ClearError();
}
#endif
} // namespace detail

} // namespace gk

namespace gk::detail {

int QueueDraw(DrawPacket& packet) {
    Context& context = GetContext();
    if (!context.initialized || !context.backend) return SetError("framework is not initialized");
    if (!context.frameOpen) return SetError("BeginFrame must be called before drawing");

    ShaderSnapshot shader;
    if (!context.shaders.Snapshot(shader))
        return SetError("not enough memory to snapshot shader constants");
    ShaderHandle backendShader;
    if (shader.shaderHandle.IsValid()) {
        backendShader = FindBackendShader(shader.shaderHandle);
        if (!backendShader.IsValid()) return SetError("active shader backend handle is unavailable");
    }

    bool imageRetained = false;
    bool modelRetained = false;
    if (packet.image) {
        imageRetained = Retain(&packet.image->reference);
        if (!imageRetained) return SetError("image resource is no longer available");
    }
    if (packet.model) {
        modelRetained = Retain(&packet.model->reference);
        if (!modelRetained) {
            if (imageRetained) Release(&packet.image->reference);
            return SetError("model resource is no longer available");
        }
    }

    packet.layer = static_cast<uint8_t>(effects::Current().layer);
    packet.cameraPosition = context.cameraPosition;
    packet.cameraTarget = context.cameraTarget;
    packet.shader = backendShader;
    if (shader.constants.Count() > 64) {
        if (imageRetained) Release(&packet.image->reference);
        if (modelRetained) Release(&packet.model->reference);
        return SetError("shader constant count exceeds the packet limit");
    }
    for (uint32_t i = 0; i < shader.constants.Count(); ++i) {
        packet.shaderConstants[packet.shaderConstantCount++] = shader.constants.At(i);
    }
    packet.sequence = context.frame.draws.Count();
    if (!context.frame.draws.Append(packet)) {
        if (imageRetained) Release(&packet.image->reference);
        if (modelRetained) Release(&packet.model->reference);
        return SetError("not enough memory to queue a draw command");
    }
    ClearError();
    return 0;
}

} // namespace gk::detail
