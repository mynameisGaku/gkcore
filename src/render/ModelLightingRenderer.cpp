#include "ModelLightingRenderer.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/FSL/defaults.h>
#include "../../shaders/gkcore_model.srt.h"
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace gk::render
{
/**
 * model照明resourceの初期化とbindに使う診断処理。
 */
namespace
{

/**
 * 初期化またはbindの失敗理由を設定する。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

/**
 * rendererと形式を受け取り、model用pipelineとframe別resourceを作る。
 */
bool ModelLightingRenderer::Initialize(Renderer* renderer, TinyImageFormat sceneFormat, TinyImageFormat displayFormat, TinyImageFormat depthFormat, SampleCount sampleCount, uint32_t sampleQuality, uint32_t vertexCapacity, String& error)
{
    if (renderer_ || !renderer || sceneFormat == TinyImageFormat_UNDEFINED || displayFormat == TinyImageFormat_UNDEFINED || depthFormat == TinyImageFormat_UNDEFINED || vertexCapacity == 0)
        return Fail(error, "The model lighting renderer initialization is invalid");

    renderer_ = renderer;
    vertexCapacity_ = vertexCapacity;

    // built-in model vertex/pixel shaderの読み込み指定。
    ShaderLoadDesc shaderDesc{};
    shaderDesc.mVert.pFileName = "gkcore_model.vert";
    shaderDesc.mFrag.pFileName = "gkcore_model.frag";
    addShader(renderer_, &shaderDesc, &shader_);
    if (!shader_)
    {
        Shutdown();
        return Fail(error, "The Forge could not load the built-in PBR model shaders");
    }

    // 80byte頂点payloadの属性位置と形式。
    VertexLayout layout{};
    layout.mBindingCount = 1;
    layout.mBindings[0].mStride = sizeof(ModelRenderVertex);
    layout.mAttribCount = 8;
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
    // 材質ごとのアルファ抜きの有効値と境界値。
    layout.mAttribs[6].mSemantic = SEMANTIC_TEXCOORD3;
    layout.mAttribs[6].mFormat = TinyImageFormat_R32G32_SFLOAT;
    layout.mAttribs[6].mBinding = 0;
    layout.mAttribs[6].mLocation = 6;
    layout.mAttribs[6].mOffset = offsetof(ModelRenderVertex, alphaMaskCutoff);
    // 金属度・粗さの画像だけが使うUVの入力。
    layout.mAttribs[7].mSemantic = SEMANTIC_TEXCOORD4;
    layout.mAttribs[7].mFormat = TinyImageFormat_R32G32_SFLOAT;
    layout.mAttribs[7].mBinding = 0;
    layout.mAttribs[7].mLocation = 7;
    layout.mAttribs[7].mOffset = offsetof(ModelRenderVertex, metallicRoughnessUv);

    // scene/UIで共通利用するrasterizer設定。
    RasterizerStateDesc rasterizer{};
    rasterizer.mCullMode = CULL_MODE_NONE;
    rasterizer.mFillMode = FILL_MODE_SOLID;
    // scene描画で使うdepth test/write設定。
    DepthStateDesc depth{};
    depth.mDepthTest = true;
    depth.mDepthWrite = true;
    depth.mDepthFunc = CMP_LESS;
    // UI描画でdepth test/writeを無効にする設定。
    DepthStateDesc noDepth{};
    noDepth.mDepthTest = false;
    noDepth.mDepthWrite = false;
    noDepth.mDepthFunc = CMP_ALWAYS;

    // 指定したtarget形式でmodel graphics pipelineを作る処理。
    auto createPipeline = [&](const char* name, TinyImageFormat colorFormat, TinyImageFormat pipelineDepthFormat, DepthStateDesc* depthState, Pipeline** output)
    {
        // pipelineとgraphics設定をまとめる一時descriptor。
        PipelineDesc pipelineDesc{};
        pipelineDesc.mType = PIPELINE_TYPE_GRAPHICS;
        // graphics専用のpipeline設定。
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
    createPipeline("gkcore HDR PBR Model Pipeline", sceneFormat, depthFormat, &depth, &scenePipeline_);
    createPipeline("gkcore UI PBR Model Pipeline", displayFormat, TinyImageFormat_UNDEFINED, &noDepth, &uiPipeline_);
    if (!scenePipeline_ || !uiPipeline_)
    {
        Shutdown();
        return Fail(error, "The Forge could not create the PBR model pipelines");
    }

    // frame別の頂点・照明bufferを非同期作成するloop。
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        // CPUから頂点dataを書き込むbuffer descriptor。
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

        // CPUから照明定数を書き込むbuffer descriptor。
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
    // 非同期load後に全frame bufferが利用可能か調べるloop。
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        if (!vertexBuffers_[frame] || !vertexBuffers_[frame]->pCpuMappedAddress || !lightingBuffers_[frame] || !lightingBuffers_[frame]->pCpuMappedAddress)
        {
            Shutdown();
            return Fail(error, "The Forge could not allocate PBR model frame buffers");
        }
    }

    // frame別照明bufferをまとめるdescriptor setの配置。
    const DescriptorSetDesc descriptorDesc = SRT_SET_DESC(ModelLightingResources, PerFrame, kFramesInFlight, 0);
    addDescriptorSet(renderer_, &descriptorDesc, &lightingDescriptorSet_);
    if (!lightingDescriptorSet_)
    {
        Shutdown();
        return Fail(error, "The Forge could not allocate model lighting descriptors");
    }
    // frameごとの照明bufferをdescriptor setへ登録するloop。
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        // 1件のuniform bufferを指すdescriptor値。
        DescriptorData data{};
        data.mIndex = SRT_RES_IDX(ModelLightingResources, PerFrame, gModelLighting);
        data.mCount = 1;
        data.ppBuffers = &lightingBuffers_[frame];
        updateDescriptorSet(renderer_, frame, lightingDescriptorSet_, 1, &data);
    }

    error.Clear();
    return true;
}

/**
 * 所有するdescriptor、buffer、pipeline、shaderを逆順に解放する。
 */
void ModelLightingRenderer::Shutdown()
{
    if (!renderer_)
        return;
    if (lightingDescriptorSet_)
    {
        removeDescriptorSet(renderer_, lightingDescriptorSet_);
        lightingDescriptorSet_ = nullptr;
    }
    // frame別bufferを順番に解放するloop。
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        if (lightingBuffers_[frame])
        {
            removeResource(lightingBuffers_[frame]);
            lightingBuffers_[frame] = nullptr;
        }
        if (vertexBuffers_[frame])
        {
            removeResource(vertexBuffers_[frame]);
            vertexBuffers_[frame] = nullptr;
        }
    }
    if (uiPipeline_)
    {
        removePipeline(renderer_, uiPipeline_);
        uiPipeline_ = nullptr;
    }
    if (scenePipeline_)
    {
        removePipeline(renderer_, scenePipeline_);
        scenePipeline_ = nullptr;
    }
    if (shader_)
    {
        removeShader(renderer_, shader_);
        shader_ = nullptr;
    }
    renderer_ = nullptr;
    vertexCapacity_ = 0;
}

