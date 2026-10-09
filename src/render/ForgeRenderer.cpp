#if defined(_WIN32) && defined(DIRECT3D12) && defined(_DEBUG)
#include <Graphics/ThirdParty/OpenSource/Direct3d12Agility/include/d3d12.h>
#endif
#include "render/ForgeRenderer.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelDeferredPose.h"
#include "model/animation/ModelSnapshot.h"
#include "resources/TextureSampler.h"
#include "render/ModelTransparency.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include "image/Image.h"
#include "resources/Resources.h"
#if defined(GKCORE_TEST_FRAME_CAPTURE)
#include "foundation/Memory.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(_DEBUG)
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <stdio.h>
#include <stdlib.h>
#endif
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
#include <stdio.h>
#include <stdlib.h>
#endif
#if defined(GKCORE_RENDER_PERFORMANCE_METRICS) && !defined(GKCORE_TEST_FRAME_CAPTURE)
#include <windows.h>
#endif

namespace gk::render
{
namespace
{

/**
 * scene透明描画の参照part範囲を保持する。
 */
struct FTransparentPartRange
{
    // 全draw共通part配列内の開始位置。
    uint32_t first;
    // このdrawに属するpart数。
    uint32_t count;
};

#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
/**
 * 自動capture時だけ描画準備のCPU時間を集計する。
 */
struct FRenderProfileState
{
    // QPCの1秒あたりtick数。
    int64_t frequency = 0;
    // 正常に提示したframe数。
    uint64_t frames = 0;
    // 各phaseで消費したQPC tick。
    uint64_t ticks[6]{};
    // skin geometry cacheの再利用と新規pack件数。
    uint64_t skinGeometryCacheHits = 0;
    uint64_t skinGeometryCacheMisses = 0;
    // GPU skin dispatch数と、CPU経路へ戻った数。
    uint64_t gpuSkinningDispatches = 0;
    uint64_t gpuSkinningFallbacks = 0;
    // 環境変数で計測が有効か示す。
    bool enabled = false;
    // Shutdown時の二重出力を防ぐ。
    bool reported = false;
};

FRenderProfileState gRenderProfile{};

/**
 * QPC値を取得し、失敗時は0を返す。
 */
int64_t ReadRenderProfileCounter()
{
    LARGE_INTEGER counter{};
    return QueryPerformanceCounter(&counter) ? counter.QuadPart : 0;
}

/**
 * 開始・終了counterから負値を含まないtick数を得る。
 */
uint64_t MeasureRenderProfileTicks(int64_t start, int64_t end)
{
    return end > start ? static_cast<uint64_t>(end - start) : 0;
}

/**
 * 正常に提示したframeの6phase時間を平均集計へ加える。
 */
void AccumulateRenderProfileFrame(const uint64_t ticks[6])
{
    if (!gRenderProfile.enabled)
        return;
    for (uint32_t i = 0; i < 6; ++i)
        gRenderProfile.ticks[i] += ticks[i];
    ++gRenderProfile.frames;
}

/**
 * 環境変数から計測状態を初期化する。
 */
void InitializeRenderProfile()
{
    gRenderProfile = FRenderProfileState{};
    const char* enabled = getenv("GKCORE_RENDER_PROFILE");
    LARGE_INTEGER frequency{};
    gRenderProfile.enabled = enabled && enabled[0] == '1' && enabled[1] == '\0' && QueryPerformanceFrequency(&frequency);
    gRenderProfile.frequency = gRenderProfile.enabled ? frequency.QuadPart : 0;
}

/**
 * Shutdown時に平均phase時間をJSON一行で出力する。
 */
void ReportRenderProfile()
{
    if (!gRenderProfile.enabled || gRenderProfile.reported)
        return;
    gRenderProfile.reported = true;
    if (gRenderProfile.frames == 0 || gRenderProfile.frequency <= 0)
    {
        fprintf(stderr, "{\"type\":\"gkcore_render_profile\",\"frames\":0}\n");
        return;
    }
    const double millisecondsPerTick = 1000.0 / static_cast<double>(gRenderProfile.frequency);
    fprintf(stderr, "{\"type\":\"gkcore_render_profile\",\"frames\":%llu,\"transparentPlanMeanMs\":%.6f,\"modelPrepMeanMs\":%.6f,\"fenceWaitMeanMs\":%.6f,\"bufferPrepareUploadMeanMs\":%.6f,\"cmdRecordMeanMs\":%.6f,\"submitPresentMeanMs\":%.6f,\"skinGeometryCacheHits\":%llu,\"skinGeometryCacheMisses\":%llu,\"gpuSkinningDispatches\":%llu,\"gpuSkinningFallbacks\":%llu}\n", static_cast<unsigned long long>(gRenderProfile.frames), gRenderProfile.ticks[0] * millisecondsPerTick / gRenderProfile.frames, gRenderProfile.ticks[1] * millisecondsPerTick / gRenderProfile.frames, gRenderProfile.ticks[2] * millisecondsPerTick / gRenderProfile.frames, gRenderProfile.ticks[3] * millisecondsPerTick / gRenderProfile.frames, gRenderProfile.ticks[4] * millisecondsPerTick / gRenderProfile.frames, gRenderProfile.ticks[5] * millisecondsPerTick / gRenderProfile.frames, static_cast<unsigned long long>(gRenderProfile.skinGeometryCacheHits), static_cast<unsigned long long>(gRenderProfile.skinGeometryCacheMisses), static_cast<unsigned long long>(gRenderProfile.gpuSkinningDispatches), static_cast<unsigned long long>(gRenderProfile.gpuSkinningFallbacks));
}
#endif

bool SetError(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

#if defined(_DEBUG)
bool EnableGpuDiagnostics()
{
    const char* enabled = getenv("GKCORE_GPU_DIAGNOSTICS");
    if (!enabled || enabled[0] != '1' || enabled[1] != '\0')
        return false;
    ID3D12Debug1* debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer();
        debug->SetEnableGPUBasedValidation(TRUE);
        debug->Release();
    }
    ID3D12DeviceRemovedExtendedDataSettings* dred = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
    {
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->Release();
    }
    return true;
}

void __stdcall ReportD3D12Message(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context)
{
    (void)context;
    fprintf(stderr, "D3D12 validation category=%u severity=%u id=%u: %s\n", static_cast<uint32_t>(category), static_cast<uint32_t>(severity), static_cast<uint32_t>(id), description ? description : "(no description)");
    fflush(stderr);
}
#endif

bool AppendSkinningRecord(Array<FModelSkinningRecord>& records, uint32_t x, uint32_t y = 0, uint32_t z = 0, uint32_t w = 0)
{
    const FModelSkinningRecord record = { { x, y, z, w } };
    return records.Append(record);
}

bool AppendDoublePair(Array<FModelSkinningRecord>& records, double first, double second)
{
    uint64_t firstBits = 0;
    uint64_t secondBits = 0;
    memcpy(&firstBits, &first, sizeof(firstBits));
    memcpy(&secondBits, &second, sizeof(secondBits));
    return AppendSkinningRecord(records, static_cast<uint32_t>(firstBits), static_cast<uint32_t>(firstBits >> 32), static_cast<uint32_t>(secondBits), static_cast<uint32_t>(secondBits >> 32));
}

/**
 * CPU参照姿勢の位置と法線をGPU pose streamへ追加する。
 */
bool AppendSparsePoseVertices(const model::animation::FModelSparsePoseGeometry& geometry, Array<FModelPoseVertex>& output, String& error)
{
    const uint64_t required = static_cast<uint64_t>(geometry.positions.Count()) + geometry.normals.Count();
    if (required > UINT32_MAX - output.Count() || !output.Reserve(output.Count() + static_cast<uint32_t>(required)))
        return SetError(error, "The sparse pose pool allocation failed");
    for (uint32_t index = 0; index < geometry.positions.Count(); ++index)
    {
        FModelPoseVertex vertex{};
        memcpy(vertex.position, geometry.positions.At(index).value, sizeof(vertex.position));
        if (!output.Append(vertex))
            return SetError(error, "The sparse model position allocation failed");
    }
    for (uint32_t index = 0; index < geometry.normals.Count(); ++index)
    {
        FModelPoseVertex vertex{};
        memcpy(vertex.normal, geometry.normals.At(index).value, sizeof(vertex.normal));
        if (!output.Append(vertex))
            return SetError(error, "The sparse model normal allocation failed");
    }
    return true;
}

/**
 * GPUが全属性を書き換えるrangeの位置・法線placeholderをまとめて追加する。
 */
bool AppendSparsePosePlaceholders(uint32_t count, Array<FModelPoseVertex>& output, String& error)
{
    static const FModelPoseVertex zeros[256]{};
    if (count > UINT32_MAX - output.Count() || !output.Reserve(output.Count() + count))
        return SetError(error, "The sparse pose placeholder allocation failed");
    while (count)
    {
        const uint32_t chunkCount = count < 256 ? count : 256;
        if (!output.AppendRange(zeros, chunkCount))
            return SetError(error, "The sparse pose placeholder allocation failed");
        count -= chunkCount;
    }
    return true;
}

bool PackSkinningMatrices(const Array<model::animation::FModelGpuSkinningGeometry::FMatrix>& matrices, uint32_t matrixOffset, FModelSkinningDispatch& dispatch, uint32_t positionOffset, uint32_t normalOffset, Array<FModelSkinningRecord>& records, String& error)
{
    if (matrices.Count() != dispatch.matricesCount || positionOffset > UINT32_MAX / 3u || normalOffset > (UINT32_MAX - 1u) / 3u || matrices.Count() > UINT32_MAX / 6u || static_cast<uint64_t>(matrixOffset) + static_cast<uint64_t>(matrices.Count()) * 6u > UINT32_MAX)
        return SetError(error, "FBX GPU skinning matrix dimensions are invalid");
    Array<FModelSkinningRecord> candidate;
    if (!candidate.Reserve(matrices.Count() * 6u))
        return SetError(error, "FBX GPU skinning matrix allocation failed");
    dispatch.matricesOffset = matrixOffset;
    for (uint32_t index = 0; index < matrices.Count(); ++index)
    {
        const double* matrix = matrices.At(index).value;
        for (uint32_t row = 0; row < 3; ++row)
        {
            const double* values = matrix + row * 4;
            if (!isfinite(values[0]) || !isfinite(values[1]) || !isfinite(values[2]) || !isfinite(values[3]) || !AppendDoublePair(candidate, values[0], values[1]) || !AppendDoublePair(candidate, values[2], values[3]))
                return SetError(error, "FBX GPU skinning matrix is invalid or out of memory");
        }
    }
    dispatch.outputPositionsOffset = positionOffset * 3u;
    dispatch.outputPositionsCount = dispatch.positionsCount;
    dispatch.outputNormalsOffset = normalOffset * 3u + 1u;
    dispatch.outputNormalsCount = dispatch.normalGroupRangesCount;
    if (!records.AppendRange(candidate.Data(), candidate.Count()))
        return SetError(error, "FBX GPU skinning matrix append failed");
    error.Clear();
    return true;
}

#if defined(GKCORE_TEST_FRAME_CAPTURE)
/**
 * 現在frame用のGPU_TO_CPU bufferを必要容量まで再利用する。
 */
bool EnsureSkinningReadbackBuffer(Renderer* renderer, Buffer*& buffer, uint64_t& capacity, uint64_t requiredBytes, String& error)
{
    if (!renderer || requiredBytes == 0)
        return SetError(error, "The GPU skinning verification readback size is invalid");
    if (buffer && capacity >= requiredBytes && buffer->pCpuMappedAddress)
    {
        error.Clear();
        return true;
    }
    if (buffer)
    {
        removeResource(buffer);
        buffer = nullptr;
        capacity = 0;
    }
    BufferLoadDesc desc{};
    desc.mDesc.mSize = requiredBytes;
    desc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
    desc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    desc.mDesc.mStartState = RESOURCE_STATE_COPY_DEST;
    desc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
    desc.mDesc.pName = "gkcore GPU skinning verification readback";
    desc.ppBuffer = &buffer;
    addResource(&desc, nullptr);
    if (!buffer || !buffer->pCpuMappedAddress)
    {
        if (buffer)
        {
            removeResource(buffer);
            buffer = nullptr;
        }
        return SetError(error, "The GPU skinning verification readback buffer could not be created or mapped");
    }
    capacity = buffer->mSize;
    error.Clear();
    return true;
}
#endif

}

ForgeRenderer::~ForgeRenderer()
{
    Shutdown();
}

bool ForgeRenderer::Initialize(HWND window, uint32_t width, uint32_t height, String& error)
{
    if (renderer_)
        return SetError(error, "The Forge renderer is already initialized");
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    InitializeRenderProfile();
#endif
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    const char* verifyGpuSkinning = getenv("GKCORE_VERIFY_GPU_SKINNING");
    verifyGpuSkinning_ = verifyGpuSkinning && verifyGpuSkinning[0] == '1' && verifyGpuSkinning[1] == '\0';
#endif
    windowHandle_ = {};
    windowHandle_.type = WINDOW_HANDLE_TYPE_WIN32;
    windowHandle_.window = window;

    RendererDesc rendererDesc{};
    rendererDesc.mDx.mFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    rendererDesc.mShaderTarget = SHADER_TARGET_5_1;
    rendererDesc.mGpuMode = GPU_MODE_SINGLE;
#if defined(_DEBUG)
    gpuDiagnosticsEnabled_ = EnableGpuDiagnostics();
    rendererDesc.mEnableGpuBasedValidation = gpuDiagnosticsEnabled_;
#endif
    initRenderer("gkcore", &rendererDesc, &renderer_);
    if (!renderer_)
        return SetError(error, "The Forge renderer failed to initialize Direct3D 12");
#if defined(_DEBUG)
    if (gpuDiagnosticsEnabled_ && renderer_->mDx.pDevice)
    {
        ID3D12InfoQueue1* infoQueue = nullptr;
        if (SUCCEEDED(renderer_->mDx.pDevice->QueryInterface(IID_PPV_ARGS(&infoQueue))))
        {
            if (SUCCEEDED(infoQueue->RegisterMessageCallback(ReportD3D12Message, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS, nullptr, &gpuInfoCallbackCookie_)))
                gpuInfoQueue_ = infoQueue;
            else
                infoQueue->Release();
        }
    }
#endif
    setupGPUConfigurationPlatformParameters(renderer_, nullptr);

    QueueDesc queueDesc{};
    queueDesc.mType = QUEUE_TYPE_GRAPHICS;
    queueDesc.mFlag = QUEUE_FLAG_NONE;
    queueDesc.pName = "gkcore Graphics Queue";
    initQueue(renderer_, &queueDesc, &graphicsQueue_);
    if (!graphicsQueue_)
    {
        Shutdown();
        return SetError(error, "The Forge renderer failed to create a graphics queue");
    }

    GpuCmdRingDesc ringDesc{};
    ringDesc.pQueue = graphicsQueue_;
    ringDesc.mPoolCount = kFramesInFlight;
    ringDesc.mCmdPerPoolCount = 1;
    ringDesc.mAddSyncPrimitives = true;
    initGpuCmdRing(renderer_, &ringDesc, &commandRing_);
    initSemaphore(renderer_, &imageAcquiredSemaphore_);
    if (!imageAcquiredSemaphore_)
    {
        Shutdown();
        return SetError(error, "The Forge renderer failed to create a swapchain semaphore");
    }
    if (!CreateSwapChain(width, height, error))
    {
        Shutdown();
        return false;
    }
    width_ = width;
    height_ = height;
    initResourceLoaderInterface(renderer_);
    resourceLoaderInitialized_ = true;
    if (!modelGeometryCache_.Initialize(renderer_, graphicsQueue_, error))
    {
        Shutdown();
        return false;
    }
    if (!modelSparseMapCache_.Initialize(renderer_, graphicsQueue_, error))
    {
        Shutdown();
        return false;
    }
    if (!modelSkinningGeometryCache_.Initialize(renderer_, graphicsQueue_, error))
    {
        Shutdown();
        return false;
    }
    if (!InitializeGraphicsResources(error))
    {
        Shutdown();
        return false;
    }
    if (!modelSkinning_.Initialize(renderer_, error))
    {
        Shutdown();
        return false;
    }
    error.Clear();
    return true;
}

bool ForgeRenderer::SupportsGpuModelSkinning() const
{
    return modelSkinning_.SupportsDoublePrecision();
}

void ForgeRenderer::Shutdown()
{
    if (graphicsQueue_)
        waitQueueIdle(graphicsQueue_);
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    testFrameCapture_.Reset();
#endif
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    ReportRenderProfile();
#endif
    if (renderer_)
    {
        DestroyGraphicsResources();
        if (swapChain_)
        {
            removeSwapChain(renderer_, swapChain_);
            swapChain_ = nullptr;
        }
        if (imageAcquiredSemaphore_)
        {
            exitSemaphore(renderer_, imageAcquiredSemaphore_);
            imageAcquiredSemaphore_ = nullptr;
        }
        if (commandRing_.mPoolCount != 0)
            exitGpuCmdRing(renderer_, &commandRing_);
        if (graphicsQueue_)
        {
            exitQueue(renderer_, graphicsQueue_);
            graphicsQueue_ = nullptr;
        }
#if defined(_DEBUG)
        if (gpuInfoQueue_)
        {
            ID3D12InfoQueue1* infoQueue = static_cast<ID3D12InfoQueue1*>(gpuInfoQueue_);
            infoQueue->UnregisterMessageCallback(gpuInfoCallbackCookie_);
            infoQueue->Release();
            gpuInfoQueue_ = nullptr;
            gpuInfoCallbackCookie_ = 0;
        }
#endif
        exitRenderer(renderer_);
        renderer_ = nullptr;
    }
    width_ = height_ = 0;
    vertices_.Clear();
    modelVertices_.Clear();
    modelIndices_.Clear();
    modelDrawConstants_.Clear();
    modelPoseVertices_.Clear();
    modelSkinningRecords_.Clear();
    modelSkinningDispatches_.Clear();
    modelSparseMaps_.Clear();
    runs_.Clear();
    customDraws_.Clear();
}

bool ForgeRenderer::Resize(uint32_t width, uint32_t height, String& error)
{
    if (!renderer_ || width == 0 || height == 0)
        return SetError(error, "The swapchain size must be positive");
    if (width == width_ && height == height_)
        return true;
    if (graphicsQueue_)
        waitQueueIdle(graphicsQueue_);
    if (swapChain_)
    {
        removeSwapChain(renderer_, swapChain_);
        swapChain_ = nullptr;
    }
    if (!CreateSwapChain(width, height, error))
        return false;
    if (!postProcess_.Resize(width, height, error))
    {
        width_ = height_ = 0;
        return false;
    }
    if (!postEffect_.Resize(width, height, error))
    {
        width_ = height_ = 0;
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

bool ForgeRenderer::CreateSwapChain(uint32_t width, uint32_t height, String& error)
{
    SwapChainDesc desc{};
    desc.mWindowHandle = windowHandle_;
    desc.ppPresentQueues = &graphicsQueue_;
    desc.mPresentQueueCount = 1;
    desc.mImageCount = 2;
    desc.mWidth = width;
    desc.mHeight = height;
    desc.mColorClearValue = { { 0.02f, 0.03f, 0.05f, 1.0f } };
    desc.mColorSpace = COLOR_SPACE_SDR_SRGB;
    desc.mColorFormat = getSupportedSwapchainFormat(renderer_, &desc, COLOR_SPACE_SDR_SRGB);
    desc.mEnableVsync = true;
    addSwapChain(renderer_, &desc, &swapChain_);
    if (!swapChain_)
        return SetError(error, "The Forge renderer failed to create a Direct3D 12 swapchain");

    if (depthTarget_)
    {
        removeRenderTarget(renderer_, depthTarget_);
        depthTarget_ = nullptr;
    }
    RenderTargetDesc depthDesc{};
    depthDesc.mArraySize = 1;
    depthDesc.mClearValue.depth = 1.0f;
    depthDesc.mDepth = 1;
    depthDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
    depthDesc.mFormat = TinyImageFormat_D32_SFLOAT;
    depthDesc.mStartState = RESOURCE_STATE_DEPTH_WRITE;
    depthDesc.mSampleCount = SAMPLE_COUNT_1;
    depthDesc.mWidth = width;
    depthDesc.mHeight = height;
    depthDesc.mFlags = TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT;
    depthDesc.pName = "gkcore Depth Buffer";
    addRenderTarget(renderer_, &depthDesc, &depthTarget_);
    if (!depthTarget_)
        return SetError(error, "The Forge renderer failed to create a depth buffer");
    width_ = width;
    height_ = height;
    error.Clear();
    return true;
}

bool ForgeRenderer::InitializeGraphicsResources(String& error)
{
    RootSignatureDesc rootDesc{};
    rootDesc.pGraphicsFileName = "default.rootsig";
    rootDesc.pComputeFileName = "compute.rootsig";
    initRootSignature(renderer_, &rootDesc);
    rootSignatureInitialized_ = true;
    if (!postProcess_.Initialize(renderer_, width_, height_, swapChain_->mFormat, error) || !postEffect_.Initialize(renderer_, width_, height_, error))
        return false;
    if (!textureCache_.Initialize(renderer_, graphicsQueue_, error))
        return false;
    const TinyImageFormat displayFormat = swapChain_->ppRenderTargets[0]->mFormat;
    const TinyImageFormat sceneFormat = postProcess_.SceneTarget()->mFormat;
    const SampleCount sampleCount = postProcess_.SceneTarget()->mSampleCount;
    const uint32_t sampleQuality = postProcess_.SceneTarget()->mSampleQuality;
    if (!customShaders_.Initialize(renderer_, graphicsQueue_, sceneFormat, displayFormat, depthTarget_->mFormat, sampleCount, sampleQuality, error))
        return false;

    whiteImage_ = detail::CreateImageResource();
    if (!whiteImage_)
        return SetError(error, "The white fallback image could not be allocated");
    whiteImage_->width = 1;
    whiteImage_->height = 1;
    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    if (!whiteImage_->rgba.AppendRange(whitePixel, 4))
        return SetError(error, "The white fallback image pixels could not be allocated");

    ShaderLoadDesc shaderDesc{};
    shaderDesc.mVert.pFileName = "gkcore_color.vert";
    shaderDesc.mFrag.pFileName = "gkcore_color.frag";
    addShader(renderer_, &shaderDesc, &colorShader_);
    if (!colorShader_)
        return SetError(error, "The Forge renderer could not load the built-in color shader binaries");

    ShaderLoadDesc spriteShaderDesc{};
    spriteShaderDesc.mVert.pFileName = "gkcore_sprite.vert";
    spriteShaderDesc.mFrag.pFileName = "gkcore_sprite.frag";
    addShader(renderer_, &spriteShaderDesc, &spriteShader_);
    if (!spriteShader_)
        return SetError(error, "The Forge renderer could not load the built-in image shader binaries");

    VertexLayout layout{};
    layout.mBindingCount = 1;
    layout.mBindings[0].mStride = sizeof(Vertex);
    layout.mAttribCount = 2;
    layout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
    layout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    layout.mAttribs[0].mBinding = 0;
    layout.mAttribs[0].mLocation = 0;
    layout.mAttribs[0].mOffset = offsetof(Vertex, position);
    layout.mAttribs[1].mSemantic = SEMANTIC_COLOR;
    layout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    layout.mAttribs[1].mBinding = 0;
    layout.mAttribs[1].mLocation = 1;
    layout.mAttribs[1].mOffset = offsetof(Vertex, color);

    VertexLayout spriteLayout = layout;
    spriteLayout.mAttribCount = 3;
    spriteLayout.mAttribs[2].mSemantic = SEMANTIC_TEXCOORD0;
    spriteLayout.mAttribs[2].mFormat = TinyImageFormat_R32G32_SFLOAT;
    spriteLayout.mAttribs[2].mBinding = 0;
    spriteLayout.mAttribs[2].mLocation = 2;
    spriteLayout.mAttribs[2].mOffset = offsetof(Vertex, uv);

    RasterizerStateDesc rasterizer{};
    rasterizer.mCullMode = CULL_MODE_NONE;
    rasterizer.mFillMode = FILL_MODE_SOLID;
    DepthStateDesc noDepth{};
    noDepth.mDepthTest = false;
    noDepth.mDepthWrite = false;
    noDepth.mDepthFunc = CMP_ALWAYS;
    DepthStateDesc depth{};
    depth.mDepthTest = true;
    depth.mDepthWrite = true;
    depth.mDepthFunc = CMP_LESS;
    BlendStateDesc alphaBlend{};
    alphaBlend.mSrcFactors[0] = BC_SRC_ALPHA;
    alphaBlend.mDstFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    alphaBlend.mBlendModes[0] = BM_ADD;
    alphaBlend.mSrcAlphaFactors[0] = BC_ONE;
    alphaBlend.mDstAlphaFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    alphaBlend.mBlendAlphaModes[0] = BM_ADD;
    alphaBlend.mColorWriteMasks[0] = COLOR_MASK_ALL;
    alphaBlend.mRenderTargetMask = BLEND_STATE_TARGET_0;
    auto createPipeline = [&](const char* name, Shader* shader, VertexLayout* vertexLayout, TinyImageFormat colorFormat, TinyImageFormat depthFormat, DepthStateDesc* depthState, BlendStateDesc* blendState, Pipeline** output)
    {
        PipelineDesc pipelineDesc{};
        pipelineDesc.mType = PIPELINE_TYPE_GRAPHICS;
        GraphicsPipelineDesc& graphics = pipelineDesc.mGraphicsDesc;
        graphics.pShaderProgram = shader;
        graphics.pVertexLayout = vertexLayout;
        graphics.pBlendState = blendState;
        graphics.pDepthState = depthState;
        graphics.pRasterizerState = &rasterizer;
        graphics.pColorFormats = &colorFormat;
        graphics.mRenderTargetCount = 1;
        graphics.mDepthStencilFormat = depthFormat;
        graphics.mSampleCount = swapChain_->ppRenderTargets[0]->mSampleCount;
        graphics.mSampleQuality = swapChain_->ppRenderTargets[0]->mSampleQuality;
        graphics.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
        pipelineDesc.pName = name;
        addPipeline(renderer_, &pipelineDesc, output);
    };
    createPipeline("gkcore HDR 2D Pipeline", colorShader_, &layout, sceneFormat, depthTarget_->mFormat, &noDepth, nullptr, &scenePipeline_);
    createPipeline("gkcore HDR 3D Pipeline", colorShader_, &layout, sceneFormat, depthTarget_->mFormat, &depth, nullptr, &depthPipeline_);
    createPipeline("gkcore UI Color Pipeline", colorShader_, &layout, displayFormat, TinyImageFormat_UNDEFINED, &noDepth, nullptr, &uiPipeline_);
    createPipeline("gkcore HDR Image Pipeline", spriteShader_, &spriteLayout, sceneFormat, depthTarget_->mFormat, &noDepth, nullptr, &spritePipeline_);
    createPipeline("gkcore HDR Alpha Image Pipeline", spriteShader_, &spriteLayout, sceneFormat, depthTarget_->mFormat, &noDepth, &alphaBlend, &spriteAlphaPipeline_);
    createPipeline("gkcore UI Image Pipeline", spriteShader_, &spriteLayout, displayFormat, TinyImageFormat_UNDEFINED, &noDepth, nullptr, &spriteUiPipeline_);
    createPipeline("gkcore UI Alpha Image Pipeline", spriteShader_, &spriteLayout, displayFormat, TinyImageFormat_UNDEFINED, &noDepth, &alphaBlend, &spriteAlphaUiPipeline_);
    if (!scenePipeline_ || !depthPipeline_ || !uiPipeline_ || !spritePipeline_ || !spriteAlphaPipeline_ || !spriteUiPipeline_ || !spriteAlphaUiPipeline_)
        return SetError(error, "The Forge renderer could not create the built-in draw pipelines");

    for (uint32_t i = 0; i < kFramesInFlight; ++i)
    {
        BufferLoadDesc bufferDesc{};
        bufferDesc.mDesc.mSize = static_cast<uint64_t>(kVertexCapacity) * sizeof(Vertex);
        bufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        bufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        bufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
        bufferDesc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        bufferDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        bufferDesc.mDesc.pName = "gkcore Dynamic Vertex Buffer";
        bufferDesc.ppBuffer = &vertexBuffers_[i];
        addResource(&bufferDesc, nullptr);
        BufferLoadDesc indexDesc{};
        indexDesc.mDesc.mSize = static_cast<uint64_t>(kVertexCapacity) * sizeof(uint32_t);
        indexDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        indexDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        indexDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_INDEX_BUFFER;
        indexDesc.mDesc.mStartState = RESOURCE_STATE_INDEX_BUFFER;
        indexDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        indexDesc.mDesc.pName = "gkcore Model Triangle Order";
        indexDesc.ppBuffer = &modelIndexBuffers_[i];
        addResource(&indexDesc, nullptr);
        if (!modelIndexBuffers_[i])
            return SetError(error, "The Forge could not allocate a model index buffer");
        if (!vertexBuffers_[i])
            return SetError(error, "The Forge renderer could not allocate a dynamic vertex buffer");
    }
    waitForAllResourceLoads();
    if (!modelLighting_.Initialize(renderer_, sceneFormat, displayFormat, depthTarget_->mFormat, sampleCount, sampleQuality, kVertexCapacity, error))
        return false;
    waitForAllResourceLoads();
    error.Clear();
    return true;
}

void ForgeRenderer::DestroyGraphicsResources()
{
    if (!renderer_)
        return;
    postEffect_.DiscardPendingFrame();
    modelSkinning_.Shutdown();
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        if (skinningReadbackBuffers_[frame])
        {
            removeResource(skinningReadbackBuffers_[frame]);
            skinningReadbackBuffers_[frame] = nullptr;
        }
        skinningReadbackCapacities_[frame] = 0;
    }
#endif
    modelSparseMapCache_.Shutdown();
    modelSkinningGeometryCache_.Shutdown();
    modelGeometryCache_.Shutdown();
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        if (modelIndexBuffers_[frame])
        {
            removeResource(modelIndexBuffers_[frame]);
            modelIndexBuffers_[frame] = nullptr;
        }
    }
    postEffect_.Shutdown();
    modelLighting_.Shutdown();
    customShaders_.Shutdown();
    textureCache_.Shutdown();
    if (whiteImage_)
    {
        Release(&whiteImage_->reference);
        whiteImage_ = nullptr;
    }
    postProcess_.Shutdown();
    for (uint32_t i = 0; i < kFramesInFlight; ++i)
    {
        if (vertexBuffers_[i])
        {
            removeResource(vertexBuffers_[i]);
            vertexBuffers_[i] = nullptr;
        }
    }
    if (spriteAlphaPipeline_)
    {
        removePipeline(renderer_, spriteAlphaPipeline_);
        spriteAlphaPipeline_ = nullptr;
    }
    if (spriteAlphaUiPipeline_)
    {
        removePipeline(renderer_, spriteAlphaUiPipeline_);
        spriteAlphaUiPipeline_ = nullptr;
    }
    if (spriteUiPipeline_)
    {
        removePipeline(renderer_, spriteUiPipeline_);
        spriteUiPipeline_ = nullptr;
    }
    if (spritePipeline_)
    {
        removePipeline(renderer_, spritePipeline_);
        spritePipeline_ = nullptr;
    }
    if (uiPipeline_)
    {
        removePipeline(renderer_, uiPipeline_);
        uiPipeline_ = nullptr;
    }
    if (depthPipeline_)
    {
        removePipeline(renderer_, depthPipeline_);
        depthPipeline_ = nullptr;
    }
    if (scenePipeline_)
    {
        removePipeline(renderer_, scenePipeline_);
        scenePipeline_ = nullptr;
    }
    if (spriteShader_)
    {
        removeShader(renderer_, spriteShader_);
        spriteShader_ = nullptr;
    }
    if (colorShader_)
    {
        removeShader(renderer_, colorShader_);
        colorShader_ = nullptr;
    }
    if (depthTarget_)
    {
        removeRenderTarget(renderer_, depthTarget_);
        depthTarget_ = nullptr;
    }
    if (rootSignatureInitialized_)
    {
        exitRootSignature(renderer_);
        rootSignatureInitialized_ = false;
    }
    if (resourceLoaderInitialized_)
    {
        exitResourceLoaderInterface(renderer_);
        resourceLoaderInitialized_ = false;
    }
}

bool ForgeRenderer::Present(const detail::FramePacket& frame, String& error)
{
    postEffect_.DiscardPendingFrame();
    if (!renderer_ || !swapChain_ || !graphicsQueue_)
        return SetError(error, "The Forge renderer is not initialized");
    if (frame.width == 0 || frame.height == 0)
        return SetError(error, "The frame size must be positive");
    // 同期設定を切り替えるときだけ、使用中のswapchain処理を完了させる。
    if (static_cast<bool>(swapChain_->mEnableVsync) != frame.vSyncEnabled)
    {
        waitQueueIdle(graphicsQueue_);
        toggleVSync(renderer_, &swapChain_);
    }
    const PostProcessSettings settings = { frame.bloomEnabled, frame.bloomIntensity, frame.exposure, frame.toneMappingEnabled, frame.saturation, frame.contrast, frame.fxaaEnabled };
    if (!IsPostProcessSettingsValid(settings))
        return SetError(error, "The frame post-process settings are invalid");

#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    uint64_t profileTicks[6]{};
    int64_t profileStart = 0;
    if (gRenderProfile.enabled)
        profileStart = ReadRenderProfileCounter();
#endif

    // sceneの透明modelを三角形単位で奥から描く順序へ並べる。
    modelGeometryCache_.BeginFrame();
    modelSparseMapCache_.BeginFrame();
    modelSkinningGeometryCache_.BeginFrame();
    modelSparseMaps_.Clear();
    modelIndices_.Clear();
    modelDrawConstants_.Clear();
    modelPoseVertices_.Clear();
    modelSkinningRecords_.Clear();
    const FModelSkinningRecord skinningErrorSeed = {};
    if (!modelSkinningRecords_.Append(skinningErrorSeed))
        return SetError(error, "The model skinning error seed allocation failed");
    modelSkinningDispatches_.Clear();
    modelSkinningGeometryBuffers_.Clear();
    /**
     * 同じ描画snapshotがGPU姿勢buffer内で使う先頭。
     */
    struct FPreparedPoseRange
    {
        // frameが保持している独立snapshotを借用する。
        const detail::ModelResource* model;
        // dummyを除き1から始まるGPU頂点offset。
        uint32_t offset;
    };
    Array<FPreparedPoseRange> preparedPoseRanges;
    /**
     * 同じdeferred snapshotが参照する位置・法線poolと元map。
     */
    struct FPreparedSparseRange
    {
        // frameが保持する予約時の姿勢を借用する。
        const model::FModelDeferredPose* pose;
        // 原型を保持するGPU map。
        Buffer* map;
        // 位置と法線の1-based pool先頭。
        uint32_t positionOffset;
        uint32_t normalOffset;
        bool gpuSkinning;
    };
    Array<FPreparedSparseRange> preparedSparseRanges;
    uint32_t cachedModelVertexCount = 0;
    bool hasLitModelRuns = false;
    Array<ModelDrawPlan::FRange> modelDrawPlanRanges;
    Array<ModelPartPlan> modelDrawPlanParts;
    if (!modelDrawPlanRanges.Reserve(frame.draws.Count()))
        return SetError(error, "The model draw plan range allocation failed");
    for (uint32_t drawIndex = 0; drawIndex < frame.draws.Count(); ++drawIndex)
    {
        ModelDrawPlan::FRange range{};
        range.firstPart = modelDrawPlanParts.Count();
        const auto& draw = frame.draws.At(drawIndex);
        if (draw.kind == detail::DrawKind::Model && draw.model)
        {
            ModelDrawPlan plan;
            if (!BuildModelDrawPlan(*draw.model, plan, error) || !modelDrawPlanParts.AppendRange(plan.parts.Data(), plan.parts.Count()))
            {
                if (error.Empty())
                    error.Assign("The frame model draw plan allocation failed");
                return false;
            }
            range.partCount = plan.parts.Count();
        }
        if (!modelDrawPlanRanges.Append(range))
            return SetError(error, "The model draw plan range allocation failed");
    }
    Array<FModelTransparencyDraw> transparencyPlan;
    if (!BuildModelTransparencyPlan(frame, kVertexCapacity / 3, transparencyPlan, error, modelDrawPlanRanges.Data(), modelDrawPlanRanges.Count(), modelDrawPlanParts.Data(), modelDrawPlanParts.Count()))
        return false;
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    if (gRenderProfile.enabled)
    {
        const int64_t phaseEnd = ReadRenderProfileCounter();
        profileTicks[0] = MeasureRenderProfileTicks(profileStart, phaseEnd);
        profileStart = phaseEnd;
    }
#endif

    textureCache_.BeginFrame();
    if (!textureCache_.DrainPendingUploads(error))
        return false;
    vertices_.Clear();
    modelVertices_.Clear();
    runs_.Clear();
    customDraws_.Clear();
    uint32_t customDrawCount = 0;
    auto appendRun = [&](uint32_t first, uint32_t count, bool depthTest, bool textured, bool alphaBlend, detail::ImageResource* image, const detail::DrawPacket& draw, bool customShader, bool litModel, uint32_t customDrawIndex, detail::ImageResource* metallicRoughnessImage = nullptr, detail::ImageResource* normalImage = nullptr, const detail::FTextureSampler* baseSampler = nullptr, const detail::FTextureSampler* materialSampler = nullptr, const detail::FTextureSampler* surfaceNormalSampler = nullptr, detail::ImageResource* emissiveImage = nullptr, const detail::FTextureSampler* emissionSampler = nullptr, detail::ImageResource* occlusionImage = nullptr, const detail::FTextureSampler* ambientOcclusionSampler = nullptr, Buffer* cachedModelBuffer = nullptr, uint32_t modelDrawConstantIndex = 0, bool indexedModel = false) -> bool
    {
        if (count == 0)
            return true;
        // 各画像のsamplerを値で保持し、描画登録元の寿命から切り離す。
        const detail::FTextureSampler baseValue = baseSampler ? *baseSampler : detail::FTextureSampler{};
        const detail::FTextureSampler materialValue = materialSampler ? *materialSampler : detail::FTextureSampler{};
        const detail::FTextureSampler normalValue = surfaceNormalSampler ? *surfaceNormalSampler : detail::FTextureSampler{};
        // 自己発光画像も独立したsampler値で比較する。
        const detail::FTextureSampler emissiveValue = emissionSampler ? *emissionSampler : detail::FTextureSampler{};
        // 遮蔽画像も描画登録元から独立したsampler値で保持する。
        const detail::FTextureSampler occlusionValue = ambientOcclusionSampler ? *ambientOcclusionSampler : detail::FTextureSampler{};
        const bool canBatch = !customShader && runs_.Count() && !runs_.At(runs_.Count() - 1).customShader && runs_.At(runs_.Count() - 1).first + runs_.At(runs_.Count() - 1).count == first && runs_.At(runs_.Count() - 1).depthTest == depthTest && runs_.At(runs_.Count() - 1).textured == textured && runs_.At(runs_.Count() - 1).alphaBlend == alphaBlend && runs_.At(runs_.Count() - 1).litModel == litModel && runs_.At(runs_.Count() - 1).cachedModelBuffer == cachedModelBuffer && runs_.At(runs_.Count() - 1).modelDrawConstantIndex == modelDrawConstantIndex && runs_.At(runs_.Count() - 1).indexedModel == indexedModel && runs_.At(runs_.Count() - 1).layer == draw.layer && runs_.At(runs_.Count() - 1).image == image && runs_.At(runs_.Count() - 1).metallicRoughnessImage == metallicRoughnessImage && runs_.At(runs_.Count() - 1).normalImage == normalImage && runs_.At(runs_.Count() - 1).emissiveImage == emissiveImage && runs_.At(runs_.Count() - 1).occlusionImage == occlusionImage && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).baseColorSampler, baseValue) && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).metallicRoughnessSampler, materialValue) && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).normalSampler, normalValue) && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).emissiveSampler, emissiveValue) && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).occlusionSampler, occlusionValue);
        if (canBatch)
        {
            runs_.At(runs_.Count() - 1).count += count;
            return true;
        }
        const RenderRun run{ first, count, depthTest, textured, alphaBlend, draw.layer, image, draw.shader, customDrawIndex, customShader, litModel, metallicRoughnessImage, normalImage, baseValue, materialValue, normalValue, emissiveImage, emissiveValue, occlusionImage, occlusionValue, cachedModelBuffer, modelDrawConstantIndex, indexedModel };
        return runs_.Append(run);
    };
    auto appendLitPart = [&](const detail::DrawPacket& draw, const ModelPartPlan& part, const uint32_t* firstIndices = nullptr, uint32_t triangleCount = 0) -> bool
    {
        bool cached = false;
        Buffer* cachedBuffer = nullptr;
        uint32_t count = 0;
        FModelDrawConstants drawConstants{};
        bool gpuTransformSafe = modelDrawConstants_.Count() < kModelDrawMaximumConstants && PackModelDrawConstants(frame, draw, drawConstants, error);
        Buffer* sparseMap = nullptr;
        if (gpuTransformSafe && draw.deferredPose)
        {
            uint32_t positionOffset = 0;
            uint32_t normalOffset = 0;
            bool gpuSkinning = false;
            for (uint32_t range = 0; range < preparedSparseRanges.Count(); ++range)
            {
                if (preparedSparseRanges.At(range).pose == draw.deferredPose)
                {
                    positionOffset = preparedSparseRanges.At(range).positionOffset;
                    normalOffset = preparedSparseRanges.At(range).normalOffset;
                    sparseMap = preparedSparseRanges.At(range).map;
                    gpuSkinning = preparedSparseRanges.At(range).gpuSkinning;
                    break;
                }
            }
            if (positionOffset == 0)
            {
                const auto& geometry = draw.deferredPose->geometry;
                const uint64_t required = static_cast<uint64_t>(geometry.positions.Count()) + geometry.normals.Count();
                if (required > kVertexCapacity - modelPoseVertices_.Count() || !modelSparseMapCache_.Prepare(*draw.model, sparseMap, error))
                {
                    error.Clear();
                    gpuTransformSafe = false;
                }
                else
                {
                    positionOffset = modelPoseVertices_.Count() + 1;
                    normalOffset = positionOffset + geometry.positions.Count();
                    const auto* animationSource = draw.deferredPose->source && draw.deferredPose->source->animation ? draw.deferredPose->source->animation->source : nullptr;
                    const auto* gpuGeometry = animationSource ? animationSource->GpuSkinningGeometry() : nullptr;
                    const auto* gpuSparseMap = animationSource ? animationSource->SparseVertexMap() : nullptr;
                    if (gpuGeometry && draw.deferredPose->gpuSkinningMatrices.Count() && modelSkinning_.SupportsDoublePrecision())
                    {
                        Buffer* geometryBuffer = nullptr;
                        FModelSkinningDispatch dispatch{};
                        Array<FModelSkinningRecord> matrixRecords;
                        bool geometryCacheHit = false;
                        const bool geometryCached = gpuSparseMap && modelSkinningGeometryCache_.Prepare(*draw.model, *gpuGeometry, *gpuSparseMap, geometryBuffer, dispatch, geometryCacheHit, error);
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
                        if (gRenderProfile.enabled && geometryCached && geometryBuffer)
                        {
                            if (geometryCacheHit)
                                ++gRenderProfile.skinGeometryCacheHits;
                            else
                                ++gRenderProfile.skinGeometryCacheMisses;
                        }
#endif
                        const bool prepared = geometryCached && geometryBuffer && PackSkinningMatrices(draw.deferredPose->gpuSkinningMatrices, modelSkinningRecords_.Count(), dispatch, positionOffset, normalOffset, matrixRecords, error) && modelSkinningDispatches_.Reserve(modelSkinningDispatches_.Count() + 1) && modelSkinningGeometryBuffers_.Reserve(modelSkinningGeometryBuffers_.Count() + 1) && modelSkinningRecords_.Reserve(modelSkinningRecords_.Count() + matrixRecords.Count());
                        if (prepared && modelSkinningRecords_.AppendRange(matrixRecords.Data(), matrixRecords.Count()) && modelSkinningDispatches_.Append(dispatch) && modelSkinningGeometryBuffers_.Append(geometryBuffer))
                        {
                            gpuSkinning = true;
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
                            if (gRenderProfile.enabled)
                                ++gRenderProfile.gpuSkinningDispatches;
#endif
                        }
                        else
                            error.Clear();
                    }
                    if (draw.deferredPose->gpuEvaluationOnly && gpuSkinning)
                    {
                        if (!AppendSparsePosePlaceholders(static_cast<uint32_t>(required), modelPoseVertices_, error))
                            return false;
                    }
                    else if (draw.deferredPose->gpuEvaluationOnly)
                    {
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
                        if (gRenderProfile.enabled)
                            ++gRenderProfile.gpuSkinningFallbacks;
#endif
                        model::animation::FModelSparsePoseGeometry fallbackGeometry;
                        if (!animationSource || !animationSource->DeformSparse(draw.deferredPose->frozenPose, fallbackGeometry, error) || fallbackGeometry.positions.Count() != geometry.positions.Count() || fallbackGeometry.normals.Count() != geometry.normals.Count() || !AppendSparsePoseVertices(fallbackGeometry, modelPoseVertices_, error))
                        {
                            if (error.Empty())
                                error.Assign("The deferred GPU pose could not be restored for CPU rendering");
                            return false;
                        }
                    }
                    else if (!AppendSparsePoseVertices(geometry, modelPoseVertices_, error))
                        return false;
                    const FPreparedSparseRange range{ draw.deferredPose, sparseMap, positionOffset, normalOffset, gpuSkinning };
                    if (!preparedSparseRanges.Append(range))
                        return SetError(error, "The sparse pose range allocation failed");
                }
            }
            if (gpuTransformSafe)
            {
                drawConstants.flags[0] = 3.0f;
                drawConstants.flags[1] = static_cast<float>(positionOffset);
                drawConstants.flags[2] = static_cast<float>(normalOffset);
            }
        }
        if (gpuTransformSafe && draw.model->isPoseSnapshot)
        {
            gpuTransformSafe = draw.model->geometrySource && IsModelPosePartGpuSafe(*draw.model, part);
            if (gpuTransformSafe)
            {
                uint32_t poseOffset = 0;
                for (uint32_t range = 0; range < preparedPoseRanges.Count(); ++range)
                {
                    if (preparedPoseRanges.At(range).model == draw.model)
                    {
                        poseOffset = preparedPoseRanges.At(range).offset;
                        break;
                    }
                }
                if (poseOffset == 0)
                {
                    const uint32_t firstPoseVertex = modelPoseVertices_.Count();
                    if (AppendModelPoseVertices(*draw.model, modelPoseVertices_, kVertexCapacity, error))
                    {
                        poseOffset = firstPoseVertex + 1;
                        const FPreparedPoseRange range{ draw.model, poseOffset };
                        if (!preparedPoseRanges.Append(range))
                            return SetError(error, "The GPU pose range allocation failed");
                    }
                    else
                    {
                        error.Clear();
                        gpuTransformSafe = false;
                    }
                }
                if (gpuTransformSafe)
                {
                    drawConstants.flags[0] = 2.0f;
                    drawConstants.flags[1] = static_cast<float>(poseOffset);
                }
            }
        }
        if (gpuTransformSafe)
        {
            if (!modelGeometryCache_.Prepare(frame, draw, part, firstIndices, firstIndices ? triangleCount : part.indexCount / 3, cached, cachedBuffer, count, error))
                return false;
        }
        else
            error.Clear();
        uint32_t constantIndex = 0;
        bool indexedModel = false;
        uint32_t first = cached ? 0 : modelVertices_.Count();
        if (cached)
        {
            if (firstIndices)
                count = triangleCount * 3;
            if (vertices_.Count() > kVertexCapacity || modelVertices_.Count() > kVertexCapacity - vertices_.Count() || cachedModelVertexCount > kVertexCapacity - vertices_.Count() - modelVertices_.Count() || count > kVertexCapacity - vertices_.Count() - modelVertices_.Count() - cachedModelVertexCount)
            {
                cached = false;
                cachedBuffer = nullptr;
                first = modelVertices_.Count();
            }
        }
        if (!cached)
        {
            detail::DrawPacket cpuDraw = draw;
            detail::ModelResource* materialized = nullptr;
            if (draw.deferredPose)
            {
                materialized = model::MaterializeDeferredModelPose(*draw.deferredPose, error);
                if (!materialized)
                    return false;
                cpuDraw.model = materialized;
                cpuDraw.deferredPose = nullptr;
            }
            const bool appended = firstIndices ? AppendLitModelTriangles(frame, cpuDraw, part, firstIndices, triangleCount, modelVertices_, kVertexCapacity, error) : AppendLitModelPart(frame, cpuDraw, part, modelVertices_, kVertexCapacity, error);
            if (materialized)
                Release(&materialized->reference);
            if (!appended)
                return false;
            count = modelVertices_.Count() - first;
        }
        else
        {
            for (uint32_t index = 0; index < modelDrawConstants_.Count(); ++index)
            {
                if (modelSparseMaps_.At(index) == sparseMap && memcmp(&modelDrawConstants_.At(index), &drawConstants, sizeof(drawConstants)) == 0)
                {
                    constantIndex = index + 1;
                    break;
                }
            }
            if (constantIndex == 0)
            {
                if (!modelDrawConstants_.Append(drawConstants) || !modelSparseMaps_.Append(sparseMap))
                    return SetError(error, "The GPU model transform allocation failed");
                constantIndex = modelDrawConstants_.Count();
            }
            if (firstIndices)
            {
                if (count > kVertexCapacity - modelIndices_.Count())
                    return SetError(error, "The sorted GPU model indices exceed the frame capacity");
                first = modelIndices_.Count();
                for (uint32_t triangle = 0; triangle < triangleCount; ++triangle)
                {
                    const uint32_t localIndex = firstIndices[triangle] - part.firstIndex;
                    const uint32_t indices[3] = { localIndex, localIndex + 1, localIndex + 2 };
                    if (!modelIndices_.AppendRange(indices, 3))
                        return SetError(error, "The sorted GPU model index allocation failed");
                }
                indexedModel = true;
            }
            cachedModelVertexCount += count;
        }
        if (count == 0)
            return true;
        detail::ImageResource* image = part.textureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.textureIndex)) : whiteImage_;
        detail::ImageResource* metallicRoughnessImage = part.metallicRoughnessTextureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.metallicRoughnessTextureIndex)) : whiteImage_;
        detail::ImageResource* normalImage = part.normalTextureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.normalTextureIndex)) : whiteImage_;
        detail::ImageResource* emissiveImage = part.emissiveTextureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.emissiveTextureIndex)) : whiteImage_;
        detail::ImageResource* occlusionImage = part.occlusionTextureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.occlusionTextureIndex)) : whiteImage_;
        if (!image || !metallicRoughnessImage || !normalImage || !emissiveImage || !occlusionImage || !textureCache_.PrepareModel(image, metallicRoughnessImage, error, normalImage, &part.baseColorSampler, &part.metallicRoughnessSampler, &part.normalSampler, emissiveImage, &part.emissiveSampler, occlusionImage, &part.occlusionSampler))
            return false;
        if (!appendRun(first, count, draw.layer == 0, true, part.alphaBlend, image, draw, false, true, 0, metallicRoughnessImage, normalImage, &part.baseColorSampler, &part.metallicRoughnessSampler, &part.normalSampler, emissiveImage, &part.emissiveSampler, occlusionImage, &part.occlusionSampler, cachedBuffer, constantIndex, indexedModel))
            return SetError(error, "The frame draw-run allocation failed");
        hasLitModelRuns = true;
        return true;
    };
    Array<ModelPartPlan> transparencyParts;
    Array<FTransparentPartRange> transparencyRanges;
    if (!transparencyRanges.Reserve(frame.draws.Count()))
        return SetError(error, "The transparent model range allocation failed");
    for (uint32_t drawIndex = 0; drawIndex < frame.draws.Count(); ++drawIndex)
        if (!transparencyRanges.Append({ 0, 0 }))
            return SetError(error, "The transparent model range allocation failed");
    uint32_t transparencyCursor = 0;
    // 同じmodel・材質が連続する透明triangleを指定順にまとめる。
    Array<uint32_t> sortedTriangleFirstIndices;
    auto flushDeferred = [&](uint32_t beforeIndex) -> bool
    {
        while (transparencyCursor < transparencyPlan.Count() && transparencyPlan.At(transparencyCursor).barrierIndex <= beforeIndex)
        {
            const FModelTransparencyDraw& entry = transparencyPlan.At(transparencyCursor);
            if (entry.drawIndex >= frame.draws.Count())
                return SetError(error, "The transparent model draw reference is invalid");
            const FTransparentPartRange& range = transparencyRanges.At(entry.drawIndex);
            if (entry.partIndex >= range.count || range.first > transparencyParts.Count() || range.count > transparencyParts.Count() - range.first)
                return SetError(error, "The transparent model part reference is invalid");
            const ModelPartPlan& part = transparencyParts.At(range.first + entry.partIndex);
            sortedTriangleFirstIndices.Clear();
            const uint32_t groupDraw = entry.drawIndex;
            const uint32_t groupPart = entry.partIndex;
            const uint32_t groupBarrier = entry.barrierIndex;
            while (transparencyCursor < transparencyPlan.Count())
            {
                const auto& next = transparencyPlan.At(transparencyCursor);
                if (next.drawIndex != groupDraw || next.partIndex != groupPart || next.barrierIndex != groupBarrier)
                    break;
                if (!sortedTriangleFirstIndices.Append(next.firstIndex))
                    return SetError(error, "The sorted model triangle allocation failed");
                ++transparencyCursor;
            }
            if (!appendLitPart(frame.draws.At(groupDraw), part, sortedTriangleFirstIndices.Data(), sortedTriangleFirstIndices.Count()))
                return false;
        }
        return true;
    };
    for (uint32_t layer = 0; layer < 2; ++layer)
    {
        for (uint32_t i = 0; i < frame.draws.Count(); ++i)
        {
            const detail::DrawPacket& draw = frame.draws.At(i);
            if (draw.layer > 1)
                return SetError(error, "The draw packet has an invalid layer");
            if (draw.layer != layer)
                continue;
            if (layer == 0 && !flushDeferred(i))
                return false;

            const bool customShader = draw.shader.IsValid();
            if (customShader)
            {
                if (customDrawCount >= kCustomShaderMaximumDraws)
                    return SetError(error, "The frame exceeds the 4096 custom shader draw limit");
                if (draw.shaderConstantCount > kShaderConstantSlotCount)
                    return SetError(error, "The custom shader draw has too many constant values");
            }
            const uint32_t customDrawIndex = customDrawCount;

            if (draw.kind == detail::DrawKind::Model && draw.model)
            {
                const ModelDrawPlan::FRange& drawPlanRange = modelDrawPlanRanges.At(i);
                const uint32_t partCount = drawPlanRange.partCount;
                const uint32_t firstPart = drawPlanRange.firstPart;
                if (firstPart > modelDrawPlanParts.Count() || partCount > modelDrawPlanParts.Count() - firstPart)
                    return SetError(error, "The model draw plan range is invalid");
                if (partCount == 0)
                    continue;
                if (layer == 0)
                {
                    FTransparentPartRange& range = transparencyRanges.At(i);
                    range.first = transparencyParts.Count();
                    range.count = partCount;
                    if (!transparencyParts.AppendRange(modelDrawPlanParts.Data() + firstPart, partCount))
                        return SetError(error, "The transparent model part allocation failed");
                }
                // 独自ピクセルシェーダーの入力には材質の切り抜き条件がないため、無視して描かない。
                if (customShader)
                {
                    for (uint32_t partIndex = 0; partIndex < partCount; ++partIndex)
                    {
                        const ModelPartPlan& part = modelDrawPlanParts.At(firstPart + partIndex);
                        if (part.alphaMask || part.alphaBlend)
                        {
                            return SetError(error, part.alphaMask ? "custom pixel shaders do not support masked model materials" : "custom pixel shaders do not support blended model materials");
                        }
                    }
                }
                const bool customNeedsTexture = customShader && (customShaders_.RequiresTexture(draw.shader) || customShaders_.RequiresSampler(draw.shader));
                if (customShader)
                {
                    CustomShaderDraw snapshot{};
                    snapshot.shader = draw.shader;
                    snapshot.constantCount = draw.shaderConstantCount;
                    for (uint32_t constant = 0; constant < draw.shaderConstantCount; ++constant)
                        snapshot.constants[constant] = draw.shaderConstants[constant];
                    if (!customDraws_.Append(snapshot))
                        return SetError(error, "The custom shader draw snapshot allocation failed");
                }
                for (uint32_t partIndex = 0; partIndex < partCount; ++partIndex)
                {
                    const ModelPartPlan& part = modelDrawPlanParts.At(firstPart + partIndex);
                    if (!customShader)
                    {
                        if (layer == 0 && part.alphaBlend)
                            continue;
                        if (!appendLitPart(draw, part))
                            return false;
                        continue;
                    }
                    const bool litModel = !customShader;
                    const uint32_t first = litModel ? modelVertices_.Count() : vertices_.Count();
                    if (litModel)
                    {
                        const uint32_t available = kVertexCapacity - vertices_.Count();
                        if (!AppendLitModelPart(frame, draw, part, modelVertices_, available, error))
                            return false;
                    }
                    else
                    {
                        const uint32_t available = kVertexCapacity - modelVertices_.Count();
                        if (!AppendModelPart(frame, draw, part, vertices_, available, error))
                            return false;
                    }
                    const uint32_t count = (litModel ? modelVertices_.Count() : vertices_.Count()) - first;
                    if (count == 0)
                        continue;
                    detail::ImageResource* materialImage = part.textureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.textureIndex)) : nullptr;
                    const bool needsTexture = customNeedsTexture || !customShader;
                    detail::ImageResource* image = needsTexture ? (materialImage ? materialImage : whiteImage_) : nullptr;
                    // MR画像がないモデルは線形の白を使い、係数だけの材質を保つ。
                    detail::ImageResource* metallicRoughnessImage = litModel ? (part.metallicRoughnessTextureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.metallicRoughnessTextureIndex)) : whiteImage_) : nullptr;
                    // 法線画像は線形形式で準備し、未使用時は白画像を共有する。
                    detail::ImageResource* normalImage = litModel ? (part.normalTextureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.normalTextureIndex)) : whiteImage_) : nullptr;
                    // 自己発光画像がない材質では、線形RGB係数へ白を掛ける。
                    detail::ImageResource* emissiveImage = litModel ? (part.emissiveTextureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.emissiveTextureIndex)) : whiteImage_) : nullptr;
                    // 画像がない材質は環境光を弱めない白を使う。
                    detail::ImageResource* occlusionImage = litModel ? (part.occlusionTextureIndex >= 0 ? draw.model->textures.At(static_cast<uint32_t>(part.occlusionTextureIndex)) : whiteImage_) : nullptr;
                    if (needsTexture)
                    {
                        if (!image)
                            return SetError(error, "The model texture fallback is unavailable");
                        if (litModel)
                        {
                            if (!metallicRoughnessImage || !normalImage || !emissiveImage || !occlusionImage || !textureCache_.PrepareModel(image, metallicRoughnessImage, error, normalImage, &part.baseColorSampler, &part.metallicRoughnessSampler, &part.normalSampler, emissiveImage, &part.emissiveSampler, occlusionImage, &part.occlusionSampler))
                                return false;
                        }
                        else if (!textureCache_.Prepare(image, error))
                            return false;
                    }
                    const bool alphaBlend = customShader && (draw.flags & detail::DrawAlphaBlend) != 0;
                    const bool depthTest = layer == 0;
                    if (!appendRun(first, count, depthTest, needsTexture, alphaBlend, image, draw, customShader, litModel, customDrawIndex, metallicRoughnessImage, normalImage, litModel ? &part.baseColorSampler : nullptr, litModel ? &part.metallicRoughnessSampler : nullptr, litModel ? &part.normalSampler : nullptr, emissiveImage, litModel ? &part.emissiveSampler : nullptr, occlusionImage, litModel ? &part.occlusionSampler : nullptr))
                        return SetError(error, "The frame draw-run allocation failed");
                }
                if (customShader)
                    ++customDrawCount;
                continue;
            }

            bool needsTexture = draw.kind == detail::DrawKind::Image;
            if (customShader)
            {
                needsTexture = customShaders_.RequiresTexture(draw.shader) || customShaders_.RequiresSampler(draw.shader);
            }

            const uint32_t first = vertices_.Count();
            if (!AppendDraw(frame, draw, vertices_, kVertexCapacity - modelVertices_.Count(), error))
                return false;
            detail::ImageResource* image = nullptr;
            if (needsTexture)
            {
                image = draw.kind == detail::DrawKind::Image ? draw.image : whiteImage_;
                if (!image)
                    return SetError(error, "The custom shader texture fallback is unavailable");
                if (!textureCache_.Prepare(image, error))
                    return false;
            }
            const uint32_t count = vertices_.Count() - first;
            if (count == 0)
                continue;
            const bool textured = needsTexture;
            const bool alphaBlend = customShader ? (draw.flags & detail::DrawAlphaBlend) != 0 : (textured && (draw.flags & detail::DrawAlphaBlend) != 0);
            const bool depthTest = layer == 0 && draw.kind != detail::DrawKind::Rect && draw.kind != detail::DrawKind::Image;

            if (customShader)
            {
                CustomShaderDraw snapshot{};
                snapshot.shader = draw.shader;
                snapshot.constantCount = draw.shaderConstantCount;
                for (uint32_t constant = 0; constant < draw.shaderConstantCount; ++constant)
                    snapshot.constants[constant] = draw.shaderConstants[constant];
                if (!customDraws_.Append(snapshot))
                    return SetError(error, "The custom shader draw snapshot allocation failed");
            }

            if (!appendRun(first, count, depthTest, textured, alphaBlend, image, draw, customShader, false, customDrawIndex))
                return SetError(error, "The frame draw-run allocation failed");
            if (customShader)
                ++customDrawCount;
        }
        if (layer == 0 && !flushDeferred(frame.draws.Count()))
            return false;
    }
    PostEffectPlan postEffectPlan{};
    if (!BuildPostEffectPlan(frame, customDrawCount, postEffectPlan, error))
        return false;
    if (postEffectPlan.enabled)
    {
        CustomShaderDraw postEffectDraw{};
        postEffectDraw.shader = postEffectPlan.shader;
        postEffectDraw.constantCount = postEffectPlan.constantCount;
        for (uint32_t constant = 0; constant < postEffectPlan.constantCount; ++constant)
            postEffectDraw.constants[constant] = postEffectPlan.constants[constant];
        if (!customDraws_.Append(postEffectDraw))
            return SetError(error, "The post-effect shader draw snapshot allocation failed");
    }
    if (customDraws_.Count() != postEffectPlan.preparedDrawCount)
        return SetError(error, "The custom shader draw snapshot count does not match the frame plan");
    if (vertices_.Count() > kVertexCapacity || modelVertices_.Count() > kVertexCapacity - vertices_.Count() || cachedModelVertexCount > kVertexCapacity - vertices_.Count() - modelVertices_.Count())
        return SetError(error, "The frame exceeds the dynamic vertex capacity");

