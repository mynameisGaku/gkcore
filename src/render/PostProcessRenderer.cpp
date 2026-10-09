#include "render/PostProcessRenderer.h"
#include "render/PostProcessPlan.h"

#if defined(_WIN32) && defined(DIRECT3D12)
#include <float.h>
#include <math.h>
#include <Graphics/FSL/defaults.h>
#include "shaders/gkcore_postprocess.srt.h"
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>
#include <stddef.h>
#include <string.h>

namespace gk::render
{

/**
 * Direct3D 12で使う処理段階の番号とGPU定数の配置を定義する。
 */
namespace
{
/**
 * 各全面描画passに割り当てるdescriptorと定数bufferのslotを定義する。
 */
enum PassIndex : uint32_t
{
    BloomExtractPass = 0,
    BloomHorizontalPass = 1,
    BloomVerticalPass = 2,
    CompositePass = 3,
    FxaaPass = 4
};

/**
 * post-process FSLの定数bufferとbyte単位で共有する3個のfloat4を定義する。
 */
struct GpuPostProcessConstants
{
    float parameters[4];
    float colorAdjustment[4];
    float imageSize[4];
};

static_assert(sizeof(GpuPostProcessConstants) == 48, "post-process FSL constants must remain three float4 values");

bool SetPostProcessError(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

PostProcessRenderer::PostProcessRenderer() : renderer_(nullptr), outputFormat_(TinyImageFormat_UNDEFINED), sceneTarget_(nullptr), bloomTargets_{}, linearLdrTarget_(nullptr), extractShader_(nullptr), blurShader_(nullptr), compositeShader_(nullptr), fxaaShader_(nullptr), extractPipeline_(nullptr), blurPipeline_(nullptr), compositeOutputPipeline_(nullptr), compositeLinearPipeline_(nullptr), fxaaPipeline_(nullptr), sampler_(nullptr), descriptorSets_{}, constants_{}, width_(0), height_(0), bloomWidth_(0), bloomHeight_(0), targetStates_{}
{
}

PostProcessRenderer::~PostProcessRenderer()
{
    Shutdown();
}

bool PostProcessRenderer::Initialize(Renderer* renderer, uint32_t width, uint32_t height, TinyImageFormat outputFormat, String& error)
{
    if (renderer_)
        return SetPostProcessError(error, "The post-process renderer is already initialized");
    if (!renderer || !width || !height || outputFormat == TinyImageFormat_UNDEFINED)
        return SetPostProcessError(error, "The post-process initialization parameters are invalid");
    renderer_ = renderer;
    outputFormat_ = outputFormat;
    if (!CreateTargets(width, height, error) || !CreatePrograms(error) || !CreateDescriptorsAndConstants(error))
    {
        Shutdown();
        return false;
    }
    const PostProcessSettings defaults = { true, 0.15f, 1.0f, true, 1.0f, 1.0f, true };
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        if (!UpdatePassBindings(frame, defaults, nullptr, error))
        {
            Shutdown();
            return false;
        }
    }
    error.Clear();
    return true;
}

bool PostProcessRenderer::CreateTargets(uint32_t width, uint32_t height, String& error)
{
    if (!renderer_ || !width || !height)
        return SetPostProcessError(error, "The post-process target size must be positive");
    RenderTarget* newScene = nullptr;
    RenderTarget* newBloom[2] = { nullptr, nullptr };
    RenderTarget* newLinearLdr = nullptr;
    const uint32_t newBloomWidth = width / 2 + width % 2;
    const uint32_t newBloomHeight = height / 2 + height % 2;
    const auto createTarget = [&](const char* name, uint32_t targetWidth, uint32_t targetHeight, RenderTarget** output) -> bool
    {
        RenderTargetDesc desc{};
        desc.mWidth = targetWidth;
        desc.mHeight = targetHeight;
        desc.mDepth = 1;
        desc.mArraySize = 1;
        desc.mMipLevels = 1;
        desc.mSampleCount = SAMPLE_COUNT_1;
        desc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
        desc.mStartState = RESOURCE_STATE_RENDER_TARGET;
        desc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
        desc.mFlags = TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT;
        desc.mClearValue.r = 0.0f;
        desc.mClearValue.g = 0.0f;
        desc.mClearValue.b = 0.0f;
        desc.mClearValue.a = 0.0f;
        desc.pName = name;
        addRenderTarget(renderer_, &desc, output);
        return *output != nullptr;
    };
    if (!createTarget("gkcore HDR Scene", width, height, &newScene) || !createTarget("gkcore Bloom Extract", newBloomWidth, newBloomHeight, &newBloom[0]) || !createTarget("gkcore Bloom Blur", newBloomWidth, newBloomHeight, &newBloom[1]) || !createTarget("gkcore Linear LDR FXAA Input", width, height, &newLinearLdr))
    {
        if (newLinearLdr)
            removeRenderTarget(renderer_, newLinearLdr);
        if (newBloom[1])
            removeRenderTarget(renderer_, newBloom[1]);
        if (newBloom[0])
            removeRenderTarget(renderer_, newBloom[0]);
        if (newScene)
            removeRenderTarget(renderer_, newScene);
        return SetPostProcessError(error, "The Forge could not create the HDR post-process targets");
    }
    if (bloomTargets_[1])
        removeRenderTarget(renderer_, bloomTargets_[1]);
    if (bloomTargets_[0])
        removeRenderTarget(renderer_, bloomTargets_[0]);
    if (linearLdrTarget_)
        removeRenderTarget(renderer_, linearLdrTarget_);
    if (sceneTarget_)
        removeRenderTarget(renderer_, sceneTarget_);
    sceneTarget_ = newScene;
    bloomTargets_[0] = newBloom[0];
    bloomTargets_[1] = newBloom[1];
    linearLdrTarget_ = newLinearLdr;
    width_ = width;
    height_ = height;
    bloomWidth_ = newBloomWidth;
    bloomHeight_ = newBloomHeight;
    targetStates_.Reset();
    error.Clear();
    return true;
}

bool PostProcessRenderer::CreatePrograms(String& error)
{
    SamplerDesc samplerDesc{};
    samplerDesc.mMinFilter = FILTER_LINEAR;
    samplerDesc.mMagFilter = FILTER_LINEAR;
    samplerDesc.mMipMapMode = MIPMAP_MODE_LINEAR;
    samplerDesc.mAddressU = ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.mAddressV = ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.mAddressW = ADDRESS_MODE_CLAMP_TO_EDGE;
    addSampler(renderer_, &samplerDesc, &sampler_);
    if (!sampler_)
        return SetPostProcessError(error, "The Forge could not create the post-process sampler");

    ShaderLoadDesc shaderDesc{};
    shaderDesc.mVert.pFileName = "gkcore_post.vert";
    shaderDesc.mFrag.pFileName = "gkcore_bloom_extract.frag";
    addShader(renderer_, &shaderDesc, &extractShader_);
    shaderDesc.mFrag.pFileName = "gkcore_bloom_blur.frag";
    addShader(renderer_, &shaderDesc, &blurShader_);
    shaderDesc.mFrag.pFileName = "gkcore_post_composite.frag";
    addShader(renderer_, &shaderDesc, &compositeShader_);
    shaderDesc.mFrag.pFileName = "gkcore_fxaa.frag";
    addShader(renderer_, &shaderDesc, &fxaaShader_);
    waitForAllResourceLoads();
    if (!extractShader_ || !blurShader_ || !compositeShader_ || !fxaaShader_)
        return SetPostProcessError(error, "The Forge could not load the post-process shader programs");

    RasterizerStateDesc rasterizer{};
    rasterizer.mCullMode = CULL_MODE_NONE;
    rasterizer.mFillMode = FILL_MODE_SOLID;
    DepthStateDesc noDepth{};
    noDepth.mDepthTest = false;
    noDepth.mDepthWrite = false;
    noDepth.mDepthFunc = CMP_ALWAYS;
    const auto createPipeline = [&](const char* name, Shader* shader, TinyImageFormat colorFormat, Pipeline** output)
    {
        PipelineDesc desc{};
        desc.mType = PIPELINE_TYPE_GRAPHICS;
        GraphicsPipelineDesc& graphics = desc.mGraphicsDesc;
        graphics.pShaderProgram = shader;
        graphics.pBlendState = nullptr;
        graphics.pDepthState = &noDepth;
        graphics.pRasterizerState = &rasterizer;
        graphics.pColorFormats = &colorFormat;
        graphics.mRenderTargetCount = 1;
        graphics.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
        graphics.mSampleCount = SAMPLE_COUNT_1;
        graphics.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
        desc.pName = name;
        addPipeline(renderer_, &desc, output);
    };
    const TinyImageFormat hdrFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
    createPipeline("gkcore Bloom Extract Pipeline", extractShader_, hdrFormat, &extractPipeline_);
    createPipeline("gkcore Bloom Blur Pipeline", blurShader_, hdrFormat, &blurPipeline_);
    createPipeline("gkcore Graded Linear Composite Pipeline", compositeShader_, hdrFormat, &compositeLinearPipeline_);
    createPipeline("gkcore Direct Post Composite Pipeline", compositeShader_, outputFormat_, &compositeOutputPipeline_);
    createPipeline("gkcore FXAA Pipeline", fxaaShader_, outputFormat_, &fxaaPipeline_);
    if (!extractPipeline_ || !blurPipeline_ || !compositeLinearPipeline_ || !compositeOutputPipeline_ || !fxaaPipeline_)
        return SetPostProcessError(error, "The Forge could not create the post-process pipelines");
    error.Clear();
    return true;
}

bool PostProcessRenderer::CreateDescriptorsAndConstants(String& error)
{
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        for (uint32_t pass = 0; pass < kPassCount; ++pass)
        {
            BufferLoadDesc desc{};
            desc.mDesc.mSize = 256;
            desc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
            desc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
            desc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            desc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            desc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
            desc.mDesc.pName = "gkcore Post Process Constants";
            desc.ppBuffer = &constants_[frame][pass];
            SyncToken token = 0;
            addResource(&desc, &token);
            if (token)
                waitForToken(&token);
            if (!constants_[frame][pass] || !constants_[frame][pass]->pCpuMappedAddress)
                return SetPostProcessError(error, "The Forge could not allocate post-process constants");
        }
    }
    const DescriptorSetDesc desc = SRT_SET_DESC(PostProcessResources, Persistent, kFramesInFlight, 0);
    for (uint32_t pass = 0; pass < kPassCount; ++pass)
    {
        addDescriptorSet(renderer_, &desc, &descriptorSets_[pass]);
        if (!descriptorSets_[pass])
            return SetPostProcessError(error, "The Forge could not allocate post-process descriptor sets");
    }
    error.Clear();
    return true;
}

bool PostProcessRenderer::UpdatePassBindings(uint32_t frameIndex, const PostProcessSettings& settings, RenderTarget* sceneSource, String& error)
{
    if (frameIndex >= kFramesInFlight || !IsPostProcessSettingsValid(settings))
        return SetPostProcessError(error, "The post-process frame settings are invalid");
    if (!sceneSource)
        sceneSource = sceneTarget_;
    Texture* scene = sceneSource ? sceneSource->pTexture : nullptr;
    Texture* bloom0 = bloomTargets_[0] ? bloomTargets_[0]->pTexture : nullptr;
    Texture* bloom1 = bloomTargets_[1] ? bloomTargets_[1]->pTexture : nullptr;
    Texture* linearLdr = linearLdrTarget_ ? linearLdrTarget_->pTexture : nullptr;
    if (!scene || !bloom0 || !bloom1 || !linearLdr || !sampler_)
        return SetPostProcessError(error, "The post-process textures are not initialized");
    const float horizontal = 1.0f / static_cast<float>(bloomWidth_);
    const float vertical = 1.0f / static_cast<float>(bloomHeight_);
    const GpuPostProcessConstants parameters[kPassCount] = { { { kBloomKnee, kBloomThreshold, 0.0f, 0.0f }, {}, { 1.0f / width_, 1.0f / height_, 0.0f, 0.0f } }, { { horizontal, 0.0f, 0.0f, 0.0f }, {}, { 1.0f / width_, 1.0f / height_, 0.0f, 0.0f } }, { { 0.0f, vertical, 0.0f, 0.0f }, {}, { 1.0f / width_, 1.0f / height_, 0.0f, 0.0f } }, { { settings.bloomEnabled ? settings.bloomIntensity : 0.0f, settings.exposure, settings.toneMappingEnabled ? 1.0f : 0.0f, 0.0f }, { settings.saturation, settings.contrast, 0.0f, 0.0f }, { 1.0f / width_, 1.0f / height_, 0.0f, 0.0f } }, { { 0.0f, 0.0f, 0.0f, 0.0f }, {}, { 1.0f / width_, 1.0f / height_, 0.0f, 0.0f } } };
    Texture* sources[kPassCount] = { scene, bloom0, bloom1, scene, linearLdr };
    Texture* blooms[kPassCount] = { scene, scene, scene, settings.bloomEnabled ? bloom0 : scene, linearLdr };
    for (uint32_t pass = 0; pass < kPassCount; ++pass)
    {
        if (!constants_[frameIndex][pass] || !constants_[frameIndex][pass]->pCpuMappedAddress || !descriptorSets_[pass])
            return SetPostProcessError(error, "The post-process pass resources are unavailable");
        memcpy(constants_[frameIndex][pass]->pCpuMappedAddress, &parameters[pass], sizeof(parameters[pass]));
        // shader配列の0番へsource、1番へbloomを割り当てる。
        Texture* textures[2] = { sources[pass], blooms[pass] };
        DescriptorData data[3]{};
        data[0].mIndex = SRT_RES_IDX(PostProcessResources, Persistent, gPostTextures);
        data[0].mCount = 2;
        data[0].ppTextures = textures;
        data[1].mIndex = SRT_RES_IDX(PostProcessResources, Persistent, gPostSampler);
        data[1].mCount = 1;
        data[1].ppSamplers = &sampler_;
        data[2].mIndex = SRT_RES_IDX(PostProcessResources, Persistent, gPostConstants);
        data[2].mCount = 1;
        data[2].ppBuffers = &constants_[frameIndex][pass];
        updateDescriptorSet(renderer_, frameIndex, descriptorSets_[pass], 3, data);
    }
    error.Clear();
    return true;
}

void PostProcessRenderer::Transition(Cmd* command, RenderTarget* target, ResourceState before, ResourceState after)
{
    RenderTargetBarrier barrier{ target, before, after };
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &barrier);
}

