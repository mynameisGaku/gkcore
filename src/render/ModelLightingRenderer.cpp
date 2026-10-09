#include "render/ModelLightingRenderer.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/FSL/defaults.h>
#include "shaders/gkcore_model.srt.h"
#include "shaders/gkcore_model_draw.srt.h"
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <stddef.h>
#include <stdint.h>
#include <float.h>
#include <math.h>
#include <string.h>

namespace gk::render
{
/**
 * model照明resourceの初期化とbindに使う診断処理。
 */
namespace
{

// 各draw定数を置く256byte境界付きslotの大きさ。
constexpr uint32_t kModelDrawConstantSlotBytes = 256;
// 各frameで既定slotと最大draw数を保持するbuffer byte数。
constexpr uint64_t kModelDrawConstantArenaBytes = static_cast<uint64_t>(kModelDrawDescriptorSlots) * kModelDrawConstantSlotBytes;
// 先頭dummy頂点を含むpose streamの頂点数。
constexpr uint32_t kModelPoseVertexBufferCount = kModelPoseVertexMaximumCount + 1;
// pose頂点は位置・法線・接線をfloat4で連続配置する。
constexpr uint32_t kModelPoseVertexFloat4Count = 3;
// float4単位のpose頂点stride。
constexpr uint32_t kModelPoseVertexStride = sizeof(float) * 4;
// 先頭dummyを含むframe別pose streamのbyte数。
constexpr uint64_t kModelPoseVertexBufferBytes = static_cast<uint64_t>(kModelPoseVertexBufferCount) * kModelPoseVertexFloat4Count * kModelPoseVertexStride;
// sparse mapの1 entryはposition indexとnormal indexの組。
constexpr uint32_t kModelSparseVertexMapStride = sizeof(uint32_t) * 2;

/**
 * 初期化またはbindの失敗理由を設定する。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * 1 frameとdraw slotをdescriptor set番号へ変換する。
 */
bool MakeModelDrawDescriptorIndex(uint32_t frameIndex, uint32_t drawConstantIndex, uint32_t& output)
{
    if (frameIndex >= 2 || drawConstantIndex >= kModelDrawDescriptorSlots)
        return false;
    output = frameIndex * kModelDrawDescriptorSlots + drawConstantIndex;
    return true;
}

/**
 * GPUへ渡す定数の全成分が有限値か調べる。
 */
bool IsFiniteModelDrawConstants(const FModelDrawConstants& constants)
{
    float values[sizeof(constants) / sizeof(float)];
    memcpy(values, &constants, sizeof(constants));
    for (uint32_t i = 0; i < sizeof(constants) / sizeof(float); ++i)
        if (!isfinite(values[i]))
            return false;
    return constants.flags[0] == 0.0f || constants.flags[0] == 1.0f || constants.flags[0] == 2.0f || constants.flags[0] == 3.0f;
}

/**
 * CRT呼び出しを避けてfloat32の有限値を確認する。
 */
bool IsFiniteModelPoseValue(float value)
{
    return value >= -FLT_MAX && value <= FLT_MAX;
}

/**
 * 既定slot向けにidentity変換とCPU射影modeを設定する。
 */
void MakeDefaultModelDrawConstants(FModelDrawConstants& constants)
{
    memset(&constants, 0, sizeof(constants));
    constants.rotationRows[0][0] = 1.0f;
    constants.rotationRows[1][1] = 1.0f;
    constants.rotationRows[2][2] = 1.0f;
    constants.inverseScale[0] = constants.inverseScale[1] = constants.inverseScale[2] = 1.0f;
    constants.scale[0] = constants.scale[1] = constants.scale[2] = 1.0f;
    constants.tint[0] = constants.tint[1] = constants.tint[2] = constants.tint[3] = 1.0f;
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

    // 160byte頂点payloadの属性位置と形式。
    VertexLayout layout{};
    layout.mBindingCount = 1;
    layout.mBindings[0].mStride = sizeof(ModelRenderVertex);
    layout.mAttribCount = 14;
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
    // 法線画像の横方向と鏡映の符号。
    layout.mAttribs[8].mSemantic = SEMANTIC_TANGENT;
    layout.mAttribs[8].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    layout.mAttribs[8].mBinding = 0;
    layout.mAttribs[8].mLocation = 8;
    layout.mAttribs[8].mOffset = offsetof(ModelRenderVertex, worldTangent);
    // 法線画像だけが使うUVの入力。
    layout.mAttribs[9].mSemantic = SEMANTIC_TEXCOORD5;
    layout.mAttribs[9].mFormat = TinyImageFormat_R32G32_SFLOAT;
    layout.mAttribs[9].mBinding = 0;
    layout.mAttribs[9].mLocation = 9;
    layout.mAttribs[9].mOffset = offsetof(ModelRenderVertex, normalUv);
    // 法線画像の有効値とXYの倍率。
    layout.mAttribs[10].mSemantic = SEMANTIC_TEXCOORD6;
    layout.mAttribs[10].mFormat = TinyImageFormat_R32G32_SFLOAT;
    layout.mAttribs[10].mBinding = 0;
    layout.mAttribs[10].mLocation = 10;
    layout.mAttribs[10].mOffset = offsetof(ModelRenderVertex, normalParameters);

    // 自己発光画像だけが使うUVの入力。
    layout.mAttribs[11].mSemantic = SEMANTIC_TEXCOORD7;
    layout.mAttribs[11].mFormat = TinyImageFormat_R32G32_SFLOAT;
    layout.mAttribs[11].mBinding = 0;
    layout.mAttribs[11].mLocation = 11;
    layout.mAttribs[11].mOffset = offsetof(ModelRenderVertex, emissiveUv);
    // 自己発光のRGB係数と、HDRへ渡す強度。
    layout.mAttribs[12].mSemantic = SEMANTIC_TEXCOORD8;
    layout.mAttribs[12].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    layout.mAttribs[12].mBinding = 0;
    layout.mAttribs[12].mLocation = 12;
    layout.mAttribs[12].mOffset = offsetof(ModelRenderVertex, emissiveFactorStrength);

    // 画像のUVと環境光を弱める強度。予約成分はGPU入力へ渡さない。
    layout.mAttribs[13].mSemantic = SEMANTIC_TEXCOORD9;
    layout.mAttribs[13].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
    layout.mAttribs[13].mBinding = 0;
    layout.mAttribs[13].mLocation = 13;
    layout.mAttribs[13].mOffset = offsetof(ModelRenderVertex, occlusionUvStrength);

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
    // 透明modelは標準alpha blendを使う。
    BlendStateDesc alphaBlend{};
    alphaBlend.mSrcFactors[0] = BC_SRC_ALPHA;
    alphaBlend.mDstFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    alphaBlend.mBlendModes[0] = BM_ADD;
    alphaBlend.mSrcAlphaFactors[0] = BC_ONE;
    alphaBlend.mDstAlphaFactors[0] = BC_ONE_MINUS_SRC_ALPHA;
    alphaBlend.mBlendAlphaModes[0] = BM_ADD;
    alphaBlend.mColorWriteMasks[0] = COLOR_MASK_ALL;
    alphaBlend.mRenderTargetMask = BLEND_STATE_TARGET_0;
    // 透明sceneは隠れた面を除外し、depth bufferへ影響させない。
    DepthStateDesc sceneBlendDepth{};
    sceneBlendDepth.mDepthTest = true;
    sceneBlendDepth.mDepthWrite = false;
    sceneBlendDepth.mDepthFunc = CMP_LESS;

    // 指定したtarget形式でmodel graphics pipelineを作る処理。
    auto createPipeline = [&](const char* name, TinyImageFormat colorFormat, TinyImageFormat pipelineDepthFormat, DepthStateDesc* depthState, BlendStateDesc* blendState, Pipeline** output)
    {
        // pipelineとgraphics設定をまとめる一時descriptor。
        PipelineDesc pipelineDesc{};
        pipelineDesc.mType = PIPELINE_TYPE_GRAPHICS;
        // graphics専用のpipeline設定。
        GraphicsPipelineDesc& graphics = pipelineDesc.mGraphicsDesc;
        graphics.pShaderProgram = shader_;
        graphics.pVertexLayout = &layout;
        graphics.pBlendState = blendState;
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
    createPipeline("gkcore HDR PBR Model Pipeline", sceneFormat, depthFormat, &depth, nullptr, &scenePipeline_);
    createPipeline("gkcore HDR PBR Model Alpha Pipeline", sceneFormat, depthFormat, &sceneBlendDepth, &alphaBlend, &sceneBlendPipeline_);
    createPipeline("gkcore UI PBR Model Pipeline", displayFormat, TinyImageFormat_UNDEFINED, &noDepth, nullptr, &uiPipeline_);
    createPipeline("gkcore UI PBR Model Alpha Pipeline", displayFormat, TinyImageFormat_UNDEFINED, &noDepth, &alphaBlend, &uiBlendPipeline_);
    if (!scenePipeline_ || !sceneBlendPipeline_ || !uiPipeline_ || !uiBlendPipeline_)
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

        // frame別GPU射影定数を256byte slotで保持するbuffer。
        BufferLoadDesc drawConstantDesc{};
        drawConstantDesc.mDesc.mSize = kModelDrawConstantArenaBytes;
        drawConstantDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        drawConstantDesc.mDesc.mFlags = static_cast<BufferCreationFlags>(BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT | BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION);
        drawConstantDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        drawConstantDesc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        drawConstantDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        drawConstantDesc.mDesc.pName = "gkcore PBR Model Draw Constants";
        drawConstantDesc.ppBuffer = &drawConstantBuffers_[frame];
        addResource(&drawConstantDesc, nullptr);

        // CPU変形頂点をshaderから読むframe専用structured buffer。
        BufferLoadDesc poseVertexDesc{};
        poseVertexDesc.mDesc.mSize = kModelPoseVertexBufferBytes;
        poseVertexDesc.mDesc.mElementCount = kModelPoseVertexBufferCount * kModelPoseVertexFloat4Count;
        poseVertexDesc.mDesc.mStructStride = kModelPoseVertexStride;
        poseVertexDesc.mDesc.mFormat = TinyImageFormat_UNDEFINED;
        poseVertexDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        poseVertexDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        poseVertexDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
        poseVertexDesc.mDesc.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
        poseVertexDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        poseVertexDesc.mDesc.pName = "gkcore PBR Model Pose Vertices";
        poseVertexDesc.ppBuffer = &poseVertexBuffers_[frame];
        addResource(&poseVertexDesc, nullptr);

        // map未使用slotへ結ぶ、常に安全な1要素structured buffer。
        BufferLoadDesc sparseMapDesc{};
        sparseMapDesc.mDesc.mSize = kModelSparseVertexMapStride;
        sparseMapDesc.mDesc.mElementCount = 1;
        sparseMapDesc.mDesc.mStructStride = kModelSparseVertexMapStride;
        sparseMapDesc.mDesc.mFormat = TinyImageFormat_UNDEFINED;
        sparseMapDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        sparseMapDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        sparseMapDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
        sparseMapDesc.mDesc.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
        sparseMapDesc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        sparseMapDesc.mDesc.pName = "gkcore PBR Model Sparse Map Dummy";
        sparseMapDesc.ppBuffer = &sparseVertexMapDefaultBuffers_[frame];
        addResource(&sparseMapDesc, nullptr);
    }
    waitForAllResourceLoads();
    // 非同期load後に全frame bufferが利用可能か調べるloop。
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        if (!vertexBuffers_[frame] || !vertexBuffers_[frame]->pCpuMappedAddress || !lightingBuffers_[frame] || !lightingBuffers_[frame]->pCpuMappedAddress || !drawConstantBuffers_[frame] || !drawConstantBuffers_[frame]->pCpuMappedAddress || !poseVertexBuffers_[frame] || !poseVertexBuffers_[frame]->pCpuMappedAddress || !sparseVertexMapDefaultBuffers_[frame] || !sparseVertexMapDefaultBuffers_[frame]->pCpuMappedAddress)
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

    // 2frame分のper-draw CBV descriptor setを確保する。
    const DescriptorSetDesc drawDescriptorDesc = SRT_SET_DESC(ModelDrawResources, PerDraw, kFramesInFlight * kModelDrawDescriptorSlots, 0);
    addDescriptorSet(renderer_, &drawDescriptorDesc, &drawConstantDescriptorSet_);
    if (!drawConstantDescriptorSet_)
    {
        Shutdown();
        return Fail(error, "The Forge could not allocate model draw descriptors");
    }
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        // map済み定数bufferを既定値で初期化する。
        memset(drawConstantBuffers_[frame]->pCpuMappedAddress, 0, static_cast<size_t>(kModelDrawConstantArenaBytes));
        FModelDrawConstants defaultConstants{};
        MakeDefaultModelDrawConstants(defaultConstants);
        memcpy(drawConstantBuffers_[frame]->pCpuMappedAddress, &defaultConstants, sizeof(defaultConstants));
        // index zeroはshader descriptorが常に安全に読めるdummy頂点。
        memset(poseVertexBuffers_[frame]->pCpuMappedAddress, 0, kModelPoseVertexStride * kModelPoseVertexFloat4Count);
        float* dummyPose = static_cast<float*>(poseVertexBuffers_[frame]->pCpuMappedAddress);
        dummyPose[3] = 1.0f;
        dummyPose[4] = 0.0f;
        dummyPose[5] = 1.0f;
        dummyPose[8] = 1.0f;
        dummyPose[11] = 1.0f;
        // dummy mapはpose slot zeroを参照する。
        const uint32_t dummySparseMap[2] = { 0, 0 };
        memcpy(sparseVertexMapDefaultBuffers_[frame]->pCpuMappedAddress, dummySparseMap, sizeof(dummySparseMap));
        for (uint32_t slot = 0; slot < kModelDrawDescriptorSlots; ++slot)
        {
            // CBVと二種類のstructured bufferを同じPerDraw setへ登録する。
            DescriptorDataRange constantRange = { slot * kModelDrawConstantSlotBytes, static_cast<uint32_t>(sizeof(FModelDrawConstants)), 0 };
            DescriptorDataRange poseRange = { 0, static_cast<uint32_t>(kModelPoseVertexBufferBytes), kModelPoseVertexStride };
            DescriptorData data[3]{};
            data[0].mIndex = SRT_RES_IDX(ModelDrawResources, PerDraw, gModelDraw);
            data[0].mCount = 1;
            data[0].pRanges = &constantRange;
            data[0].ppBuffers = &drawConstantBuffers_[frame];
            data[1].mIndex = SRT_RES_IDX(ModelDrawResources, PerDraw, gModelPoseVertices);
            data[1].mCount = 1;
            data[1].pRanges = &poseRange;
            data[1].ppBuffers = &poseVertexBuffers_[frame];
            data[2].mIndex = SRT_RES_IDX(ModelDrawResources, PerDraw, gModelSparseVertexMap);
            data[2].mCount = 1;
            data[2].ppBuffers = &sparseVertexMapDefaultBuffers_[frame];
            uint32_t descriptorIndex = 0;
            if (!MakeModelDrawDescriptorIndex(frame, slot, descriptorIndex))
            {
                Shutdown();
                return Fail(error, "The model draw descriptor index is invalid");
            }
            updateDescriptorSet(renderer_, descriptorIndex, drawConstantDescriptorSet_, 3, data);
        }
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
    if (drawConstantDescriptorSet_)
    {
        removeDescriptorSet(renderer_, drawConstantDescriptorSet_);
        drawConstantDescriptorSet_ = nullptr;
    }
    // frame別bufferを順番に解放するloop。
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        if (lightingBuffers_[frame])
        {
            removeResource(lightingBuffers_[frame]);
            lightingBuffers_[frame] = nullptr;
        }
        if (drawConstantBuffers_[frame])
        {
            removeResource(drawConstantBuffers_[frame]);
            drawConstantBuffers_[frame] = nullptr;
        }
        if (poseVertexBuffers_[frame])
        {
            removeResource(poseVertexBuffers_[frame]);
            poseVertexBuffers_[frame] = nullptr;
        }
        if (sparseVertexMapDefaultBuffers_[frame])
        {
            removeResource(sparseVertexMapDefaultBuffers_[frame]);
            sparseVertexMapDefaultBuffers_[frame] = nullptr;
        }
        if (vertexBuffers_[frame])
        {
            removeResource(vertexBuffers_[frame]);
            vertexBuffers_[frame] = nullptr;
        }
    }
    if (uiBlendPipeline_)
    {
        removePipeline(renderer_, uiBlendPipeline_);
        uiBlendPipeline_ = nullptr;
    }
    if (sceneBlendPipeline_)
    {
        removePipeline(renderer_, sceneBlendPipeline_);
        sceneBlendPipeline_ = nullptr;
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
    preparedDrawCounts_[0] = preparedDrawCounts_[1] = 0;
    preparedPoseVertexCounts_[0] = preparedPoseVertexCounts_[1] = 0;
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
 * 1 frame分のGPU射影定数を検証してmap済みbufferへ送る。
 */
bool ModelLightingRenderer::PrepareDrawConstants(uint32_t frameIndex, const FModelDrawConstants* constants, uint32_t count, String& error, const Buffer* const* sparseMaps)
{
    if (frameIndex < kFramesInFlight)
        preparedDrawCounts_[frameIndex] = 0;
    if (!renderer_ || frameIndex >= kFramesInFlight || count > kModelDrawMaximumConstants || (count && !constants))
        return Fail(error, "The model draw constants exceed their supported bounds");
    Buffer* buffer = drawConstantBuffers_[frameIndex];
    if (!buffer || !buffer->pCpuMappedAddress || !drawConstantDescriptorSet_)
        return Fail(error, "The model draw constant arena is unavailable");
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!IsFiniteModelDrawConstants(constants[i]))
            return Fail(error, "The model draw constants contain a non-finite or unsupported value");
        if (constants[i].flags[0] == 2.0f || constants[i].flags[0] == 3.0f)
        {
            const float poseOffset = constants[i].flags[1];
            if (preparedPoseVertexCounts_[frameIndex] == 0 || poseOffset < 1.0f || poseOffset >= static_cast<float>(preparedPoseVertexCounts_[frameIndex] + 1) || floorf(poseOffset) != poseOffset)
                return Fail(error, "The model draw constants reference an unavailable pose vertex range");
        }
        if (constants[i].flags[0] == 3.0f)
        {
            const float normalOffset = constants[i].flags[2];
            if (!sparseMaps || !sparseMaps[i] || normalOffset < 1.0f || normalOffset >= static_cast<float>(preparedPoseVertexCounts_[frameIndex] + 1) || floorf(normalOffset) != normalOffset)
                return Fail(error, "The sparse model draw constants reference an unavailable normal map or pose range");
        }
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        // slot zeroは従来頂点用に予約し、GPU射影はslot一から置く。
        const uint64_t byteOffset = static_cast<uint64_t>(i + 1) * kModelDrawConstantSlotBytes;
        memcpy(static_cast<uint8_t*>(buffer->pCpuMappedAddress) + byteOffset, &constants[i], sizeof(FModelDrawConstants));
        Buffer* sparseMap = constants[i].flags[0] == 3.0f ? const_cast<Buffer*>(sparseMaps[i]) : sparseVertexMapDefaultBuffers_[frameIndex];
        DescriptorDataRange poseRange = { 0, preparedPoseRangeBytes_[frameIndex], kModelPoseVertexStride };
        DescriptorData data[2]{};
        data[0].mIndex = SRT_RES_IDX(ModelDrawResources, PerDraw, gModelPoseVertices);
        data[0].mCount = 1;
        data[0].pRanges = &poseRange;
        data[0].ppBuffers = &preparedPoseBuffers_[frameIndex];
        data[1].mIndex = SRT_RES_IDX(ModelDrawResources, PerDraw, gModelSparseVertexMap);
        data[1].mCount = 1;
        data[1].ppBuffers = &sparseMap;
        uint32_t descriptorIndex = 0;
        if (!MakeModelDrawDescriptorIndex(frameIndex, i + 1, descriptorIndex))
            return Fail(error, "The model sparse map descriptor index is invalid");
        updateDescriptorSet(renderer_, descriptorIndex, drawConstantDescriptorSet_, 2, data);
    }
    preparedDrawCounts_[frameIndex] = count;
    error.Clear();
    return true;
}

/**
 * 変形済みpose頂点を検査してframe専用shader resourceへ転送する。
 */
bool ModelLightingRenderer::PreparePoseVertices(uint32_t frameIndex, const FModelPoseVertex* vertices, uint32_t count, Buffer* poseBufferOverride, uint32_t poseFloat4Count, bool skipCpuUpload, String& error)
{
    if (frameIndex < kFramesInFlight)
        preparedPoseVertexCounts_[frameIndex] = 0;
    if (!renderer_ || frameIndex >= kFramesInFlight || count > kModelPoseVertexMaximumCount || (count && !vertices) || (skipCpuUpload && !poseBufferOverride))
        return Fail(error, "The model pose vertex stream exceeds its supported bounds");
    Buffer* uploadBuffer = poseVertexBuffers_[frameIndex];
    if (!uploadBuffer || !uploadBuffer->pCpuMappedAddress)
        return Fail(error, "The model pose vertex stream is unavailable");
    if (poseBufferOverride && (poseFloat4Count == 0 || poseFloat4Count > poseBufferOverride->mSize / kModelPoseVertexStride))
        return Fail(error, "The model pose override buffer range is invalid");
    // GPU skinningでは計算済み範囲の有限性をPresent時に確認するため、全件走査を省く。
    if (!poseBufferOverride)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            for (uint32_t component = 0; component < 4; ++component)
                if (!IsFiniteModelPoseValue(vertices[i].position[component]) || !IsFiniteModelPoseValue(vertices[i].normal[component]) || !IsFiniteModelPoseValue(vertices[i].tangent[component]))
                    return Fail(error, "The model pose vertex stream contains a non-finite value");
        }
    }
    if (count && !skipCpuUpload)
        memcpy(static_cast<uint8_t*>(uploadBuffer->pCpuMappedAddress) + kModelPoseVertexStride * kModelPoseVertexFloat4Count, vertices, static_cast<size_t>(count) * sizeof(FModelPoseVertex));
    if (!drawConstantDescriptorSet_)
        return Fail(error, "The model pose descriptor set is unavailable");
    Buffer* selectedPoseBuffer = poseBufferOverride ? poseBufferOverride : uploadBuffer;
    preparedPoseBuffers_[frameIndex] = selectedPoseBuffer;
    preparedPoseRangeBytes_[frameIndex] = poseBufferOverride ? poseFloat4Count * kModelPoseVertexStride : static_cast<uint32_t>(kModelPoseVertexBufferBytes);
    preparedPoseVertexCounts_[frameIndex] = count;
    error.Clear();
    return true;
}

