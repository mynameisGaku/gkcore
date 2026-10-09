#pragma once

#if defined(_WIN32) && defined(DIRECT3D12)

#include "effects/Shaders.h"
#include "foundation/Array.h"
#include "foundation/String.h"
#include "render/Geometry.h"
#include "render/ShaderAbi.h"
#include "render/CustomShaderPolicy.h"

#include <Graphics/Interfaces/IGraphics.h>

/**
 * Per-draw custom shader bindings captured from one frame packet.
 */
namespace gk::render
{

static_assert(kCustomShaderFrameCount == kShaderConstantFrameCount, "custom shader frame slots must match the constant arena");
static_assert(kCustomShaderMaximumDraws == kShaderConstantDrawCapacity, "custom shader draw capacity must match the constant descriptor arena");

/**
 * POD snapshot for one draw that selects a custom pixel shader.
 */
struct CustomShaderDraw
{
    ShaderHandle shader;
    uint32_t constantCount;
    ShaderConstant constants[kShaderConstantSlotCount];
};

/**
 * Owns loaded custom shader programs, pipeline variants, and per-frame constant arenas.
 */
class CustomShaders
{
  public:
    /**
     * Creates the shared constant descriptor set and loads the stock universal vertex shader.
     */
    bool Initialize(Renderer* renderer, Queue* queue, TinyImageFormat sceneFormat, TinyImageFormat displayFormat, TinyImageFormat depthFormat, SampleCount sampleCount, uint32_t sampleQuality, String& error);
    /**
     * Waits for queued GPU work before releasing all custom shaders and arena resources.
     */
    void Shutdown();
    /**
     * Loads a validated compiled pixel shader and publishes its handle after all PSOs exist.
     */
    ShaderHandle Load(const char* path, String& error);
    /**
     * Drains the queue and releases every pipeline and shader associated with a handle.
     */
    bool Release(ShaderHandle shader, String& error);
    /**
     * Validates all snapshots, writes one frame's constant blocks, and updates their CBV slices.
     */
    bool PrepareFrame(uint32_t frameIndex, const CustomShaderDraw* draws, uint32_t drawCount, String& error);
    /**
     * Binds the selected target/blend/depth pipeline and that draw's constant slice.
     */
    bool Bind(Cmd* command, ShaderHandle shader, uint32_t frameIndex, uint32_t customDrawIndex, uint32_t layer, bool depthTest, bool alphaBlend, String& error) const;
    /**
     * Binds the dedicated single-sample HDR pipeline and this draw's constant slice.
     */
    bool BindPostEffect(Cmd* command, ShaderHandle shader, uint32_t frameIndex, uint32_t customDrawIndex, String& error) const;
    /**
     * Reports whether the shader expects the optional t0 texture binding.
     */
    bool RequiresTexture(ShaderHandle shader) const;
    /**
     * Reports whether the shader expects the optional s0 sampler binding.
     */
    bool RequiresSampler(ShaderHandle shader) const;

  private:
    /**
     * Owns all target/depth/blend pipelines and reflected texture usage for one shader.
     */
    struct ShaderRecord
    {
        ShaderHandle handle;
        Shader* program;
        Pipeline* sceneOpaqueDepth;
        Pipeline* sceneAlphaDepth;
        Pipeline* sceneOpaque;
        Pipeline* sceneAlpha;
        Pipeline* uiOpaque;
        Pipeline* uiAlpha;
        Pipeline* postEffect;
        bool texture;
        bool sampler;
    };

    /**
     * Finds one loaded backend shader record by its nonrecycled handle.
     */
    ShaderRecord* Find(ShaderHandle shader);
    /**
     * Finds one loaded backend shader record by its nonrecycled handle.
     */
    const ShaderRecord* Find(ShaderHandle shader) const;
    /**
     * Removes all pipelines before the shader program and clears the record.
     */
    void DestroyRecord(ShaderRecord& record);
    /**
     * Creates one pipeline variant using the shared vertex ABI and configured target formats.
     */
    bool CreatePipeline(Shader* shader, const char* name, TinyImageFormat colorFormat, TinyImageFormat depthFormat, bool depthTest, bool alphaBlend, Pipeline** output, String& error, bool singleSample = false);

    static constexpr uint64_t kArenaBytes = static_cast<uint64_t>(kCustomShaderMaximumDraws) * kShaderConstantBlockBytes;
    Renderer* renderer_ = nullptr;
    Queue* queue_ = nullptr;
    Array<uint8_t> vertexBytecode_;
    DescriptorSet* constantDescriptorSet_ = nullptr;
    Buffer* constantBuffers_[kShaderConstantFrameCount]{};
    uint8_t* constantStaging_[kShaderConstantFrameCount]{};
    uint32_t preparedDrawCounts_[kShaderConstantFrameCount]{};
    Array<ShaderRecord> shaders_;
    TinyImageFormat sceneFormat_ = TinyImageFormat_UNDEFINED;
    TinyImageFormat displayFormat_ = TinyImageFormat_UNDEFINED;
    TinyImageFormat depthFormat_ = TinyImageFormat_UNDEFINED;
    SampleCount sampleCount_ = SAMPLE_COUNT_1;
    uint32_t sampleQuality_ = 0;
};

}

#endif
