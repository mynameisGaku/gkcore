#include "render/CustomShaders.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include "render/ShaderReflection.h"
#include "render/Shaders.h"
#include "foundation/Memory.h"

#include <Graphics/FSL/defaults.h>
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>
#include <Utilities/Interfaces/IFileSystem.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef new
#undef new
#endif
#ifdef delete
#undef delete
#endif

/**
 * Direct3D 12 implementation for the custom pixel-shader ABI.
 */
namespace gk::render
{
/**
 * Local file and cleanup helpers for native custom shader resources.
 */
namespace
{

uint32_t gNextCustomShaderId = 1;
const Descriptor kCustomConstantBufferDescriptor = { IF_VALIDATE_DESCRIPTOR("gkcoreUserData", ROOT_PARAM_PerDraw, ) DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, 0 };

/**
 * Assigns one failure reason and returns false for initialization and pipeline branches.
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * Reads the packaged universal vertex artifact through the runtime shader directory.
 */
bool ReadStockVertexShader(CompiledShader& output, String& error)
{
    FileStream stream = {};
    if (!fsOpenStreamFromPath(RD_SHADER_BINARIES, "DIRECT3D12/gkcore_sprite.vert", FM_READ, &stream))
        return Fail(error, "the stock universal vertex shader is missing from CompiledShaders/DIRECT3D12");
    const ssize_t fileSize = fsGetStreamFileSize(&stream);
    if (fileSize <= 0 || static_cast<uint64_t>(fileSize) > 64u * 1024u * 1024u)
    {
        fsCloseStream(&stream);
        return Fail(error, "the stock universal vertex shader has an invalid size");
    }
    uint8_t* fileBytes = static_cast<uint8_t*>(Allocate(static_cast<size_t>(fileSize)));
    if (!fileBytes)
    {
        fsCloseStream(&stream);
        return Fail(error, "not enough memory to read the stock universal vertex shader");
    }
    const size_t readBytes = fsReadFromStream(&stream, fileBytes, static_cast<size_t>(fileSize));
    fsCloseStream(&stream);
    const bool read = readBytes == static_cast<size_t>(fileSize);
    const bool parsed = read && ParseCompiledShader(fileBytes, static_cast<uint32_t>(fileSize), "CompiledShaders/DIRECT3D12/gkcore_sprite.vert", CompiledShaderStage::Vertex, output, error);
    Deallocate(fileBytes);
    if (!read)
        return Fail(error, "the stock universal vertex shader could not be read completely");
    return parsed;
}

/**
 * Removes a pipeline if created and always clears the caller's pointer.
 */
void RemovePipeline(Renderer* renderer, Pipeline*& pipeline)
{
    if (renderer && pipeline)
        removePipeline(renderer, pipeline);
    pipeline = nullptr;
}

}

bool CustomShaders::Initialize(Renderer* renderer, Queue* queue, TinyImageFormat sceneFormat, TinyImageFormat displayFormat, TinyImageFormat depthFormat, SampleCount sampleCount, uint32_t sampleQuality, String& error)
{
    if (renderer_ || !renderer || !queue || sceneFormat == TinyImageFormat_UNDEFINED || displayFormat == TinyImageFormat_UNDEFINED || depthFormat == TinyImageFormat_UNDEFINED)
        return Fail(error, "custom shader resources received an invalid initialization state");

    renderer_ = renderer;
    queue_ = queue;
    sceneFormat_ = sceneFormat;
    displayFormat_ = displayFormat;
    depthFormat_ = depthFormat;
    sampleCount_ = sampleCount;
    sampleQuality_ = sampleQuality;

    DescriptorSetDesc setDesc = { ROOT_PARAM_PerDraw, kShaderConstantDescriptorCapacity, 0, 1, &kCustomConstantBufferDescriptor };
    addDescriptorSet(renderer_, &setDesc, &constantDescriptorSet_);
    if (!constantDescriptorSet_)
    {
        Shutdown();
        return Fail(error, "The Forge could not allocate custom shader constant descriptors");
    }

    for (uint32_t frame = 0; frame < kShaderConstantFrameCount; ++frame)
    {
        constantStaging_[frame] = static_cast<uint8_t*>(Allocate(static_cast<size_t>(kArenaBytes)));
        if (!constantStaging_[frame])
        {
            Shutdown();
            return Fail(error, "not enough memory for custom shader constant staging");
        }

        BufferLoadDesc bufferDesc = {};
        bufferDesc.mDesc.mSize = kArenaBytes;
        bufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        bufferDesc.mDesc.mFlags = static_cast<BufferCreationFlags>(BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT | BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION);
        bufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferDesc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        bufferDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        bufferDesc.mDesc.pName = "gkcore Custom Shader Constant Arena";
        bufferDesc.ppBuffer = &constantBuffers_[frame];
        addResource(&bufferDesc, nullptr);
        if (!constantBuffers_[frame])
        {
            waitForAllResourceLoads();
            Shutdown();
            return Fail(error, "The Forge could not allocate a custom shader constant arena");
        }
    }
    waitForAllResourceLoads();
    if (!constantBuffers_[0] || !constantBuffers_[1] || !constantBuffers_[0]->pCpuMappedAddress || !constantBuffers_[1]->pCpuMappedAddress)
    {
        Shutdown();
        return Fail(error, "The Forge did not map custom shader constant arenas");
    }

    CompiledShader vertex;
    if (!ReadStockVertexShader(vertex, error))
    {
        Shutdown();
        return false;
    }
    if (!vertexBytecode_.AppendRange(vertex.bytecode.Data(), vertex.bytecode.Count()))
    {
        Shutdown();
        return Fail(error, "not enough memory to retain the universal custom-shader vertex stage");
    }
    error.Clear();
    return true;
}

void CustomShaders::Shutdown()
{
    if (renderer_)
        waitForAllResourceLoads();
    if (queue_)
        waitQueueIdle(queue_);
    if (renderer_)
    {
        for (uint32_t i = 0; i < shaders_.Count(); ++i)
            DestroyRecord(shaders_.At(i));
        shaders_.Clear();
        if (constantDescriptorSet_)
        {
            removeDescriptorSet(renderer_, constantDescriptorSet_);
            constantDescriptorSet_ = nullptr;
        }
        for (uint32_t i = 0; i < kShaderConstantFrameCount; ++i)
        {
            if (constantBuffers_[i])
            {
                removeResource(constantBuffers_[i]);
                constantBuffers_[i] = nullptr;
            }
        }
    }
    for (uint32_t i = 0; i < kShaderConstantFrameCount; ++i)
    {
        Deallocate(constantStaging_[i]);
        constantStaging_[i] = nullptr;
    }
    shaders_.Clear();
    vertexBytecode_.Reset();
    preparedDrawCounts_[0] = preparedDrawCounts_[1] = 0;
    renderer_ = nullptr;
    queue_ = nullptr;
    sceneFormat_ = displayFormat_ = depthFormat_ = TinyImageFormat_UNDEFINED;
    sampleCount_ = SAMPLE_COUNT_1;
    sampleQuality_ = 0;
}

ShaderHandle CustomShaders::Load(const char* path, String& error)
{
    if (!renderer_ || !vertexBytecode_.Count() || gNextCustomShaderId == 0)
    {
        Fail(error, "custom shader resources are unavailable or the handle space is exhausted");
        return ShaderHandle();
    }
    CompiledShader pixel;
    if (!LoadCompiledPixelShader(path, pixel, error))
        return ShaderHandle();
    ShaderBindingUsage usage = {};
    if (!ValidatePixelShaderReflection(pixel.bytecode.Data(), pixel.bytecode.Count(), usage, error))
        return ShaderHandle();

    ShaderRecord record = {};
    BinaryShaderDesc shaderDesc = {};
    shaderDesc.mStages = static_cast<::ShaderStage>(SHADER_STAGE_VERT | SHADER_STAGE_FRAG);
    shaderDesc.mVert.pByteCode = const_cast<uint8_t*>(vertexBytecode_.Data());
    shaderDesc.mVert.mByteCodeSize = vertexBytecode_.Count();
    shaderDesc.mVert.pEntryPoint = "main";
    shaderDesc.mFrag.pByteCode = const_cast<uint8_t*>(pixel.bytecode.Data());
    shaderDesc.mFrag.mByteCodeSize = pixel.bytecode.Count();
    shaderDesc.mFrag.pEntryPoint = "main";
    addShaderBinary(renderer_, &shaderDesc, &record.program);
    if (!record.program)
    {
        Fail(error, "The Forge could not create the custom pixel shader program");
        return ShaderHandle();
    }

    record.handle = ShaderHandle(gNextCustomShaderId);
    record.texture = usage.texture;
    record.sampler = usage.sampler;
    static const char* const kPipelineNames[] = { "gkcore Custom Scene Depth Opaque", "gkcore Custom Scene Depth Alpha", "gkcore Custom Scene Opaque", "gkcore Custom Scene Alpha", "gkcore Custom UI Opaque", "gkcore Custom UI Alpha", "gkcore Custom Post Effect" };
    bool created = CreatePipeline(record.program, kPipelineNames[0], sceneFormat_, depthFormat_, true, false, &record.sceneOpaqueDepth, error) && CreatePipeline(record.program, kPipelineNames[1], sceneFormat_, depthFormat_, true, true, &record.sceneAlphaDepth, error) && CreatePipeline(record.program, kPipelineNames[2], sceneFormat_, depthFormat_, false, false, &record.sceneOpaque, error) && CreatePipeline(record.program, kPipelineNames[3], sceneFormat_, depthFormat_, false, true, &record.sceneAlpha, error) && CreatePipeline(record.program, kPipelineNames[4], displayFormat_, TinyImageFormat_UNDEFINED, false, false, &record.uiOpaque, error) && CreatePipeline(record.program, kPipelineNames[5], displayFormat_, TinyImageFormat_UNDEFINED, false, true, &record.uiAlpha, error) && CreatePipeline(record.program, kPipelineNames[6], TinyImageFormat_R16G16B16A16_SFLOAT, TinyImageFormat_UNDEFINED, false, false, &record.postEffect, error, true);
    if (!created || !shaders_.Append(record))
    {
        DestroyRecord(record);
        if (created)
            Fail(error, "not enough memory to register custom shader pipelines");
        return ShaderHandle();
    }
    ++gNextCustomShaderId;
    error.Clear();
    return record.handle;
}

bool CustomShaders::Release(ShaderHandle shader, String& error)
{
    if (!renderer_ || !shader.IsValid())
        return Fail(error, "custom shader handle is invalid");
    for (uint32_t i = 0; i < shaders_.Count(); ++i)
    {
        if (shaders_.At(i).handle != shader)
            continue;
        if (queue_)
            waitQueueIdle(queue_);
        DestroyRecord(shaders_.At(i));
        shaders_.RemoveAt(i);
        error.Clear();
        return true;
    }
    return Fail(error, "custom shader handle is not loaded");
}

bool CustomShaders::PrepareFrame(uint32_t frameIndex, const CustomShaderDraw* draws, uint32_t drawCount, String& error)
{
    if (frameIndex < kShaderConstantFrameCount)
        preparedDrawCounts_[frameIndex] = 0;
    if (!renderer_ || !IsCustomShaderFrameValid(frameIndex, drawCount) || (drawCount && !draws))
        return Fail(error, "custom shader frame snapshots exceed the supported bounds");

    uint8_t validationBlock[kShaderConstantBlockBytes];
    for (uint32_t i = 0; i < drawCount; ++i)
    {
        if (!Find(draws[i].shader) || draws[i].constantCount > kShaderConstantSlotCount)
            return Fail(error, "custom shader frame contains an unknown handle or invalid constant count");
        if (!PackShaderConstantBlock(draws[i].constants, draws[i].constantCount, validationBlock, sizeof(validationBlock), error))
            return false;
    }

    uint8_t* staging = constantStaging_[frameIndex];
    Buffer* buffer = constantBuffers_[frameIndex];
    if (!staging || !buffer || !buffer->pCpuMappedAddress)
        return Fail(error, "custom shader constant arena is not available for this frame");
    const uint64_t usedBytes = static_cast<uint64_t>(drawCount) * kShaderConstantBlockBytes;
    for (uint32_t i = 0; i < drawCount; ++i)
    {
        uint8_t* block = staging + static_cast<uint64_t>(i) * kShaderConstantBlockBytes;
        if (!PackShaderConstantBlock(draws[i].constants, draws[i].constantCount, block, kShaderConstantBlockBytes, error))
            return false;
    }
    if (usedBytes)
        memcpy(buffer->pCpuMappedAddress, staging, static_cast<size_t>(usedBytes));

    for (uint32_t i = 0; i < drawCount; ++i)
    {
        ShaderConstantSlice slice = {};
        if (!MakeShaderConstantSlice(frameIndex, i, slice))
            return Fail(error, "custom shader constant descriptor index is invalid");
        DescriptorDataRange range = { slice.byteOffset, kShaderConstantBlockBytes, 0 };
        DescriptorData data = {};
        data.mIndex = 0;
        data.mCount = 1;
        data.pRanges = &range;
        data.ppBuffers = &buffer;
        updateDescriptorSet(renderer_, slice.descriptorIndex, constantDescriptorSet_, 1, &data);
    }
    preparedDrawCounts_[frameIndex] = drawCount;
    error.Clear();
    return true;
}

bool CustomShaders::Bind(Cmd* command, ShaderHandle shader, uint32_t frameIndex, uint32_t customDrawIndex, uint32_t layer, bool depthTest, bool alphaBlend, String& error) const
{
    if (!command || !IsCustomShaderBindingValid(frameIndex, customDrawIndex, layer))
        return Fail(error, "custom shader draw binding is outside the supported range");
    if (customDrawIndex >= preparedDrawCounts_[frameIndex])
        return Fail(error, "custom shader draw index has no prepared constant snapshot");
    const ShaderRecord* record = Find(shader);
    if (!record)
        return Fail(error, "custom shader handle is not loaded");
    CustomShaderPipelineVariant variant = CustomShaderPipelineVariant::UiOpaque;
    if (!SelectCustomShaderPipelineVariant(layer, depthTest, alphaBlend, variant))
        return Fail(error, "custom shader layer has no compatible pipeline variant");
    Pipeline* pipeline = nullptr;
    switch (variant)
    {
    case CustomShaderPipelineVariant::SceneOpaqueDepth:
        pipeline = record->sceneOpaqueDepth;
        break;
    case CustomShaderPipelineVariant::SceneAlphaDepth:
        pipeline = record->sceneAlphaDepth;
        break;
    case CustomShaderPipelineVariant::SceneOpaque:
        pipeline = record->sceneOpaque;
        break;
    case CustomShaderPipelineVariant::SceneAlpha:
        pipeline = record->sceneAlpha;
        break;
    case CustomShaderPipelineVariant::UiOpaque:
        pipeline = record->uiOpaque;
        break;
    case CustomShaderPipelineVariant::UiAlpha:
        pipeline = record->uiAlpha;
        break;
    case CustomShaderPipelineVariant::PostEffect:
        return Fail(error, "post-effect pipelines require post-effect shader binding");
    }
    if (!pipeline || !constantDescriptorSet_)
        return Fail(error, "custom shader pipeline resources are incomplete");
    ShaderConstantSlice slice = {};
    if (!MakeShaderConstantSlice(frameIndex, customDrawIndex, slice))
        return Fail(error, "custom shader descriptor slice is invalid");
    cmdBindPipeline(command, pipeline);
    cmdBindDescriptorSet(command, slice.descriptorIndex, constantDescriptorSet_);
    error.Clear();
    return true;
}

bool CustomShaders::BindPostEffect(Cmd* command, ShaderHandle shader, uint32_t frameIndex, uint32_t customDrawIndex, String& error) const
{
    CustomShaderPipelineVariant variant = CustomShaderPipelineVariant::UiOpaque;
    if (!command || !SelectPostEffectShaderPipelineVariant(frameIndex, customDrawIndex, variant))
        return Fail(error, "post-effect shader binding is outside the supported frame or draw range");
    if (variant != CustomShaderPipelineVariant::PostEffect)
        return Fail(error, "post-effect shader binding selected an incompatible pipeline variant");
    if (customDrawIndex >= preparedDrawCounts_[frameIndex])
        return Fail(error, "post-effect shader draw has no prepared constant snapshot");
    const ShaderRecord* record = Find(shader);
    if (!record || !record->postEffect)
        return Fail(error, "post-effect shader handle or pipeline is unavailable");
    ShaderConstantSlice slice = {};
    if (!MakeShaderConstantSlice(frameIndex, customDrawIndex, slice))
        return Fail(error, "post-effect shader constant descriptor slice is invalid");
    cmdBindPipeline(command, record->postEffect);
    cmdBindDescriptorSet(command, slice.descriptorIndex, constantDescriptorSet_);
    error.Clear();
    return true;
}

bool CustomShaders::RequiresTexture(ShaderHandle shader) const
{
    const ShaderRecord* record = Find(shader);
    return record && record->texture;
}

bool CustomShaders::RequiresSampler(ShaderHandle shader) const
{
    const ShaderRecord* record = Find(shader);
    return record && record->sampler;
}

CustomShaders::ShaderRecord* CustomShaders::Find(ShaderHandle shader)
{
    for (uint32_t i = 0; i < shaders_.Count(); ++i)
        if (shaders_.At(i).handle == shader)
            return &shaders_.At(i);
    return nullptr;
}

const CustomShaders::ShaderRecord* CustomShaders::Find(ShaderHandle shader) const
{
    for (uint32_t i = 0; i < shaders_.Count(); ++i)
        if (shaders_.At(i).handle == shader)
            return &shaders_.At(i);
    return nullptr;
}

void CustomShaders::DestroyRecord(ShaderRecord& record)
{
    RemovePipeline(renderer_, record.sceneOpaqueDepth);
    RemovePipeline(renderer_, record.sceneAlphaDepth);
    RemovePipeline(renderer_, record.sceneOpaque);
    RemovePipeline(renderer_, record.sceneAlpha);
    RemovePipeline(renderer_, record.uiOpaque);
    RemovePipeline(renderer_, record.uiAlpha);
    RemovePipeline(renderer_, record.postEffect);
    if (renderer_ && record.program)
        removeShader(renderer_, record.program);
    record = {};
}

bool CustomShaders::CreatePipeline(Shader* shader, const char* name, TinyImageFormat colorFormat, TinyImageFormat depthFormat, bool depthTest, bool alphaBlend, Pipeline** output, String& error, bool singleSample)
{
    VertexLayout layout = {};
    layout.mBindingCount = 1;
    layout.mBindings[0].mStride = sizeof(Vertex);
    layout.mAttribCount = 3;
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
    layout.mAttribs[2].mSemantic = SEMANTIC_TEXCOORD0;
    layout.mAttribs[2].mFormat = TinyImageFormat_R32G32_SFLOAT;
    layout.mAttribs[2].mBinding = 0;
    layout.mAttribs[2].mLocation = 2;
    layout.mAttribs[2].mOffset = offsetof(Vertex, uv);

    RasterizerStateDesc rasterizer = {};
    rasterizer.mCullMode = CULL_MODE_NONE;
    rasterizer.mFillMode = FILL_MODE_SOLID;
    DepthStateDesc depth = {};
    depth.mDepthTest = depthTest;
    depth.mDepthWrite = depthTest;
    depth.mDepthFunc = depthTest ? CMP_LESS : CMP_ALWAYS;
    BlendStateDesc blend = {};
    blend.mSrcFactors[0] = BC_SRC_ALPHA;
    blend.mDstFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    blend.mBlendModes[0] = BM_ADD;
    blend.mSrcAlphaFactors[0] = BC_ONE;
    blend.mDstAlphaFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    blend.mBlendAlphaModes[0] = BM_ADD;
    blend.mColorWriteMasks[0] = COLOR_MASK_ALL;
    blend.mRenderTargetMask = BLEND_STATE_TARGET_0;

    PipelineDesc desc = {};
    desc.mType = PIPELINE_TYPE_GRAPHICS;
    GraphicsPipelineDesc& graphics = desc.mGraphicsDesc;
    graphics.pShaderProgram = shader;
    graphics.pVertexLayout = &layout;
    graphics.pBlendState = alphaBlend ? &blend : nullptr;
    graphics.pDepthState = &depth;
    graphics.pRasterizerState = &rasterizer;
    graphics.pColorFormats = &colorFormat;
    graphics.mRenderTargetCount = 1;
    graphics.mDepthStencilFormat = depthFormat;
    graphics.mSampleCount = singleSample ? SAMPLE_COUNT_1 : sampleCount_;
    graphics.mSampleQuality = singleSample ? 0 : sampleQuality_;
    graphics.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
    desc.pName = name;
    addPipeline(renderer_, &desc, output);
    if (!*output)
        return Fail(error, "The Forge could not create a custom shader pipeline variant");
    return true;
}

}

#endif