void PostProcessRenderer::DrawFullscreen(Cmd* command, RenderTarget* target, Pipeline* pipeline, DescriptorSet* descriptorSet, uint32_t frameIndex)
{
    BindRenderTargetsDesc bind{};
    bind.mRenderTargetCount = 1;
    bind.mRenderTargets[0].pRenderTarget = target;
    bind.mRenderTargets[0].mLoadAction = LOAD_ACTION_CLEAR;
    bind.mRenderTargets[0].mStoreAction = STORE_ACTION_STORE;
    bind.mRenderTargets[0].mClearValue.r = 0.0f;
    bind.mRenderTargets[0].mClearValue.g = 0.0f;
    bind.mRenderTargets[0].mClearValue.b = 0.0f;
    bind.mRenderTargets[0].mClearValue.a = 1.0f;
    cmdBindRenderTargets(command, &bind);
    cmdSetViewport(command, 0.0f, 0.0f, static_cast<float>(target->mWidth), static_cast<float>(target->mHeight), 0.0f, 1.0f);
    cmdSetScissor(command, 0, 0, target->mWidth, target->mHeight);
    cmdBindPipeline(command, pipeline);
    cmdBindDescriptorSet(command, frameIndex, descriptorSet);
    cmdDraw(command, 3, 0);
    cmdBindRenderTargets(command, nullptr);
}

