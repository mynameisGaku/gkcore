#pragma once

#if defined(_WIN32) && defined(DIRECT3D12)

#include "../internal/Backend.hpp"
#include "../platform/WindowsWindow.h"
#include "Geometry.h"
#include "ModelGeometry.h"
#include "ModelLightingRenderer.h"
#include "CustomShaders.h"
#include "PostProcessRenderer.h"
#include "PostEffectRenderer.h"
#include "TextureCache.h"

#if defined(GKCORE_TEST_FRAME_CAPTURE)
#include "../../tests/support/FrameCapture.h"
#endif

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
 * The ForgeのDirect3D 12描画とGPU資源の寿命を管理する。
 */
namespace gk::render
{

/**
 * 描画状態を共有する色付き頂点または画像頂点の連続範囲を表す。
 */
struct RenderRun
{
    uint32_t first;
    uint32_t count;
    bool depthTest;
    bool textured;
    bool alphaBlend;
    uint8_t layer;
    detail::ImageResource* image;
    ShaderHandle shader;
    uint32_t customDrawIndex;
    bool customShader;
    bool litModel;
};

/**
 * The ForgeのDirect3D 12描画資源を所有し、フレームの描画内容を処理する。
 */
class ForgeRenderer
{
  public:
    /**
     * 未初期化の描画資源所有者を作る。
     */
    ForgeRenderer() = default;
    /**
     * 描画資源所有者が保持するGPU資源とThe Forgeの資源を解放する。
     */
    ~ForgeRenderer();
    /**
     * GPU資源とqueueは単独所有のため、複製を禁止する。
     */
    ForgeRenderer(const ForgeRenderer&) = delete;
    ForgeRenderer& operator=(const ForgeRenderer&) = delete;

    /**
     * renderer、swapchain、depth target、shader、pipeline、bufferを作成する。
     */
    bool Initialize(HWND window, uint32_t width, uint32_t height, String& error);
    /**
     * GPU処理の完了を待ち、描画資源をすべて解放する。
     */
    void Shutdown();
    /**
     * 新しいclient領域の寸法に合わせてswapchainとdepth資源を作り直す。
     */
    bool Resize(uint32_t width, uint32_t height, String& error);
    /**
     * 受け取った1フレームを描画して画面へ提示する。
     */
    bool Present(const detail::FramePacket& frame, String& error);
    /**
     * 検証済みのpixel shaderを読み込み、描画先ごとのpipelineを作成する。
     */
    ShaderHandle LoadPixelShader(const char* path, String& error);
    /**
     * GPU処理の完了を待ち、shaderに対応するpipelineを解放する。
     */
    bool ReleasePixelShader(ShaderHandle shader, String& error);

  private:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kVertexCapacity = 1u << 20;

    /**
     * 現在のclient領域の寸法に合わせて画面提示先を作成する。
     */
    bool CreateSwapChain(uint32_t width, uint32_t height, String& error);
    /**
     * shader、pipeline、bufferと、texture・後処理に必要な資源を作成する。
     */
    bool InitializeGraphicsResources(String& error);
    /**
     * renderer終了前に、依存する描画資源を解放する。
     */
    void DestroyGraphicsResources();
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
    Buffer* vertexBuffers_[kFramesInFlight]{};
    Semaphore* imageAcquiredSemaphore_ = nullptr;
    GpuCmdRing commandRing_{};
    WindowHandle windowHandle_{};
    TextureCache textureCache_;
    ModelLightingRenderer modelLighting_;
    CustomShaders customShaders_;
    PostProcessRenderer postProcess_;
    PostEffectRenderer postEffect_;
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    // GPU画像検査用の読み戻し資源を所有する。
    test_support::FFrameCapture testFrameCapture_;
#endif
    detail::ImageResource* whiteImage_ = nullptr;
    Array<Vertex> vertices_;
    Array<ModelRenderVertex> modelVertices_;
    Array<RenderRun> runs_;
    Array<CustomShaderDraw> customDraws_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool resourceLoaderInitialized_ = false;
    bool rootSignatureInitialized_ = false;
};

}

#endif