#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    if (gRenderProfile.enabled)
    {
        const int64_t phaseEnd = ReadRenderProfileCounter();
        profileTicks[1] = MeasureRenderProfileTicks(profileStart, phaseEnd);
        profileStart = phaseEnd;
    }
#endif
    GpuCmdRingElement element = getNextGpuCmdRingElement(&commandRing_, true, 1);
    if (!element.pCmdPool || !element.pFence || !element.pSemaphore)
        return SetError(error, "The Forge command ring is not ready");
    FenceStatus fenceStatus = FENCE_STATUS_COMPLETE;
    getFenceStatus(renderer_, element.pFence, &fenceStatus);
    if (fenceStatus == FENCE_STATUS_INCOMPLETE)
        waitForFences(renderer_, 1, &element.pFence);
    resetCmdPool(renderer_, element.pCmdPool);
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    if (gRenderProfile.enabled)
    {
        const int64_t phaseEnd = ReadRenderProfileCounter();
        profileTicks[2] = MeasureRenderProfileTicks(profileStart, phaseEnd);
        profileStart = phaseEnd;
    }
#endif

    const uint32_t frameIndex = commandRing_.mPoolIndex;
    if (!customShaders_.PrepareFrame(frameIndex, customDraws_.Data(), customDraws_.Count(), error))
        return false;
    if (hasLitModelRuns && !modelLighting_.PrepareFrame(frameIndex, modelVertices_.Data(), modelVertices_.Count(), frame.lighting, error))
        return false;
    uint32_t poseFloat4Count = 0;
    Buffer* poseBufferOverride = nullptr;
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    uint64_t skinningReadbackByteCount = 0;
#endif
    if (hasLitModelRuns)
    {
        const uint64_t requiredFloat4Count = (static_cast<uint64_t>(modelPoseVertices_.Count()) + 1u) * 3u;
        if (requiredFloat4Count > UINT32_MAX)
            return SetError(error, "The model pose stream exceeds its GPU index range");
        poseFloat4Count = static_cast<uint32_t>(requiredFloat4Count);
        if (modelSkinningDispatches_.Count())
        {
            if (!modelSkinning_.PrepareFrame(frameIndex, modelSkinningRecords_.Data(), modelSkinningRecords_.Count(), modelSkinningGeometryBuffers_.Data(), modelSkinningDispatches_.Data(), modelSkinningDispatches_.Count(), poseFloat4Count, error))
                return false;
            poseBufferOverride = modelSkinning_.OutputBuffer(frameIndex);
#if defined(GKCORE_TEST_FRAME_CAPTURE)
            if (verifyGpuSkinning_)
            {
                skinningReadbackByteCount = static_cast<uint64_t>(poseFloat4Count) * sizeof(float) * 4u;
                if (!EnsureSkinningReadbackBuffer(renderer_, skinningReadbackBuffers_[frameIndex], skinningReadbackCapacities_[frameIndex], skinningReadbackByteCount, error))
                    return false;
            }
#endif
        }
#if defined(GKCORE_TEST_FRAME_CAPTURE)
        else if (verifyGpuSkinning_)
            return SetError(error, "GPU skinning verification is enabled, but this frame has no GPU skinning dispatch");
#endif
        bool skipPoseUpload = poseBufferOverride && preparedPoseRanges.Count() == 0;
        for (uint32_t rangeIndex = 0; skipPoseUpload && rangeIndex < preparedSparseRanges.Count(); ++rangeIndex)
            skipPoseUpload = preparedSparseRanges.At(rangeIndex).gpuSkinning;
        if (!modelLighting_.PreparePoseVertices(frameIndex, modelPoseVertices_.Data(), modelPoseVertices_.Count(), poseBufferOverride, poseFloat4Count, skipPoseUpload, error))
            return false;
    }
    if (hasLitModelRuns && !modelLighting_.PrepareDrawConstants(frameIndex, modelDrawConstants_.Data(), modelDrawConstants_.Count(), error, modelSparseMaps_.Data()))
        return false;
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    if (verifyGpuSkinning_ && !hasLitModelRuns)
        return SetError(error, "GPU skinning verification is enabled, but this frame has no lit model draws");
