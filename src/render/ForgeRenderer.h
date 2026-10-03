#pragma once

#if defined(_WIN32) && defined(DIRECT3D12)

#include "../internal/Backend.hpp"
#include "../platform/WindowsWindow.h"
#include "Geometry.h"
#include "PostProcessRenderer.h"
#include "TextureCache.h"

#include <Graphics/Interfaces/IGraphics.h>
#include <Graphics/GraphicsConfig.h>
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>
#include <Utilities/RingBuffer.h>

#ifdef new
#undef new
#endif
#ifdef delete
#undef delete
#endif

/**
 * The Forge Direct3D 12 renderer and GPU lifetime management.
 */
namespace gk::render {

/**
 * A contiguous group of color or image vertices with matching GPU state.
 */
struct RenderRun {
    uint32_t first;
    uint32_t count;
    bool depthTest;
    bool textured;
    bool alphaBlend;
    uint8_t layer;
    detail::ImageResource* image;
};

/**
 * Owns the The Forge Direct3D 12 renderer and consumes frame snapshots.
 */
class ForgeRenderer {
public:
    /**
     * Creates an empty renderer owner.
     */
    ForgeRenderer() = default;
    /**
     * Releases renderer-owned GPU and The Forge resources.
     */
    ~ForgeRenderer();
    /**
     * GPU objects and queues have a single owner and cannot be copied.
     */
    ForgeRenderer(const ForgeRenderer&) = delete;
    ForgeRenderer& operator=(const ForgeRenderer&) = delete;

    /**
     * Creates the renderer, swapchain, depth target, shaders, pipelines, and buffers.
     */
    bool Initialize(HWND window, uint32_t width, uint32_t height, String& error);
    /**
     * Waits for GPU work and releases all renderer resources.
     */
    void Shutdown();
    /**
     * Recreates swapchain and depth resources for a new client size.
     */
    bool Resize(uint32_t width, uint32_t height, String& error);
    /**
     * Draws and presents one captured frame.
     */
    bool Present(const detail::FramePacket& frame, String& error);
    /**
     * Returns an explicit unsupported result for unrecognized shader assets.
     */
    ShaderHandle LoadPixelShader(const char* path, String& error);
    /**
     * Returns an explicit unsupported result for shader handles.
     */
    bool ReleasePixelShader(ShaderHandle shader, String& error);

private:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kVertexCapacity = 1u << 20;

    /**
     * Creates the window presentation target for the current client size.
     */
    bool CreateSwapChain(uint32_t width, uint32_t height, String& error);
    /**
     * Creates shaders, pipelines, buffers, and dependent texture/post resources.
     */
    bool InitializeGraphicsResources(String& error);
    /**
     * Releases dependent graphics objects before the renderer is shut down.
     */
    void DestroyGraphicsResources();
    /**
     * Records one ordered group of compatible draw packets into the active command.
     */
    bool RecordDrawRuns(Cmd* command, uint8_t layer, String& error);

    Renderer* renderer_ = nullptr;
    Queue* graphicsQueue_ = nullptr;
    SwapChain* swapChain_ = nullptr;
    RenderTarget* depthTarget_ = nullptr;
    Shader* colorShader_ = nullptr;
    Shader* spriteShader_ = nullptr;
    Pipeline* scenePipeline_ = nullptr;
    Pipeline* depthPipeline_ = nullptr;
    Pipeline* uiPipeline_ = nullptr;
    Pipeline* spritePipeline_ = nullptr;
    Pipeline* spriteAlphaPipeline_ = nullptr;
    Pipeline* spriteUiPipeline_ = nullptr;
    Pipeline* spriteAlphaUiPipeline_ = nullptr;
    Pipeline* sceneHdrPipeline_ = nullptr;
    Pipeline* depthHdrPipeline_ = nullptr;
    Pipeline* spriteHdrPipeline_ = nullptr;
    Pipeline* spriteAlphaHdrPipeline_ = nullptr;
    Buffer* vertexBuffers_[kFramesInFlight]{};
    Semaphore* imageAcquiredSemaphore_ = nullptr;
    GpuCmdRing commandRing_{};
    WindowHandle windowHandle_{};
    TextureCache textureCache_;
    PostProcessRenderer postProcess_;
    Array<Vertex> vertices_;
    Array<RenderRun> runs_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool resourceLoaderInitialized_ = false;
    bool rootSignatureInitialized_ = false;
};

}

#endif
