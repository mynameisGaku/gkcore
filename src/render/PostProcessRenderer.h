#pragma once

#if defined(_WIN32) && defined(DIRECT3D12)

#include "../foundation/String.h"
#include "PostProcess.h"
#include "PostProcessPlan.h"

#include <Graphics/Interfaces/IGraphics.h>

namespace gk::render {

/**
 * Owns HDR scene, bloom, grading, FXAA, and fullscreen presentation resources.
 */
class PostProcessRenderer {
public:
    /**
     * Starts empty; initialization requires an active Forge renderer root signature.
     */
    PostProcessRenderer();
    /**
     * Releases resources; the owning renderer must idle its graphics queue first.
     */
    ~PostProcessRenderer();
    PostProcessRenderer(const PostProcessRenderer&) = delete;
    PostProcessRenderer& operator=(const PostProcessRenderer&) = delete;

    /**
     * Creates HDR/LDR targets, shader programs, pipelines, descriptor sets, and constants.
     */
    bool Initialize(Renderer* renderer, uint32_t width, uint32_t height,
                    TinyImageFormat outputFormat, String& error);
    /**
     * Recreates size-dependent targets after the renderer idles its queue.
     */
    bool Resize(uint32_t width, uint32_t height, String& error);
    /**
     * Records bloom, grading, and optional FXAA passes before UI composition.
     * The destination must be in render-target state; shader output stays linear for an sRGB RTV.
     */
    bool Apply(Cmd* command, RenderTarget* destination, uint32_t frameIndex,
               const PostProcessSettings& settings, String& error);
    /**
     * Commits recorded target states after the owning graphics queue accepts the command.
     */
    void CommitFrame();
    /**
     * Releases Forge resources in reverse initialization order.
     */
    void Shutdown();
    /**
     * Returns the linear HDR scene target used by scene draw commands.
     */
    RenderTarget* SceneTarget() const;

private:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kPassCount = 5;

    /**
     * Recreates the scene, full-resolution FXAA input, and bloom targets transactionally.
     */
    bool CreateTargets(uint32_t width, uint32_t height, String& error);
    /**
     * Loads the fullscreen shaders and creates their GPU pipelines.
     */
    bool CreatePrograms(String& error);
    /**
     * Allocates per-frame descriptors and constant buffers for every pass.
     */
    bool CreateDescriptorsAndConstants(String& error);
    /**
     * Binds source, bloom, sampler, and settings for one frame slot.
     */
    bool UpdatePassBindings(uint32_t frameIndex, const PostProcessSettings& settings, String& error);
    /**
     * Draws one fullscreen pass using its frame-specific resource bindings.
     */
    void DrawFullscreen(Cmd* command, RenderTarget* target, Pipeline* pipeline,
                        DescriptorSet* descriptorSet, uint32_t frameIndex);
    /**
     * Records a target state transition before its next read or write.
     */
    void Transition(Cmd* command, RenderTarget* target, ResourceState before, ResourceState after);

    Renderer* renderer_;
    TinyImageFormat outputFormat_;
    RenderTarget* sceneTarget_;
    RenderTarget* bloomTargets_[2];
    RenderTarget* linearLdrTarget_;
    Shader* extractShader_;
    Shader* blurShader_;
    Shader* compositeShader_;
    Shader* fxaaShader_;
    Pipeline* extractPipeline_;
    Pipeline* blurPipeline_;
    Pipeline* compositeOutputPipeline_;
    Pipeline* compositeLinearPipeline_;
    Pipeline* fxaaPipeline_;
    Sampler* sampler_;
    DescriptorSet* descriptorSets_[kPassCount];
    Buffer* constants_[kFramesInFlight][kPassCount];
    uint32_t width_;
    uint32_t height_;
    uint32_t bloomWidth_;
    uint32_t bloomHeight_;
    PostProcessTargetStateTracker targetStates_;
};

}

#endif