#endif
    if (modelIndices_.Count())
    {
        Buffer* indexBuffer = modelIndexBuffers_[frameIndex];
        if (!indexBuffer || !indexBuffer->pCpuMappedAddress)
            return SetError(error, "The GPU model index buffer is unavailable");
        memcpy(indexBuffer->pCpuMappedAddress, modelIndices_.Data(), static_cast<size_t>(modelIndices_.Count()) * sizeof(uint32_t));
    }
    if (!textureCache_.UploadPending(error))
        return false;

    uint32_t imageIndex = 0;
    acquireNextImage(renderer_, swapChain_, imageAcquiredSemaphore_, nullptr, &imageIndex);
    if (imageIndex >= swapChain_->mImageCount)
        return SetError(error, "The Forge renderer returned an invalid swapchain image index");

    if (vertices_.Count())
    {
        BufferUpdateDesc update{};
        update.pBuffer = vertexBuffers_[commandRing_.mPoolIndex];
        update.mSize = static_cast<uint64_t>(vertices_.Count()) * sizeof(Vertex);
        beginUpdateResource(&update);
        memcpy(update.pMappedData, vertices_.Data(), static_cast<size_t>(update.mSize));
        endUpdateResource(&update);
    }

#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    if (gRenderProfile.enabled)
    {
        const int64_t phaseEnd = ReadRenderProfileCounter();
        profileTicks[3] = MeasureRenderProfileTicks(profileStart, phaseEnd);
        profileStart = phaseEnd;
    }
