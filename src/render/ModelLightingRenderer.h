// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_MODELLIGHTINGRENDERER_H
#define GKCORE_RENDER_MODELLIGHTINGRENDERER_H

#if defined(_WIN32) && defined(DIRECT3D12)

#include "ModelGeometry.h"
#include "LightingAbi.h"

#include <Graphics/Interfaces/IGraphics.h>

/**
 * Direct3D 12のmodel pipelineとframe別resourceを管理する。
 */
namespace gk::render
{

static_assert(sizeof(ModelRenderVertex) == 120, "PBR model vertices must occupy 120 bytes");

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
     * sceneまたはUI用PBR pipelineと、指定frameの照明値をbindする。
     */
    bool Bind(Cmd* command, uint32_t frameIndex, bool ui, String& error) const;

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
    // frame別の展開頂点buffer。
    Buffer* vertexBuffers_[kFramesInFlight]{};
    // frame別の照明定数buffer。
    Buffer* lightingBuffers_[kFramesInFlight]{};
    // 照明定数のdescriptor。
    DescriptorSet* lightingDescriptorSet_ = nullptr;
    // 1 frameへ送れる頂点数の上限。
    uint32_t vertexCapacity_ = 0;
};

}

#endif

#endif
