#include "ForgeRenderer.h"
#include "../resources/TextureSampler.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include "../image/Image.h"
#include "../resources/Resources.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace gk::render
{
namespace
{

bool SetError(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

ForgeRenderer::~ForgeRenderer()
{
    Shutdown();
}

bool ForgeRenderer::Initialize(HWND window, uint32_t width, uint32_t height, String& error)
{
    if (renderer_)
        return SetError(error, "The Forge renderer is already initialized");
    windowHandle_ = {};
    windowHandle_.type = WINDOW_HANDLE_TYPE_WIN32;
    windowHandle_.window = window;

    RendererDesc rendererDesc{};
    rendererDesc.mDx.mFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    rendererDesc.mShaderTarget = SHADER_TARGET_5_1;
    rendererDesc.mGpuMode = GPU_MODE_SINGLE;
    initRenderer("gkcore", &rendererDesc, &renderer_);
    if (!renderer_)
        return SetError(error, "The Forge renderer failed to initialize Direct3D 12");
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
    if (!InitializeGraphicsResources(error))
    {
        Shutdown();
        return false;
    }
    error.Clear();
    return true;
}

void ForgeRenderer::Shutdown()
{
    if (graphicsQueue_)
        waitQueueIdle(graphicsQueue_);
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    testFrameCapture_.Reset();
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
        exitRenderer(renderer_);
        renderer_ = nullptr;
    }
    width_ = height_ = 0;
    vertices_.Clear();
    modelVertices_.Clear();
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
    const PostProcessSettings settings = { frame.bloomEnabled, frame.bloomIntensity, frame.exposure, frame.toneMappingEnabled, frame.saturation, frame.contrast, frame.fxaaEnabled };
    if (!IsPostProcessSettingsValid(settings))
        return SetError(error, "The frame post-process settings are invalid");

    textureCache_.BeginFrame();
    if (!textureCache_.DrainPendingUploads(error))
        return false;
    vertices_.Clear();
    modelVertices_.Clear();
    runs_.Clear();
    customDraws_.Clear();
    uint32_t customDrawCount = 0;
    auto appendRun = [&](uint32_t first, uint32_t count, bool depthTest, bool textured, bool alphaBlend, detail::ImageResource* image, const detail::DrawPacket& draw, bool customShader, bool litModel, uint32_t customDrawIndex, detail::ImageResource* metallicRoughnessImage = nullptr, detail::ImageResource* normalImage = nullptr, const detail::FTextureSampler* baseSampler = nullptr, const detail::FTextureSampler* materialSampler = nullptr, const detail::FTextureSampler* surfaceNormalSampler = nullptr, detail::ImageResource* emissiveImage = nullptr, const detail::FTextureSampler* emissionSampler = nullptr) -> bool
    {
        if (count == 0)
            return true;
        // 各画像のsamplerを値で保持し、描画登録元の寿命から切り離す。
        const detail::FTextureSampler baseValue = baseSampler ? *baseSampler : detail::FTextureSampler{};
        const detail::FTextureSampler materialValue = materialSampler ? *materialSampler : detail::FTextureSampler{};
        const detail::FTextureSampler normalValue = surfaceNormalSampler ? *surfaceNormalSampler : detail::FTextureSampler{};
        // 自己発光画像も独立したsampler値で比較する。
        const detail::FTextureSampler emissiveValue = emissionSampler ? *emissionSampler : detail::FTextureSampler{};
        const bool canBatch = !customShader && runs_.Count() && !runs_.At(runs_.Count() - 1).customShader && runs_.At(runs_.Count() - 1).first + runs_.At(runs_.Count() - 1).count == first && runs_.At(runs_.Count() - 1).depthTest == depthTest && runs_.At(runs_.Count() - 1).textured == textured && runs_.At(runs_.Count() - 1).alphaBlend == alphaBlend && runs_.At(runs_.Count() - 1).litModel == litModel && runs_.At(runs_.Count() - 1).layer == draw.layer && runs_.At(runs_.Count() - 1).image == image && runs_.At(runs_.Count() - 1).metallicRoughnessImage == metallicRoughnessImage && runs_.At(runs_.Count() - 1).normalImage == normalImage && runs_.At(runs_.Count() - 1).emissiveImage == emissiveImage && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).baseColorSampler, baseValue) && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).metallicRoughnessSampler, materialValue) && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).normalSampler, normalValue) && detail::AreTextureSamplersEqual(runs_.At(runs_.Count() - 1).emissiveSampler, emissiveValue);
        if (canBatch)
        {
            runs_.At(runs_.Count() - 1).count += count;
            return true;
        }
        const RenderRun run{ first, count, depthTest, textured, alphaBlend, draw.layer, image, draw.shader, customDrawIndex, customShader, litModel, metallicRoughnessImage, normalImage, baseValue, materialValue, normalValue, emissiveImage, emissiveValue };
        return runs_.Append(run);
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
                ModelDrawPlan plan;
                if (!BuildModelDrawPlan(*draw.model, plan, error))
                    return false;
                if (plan.parts.Count() == 0)
                    continue;
                // 独自ピクセルシェーダーの入力には材質の切り抜き条件がないため、無視して描かない。
                if (customShader)
                {
                    for (uint32_t partIndex = 0; partIndex < plan.parts.Count(); ++partIndex)
                    {
                        if (plan.parts.At(partIndex).alphaMask)
                        {
                            return SetError(error, "custom pixel shaders do not support masked model materials");
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
                for (uint32_t partIndex = 0; partIndex < plan.parts.Count(); ++partIndex)
                {
                    const ModelPartPlan& part = plan.parts.At(partIndex);
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
                    if (needsTexture)
                    {
                        if (!image)
                            return SetError(error, "The model texture fallback is unavailable");
                        if (litModel)
                        {
                            if (!metallicRoughnessImage || !normalImage || !emissiveImage || !textureCache_.PrepareModel(image, metallicRoughnessImage, error, normalImage, &part.baseColorSampler, &part.metallicRoughnessSampler, &part.normalSampler, emissiveImage, &part.emissiveSampler))
                                return false;
                        }
                        else if (!textureCache_.Prepare(image, error))
                            return false;
                    }
                    const bool alphaBlend = customShader && (draw.flags & detail::DrawAlphaBlend) != 0;
                    const bool depthTest = layer == 0;
                    if (!appendRun(first, count, depthTest, needsTexture, alphaBlend, image, draw, customShader, litModel, customDrawIndex, metallicRoughnessImage, normalImage, litModel ? &part.baseColorSampler : nullptr, litModel ? &part.metallicRoughnessSampler : nullptr, litModel ? &part.normalSampler : nullptr, emissiveImage, litModel ? &part.emissiveSampler : nullptr))
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
    if (vertices_.Count() > kVertexCapacity || modelVertices_.Count() > kVertexCapacity - vertices_.Count())
        return SetError(error, "The frame exceeds the dynamic vertex capacity");

    GpuCmdRingElement element = getNextGpuCmdRingElement(&commandRing_, true, 1);
    if (!element.pCmdPool || !element.pFence || !element.pSemaphore)
        return SetError(error, "The Forge command ring is not ready");
    FenceStatus fenceStatus = FENCE_STATUS_COMPLETE;
    getFenceStatus(renderer_, element.pFence, &fenceStatus);
    if (fenceStatus == FENCE_STATUS_INCOMPLETE)
        waitForFences(renderer_, 1, &element.pFence);
    resetCmdPool(renderer_, element.pCmdPool);

    const uint32_t frameIndex = commandRing_.mPoolIndex;
    if (!customShaders_.PrepareFrame(frameIndex, customDraws_.Data(), customDraws_.Count(), error))
        return false;
    if (modelVertices_.Count() && !modelLighting_.PrepareFrame(frameIndex, modelVertices_.Data(), modelVertices_.Count(), frame.lighting, error))
        return false;
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

    Cmd* command = element.pCmds[0];
    beginCmd(command);
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
    if (vertices_.Count() || modelVertices_.Count())
    {
        for (uint32_t i = 0; i < runs_.Count(); ++i)
        {
            const RenderRun& run = runs_.At(i);
            if (run.layer != 0)
                continue;
            Buffer* vertexBuffer = run.litModel ? modelLighting_.VertexBuffer(frameIndex) : vertexBuffers_[frameIndex];
            const uint32_t stride = run.litModel ? sizeof(ModelRenderVertex) : sizeof(Vertex);
            const uint64_t offset = 0;
            cmdBindVertexBuffer(command, 1, &vertexBuffer, &stride, &offset);
            if (run.litModel)
            {
                if (!modelLighting_.Bind(command, frameIndex, false, error))
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
                const bool bound = run.litModel ? textureCache_.BindModel(command, run.image, run.metallicRoughnessImage, error, run.normalImage, &run.baseColorSampler, &run.metallicRoughnessSampler, &run.normalSampler, run.emissiveImage, &run.emissiveSampler) : textureCache_.Bind(command, run.image, error);
                if (!bound)
                {
                    endCmd(command);
                    return false;
                }
            }
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
            Buffer* vertexBuffer = run.litModel ? modelLighting_.VertexBuffer(frameIndex) : vertexBuffers_[frameIndex];
            const uint32_t stride = run.litModel ? sizeof(ModelRenderVertex) : sizeof(Vertex);
            const uint64_t offset = 0;
            cmdBindVertexBuffer(command, 1, &vertexBuffer, &stride, &offset);
            if (run.litModel)
            {
                if (!modelLighting_.Bind(command, frameIndex, true, error))
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
                const bool bound = run.litModel ? textureCache_.BindModel(command, run.image, run.metallicRoughnessImage, error, run.normalImage, &run.baseColorSampler, &run.metallicRoughnessSampler, &run.normalSampler, run.emissiveImage, &run.emissiveSampler) : textureCache_.Bind(command, run.image, error);
                if (!bound)
                {
                    endCmd(command);
                    return false;
                }
            }
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