#endif

    Cmd* command = element.pCmds[0];
    beginCmd(command);
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    bool skinningReadbackRecorded = false;
#endif
    if (modelSkinningDispatches_.Count())
    {
        if (!modelSkinning_.SeedOutputBuffer(command, frameIndex, modelLighting_.PoseVertexBuffer(frameIndex), poseFloat4Count, error) || !modelSkinning_.Dispatch(command, frameIndex, error))
        {
            endCmd(command);
            return false;
        }
#if defined(GKCORE_TEST_FRAME_CAPTURE)
        if (verifyGpuSkinning_)
        {
            if (!test_support::RecordModelSkinningReadback(renderer_, command, modelSkinning_.OutputBuffer(frameIndex), skinningReadbackBuffers_[frameIndex], skinningReadbackByteCount, error))
            {
                endCmd(command);
                return false;
            }
            skinningReadbackRecorded = true;
        }
#endif
    }
    RenderTarget* renderTarget = swapChain_->ppRenderTargets[imageIndex];
    RenderTargetBarrier toRenderTarget{ renderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET };
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &toRenderTarget);

    BindRenderTargetsDesc bindTargets{};
    bindTargets.mRenderTargetCount = 1;
    bindTargets.mRenderTargets[0].pRenderTarget = postProcess_.SceneTarget();
    bindTargets.mRenderTargets[0].mLoadAction = LOAD_ACTION_CLEAR;
    bindTargets.mRenderTargets[0].mStoreAction = STORE_ACTION_STORE;
    bindTargets.mRenderTargets[0].mClearValue = { { 0.02f, 0.03f, 0.05f, 1.0f } };
    bindTargets.mDepthStencil.pDepthStencil = depthTarget_;
    bindTargets.mDepthStencil.mLoadAction = LOAD_ACTION_CLEAR;
    bindTargets.mDepthStencil.mStoreAction = STORE_ACTION_STORE;
    bindTargets.mDepthStencil.mClearValue.depth = 1.0f;
    cmdBindRenderTargets(command, &bindTargets);
    RenderTarget* sceneTarget = postProcess_.SceneTarget();
    cmdSetViewport(command, 0.0f, 0.0f, static_cast<float>(sceneTarget->mWidth), static_cast<float>(sceneTarget->mHeight), 0.0f, 1.0f);
    cmdSetScissor(command, 0, 0, sceneTarget->mWidth, sceneTarget->mHeight);
    if (vertices_.Count() || modelVertices_.Count() || cachedModelVertexCount)
    {
        for (uint32_t i = 0; i < runs_.Count(); ++i)
        {
            const RenderRun& run = runs_.At(i);
            if (run.layer != 0)
                continue;
            Buffer* vertexBuffer = run.litModel ? (run.cachedModelBuffer ? run.cachedModelBuffer : modelLighting_.VertexBuffer(frameIndex)) : vertexBuffers_[frameIndex];
            const uint32_t stride = run.litModel ? sizeof(ModelRenderVertex) : sizeof(Vertex);
            const uint64_t offset = 0;
            cmdBindVertexBuffer(command, 1, &vertexBuffer, &stride, &offset);
            if (run.litModel)
            {
                if (!modelLighting_.Bind(command, frameIndex, false, run.alphaBlend, error, run.modelDrawConstantIndex))
                {
                    endCmd(command);
                    return false;
                }
            }
            else if (run.customShader)
            {
                if (!customShaders_.Bind(command, run.shader, frameIndex, run.customDrawIndex, run.layer, run.depthTest, run.alphaBlend, error))
                {
                    endCmd(command);
                    return false;
                }
            }
            else
            {
                Pipeline* pipeline = run.textured ? (run.alphaBlend ? spriteAlphaPipeline_ : spritePipeline_) : (run.depthTest ? depthPipeline_ : scenePipeline_);
                cmdBindPipeline(command, pipeline);
            }
            if (run.textured)
            {
                // 内蔵モデルは2画像を同時に、その他の描画は従来の1画像を結ぶ。
                const bool bound = run.litModel ? textureCache_.BindModel(command, run.image, run.metallicRoughnessImage, error, run.normalImage, &run.baseColorSampler, &run.metallicRoughnessSampler, &run.normalSampler, run.emissiveImage, &run.emissiveSampler, run.occlusionImage, &run.occlusionSampler) : textureCache_.Bind(command, run.image, error);
                if (!bound)
                {
                    endCmd(command);
                    return false;
                }
            }
            if (run.indexedModel)
            {
                cmdBindIndexBuffer(command, modelIndexBuffers_[frameIndex], INDEX_TYPE_UINT32, 0);
                cmdDrawIndexed(command, run.count, run.first, 0);
            }
            else
                cmdDraw(command, run.count, run.first);
        }
    }
    cmdBindRenderTargets(command, nullptr);

    if (postEffectPlan.enabled && !postEffect_.Apply(command, postProcess_.SceneTarget(), customShaders_, frameIndex, postEffectPlan, error))
    {
        endCmd(command);
        return false;
    }

    RenderTarget* postProcessSource = postEffectPlan.enabled ? postEffect_.OutputTarget() : nullptr;
    if (!postProcess_.Apply(command, renderTarget, frameIndex, settings, error, postProcessSource))
    {
        endCmd(command);
        return false;
    }

    bool hasUiRuns = false;
    for (uint32_t i = 0; i < runs_.Count(); ++i)
    {
        if (runs_.At(i).layer == 1)
        {
            hasUiRuns = true;
            break;
        }
    }
    if (hasUiRuns)
    {
        BindRenderTargetsDesc uiTargets{};
        uiTargets.mRenderTargetCount = 1;
        uiTargets.mRenderTargets[0].pRenderTarget = renderTarget;
        uiTargets.mRenderTargets[0].mLoadAction = LOAD_ACTION_LOAD;
        uiTargets.mRenderTargets[0].mStoreAction = STORE_ACTION_STORE;
        cmdBindRenderTargets(command, &uiTargets);
        cmdSetViewport(command, 0.0f, 0.0f, static_cast<float>(renderTarget->mWidth), static_cast<float>(renderTarget->mHeight), 0.0f, 1.0f);
        cmdSetScissor(command, 0, 0, renderTarget->mWidth, renderTarget->mHeight);
        for (uint32_t i = 0; i < runs_.Count(); ++i)
        {
            const RenderRun& run = runs_.At(i);
            if (run.layer != 1)
                continue;
            Buffer* vertexBuffer = run.litModel ? (run.cachedModelBuffer ? run.cachedModelBuffer : modelLighting_.VertexBuffer(frameIndex)) : vertexBuffers_[frameIndex];
            const uint32_t stride = run.litModel ? sizeof(ModelRenderVertex) : sizeof(Vertex);
            const uint64_t offset = 0;
            cmdBindVertexBuffer(command, 1, &vertexBuffer, &stride, &offset);
            if (run.litModel)
            {
                if (!modelLighting_.Bind(command, frameIndex, true, run.alphaBlend, error, run.modelDrawConstantIndex))
                {
                    endCmd(command);
                    return false;
                }
            }
            else if (run.customShader)
            {
                if (!customShaders_.Bind(command, run.shader, frameIndex, run.customDrawIndex, run.layer, run.depthTest, run.alphaBlend, error))
                {
                    endCmd(command);
                    return false;
                }
            }
            else
            {
                Pipeline* pipeline = run.textured ? (run.alphaBlend ? spriteAlphaUiPipeline_ : spriteUiPipeline_) : uiPipeline_;
                cmdBindPipeline(command, pipeline);
            }
            if (run.textured)
            {
                // 内蔵モデルは2画像を同時に、その他の描画は従来の1画像を結ぶ。
                const bool bound = run.litModel ? textureCache_.BindModel(command, run.image, run.metallicRoughnessImage, error, run.normalImage, &run.baseColorSampler, &run.metallicRoughnessSampler, &run.normalSampler, run.emissiveImage, &run.emissiveSampler, run.occlusionImage, &run.occlusionSampler) : textureCache_.Bind(command, run.image, error);
                if (!bound)
                {
                    endCmd(command);
                    return false;
                }
            }
            if (run.indexedModel)
            {
                cmdBindIndexBuffer(command, modelIndexBuffers_[frameIndex], INDEX_TYPE_UINT32, 0);
                cmdDrawIndexed(command, run.count, run.first, 0);
            }
            else
                cmdDraw(command, run.count, run.first);
        }
        cmdBindRenderTargets(command, nullptr);
    }

    RenderTargetBarrier toPresent{ renderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    // 最終画面を提示遷移の前にコピーし、同じcommandへ記録する。
    String captureError;
    const bool captureRequested = testFrameCapture_.ReadRequest(captureError);
    const bool captureRecorded = captureRequested && testFrameCapture_.Record(renderer_, command, renderTarget, captureError);
#endif
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &toPresent);
    endCmd(command);

