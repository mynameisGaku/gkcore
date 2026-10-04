#include "ModelLightingRenderer.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/FSL/defaults.h>
#include "../../shaders/gkcore_model.srt.h"
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace gk::render {
/**
 * Local initialization diagnostics for model lighting resources.
 */
namespace {

/**
 * Sets one initialization or binding diagnostic and reports failure.
 */
bool Fail(String& error, const char* message) {
    error.Assign(message);
    return false;
}

}

bool ModelLightingRenderer::Initialize(Renderer* renderer, TinyImageFormat sceneFormat,
                                        TinyImageFormat displayFormat, TinyImageFormat depthFormat,
                                        SampleCount sampleCount, uint32_t sampleQuality,
                                        uint32_t vertexCapacity, String& error) {
    if (renderer_ || !renderer || sceneFormat == TinyImageFormat_UNDEFINED ||
        displayFormat == TinyImageFormat_UNDEFINED || depthFormat == TinyImageFormat_UNDEFINED ||
        vertexCapacity == 0)
        return Fail(error, "The model lighting renderer initialization is invalid");

    renderer_ = renderer;
    vertexCapacity_ = vertexCapacity;

    ShaderLoadDesc shaderDesc{};
    shaderDesc.mVert.pFileName = "gkcore_model.vert";
    shaderDesc.mFrag.pFileName = "gkcore_model.frag";
    addShader(renderer_, &shaderDesc, &shader_);
    if (!shader_) {
        Shutdown();
        return Fail(error, "The Forge could not load the built-in PBR model shaders");
    }

    VertexLayout layout{};
    layout.mBindingCount = 1;
    layout.mBindings[0].mStride = sizeof(ModelRenderVertex);
    layout.mAttribCount = 6;
    layout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
    layout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    layout.mAttribs[0].mBinding = 0;
    layout.mAttribs[0].mLocation = 0;
    layout.mAttribs[0].mOffset = offsetof(ModelRenderVertex, surface) + offsetof(Vertex, position);
    layout.mAttribs[1].mSemantic = SEMANTIC_COLOR;
    layout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    layout.mAttribs[1].mBinding = 0;
    layout.mAttribs[1].mLocation = 1;
    layout.mAttribs[1].mOffset = offsetof(ModelRenderVertex, surface) + offsetof(Vertex, color);
    layout.mAttribs[2].mSemantic = SEMANTIC_TEXCOORD0;
    layout.mAttribs[2].mFormat = TinyImageFormat_R32G32_SFLOAT;
    layout.mAttribs[2].mBinding = 0;
    layout.mAttribs[2].mLocation = 2;
    layout.mAttribs[2].mOffset = offsetof(ModelRenderVertex, surface) + offsetof(Vertex, uv);
    layout.mAttribs[3].mSemantic = SEMANTIC_NORMAL;
    layout.mAttribs[3].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
    layout.mAttribs[3].mBinding = 0;
    layout.mAttribs[3].mLocation = 3;
    layout.mAttribs[3].mOffset = offsetof(ModelRenderVertex, worldNormal);
    layout.mAttribs[4].mSemantic = SEMANTIC_TEXCOORD1;
    layout.mAttribs[4].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
    layout.mAttribs[4].mBinding = 0;
    layout.mAttribs[4].mLocation = 4;
    layout.mAttribs[4].mOffset = offsetof(ModelRenderVertex, viewDirection);
    layout.mAttribs[5].mSemantic = SEMANTIC_TEXCOORD2;
    layout.mAttribs[5].mFormat = TinyImageFormat_R32G32_SFLOAT;
    layout.mAttribs[5].mBinding = 0;
    layout.mAttribs[5].mLocation = 5;
    layout.mAttribs[5].mOffset = offsetof(ModelRenderVertex, metallicRoughness);

    RasterizerStateDesc rasterizer{};
    rasterizer.mCullMode = CULL_MODE_NONE;
    rasterizer.mFillMode = FILL_MODE_SOLID;
    DepthStateDesc depth{};
    depth.mDepthTest = true;
    depth.mDepthWrite = true;
    depth.mDepthFunc = CMP_LESS;
    DepthStateDesc noDepth{};
    noDepth.mDepthTest = false;
    noDepth.mDepthWrite = false;
    noDepth.mDepthFunc = CMP_ALWAYS;

    auto createPipeline = [&](const char* name, TinyImageFormat colorFormat,
                              TinyImageFormat pipelineDepthFormat, DepthStateDesc* depthState,
                              Pipeline** output) {
        PipelineDesc pipelineDesc{};
        pipelineDesc.mType = PIPELINE_TYPE_GRAPHICS;
        GraphicsPipelineDesc& graphics = pipelineDesc.mGraphicsDesc;
        graphics.pShaderProgram = shader_;
        graphics.pVertexLayout = &layout;
        graphics.pDepthState = depthState;
        graphics.pRasterizerState = &rasterizer;
        graphics.pColorFormats = &colorFormat;
        graphics.mRenderTargetCount = 1;
        graphics.mDepthStencilFormat = pipelineDepthFormat;
        graphics.mSampleCount = sampleCount;
        graphics.mSampleQuality = sampleQuality;
        graphics.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
        pipelineDesc.pName = name;
        addPipeline(renderer_, &pipelineDesc, output);
    };
    createPipeline("gkcore HDR PBR Model Pipeline", sceneFormat, depthFormat,
                   &depth, &scenePipeline_);
    createPipeline("gkcore UI PBR Model Pipeline", displayFormat,
                   TinyImageFormat_UNDEFINED, &noDepth, &uiPipeline_);
    if (!scenePipeline_ || !uiPipeline_) {
        Shutdown();
        return Fail(error, "The Forge could not create the PBR model pipelines");
    }

    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        BufferLoadDesc vertexDesc{};
        vertexDesc.mDesc.mSize = static_cast<uint64_t>(vertexCapacity_) * sizeof(ModelRenderVertex);
        vertexDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        vertexDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        vertexDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
        vertexDesc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        vertexDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        vertexDesc.mDesc.pName = "gkcore PBR Model Vertex Buffer";
        vertexDesc.ppBuffer = &vertexBuffers_[frame];
        addResource(&vertexDesc, nullptr);

        BufferLoadDesc lightingDesc{};
        lightingDesc.mDesc.mSize = 256;
        lightingDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        lightingDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        lightingDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightingDesc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        lightingDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        lightingDesc.mDesc.pName = "gkcore PBR Model Lighting Constants";
        lightingDesc.ppBuffer = &lightingBuffers_[frame];
        addResource(&lightingDesc, nullptr);
    }
    waitForAllResourceLoads();
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        if (!vertexBuffers_[frame] || !vertexBuffers_[frame]->pCpuMappedAddress ||
            !lightingBuffers_[frame] || !lightingBuffers_[frame]->pCpuMappedAddress) {
            Shutdown();
            return Fail(error, "The Forge could not allocate PBR model frame buffers");
        }
    }

    const DescriptorSetDesc descriptorDesc =
        SRT_SET_DESC(ModelLightingResources, PerFrame, kFramesInFlight, 0);
    addDescriptorSet(renderer_, &descriptorDesc, &lightingDescriptorSet_);
    if (!lightingDescriptorSet_) {
        Shutdown();
        return Fail(error, "The Forge could not allocate model lighting descriptors");
    }
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        DescriptorData data{};
        data.mIndex = SRT_RES_IDX(ModelLightingResources, PerFrame, gModelLighting);
        data.mCount = 1;
        data.ppBuffers = &lightingBuffers_[frame];
        updateDescriptorSet(renderer_, frame, lightingDescriptorSet_, 1, &data);
    }

    error.Clear();
    return true;
}

