// SPDX-License-Identifier: NOASSERTION
#include "tests/support/FModelSkinningReadback.h"

#include "foundation/Memory.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <float.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * 開発テスト用のskin compute結果読み戻しを実装する。
 */
namespace gk::render::test_support
{
namespace
{

/**
 * 読み戻し失敗の理由を呼び出し元へ返す。
 */
bool SetReadbackError(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

bool RecordModelSkinningReadback(Renderer* renderer, Cmd* command, Buffer* output, Buffer* readback, uint64_t byteCount, String& error)
{
    if (!renderer || !renderer->mDx.pDevice || !command || !command->mDx.pCmdList || !output || !output->mDx.pResource || !readback || !readback->mDx.pResource || byteCount == 0 || byteCount > output->mSize || byteCount > readback->mSize || readback->mMemoryUsage != RESOURCE_MEMORY_USAGE_GPU_TO_CPU)
        return SetReadbackError(error, "The model skinning readback resources or byte count are invalid");

    // compute出力をcopy元へ遷移し、同じcommandへcopyを記録する。
    BufferBarrier toCopy{};
    toCopy.pBuffer = output;
    toCopy.mCurrentState = RESOURCE_STATE_SHADER_RESOURCE;
    toCopy.mNewState = RESOURCE_STATE_COPY_SOURCE;
    cmdResourceBarrier(command, 1, &toCopy, 0, nullptr, 0, nullptr);
    command->mDx.pCmdList->CopyBufferRegion(readback->mDx.pResource, 0, output->mDx.pResource, 0, byteCount);

    // 後続drawが同じ出力をshader resourceとして読む状態へ戻す。
    BufferBarrier toShader{};
    toShader.pBuffer = output;
    toShader.mCurrentState = RESOURCE_STATE_COPY_SOURCE;
    toShader.mNewState = RESOURCE_STATE_SHADER_RESOURCE;
    cmdResourceBarrier(command, 1, &toShader, 0, nullptr, 0, nullptr);
    error.Clear();
    return true;
}

bool ReadModelSkinningReadback(Renderer* renderer, Fence* fence, Buffer* readback, uint64_t byteCount, void* destination, String& error)
{
    if (!renderer || !fence || !readback || readback->mMemoryUsage != RESOURCE_MEMORY_USAGE_GPU_TO_CPU || !readback->pCpuMappedAddress || !destination || byteCount == 0 || byteCount > readback->mSize)
        return SetReadbackError(error, "The model skinning readback completion parameters are invalid");

    // fence完了前はGPUがcopy先を書き込み中の可能性がある。
    waitForFences(renderer, 1, &fence);
    memcpy(destination, readback->pCpuMappedAddress, static_cast<size_t>(byteCount));
    error.Clear();
    return true;
}

bool CompareModelSkinningReadback(const float* gpuFloat4, uint32_t gpuFloat4Count, const FModelSkinningDispatch* dispatches, uint32_t dispatchCount, const FModelPoseVertex* cpuPoseVertices, uint32_t cpuPoseVertexCount, float tolerance, FModelSkinningReadbackSummary& summary, String& error)
{
    summary = FModelSkinningReadbackSummary{};
    if (!gpuFloat4 || !dispatches || dispatchCount == 0 || !cpuPoseVertices || gpuFloat4Count == 0 || !isfinite(tolerance) || tolerance < 0.0f)
        return SetReadbackError(error, "The model skinning comparison inputs are invalid or contain no dispatches");

    bool matches = true;
    for (uint32_t dispatchIndex = 0; dispatchIndex < dispatchCount; ++dispatchIndex)
    {
        const FModelSkinningDispatch& dispatch = dispatches[dispatchIndex];
        for (uint32_t phase = 0; phase < 2; ++phase)
        {
            const uint32_t outputOffset = phase == 0 ? dispatch.outputPositionsOffset : dispatch.outputNormalsOffset;
            const uint32_t outputCount = phase == 0 ? dispatch.outputPositionsCount : dispatch.outputNormalsCount;
            for (uint32_t vertexIndex = 0; vertexIndex < outputCount; ++vertexIndex)
            {
                const uint64_t row = static_cast<uint64_t>(outputOffset) + static_cast<uint64_t>(vertexIndex) * 3u;
                const uint32_t expectedRowInTriplet = phase == 0 ? 0u : 1u;
                if (row >= gpuFloat4Count || row % 3u != expectedRowInTriplet || row / 3u == 0 || row / 3u - 1u >= cpuPoseVertexCount)
                {
                    matches = false;
                    if (summary.firstMismatchDispatch == UINT32_MAX)
                    {
                        summary.firstMismatchDispatch = dispatchIndex;
                        summary.firstMismatchVertex = vertexIndex;
                        summary.firstMismatchPhase = phase;
                    }
                    continue;
                }

                const uint32_t cpuIndex = static_cast<uint32_t>(row / 3u - 1u);
                const float* actual = gpuFloat4 + static_cast<size_t>(row) * 4u;
                const float* expected = phase == 0 ? cpuPoseVertices[cpuIndex].position : cpuPoseVertices[cpuIndex].normal;
                float expectedValues[4]{};
                float maximumError = 0.0f;
                for (uint32_t component = 0; component < 4; ++component)
                {
                    const float expectedValue = component == 3 ? (phase == 0 ? 1.0f : 0.0f) : expected[component];
                    expectedValues[component] = expectedValue;
                    if (!isfinite(actual[component]) || !isfinite(expectedValue))
                    {
                        matches = false;
                        maximumError = FLT_MAX;
                        continue;
                    }
                    const float difference = fabsf(actual[component] - expectedValue);
                    if (!isfinite(difference))
                    {
                        matches = false;
                        maximumError = FLT_MAX;
                    }
                    else if (difference > maximumError)
                        maximumError = difference;
                    if (difference > tolerance)
                        matches = false;
                }

                float& phaseMaximum = phase == 0 ? summary.maximumPositionError : summary.maximumNormalError;
                if (maximumError > phaseMaximum)
                    phaseMaximum = maximumError;
                if (maximumError > tolerance && summary.firstMismatchDispatch == UINT32_MAX)
                {
                    summary.firstMismatchDispatch = dispatchIndex;
                    summary.firstMismatchVertex = vertexIndex;
                    summary.firstMismatchPhase = phase;
                    memcpy(summary.firstMismatchActual, actual, sizeof(summary.firstMismatchActual));
                    memcpy(summary.firstMismatchExpected, expectedValues, sizeof(summary.firstMismatchExpected));
                }
                if (phase == 0)
                    ++summary.positionCount;
                else
                    ++summary.normalCount;
            }
        }
    }

    if (summary.positionCount == 0 || summary.normalCount == 0)
        matches = false;
    if (!matches)
    {
        char diagnostic[224]{};
        snprintf(diagnostic, sizeof(diagnostic), "GPU skinning output disagrees with CPU pose data (dispatch=%u vertex=%u phase=%u actual=(%.9g,%.9g,%.9g,%.9g) expected=(%.9g,%.9g,%.9g,%.9g) positions=%llu normals=%llu maxPositionError=%.9g maxNormalError=%.9g tolerance=%.9g)", summary.firstMismatchDispatch, summary.firstMismatchVertex, summary.firstMismatchPhase, summary.firstMismatchActual[0], summary.firstMismatchActual[1], summary.firstMismatchActual[2], summary.firstMismatchActual[3], summary.firstMismatchExpected[0], summary.firstMismatchExpected[1], summary.firstMismatchExpected[2], summary.firstMismatchExpected[3], static_cast<unsigned long long>(summary.positionCount), static_cast<unsigned long long>(summary.normalCount), summary.maximumPositionError, summary.maximumNormalError, tolerance);
        return SetReadbackError(error, diagnostic);
    }
    error.Clear();
    return true;
}

}

#endif