#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    if (gRenderProfile.enabled)
    {
        const int64_t phaseEnd = ReadRenderProfileCounter();
        profileTicks[4] = MeasureRenderProfileTicks(profileStart, phaseEnd);
        profileStart = phaseEnd;
    }
#endif

    FlushResourceUpdateDesc flushUpdates{};
    const bool hasTextureUploads = textureCache_.HasPendingSubmission();
    if (hasTextureUploads)
    {
        if (!textureCache_.FlushPendingUploads(flushUpdates, error))
            return false;
    }

    QueueSubmitDesc submit{};
    Semaphore* waitSemaphores[2]{};
    uint32_t waitSemaphoreCount = 0;
    if (hasTextureUploads)
        waitSemaphores[waitSemaphoreCount++] = flushUpdates.pOutSubmittedSemaphore;
    waitSemaphores[waitSemaphoreCount++] = imageAcquiredSemaphore_;
    submit.ppCmds = &command;
    submit.mCmdCount = 1;
    submit.pSignalFence = element.pFence;
    submit.ppWaitSemaphores = waitSemaphores;
    submit.mWaitSemaphoreCount = waitSemaphoreCount;
    submit.ppSignalSemaphores = &element.pSemaphore;
    submit.mSignalSemaphoreCount = 1;
    queueSubmit(graphicsQueue_, &submit);
    postEffect_.CommitFrame();
    postProcess_.CommitFrame();
    if (hasTextureUploads)
        textureCache_.MarkSubmitted();

    QueuePresentDesc present{};
    present.pSwapChain = swapChain_;
    present.mIndex = static_cast<uint8_t>(imageIndex);
    present.ppWaitSemaphores = &element.pSemaphore;
    present.mWaitSemaphoreCount = 1;
    queuePresent(graphicsQueue_, &present);
    if (modelSkinningDispatches_.Count() && !modelSkinning_.ReadErrorFlag(frameIndex, element.pFence, error))
        return false;
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    if (verifyGpuSkinning_)
    {
        test_support::FModelSkinningReadbackSummary summary{};
        const size_t allocationSize = static_cast<size_t>(skinningReadbackByteCount);
        void* readbackBytes = Allocate(allocationSize);
        bool readbackValid = skinningReadbackRecorded && readbackBytes && test_support::ReadModelSkinningReadback(renderer_, element.pFence, skinningReadbackBuffers_[frameIndex], skinningReadbackByteCount, readbackBytes, error);
        if (readbackValid)
            readbackValid = test_support::CompareModelSkinningReadback(static_cast<const float*>(readbackBytes), poseFloat4Count, modelSkinningDispatches_.Data(), modelSkinningDispatches_.Count(), modelPoseVertices_.Data(), modelPoseVertices_.Count(), 0.0005f, summary, error);
        fprintf(stdout, "{\"type\":\"gkcore_gpu_skinning_validation\",\"frame\":%u,\"dispatches\":%u,\"positions\":%llu,\"normals\":%llu,\"maxPositionAbsError\":%.9g,\"maxNormalAbsError\":%.9g,\"tolerance\":0.0005,\"valid\":%s}\n", frameIndex, modelSkinningDispatches_.Count(), static_cast<unsigned long long>(summary.positionCount), static_cast<unsigned long long>(summary.normalCount), summary.maximumPositionError, summary.maximumNormalError, readbackValid ? "true" : "false");
        fflush(stdout);
        Deallocate(readbackBytes);
        if (!readbackValid)
        {
            if (error.Empty())
                error.Assign("GPU skinning verification readback allocation failed");
            fprintf(stderr, "GPU skinning verification failed: %s\n", error.CStr());
            return false;
        }
    }
