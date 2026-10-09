// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_TESTS_SUPPORT_FMODELSKINNINGREADBACK_H
#define GKCORE_TESTS_SUPPORT_FMODELSKINNINGREADBACK_H

#include "foundation/String.h"
#include "render/FModelPoseVertex.h"
#include "render/FModelSkinningDispatch.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/Interfaces/IGraphics.h>

/**
 * 開発テスト用にskin compute結果をGPUから読み戻す。
 */
namespace gk::render::test_support
{

/**
 * skin出力を読み戻しbufferへcopyする。readback bufferは呼び出し側が所有し、GPU_TO_CPUで作成する。
 */
bool RecordModelSkinningReadback(Renderer* renderer, Cmd* command, Buffer* output, Buffer* readback, uint64_t byteCount, String& error);

/**
 * 提出済みGPU処理の完了を待ち、読み戻したbyte列を呼び出し側の領域へ複製する。
 */
bool ReadModelSkinningReadback(Renderer* renderer, Fence* fence, Buffer* readback, uint64_t byteCount, void* destination, String& error);

/**
 * GPUのfloat4出力をCPU姿勢arenaの位置・法線と比較した結果。
 */
struct FModelSkinningReadbackSummary
{
    uint64_t positionCount = 0;
    uint64_t normalCount = 0;
    float maximumPositionError = 0.0f;
    float maximumNormalError = 0.0f;
    uint32_t firstMismatchDispatch = UINT32_MAX;
    uint32_t firstMismatchVertex = UINT32_MAX;
    uint32_t firstMismatchPhase = UINT32_MAX;
    float firstMismatchActual[4]{};
    float firstMismatchExpected[4]{};
};

/**
 * 全dispatchの位置・法線float4をCPU姿勢arenaと照合し、非有限値や未実行出力を拒否する。
 */
bool CompareModelSkinningReadback(const float* gpuFloat4, uint32_t gpuFloat4Count, const FModelSkinningDispatch* dispatches, uint32_t dispatchCount, const FModelPoseVertex* cpuPoseVertices, uint32_t cpuPoseVertexCount, float tolerance, FModelSkinningReadbackSummary& summary, String& error);

}

#endif

#endif
