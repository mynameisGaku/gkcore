#pragma once

#if defined(_WIN32) && defined(DIRECT3D12)

#include "ModelGeometry.h"
#include "LightingAbi.h"

#include <Graphics/Interfaces/IGraphics.h>

/**
 * Direct3D 12 model pipeline ownership and per-frame resource management.
 */
namespace gk::render {

static_assert(sizeof(ModelRenderVertex) == 72, "PBR model vertices must occupy 72 bytes");

/**
 * Owns the built-in physically based model pipelines and per-frame GPU data.
 */
class ModelLightingRenderer {
public:
    /**
     * Creates an empty model lighting renderer.
     */
    ModelLightingRenderer() = default;
    /**
     * Model GPU resources have one owner and cannot be copied.
     */
    ModelLightingRenderer(const ModelLightingRenderer&) = delete;
    /**
     * Model GPU resources have one owner and cannot be assigned.
     */
    ModelLightingRenderer& operator=(const ModelLightingRenderer&) = delete;

    /**
     * Creates the scene and UI model pipelines, vertex buffers, and lighting bindings.
     */
    bool Initialize(Renderer* renderer, TinyImageFormat sceneFormat,
                    TinyImageFormat displayFormat, TinyImageFormat depthFormat,
                    SampleCount sampleCount, uint32_t sampleQuality,
                    uint32_t vertexCapacity, String& error);

    /**
     * Releases model pipelines, per-frame buffers, and descriptor sets.
     */
    void Shutdown();

    /**
     * Uploads one frame's expanded model vertices and packed lighting settings.
     */
    bool PrepareFrame(uint32_t frameIndex, const ModelRenderVertex* vertices,
                      uint32_t vertexCount, const effects::LightingSettings& lighting,
                      String& error);

    /**
     * Binds the scene or UI PBR pipeline and this frame's light constants.
     */
    bool Bind(Cmd* command, uint32_t frameIndex, bool ui, String& error) const;

    /**
     * Returns the uploaded model vertex buffer for the selected frame in flight.
     */
    Buffer* VertexBuffer(uint32_t frameIndex) const;

private:
    static constexpr uint32_t kFramesInFlight = 2;

    Renderer* renderer_ = nullptr;
    Shader* shader_ = nullptr;
    Pipeline* scenePipeline_ = nullptr;
    Pipeline* uiPipeline_ = nullptr;
    Buffer* vertexBuffers_[kFramesInFlight]{};
    Buffer* lightingBuffers_[kFramesInFlight]{};
    DescriptorSet* lightingDescriptorSet_ = nullptr;
    uint32_t vertexCapacity_ = 0;
};

}

#endif