void ModelLightingRenderer::Shutdown() {
    if (!renderer_) return;
    if (lightingDescriptorSet_) {
        removeDescriptorSet(renderer_, lightingDescriptorSet_);
        lightingDescriptorSet_ = nullptr;
    }
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        if (lightingBuffers_[frame]) {
            removeResource(lightingBuffers_[frame]);
            lightingBuffers_[frame] = nullptr;
        }
        if (vertexBuffers_[frame]) {
            removeResource(vertexBuffers_[frame]);
            vertexBuffers_[frame] = nullptr;
        }
    }
    if (uiPipeline_) { removePipeline(renderer_, uiPipeline_); uiPipeline_ = nullptr; }
    if (scenePipeline_) { removePipeline(renderer_, scenePipeline_); scenePipeline_ = nullptr; }
    if (shader_) { removeShader(renderer_, shader_); shader_ = nullptr; }
    renderer_ = nullptr;
    vertexCapacity_ = 0;
}

bool ModelLightingRenderer::PrepareFrame(uint32_t frameIndex, const ModelRenderVertex* vertices,
                                         uint32_t vertexCount,
                                         const effects::LightingSettings& lighting,
                                         String& error) {
    if (!renderer_ || frameIndex >= kFramesInFlight || vertexCount > vertexCapacity_ ||
        (vertexCount && !vertices))
        return Fail(error, "The PBR model frame data exceeds its supported bounds");
    LightingConstants constants{};
    if (!PackLightingConstants(lighting, constants, error)) return false;
    Buffer* vertexBuffer = vertexBuffers_[frameIndex];
    Buffer* lightingBuffer = lightingBuffers_[frameIndex];
    if (!vertexBuffer || !vertexBuffer->pCpuMappedAddress ||
        !lightingBuffer || !lightingBuffer->pCpuMappedAddress)
        return Fail(error, "The PBR model frame buffers are unavailable");
    if (vertexCount)
        memcpy(vertexBuffer->pCpuMappedAddress, vertices,
               static_cast<size_t>(vertexCount) * sizeof(ModelRenderVertex));
    memcpy(lightingBuffer->pCpuMappedAddress, &constants, sizeof(constants));
    error.Clear();
    return true;
}

bool ModelLightingRenderer::Bind(Cmd* command, uint32_t frameIndex, bool ui,
                                 String& error) const {
    if (!renderer_ || !command || frameIndex >= kFramesInFlight || !lightingDescriptorSet_) 
        return Fail(error, "The PBR model pipeline binding is invalid");
    Pipeline* pipeline = ui ? uiPipeline_ : scenePipeline_;
    if (!pipeline) return Fail(error, "The requested PBR model pipeline is unavailable");
    cmdBindPipeline(command, pipeline);
    cmdBindDescriptorSet(command, frameIndex, lightingDescriptorSet_);
    error.Clear();
    return true;
}

Buffer* ModelLightingRenderer::VertexBuffer(uint32_t frameIndex) const {
    return frameIndex < kFramesInFlight ? vertexBuffers_[frameIndex] : nullptr;
}

}

#endif
