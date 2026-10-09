// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FORGERENDERER_H
#define GKCORE_RENDER_FORGERENDERER_H

#if defined(_WIN32) && defined(DIRECT3D12)

#include "internal/Backend.hpp"
#include "platform/WindowsWindow.h"
#include "render/Geometry.h"
#include "render/ModelGeometry.h"
#include "render/ModelLightingRenderer.h"
#include "render/FModelGeometryCache.h"
#include "render/FModelSkinningRenderer.h"
#include "render/FModelSkinningGeometryCache.h"
#include "render/FModelSkinningDispatch.h"
#include "render/ModelDrawConstants.h"
#include "render/ModelPoseGeometry.h"
#include "render/FModelSparseMapCache.h"
#include "render/CustomShaders.h"
#include "render/PostProcessRenderer.h"
#include "render/PostEffectRenderer.h"
#include "render/TextureCache.h"

#if defined(GKCORE_TEST_FRAME_CAPTURE)
#include "tests/support/FrameCapture.h"
#include "tests/support/FModelSkinningReadback.h"
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
    // 共通頂点配列内の開始位置。
    uint32_t first;
    // この範囲に含める頂点数。
    uint32_t count;
    // scene depth testを使うか。
    bool depthTest;
    // base color画像を使うか。
    bool textured;
    // alpha blendを有効にするか。
    bool alphaBlend;
    // 同一pipeline内での描画順layer。
    uint8_t layer;
    // base color描画で参照する画像resource。
    detail::ImageResource* image;
    // customまたは内蔵shaderのhandle。
    ShaderHandle shader;
    // 対応するcustom drawのindex。
    uint32_t customDrawIndex;
    // custom shaderを使う範囲か。
    bool customShader;
    // 内蔵lighting modelの範囲か。
    bool litModel;
    // 内蔵モデルで使う金属度・粗さの画像。
    detail::ImageResource* metallicRoughnessImage = nullptr;
    // 内蔵モデルで使う法線画像。
    detail::ImageResource* normalImage = nullptr;
    // 基本色画像の繰り返しと補間方法。
    detail::FTextureSampler baseColorSampler{};
    // 金属度・粗さ画像の繰り返しと補間方法。
    detail::FTextureSampler metallicRoughnessSampler{};
    // 法線画像の繰り返しと補間方法。
    detail::FTextureSampler normalSampler{};
    // 内蔵モデルで使う自己発光画像。
    detail::ImageResource* emissiveImage = nullptr;
    // 自己発光画像の繰り返しと補間方法。
    detail::FTextureSampler emissiveSampler{};
    // 環境光を弱める材質画像。
    detail::ImageResource* occlusionImage = nullptr;
    // 遮蔽画像の繰り返しと補間方法。
    detail::FTextureSampler occlusionSampler{};
    // 不変のmodel頂点を再利用するGPU buffer。nullならframe用bufferを使う。
    Buffer* cachedModelBuffer = nullptr;
    // GPU model変換定数のslot。0はCPUで投影した頂点。
    uint32_t modelDrawConstantIndex = 0;
    // 透明triangleの並びをindex bufferで指定するか。
    bool indexedModel = false;
};