#endif
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    if (gRenderProfile.enabled)
    {
        const int64_t phaseEnd = ReadRenderProfileCounter();
        profileTicks[5] = MeasureRenderProfileTicks(profileStart, phaseEnd);
    }
#endif
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    // 画像取得に失敗しても取得済みimageとcommandを提出してから呼出元へ返す。
    if (!captureError.Empty())
    {
        error.MoveFrom(captureError);
        return false;
    }
    if (captureRequested && (!captureRecorded || !testFrameCapture_.Complete(renderer_, element.pFence, captureError)))
    {
        error.MoveFrom(captureError);
        return false;
    }
#endif
#if defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS)
    AccumulateRenderProfileFrame(profileTicks);
#endif
    error.Clear();
    return true;
}

ShaderHandle ForgeRenderer::LoadPixelShader(const char* path, String& error)
{
    if (!renderer_ || !graphicsQueue_)
    {
        error.Assign("The Forge renderer is not initialized");
        return ShaderHandle();
    }
    waitQueueIdle(graphicsQueue_);
    return customShaders_.Load(path, error);
}

bool ForgeRenderer::ReleasePixelShader(ShaderHandle shader, String& error)
{
    if (!renderer_ || !graphicsQueue_)
        return SetError(error, "The Forge renderer is not initialized");
    waitQueueIdle(graphicsQueue_);
    return customShaders_.Release(shader, error);
}

}

#endif
