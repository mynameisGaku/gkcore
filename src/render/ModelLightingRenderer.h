// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_MODELLIGHTINGRENDERER_H
#define GKCORE_RENDER_MODELLIGHTINGRENDERER_H

#if defined(_WIN32) && defined(DIRECT3D12)

#include "render/ModelGeometry.h"
#include "render/FModelDrawConstants.h"
#include "render/FModelPoseVertex.h"
#include "render/LightingAbi.h"

#include <Graphics/Interfaces/IGraphics.h>

/**
 * Direct3D 12のmodel pipelineとframe別resourceを管理する。
 */
namespace gk::render
{

static_assert(sizeof(ModelRenderVertex) == 160, "PBR model vertices must occupy 160 bytes");
static_assert(sizeof(FModelDrawConstants) == 208, "model draw constants must occupy 208 bytes");

// 1 frameへ渡せるGPU射影model数の上限。
inline constexpr uint32_t kModelDrawMaximumConstants = 4096;
// 各frameの既定描画slotを含む定数descriptor数。
inline constexpr uint32_t kModelDrawDescriptorSlots = kModelDrawMaximumConstants + 1;
// 1 frame分のpose vertex streamの上限。
inline constexpr uint32_t kModelPoseVertexMaximumCount = 1u << 20;

/**
 * 内蔵PBR model pipelineとframe別GPU dataの所有者。
 */
class ModelLightingRenderer
{
  public:
    /**
     * resourceを持たないmodel照明rendererを作る。
     */
    ModelLightingRenderer() = default;
    /**
     * GPU resourceの所有者を複製できないようにする。
     */
    ModelLightingRenderer(const ModelLightingRenderer&) = delete;
    /**
     * GPU resourceの所有者を代入できないようにする。
     */
    ModelLightingRenderer& operator=(const ModelLightingRenderer&) = delete;

    /**
     * scene/UI pipeline、頂点buffer、照明bindingを作る。失敗時は理由を返す。
     */
    bool Initialize(Renderer* renderer, TinyImageFormat sceneFormat, TinyImageFormat displayFormat, TinyImageFormat depthFormat, SampleCount sampleCount, uint32_t sampleQuality, uint32_t vertexCapacity, String& error);

    /**
     * model pipeline、frame別buffer、descriptor setを解放する。
     */
    void Shutdown();

    /**
     * 指定frameの展開済み頂点と照明設定をGPUへ送る。
     */
    bool PrepareFrame(uint32_t frameIndex, const ModelRenderVertex* vertices, uint32_t vertexCount, const effects::LightingSettings& lighting, String& error);

    /**
     * 1 frame分のGPU射影値を検証してpersistently mapped bufferへ送る。
     */
    bool PrepareDrawConstants(uint32_t frameIndex, const FModelDrawConstants* constants, uint32_t count, String& error, const Buffer* const* sparseMaps = nullptr);

    /**
     * 変形済みpose頂点をframe専用のshader resource bufferへ転送する。
     */
    bool PreparePoseVertices(uint32_t frameIndex, const FModelPoseVertex* vertices, uint32_t count, Buffer* poseBufferOverride, uint32_t poseFloat4Count, bool skipCpuUpload, String& error);

    /**
     * frame別のCPU pose入力bufferを返す。範囲外ならnullを返す。
     */
    Buffer* PoseVertexBuffer(uint32_t frameIndex) const;

    /**
     * scene/UI用PBR pipelineと照明・GPU射影定数のdescriptorをbindする。
     */
    bool Bind(Cmd* command, uint32_t frameIndex, bool ui, bool alphaBlend, String& error, uint32_t drawConstantIndex = 0) const;

    /**
     * 指定frameで使用中の頂点bufferを返す。範囲外ならnullを返す。
     */
    Buffer* VertexBuffer(uint32_t frameIndex) const;

  private:
    // 同時にGPUへ送るframe数。
    static constexpr uint32_t kFramesInFlight = 2;

    // resource生成とbindに使うrenderer。
    Renderer* renderer_ = nullptr;
    // scene/UIが共有するmodel shader。
    Shader* shader_ = nullptr;
    // HDR scene用pipeline。
    Pipeline* scenePipeline_ = nullptr;
    // display UI用pipeline。
    Pipeline* uiPipeline_ = nullptr;
    // scene透明model用pipeline。depth testは行い、depthは書き込まない。
    Pipeline* sceneBlendPipeline_ = nullptr;
    // UI透明model用pipeline。depth testとdepth書込みを行わない。
    Pipeline* uiBlendPipeline_ = nullptr;
    // frame別の展開頂点buffer。
    Buffer* vertexBuffers_[kFramesInFlight]{};
    // frame別の照明定数buffer。
    Buffer* lightingBuffers_[kFramesInFlight]{};
    // frame別GPU射影定数bufferとdescriptor set。
    Buffer* drawConstantBuffers_[kFramesInFlight]{};
    // frame別にCPU変形頂点を読み取るshader resource buffer。
    Buffer* poseVertexBuffers_[kFramesInFlight]{};
    // sparse mapがないdraw向けの1要素dummy buffer。
    Buffer* sparseVertexMapDefaultBuffers_[kFramesInFlight]{};
    DescriptorSet* drawConstantDescriptorSet_ = nullptr;
    // frameごとに準備済みのGPU射影draw数。
    uint32_t preparedDrawCounts_[kFramesInFlight]{};
    // frameごとに準備済みのpose頂点数。
    uint32_t preparedPoseVertexCounts_[kFramesInFlight]{};
    // frameで射影drawが参照するpose bufferとdescriptor範囲。
    Buffer* preparedPoseBuffers_[kFramesInFlight]{};
    uint32_t preparedPoseRangeBytes_[kFramesInFlight]{};
    // 照明定数のdescriptor。
    DescriptorSet* lightingDescriptorSet_ = nullptr;
    // 1 frameへ送れる頂点数の上限。
    uint32_t vertexCapacity_ = 0;
};

}

#endif

#endif
