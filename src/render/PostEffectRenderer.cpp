#include "render/PostEffectRenderer.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/FSL/defaults.h>
#include "shaders/gkcore_sprite.srt.h"
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <string.h>

/**
 * Direct3D 12 resource setup and command recording for the custom post-effect pass.
 */
namespace gk::render
{

/**
 * Local failure helper used by resource creation and command validation.
 */
namespace
{
/**
 * Assigns a post-effect failure reason and returns false to simplify cleanup branches.
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}
}

PostEffectRenderer::PostEffectRenderer() = default;

PostEffectRenderer::~PostEffectRenderer()
{
    Shutdown();
}

bool PostEffectRenderer::CreateTarget(uint32_t width, uint32_t height, RenderTarget** output, String& error)
{
    if (!renderer_ || !output || !width || !height)
        return Fail(error, "The post-effect target dimensions or renderer are invalid");
    RenderTargetDesc desc{};
    desc.mWidth = width;
    desc.mHeight = height;
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
    desc.pName = "gkcore Custom Post Effect Output";
    addRenderTarget(renderer_, &desc, output);
    if (!*output)
        return Fail(error, "The Forge could not create the custom post-effect target");
    error.Clear();
    return true;
}

bool PostEffectRenderer::Initialize(Renderer* renderer, uint32_t width, uint32_t height, String& error)
{
    if (renderer_ || !renderer || !width || !height)
        return Fail(error, "The custom post-effect initialization parameters are invalid");
    renderer_ = renderer;

    if (!CreateTarget(width, height, &outputTarget_, error))
    {
        Shutdown();
        return false;
    }

    SamplerDesc samplerDesc{};
    samplerDesc.mMinFilter = FILTER_LINEAR;
    samplerDesc.mMagFilter = FILTER_LINEAR;
    samplerDesc.mMipMapMode = MIPMAP_MODE_LINEAR;
    samplerDesc.mAddressU = ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.mAddressV = ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.mAddressW = ADDRESS_MODE_CLAMP_TO_EDGE;
    addSampler(renderer_, &samplerDesc, &sampler_);
    if (!sampler_)
    {
        Shutdown();
        return Fail(error, "The Forge could not create the custom post-effect sampler");
    }

    const DescriptorSetDesc descriptorDesc = SRT_SET_DESC(SpriteResources, Persistent, kCustomShaderFrameCount, 0);
    addDescriptorSet(renderer_, &descriptorDesc, &descriptorSet_);
    if (!descriptorSet_)
    {
        Shutdown();
        return Fail(error, "The Forge could not create custom post-effect descriptors");
    }

    Vertex vertices[3]{};
    MakePostEffectVertices(vertices);
    BufferLoadDesc bufferDesc{};
    bufferDesc.mDesc.mSize = sizeof(vertices);
    bufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    bufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    bufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
    bufferDesc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    bufferDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
    bufferDesc.mDesc.pName = "gkcore Custom Post Effect Fullscreen Triangle";
    bufferDesc.ppBuffer = &vertexBuffer_;
    SyncToken vertexBufferToken = 0;
    addResource(&bufferDesc, &vertexBufferToken);
    if (vertexBufferToken)
        waitForToken(&vertexBufferToken);
    if (!vertexBuffer_)
    {
        Shutdown();
        return Fail(error, "The Forge could not create the custom post-effect vertex buffer");
    }
    if (!vertexBuffer_->pCpuMappedAddress)
    {
        Shutdown();
        return Fail(error, "The Forge did not map the custom post-effect vertex buffer");
    }
    memcpy(vertexBuffer_->pCpuMappedAddress, vertices, sizeof(vertices));

    width_ = width;
    height_ = height;
    outputState_.Reset();
    error.Clear();
    return true;
}

bool PostEffectRenderer::Resize(uint32_t width, uint32_t height, String& error)
{
    if (!renderer_ || !width || !height)
        return Fail(error, "The custom post-effect resize dimensions are invalid");
    if (width == width_ && height == height_)
    {
        error.Clear();
        return true;
    }
    RenderTarget* replacement = nullptr;
    if (!CreateTarget(width, height, &replacement, error))
        return false;
    if (outputTarget_)
        removeRenderTarget(renderer_, outputTarget_);
    outputTarget_ = replacement;
    width_ = width;
    height_ = height;
    outputState_.Reset();
    error.Clear();
    return true;
}

bool PostEffectRenderer::Apply(Cmd* command, RenderTarget* scene, CustomShaders& shaders, uint32_t frameIndex, const PostEffectPlan& plan, String& error)
{
    if (!renderer_ || !command || !scene || !scene->pTexture || !outputTarget_ || !outputTarget_->pTexture || !vertexBuffer_ || !sampler_ || frameIndex >= kCustomShaderFrameCount || scene == outputTarget_ || scene->mWidth != width_ || scene->mHeight != height_ || scene->mFormat != TinyImageFormat_R16G16B16A16_SFLOAT || outputTarget_->mWidth != width_ || outputTarget_->mHeight != height_ || outputTarget_->mFormat != TinyImageFormat_R16G16B16A16_SFLOAT || !plan.enabled || !plan.shader.IsValid() || plan.customDrawIndex >= plan.preparedDrawCount)
        return Fail(error, "The custom post-effect pass received invalid resources or a disabled plan");
    if (!descriptorSet_)
        return Fail(error, "The custom post-effect frame descriptor set is unavailable");
    if (outputState_.HasPending())
        return Fail(error, "The custom post-effect output has an unsubmitted state transition");

    Texture* source = scene->pTexture;
    DescriptorData descriptors[2]{};
    descriptors[0].mIndex = SRT_RES_IDX(SpriteResources, Persistent, gImageTexture);
    descriptors[0].mCount = 1;
    descriptors[0].ppTextures = &source;
    descriptors[1].mIndex = SRT_RES_IDX(SpriteResources, Persistent, gImageSampler);
    descriptors[1].mCount = 1;
    descriptors[1].ppSamplers = &sampler_;
    updateDescriptorSet(renderer_, frameIndex, descriptorSet_, 2, descriptors);

    const ResourceState outputBefore = outputState_.ShaderReadable() ? RESOURCE_STATE_PIXEL_SHADER_RESOURCE : RESOURCE_STATE_RENDER_TARGET;
    if (outputBefore != RESOURCE_STATE_RENDER_TARGET)
    {
        RenderTargetBarrier toTarget{ outputTarget_, outputBefore, RESOURCE_STATE_RENDER_TARGET };
        cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &toTarget);
    }
    RenderTargetBarrier sceneToRead{ scene, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &sceneToRead);

    BindRenderTargetsDesc bindTargets{};
    bindTargets.mRenderTargetCount = 1;
    bindTargets.mRenderTargets[0].pRenderTarget = outputTarget_;
    bindTargets.mRenderTargets[0].mLoadAction = LOAD_ACTION_DONTCARE;
    bindTargets.mRenderTargets[0].mStoreAction = STORE_ACTION_STORE;
    cmdBindRenderTargets(command, &bindTargets);
    cmdSetViewport(command, 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f);
    cmdSetScissor(command, 0, 0, width_, height_);

    if (!shaders.BindPostEffect(command, plan.shader, frameIndex, plan.customDrawIndex, error))
    {
        cmdBindRenderTargets(command, nullptr);
        return false;
    }
    cmdBindDescriptorSet(command, frameIndex, descriptorSet_);
    const uint32_t stride = sizeof(Vertex);
    const uint64_t offset = 0;
    cmdBindVertexBuffer(command, 1, &vertexBuffer_, &stride, &offset);
    cmdDraw(command, 3, 0);
    cmdBindRenderTargets(command, nullptr);

    RenderTargetBarrier outputToRead{ outputTarget_, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    RenderTargetBarrier sceneToTarget{ scene, RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET };
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &outputToRead);
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &sceneToTarget);
    if (!outputState_.Stage(true))
        return Fail(error, "The custom post-effect output state is already staged for this frame");
    error.Clear();
    return true;
}

void PostEffectRenderer::DiscardPendingFrame()
{
    outputState_.DiscardPending();
}

void PostEffectRenderer::CommitFrame()
{
    outputState_.Commit();
}

void PostEffectRenderer::Shutdown()
{
    if (renderer_)
    {
        if (descriptorSet_)
            removeDescriptorSet(renderer_, descriptorSet_);
        descriptorSet_ = nullptr;
        if (sampler_)
            removeSampler(renderer_, sampler_);
        sampler_ = nullptr;
        if (vertexBuffer_)
            removeResource(vertexBuffer_);
        vertexBuffer_ = nullptr;
        if (outputTarget_)
            removeRenderTarget(renderer_, outputTarget_);
        outputTarget_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
    renderer_ = nullptr;
    outputState_.Reset();
}

RenderTarget* PostEffectRenderer::OutputTarget() const
{
    return outputTarget_;
}

}

#endif
