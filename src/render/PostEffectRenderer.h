#pragma once

#if defined(_WIN32) && defined(DIRECT3D12)

#include "PostEffectPlan.h"
#include "CustomShaders.h"

#include <Graphics/Interfaces/IGraphics.h>

/**
 * Records a captured custom shader over the HDR scene as a full-screen pass.
 */
namespace gk::render {

/**
 * Owns the optional HDR output target, fullscreen geometry, sampler, and source descriptors.
 */
class PostEffectRenderer {
public:
    /**
     * Starts without Forge resources; initialization creates a same-size HDR output target.
     */
    PostEffectRenderer();
    /**
     * Releases owned GPU objects; the owner must idle the graphics queue first.
     */
    ~PostEffectRenderer();
    PostEffectRenderer(const PostEffectRenderer&) = delete;
    PostEffectRenderer& operator=(const PostEffectRenderer&) = delete;

    /**
     * Creates the output target, fullscreen vertex buffer, sampler, and frame descriptors.
     */
    bool Initialize(Renderer* renderer, uint32_t width, uint32_t height, String& error);
    /**
     * Recreates the output target after the owning queue has idled.
     */
    bool Resize(uint32_t width, uint32_t height, String& error);
    /**
     * Records the captured shader and restores the scene target for subsequent rendering.
     */
    bool Apply(Cmd* command, RenderTarget* scene, CustomShaders& shaders, uint32_t frameIndex,
               const PostEffectPlan& plan, String& error);
    /**
     * Discards staged states before every attempted presentation recording.
     */
    void DiscardPendingFrame();
    /**
     * Commits the output target state after successful queue submission.
     */
    void CommitFrame();
    /**
     * Releases descriptors before their sampler, buffer, and render target dependencies.
     */
    void Shutdown();
    /**
     * Returns the HDR target used for custom post-effect output.
     */
    RenderTarget* OutputTarget() const;

private:
    /**
     * Creates one render-target texture in the HDR format and requested dimensions.
     */
    bool CreateTarget(uint32_t width, uint32_t height, RenderTarget** output, String& error);

    Renderer* renderer_ = nullptr;
    RenderTarget* outputTarget_ = nullptr;
    Buffer* vertexBuffer_ = nullptr;
    Sampler* sampler_ = nullptr;
    DescriptorSet* descriptorSet_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    PostEffectTargetStateTracker outputState_{};
};

}

#endif
