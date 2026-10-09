// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELSKINNINGRENDERER_H
#define GKCORE_RENDER_FMODELSKINNINGRENDERER_H

#if defined(_WIN32) && defined(DIRECT3D12)

#include "foundation/String.h"
#include "render/FModelSkinningDispatch.h"

#include <Graphics/Interfaces/IGraphics.h>

namespace gk::render
{

/**
 * Skinning compute処理とframe別GPU bufferを所有する。
 */
class FModelSkinningRenderer
{
  public:
    /**
     * GPU resourceを持たないskin rendererを作る。
     */
    FModelSkinningRenderer() = default;
    /**
     * GPU resourceの二重所有を防ぐ。
     */
    FModelSkinningRenderer(const FModelSkinningRenderer&) = delete;
    FModelSkinningRenderer& operator=(const FModelSkinningRenderer&) = delete;
    /**
     * 所有するshader、pipeline、descriptor、bufferを解放する。
     */
    ~FModelSkinningRenderer();

    /**
     * compute shader、pipeline、frame別descriptorを作る。
     */
    bool Initialize(Renderer* renderer, String& error);
    /**
     * GPUがFP64 shader演算を提供するか返す。
     */
    bool SupportsDoublePrecision() const;
    /**
     * GPU完了済みframe slotへrecordとdispatchを準備する。
     */
    bool PrepareFrame(uint32_t frameIndex, const FModelSkinningRecord* matrices, uint32_t matrixRecordCount, Buffer* const* geometryBuffers, const FModelSkinningDispatch* dispatches, uint32_t dispatchCount, uint32_t minimumOutputFloat4Count, String& error);
    /**
     * CPU pose streamをcompute出力へ複製し、GPU skinningの未更新属性を保持する。
     */
    bool SeedOutputBuffer(Cmd* command, uint32_t frameIndex, Buffer* source, uint32_t float4Count, String& error);
    /**
     * modelごとのposition skinとnormal集計をcommandへ記録する。
     */
    bool Dispatch(Cmd* command, uint32_t frameIndex, String& error);
    /**
     * frame完了後にGPU skinning異常bitを読み、異常ならPresent失敗を返す。
     */
    bool ReadErrorFlag(uint32_t frameIndex, Fence* fence, String& error);
    /**
     * 指定frameの位置・法線出力bufferを返す。
     */
    Buffer* OutputBuffer(uint32_t frameIndex) const;
    /**
     * rendererのqueueをidleにした後、所有resourceを解放する。
     */
    void Shutdown();

  private:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kMaximumModelsPerFrame = 4096;
    static constexpr uint32_t kPhasesPerModel = 3;
    static constexpr uint32_t kDescriptorSlots = kMaximumModelsPerFrame * kPhasesPerModel;
    static constexpr uint32_t kConstantSlotBytes = 256;

    bool CreateFrameBuffers(uint32_t frameIndex, uint32_t matrixRecordCount, uint32_t faceCount, uint32_t outputFloat4Count, String& error);
    void ReleaseFrameBuffers(uint32_t frameIndex);

    Renderer* renderer_ = nullptr;                                                                                       // The Forge rendererを借用する。
    Shader* shader_ = nullptr;                                                                                           // compute shaderを所有する。
    Pipeline* pipeline_ = nullptr;                                                                                       // compute pipelineを所有する。
    DescriptorSet* descriptorSet_ = nullptr;                                                                             // frame・model別descriptorを所有する。
    Buffer* matrices_[kFramesInFlight] = {};                                                                             // frame別の可変cluster行列。
    Buffer* outputs_[kFramesInFlight] = {};                                                                              // frame別のskin位置・法線。
    Buffer* precisePositions_[kFramesInFlight] = {};                                                                     // normal計算用double位置。
    Buffer* faceNormals_[kFramesInFlight] = {};                                                                          // 面ごとに一度作るdouble法線。
    Buffer* errorFlags_[kFramesInFlight] = {};                                                                           // shaderが記録するskin異常bit。
    Buffer* errorReadbacks_[kFramesInFlight] = {};                                                                       // frame fence後に異常bitを読む領域。
    Buffer* constants_[kFramesInFlight] = {};                                                                            // dispatch定数のframe領域。
    ResourceState outputStates_[kFramesInFlight] = { RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS }; // 再利用bufferの実状態。
    ResourceState errorStates_[kFramesInFlight] = { RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS };  // 異常bit bufferの実状態。
    uint32_t matrixCapacities_[kFramesInFlight] = {};                                                                    // 行列recordのframe容量。
    uint32_t outputCapacities_[kFramesInFlight] = {};                                                                    // 出力float4のframe容量。
    uint32_t faceCapacities_[kFramesInFlight] = {};                                                                      // 面normalのframe容量。
    bool doublePrecisionSupported_ = false;                                                                              // deviceのFP64 shader対応。
    uint32_t preparedDispatchCounts_[kFramesInFlight] = {};                                                              // 準備済model数。
    uint32_t preparedGroupCounts_[kFramesInFlight][kMaximumModelsPerFrame][kPhasesPerModel] = {};                        // phaseごとのdispatch group数。
};

}

#endif

#endif