/**
 * frame番号と頂点・照明設定を受け取り、CPU可視bufferへ転送する。
 */
bool ModelLightingRenderer::PrepareFrame(uint32_t frameIndex, const ModelRenderVertex* vertices, uint32_t vertexCount, const effects::LightingSettings& lighting, String& error)
{
    if (!renderer_ || frameIndex >= kFramesInFlight || vertexCount > vertexCapacity_ || (vertexCount && !vertices))
        return Fail(error, "The PBR model frame data exceeds its supported bounds");
    // 検証済み照明値をshader ABI形式へ詰めた定数。
    LightingConstants constants{};
    if (!PackLightingConstants(lighting, constants, error))
        return false;
    // 指定frameの頂点bufferと照明定数buffer。
    Buffer* vertexBuffer = vertexBuffers_[frameIndex];
    Buffer* lightingBuffer = lightingBuffers_[frameIndex];
    if (!vertexBuffer || !vertexBuffer->pCpuMappedAddress || !lightingBuffer || !lightingBuffer->pCpuMappedAddress)
        return Fail(error, "The PBR model frame buffers are unavailable");
    if (vertexCount)
        memcpy(vertexBuffer->pCpuMappedAddress, vertices, static_cast<size_t>(vertexCount) * sizeof(ModelRenderVertex));
    memcpy(lightingBuffer->pCpuMappedAddress, &constants, sizeof(constants));
    error.Clear();
    return true;
}

/**
 * commandへscene/UI pipelineとframe別照明descriptorを設定する。
 */
bool ModelLightingRenderer::Bind(Cmd* command, uint32_t frameIndex, bool ui, String& error) const
{
    if (!renderer_ || !command || frameIndex >= kFramesInFlight || !lightingDescriptorSet_)
        return Fail(error, "The PBR model pipeline binding is invalid");
    // 呼び出し側が選んだ描画先に対応するpipeline。
    Pipeline* pipeline = ui ? uiPipeline_ : scenePipeline_;
    if (!pipeline)
        return Fail(error, "The requested PBR model pipeline is unavailable");
    cmdBindPipeline(command, pipeline);
    cmdBindDescriptorSet(command, frameIndex, lightingDescriptorSet_);
    error.Clear();
    return true;
}

/**
 * frame番号に対応する頂点bufferを返す。
 */
Buffer* ModelLightingRenderer::VertexBuffer(uint32_t frameIndex) const
{
    return frameIndex < kFramesInFlight ? vertexBuffers_[frameIndex] : nullptr;
}

}

#endif