Buffer* ModelLightingRenderer::PoseVertexBuffer(uint32_t frameIndex) const
{
    return frameIndex < kFramesInFlight ? poseVertexBuffers_[frameIndex] : nullptr;
}

/**
 * commandへscene/UI pipelineとframe別照明descriptorを設定する。
 */
bool ModelLightingRenderer::Bind(Cmd* command, uint32_t frameIndex, bool ui, bool alphaBlend, String& error, uint32_t drawConstantIndex) const
{
    uint32_t drawDescriptorIndex = 0;
    if (!renderer_ || !command || frameIndex >= kFramesInFlight || !lightingDescriptorSet_ || !drawConstantDescriptorSet_ || drawConstantIndex >= kModelDrawDescriptorSlots || (drawConstantIndex && drawConstantIndex > preparedDrawCounts_[frameIndex]) || !MakeModelDrawDescriptorIndex(frameIndex, drawConstantIndex, drawDescriptorIndex))
        return Fail(error, "The PBR model pipeline binding is invalid");
    // 呼び出し側が選んだ描画先に対応するpipeline。
    Pipeline* pipeline = ui ? (alphaBlend ? uiBlendPipeline_ : uiPipeline_) : (alphaBlend ? sceneBlendPipeline_ : scenePipeline_);
    if (!pipeline)
        return Fail(error, "The requested PBR model pipeline is unavailable");
    cmdBindPipeline(command, pipeline);
    cmdBindDescriptorSet(command, frameIndex, lightingDescriptorSet_);
    cmdBindDescriptorSet(command, drawDescriptorIndex, drawConstantDescriptorSet_);
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