bool PostProcessRenderer::Apply(Cmd* command, RenderTarget* destination, uint32_t frameIndex, const PostProcessSettings& settings, String& error, RenderTarget* sceneOverride)
{
    if (!renderer_ || !command || !destination || !sceneTarget_)
        return SetPostProcessError(error, "The post-process command or target is unavailable");
    if (destination->mFormat != outputFormat_ || destination->mWidth != width_ || destination->mHeight != height_)
        return SetPostProcessError(error, "The post-process destination does not match its pipeline format and size");
    RenderTarget* sceneSource = sceneOverride ? sceneOverride : sceneTarget_;
    if (!sceneSource || sceneSource == destination || sceneSource->mFormat != TinyImageFormat_R16G16B16A16_SFLOAT || sceneSource->mWidth != width_ || sceneSource->mHeight != height_ || !sceneSource->pTexture)
        return SetPostProcessError(error, "The post-process scene source must be a distinct full-size RGBA16F target");
    if (sceneOverride && (sceneOverride == sceneTarget_ || sceneOverride == bloomTargets_[0] || sceneOverride == bloomTargets_[1] || sceneOverride == linearLdrTarget_))
        return SetPostProcessError(error, "The post-process override must not alias an internal render target");
    PostProcessPlan plan{};
    if (!BuildPostProcessPlan(settings, plan))
        return SetPostProcessError(error, "The post-process plan could not be built from frame settings");
    if (!UpdatePassBindings(frameIndex, settings, sceneSource, error))
        return false;

    targetStates_.DiscardPending();
    PostProcessTargetStates recordedStates = targetStates_.Committed();
    if (!sceneOverride)
        Transition(command, sceneTarget_, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    for (uint32_t i = 0; i < plan.count; ++i)
    {
        const PostProcessStep& step = plan.steps[i];
        switch (step.kind)
        {
        case PostProcessStepKind::BloomExtract:
            if (recordedStates.bloomShaderReadable[0])
                Transition(command, bloomTargets_[0], RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET);
            DrawFullscreen(command, bloomTargets_[0], extractPipeline_, descriptorSets_[BloomExtractPass], frameIndex);
            Transition(command, bloomTargets_[0], RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            recordedStates.bloomShaderReadable[0] = true;
            break;
        case PostProcessStepKind::BloomBlurHorizontal:
            if (recordedStates.bloomShaderReadable[1])
                Transition(command, bloomTargets_[1], RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET);
            DrawFullscreen(command, bloomTargets_[1], blurPipeline_, descriptorSets_[BloomHorizontalPass], frameIndex);
            Transition(command, bloomTargets_[1], RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            recordedStates.bloomShaderReadable[1] = true;
            break;
        case PostProcessStepKind::BloomBlurVertical:
            if (recordedStates.bloomShaderReadable[0])
                Transition(command, bloomTargets_[0], RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET);
            DrawFullscreen(command, bloomTargets_[0], blurPipeline_, descriptorSets_[BloomVerticalPass], frameIndex);
            Transition(command, bloomTargets_[0], RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            recordedStates.bloomShaderReadable[0] = true;
            break;
        case PostProcessStepKind::Composite:
            if (step.target == PostProcessTarget::LinearLdr)
            {
                if (recordedStates.linearLdrShaderReadable)
                    Transition(command, linearLdrTarget_, RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET);
                DrawFullscreen(command, linearLdrTarget_, compositeLinearPipeline_, descriptorSets_[CompositePass], frameIndex);
                Transition(command, linearLdrTarget_, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                recordedStates.linearLdrShaderReadable = true;
            }
            else
            {
                DrawFullscreen(command, destination, compositeOutputPipeline_, descriptorSets_[CompositePass], frameIndex);
            }
            break;
        case PostProcessStepKind::Fxaa:
            DrawFullscreen(command, destination, fxaaPipeline_, descriptorSets_[FxaaPass], frameIndex);
            break;
        default:
            return SetPostProcessError(error, "The post-process plan contains an unknown pass");
        }
    }
    if (!sceneOverride)
        Transition(command, sceneTarget_, RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET);
    targetStates_.Stage(recordedStates);
    error.Clear();
    return true;
}

void PostProcessRenderer::CommitFrame()
{
    targetStates_.Commit();
}

bool PostProcessRenderer::Resize(uint32_t width, uint32_t height, String& error)
{
    if (!renderer_ || !width || !height)
        return SetPostProcessError(error, "The post-process target size must be positive");
    if (width == width_ && height == height_)
    {
        error.Clear();
        return true;
    }
    if (!CreateTargets(width, height, error))
        return false;
    const PostProcessSettings defaults = { true, 0.15f, 1.0f, true, 1.0f, 1.0f, true };
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        if (!UpdatePassBindings(frame, defaults, nullptr, error))
            return false;
    }
    error.Clear();
    return true;
}

void PostProcessRenderer::Shutdown()
{
    if (renderer_)
    {
        for (uint32_t i = 0; i < kPassCount; ++i)
        {
            if (descriptorSets_[i])
            {
                removeDescriptorSet(renderer_, descriptorSets_[i]);
                descriptorSets_[i] = nullptr;
            }
        }
        for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
        {
            for (uint32_t pass = 0; pass < kPassCount; ++pass)
            {
                if (constants_[frame][pass])
                {
                    removeResource(constants_[frame][pass]);
                    constants_[frame][pass] = nullptr;
                }
            }
        }
        for (uint32_t i = 0; i < 2; ++i)
        {
            if (bloomTargets_[i])
            {
                removeRenderTarget(renderer_, bloomTargets_[i]);
                bloomTargets_[i] = nullptr;
            }
        }
        if (linearLdrTarget_)
        {
            removeRenderTarget(renderer_, linearLdrTarget_);
            linearLdrTarget_ = nullptr;
        }
        if (sceneTarget_)
        {
            removeRenderTarget(renderer_, sceneTarget_);
            sceneTarget_ = nullptr;
        }
        if (fxaaPipeline_)
        {
            removePipeline(renderer_, fxaaPipeline_);
            fxaaPipeline_ = nullptr;
        }
        if (compositeLinearPipeline_)
        {
            removePipeline(renderer_, compositeLinearPipeline_);
            compositeLinearPipeline_ = nullptr;
        }
        if (compositeOutputPipeline_)
        {
            removePipeline(renderer_, compositeOutputPipeline_);
            compositeOutputPipeline_ = nullptr;
        }
        if (blurPipeline_)
        {
            removePipeline(renderer_, blurPipeline_);
            blurPipeline_ = nullptr;
        }
        if (extractPipeline_)
        {
            removePipeline(renderer_, extractPipeline_);
            extractPipeline_ = nullptr;
        }
        if (compositeShader_)
        {
            removeShader(renderer_, compositeShader_);
            compositeShader_ = nullptr;
        }
        if (fxaaShader_)
        {
            removeShader(renderer_, fxaaShader_);
            fxaaShader_ = nullptr;
        }
        if (blurShader_)
        {
            removeShader(renderer_, blurShader_);
            blurShader_ = nullptr;
        }
        if (extractShader_)
        {
            removeShader(renderer_, extractShader_);
            extractShader_ = nullptr;
        }
        if (sampler_)
        {
            removeSampler(renderer_, sampler_);
            sampler_ = nullptr;
        }
    }
    renderer_ = nullptr;
    outputFormat_ = TinyImageFormat_UNDEFINED;
    width_ = height_ = bloomWidth_ = bloomHeight_ = 0;
    targetStates_.Reset();
}

RenderTarget* PostProcessRenderer::SceneTarget() const
{
    return sceneTarget_;
}
} // 名前空間gk::renderの終端。
#endif