/**
 * The ForgeのDirect3D 12描画資源を所有し、frameの描画内容を処理する。
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
     * GPUによる倍精度model skinningを使えるか返す。
     */
    bool SupportsGpuModelSkinning() const;
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
    // GPUが同時処理できるframe数。
    static constexpr uint32_t kFramesInFlight = 2;
    // frameごとに確保する頂点上限。
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
    // The ForgeのGPU renderer。
    Renderer* renderer_ = nullptr;
    // GPU commandを実行するqueue。
    Queue* graphicsQueue_ = nullptr;
    // 画面へ提示するimageを管理するswapchain。
    SwapChain* swapChain_ = nullptr;
    // scene depth testに使うrender target。
    RenderTarget* depthTarget_ = nullptr;
    // 色付き頂点を描くshader。
    Shader* colorShader_ = nullptr;
    // sprite頂点を描くshader。
    Shader* spriteShader_ = nullptr;
    // scene用color pipeline。
    Pipeline* scenePipeline_ = nullptr;
    // depth write用pipeline。
    Pipeline* depthPipeline_ = nullptr;
    // UI用pipeline。
    Pipeline* uiPipeline_ = nullptr;
    // 非透過sprite用pipeline。
    Pipeline* spritePipeline_ = nullptr;
    // alpha blend sprite用pipeline。
    Pipeline* spriteAlphaPipeline_ = nullptr;
    // UI非透過sprite用pipeline。
    Pipeline* spriteUiPipeline_ = nullptr;
    // UI alpha blend sprite用pipeline。
    Pipeline* spriteAlphaUiPipeline_ = nullptr;
    // frameごとの頂点buffer。
    Buffer* vertexBuffers_[kFramesInFlight]{};
    // GPU modelの透明triangle順をframeごとに転送するindex buffer。
    Buffer* modelIndexBuffers_[kFramesInFlight]{};
    // swapchain image取得を同期するsemaphore。
    Semaphore* imageAcquiredSemaphore_ = nullptr;
    // command bufferとsemaphoreの再利用を管理するring。
    GpuCmdRing commandRing_{};
    // 初期化に使ったnative window handle。
    WindowHandle windowHandle_{};
    // 内蔵model textureの登録と寿命を管理するowner。
    TextureCache textureCache_;
    // 内蔵modelの照明pipelineを管理するowner。
    ModelLightingRenderer modelLighting_;
    // 不変の静止model頂点をGPU上で保持する所有者。
    FModelGeometryCache modelGeometryCache_;
    // skinning computeとframe別出力を管理する。
    FModelSkinningRenderer modelSkinning_;
    // skinning入力geometryをGPU_ONLY bufferで再利用する。
    FModelSkinningGeometryCache modelSkinningGeometryCache_;
    // sparse姿勢の元vertex対応mapをGPUで保持する。
    FModelSparseMapCache modelSparseMapCache_;
    // custom shader資源を管理するowner。
    CustomShaders customShaders_;
    // frame内のscene後処理を管理するowner。
    PostProcessRenderer postProcess_;
    // 選択中post effectを管理するowner。
    PostEffectRenderer postEffect_;
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    // GPU画像検査用の読み戻し資源を所有する。
    test_support::FFrameCapture testFrameCapture_;
    // skin検証を明示したcapture runtimeだけ有効にする。
    bool verifyGpuSkinning_ = false;
    // GPU skin出力をframe fence後に読むframe別buffer。
    Buffer* skinningReadbackBuffers_[kFramesInFlight]{};
    // 容量を保持してframe間にreadback bufferを再利用する。
    uint64_t skinningReadbackCapacities_[kFramesInFlight]{};
#endif
    // 画像未指定時に使う白texture。
    detail::ImageResource* whiteImage_ = nullptr;
    // 通常描画へ送る頂点。
    Array<Vertex> vertices_;
    // 内蔵model描画へ送る頂点。
    Array<ModelRenderVertex> modelVertices_;
    // 透明modelのGPU頂点buffer内のindex列。
    Array<uint32_t> modelIndices_;
    // frameで使うGPU model変換。slot0以外の値を保持する。
    Array<FModelDrawConstants> modelDrawConstants_;
    // frame内の独立した姿勢をGPUへ渡す位置・法線・接線。
    Array<FModelPoseVertex> modelPoseVertices_;
    // frame内GPU skinning dispatchへ渡すrecord列。
    Array<FModelSkinningRecord> modelSkinningRecords_;
    // frame内でskinするmodelのrecord・出力範囲。
    Array<FModelSkinningDispatch> modelSkinningDispatches_;
    // dispatch順に対応する不変geometry buffer。
    Array<Buffer*> modelSkinningGeometryBuffers_;
    // 各GPU model変換slotが参照するsparse対応map。
    Array<const Buffer*> modelSparseMaps_;
    // 同じ描画状態を共有する頂点範囲。
    Array<RenderRun> runs_;
    // frame内のcustom shader描画内容。
    Array<CustomShaderDraw> customDraws_;
    // swapchainの幅。
    uint32_t width_ = 0;
    // swapchainの高さ。
    uint32_t height_ = 0;
    // The Forge resource loaderを初期化済みか。
    bool resourceLoaderInitialized_ = false;
    // 内蔵shaderのroot signatureを初期化済みか。
    bool rootSignatureInitialized_ = false;
#if defined(_DEBUG)
    // GPU診断時だけ登録するD3D12 message callback owner。
    void* gpuInfoQueue_ = nullptr;
    // callback登録時に割り当てられる識別値。
    unsigned long gpuInfoCallbackCookie_ = 0;
    // device生成時に診断を有効化したか。
    bool gpuDiagnosticsEnabled_ = false;
#endif
};

}

#endif

#endif
