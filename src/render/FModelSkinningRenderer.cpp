#include "render/FModelSkinningRenderer.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Graphics/FSL/defaults.h>
#include "shaders/gkcore_model_skinning.srt.h"
#include <Resources/ResourceLoader/Interfaces/IResourceLoader.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

namespace gk::render
{
namespace
{
struct FModelSkinningConstants
{
    uint32_t ranges[20]; // dispatch headerと同じ20値。
    uint32_t phase;      // 0はposition、1はnormal。
    uint32_t padding[3]; // constant bufferの16 byte境界を保つ。
};

static_assert(sizeof(FModelSkinningConstants) == 96, "skin constants must match the FSL layout");

bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

bool RangeFits(uint32_t offset, uint32_t count, uint32_t total)
{
    return offset <= total && count <= total - offset;
}

bool StridedRangeFits(uint32_t offset, uint32_t count, uint32_t stride, uint32_t total)
{
    if (count == 0)
        return offset <= total;
    if (offset == UINT32_MAX || count - 1u > (UINT32_MAX - 1u - offset) / stride)
        return false;
    const uint32_t last = offset + (count - 1u) * stride;
    return last < total;
}

bool StridedRangeEnd(uint32_t offset, uint32_t count, uint32_t stride, uint32_t& end)
{
    if (count == 0)
    {
        end = offset;
        return true;
    }
    if (offset == UINT32_MAX || count - 1u > (UINT32_MAX - 1u - offset) / stride)
        return false;
    end = offset + (count - 1u) * stride + 1u;
    return true;
}

bool DispatchRangesFit(const FModelSkinningDispatch& dispatch, uint32_t recordCount, uint32_t matrixRecordCount, uint32_t outputCount)
{
    return dispatch.positionsCount <= UINT32_MAX / 2u && dispatch.matricesCount <= UINT32_MAX / 6u && RangeFits(dispatch.positionsOffset, dispatch.positionsCount * 2u, recordCount) && RangeFits(dispatch.influenceRangesOffset, dispatch.influenceRangesCount, recordCount) && RangeFits(dispatch.influencesOffset, dispatch.influencesCount, recordCount) && RangeFits(dispatch.matricesOffset, dispatch.matricesCount * 6u, matrixRecordCount) && RangeFits(dispatch.facesOffset, dispatch.facesCount, recordCount) && RangeFits(dispatch.cornersOffset, dispatch.cornersCount, recordCount) && RangeFits(dispatch.normalGroupRangesOffset, dispatch.normalGroupRangesCount, recordCount) && RangeFits(dispatch.normalFaceIdsOffset, dispatch.normalFaceIdsCount, recordCount) && StridedRangeFits(dispatch.outputPositionsOffset, dispatch.outputPositionsCount, 3u, outputCount) && StridedRangeFits(dispatch.outputNormalsOffset, dispatch.outputNormalsCount, 3u, outputCount) && dispatch.influenceRangesCount >= dispatch.positionsCount && dispatch.normalGroupRangesCount >= dispatch.outputNormalsCount && dispatch.outputPositionsCount >= dispatch.positionsCount;
}

void CopyDispatchRanges(FModelSkinningConstants& constants, const FModelSkinningDispatch& dispatch, uint32_t phase)
{
    const uint32_t values[20] = { dispatch.positionsOffset, dispatch.positionsCount, dispatch.influenceRangesOffset, dispatch.influenceRangesCount, dispatch.influencesOffset, dispatch.influencesCount, dispatch.matricesOffset, dispatch.matricesCount, dispatch.facesOffset, dispatch.facesCount, dispatch.cornersOffset, dispatch.cornersCount, dispatch.normalGroupRangesOffset, dispatch.normalGroupRangesCount, dispatch.normalFaceIdsOffset, dispatch.normalFaceIdsCount, dispatch.outputPositionsOffset, dispatch.outputPositionsCount, dispatch.outputNormalsOffset, dispatch.outputNormalsCount };
    memcpy(constants.ranges, values, sizeof(values));
    constants.phase = phase;
    memset(constants.padding, 0, sizeof(constants.padding));
}

}

FModelSkinningRenderer::~FModelSkinningRenderer()
{
    Shutdown();
}

bool FModelSkinningRenderer::Initialize(Renderer* renderer, String& error)
{
    if (renderer_ || !renderer)
        return Fail(error, "model skinning renderer initialization is invalid");
    renderer_ = renderer;
    D3D12_FEATURE_DATA_D3D12_OPTIONS deviceOptions{};
    doublePrecisionSupported_ = renderer_->mDx.pDevice && SUCCEEDED(renderer_->mDx.pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &deviceOptions, sizeof(deviceOptions))) && deviceOptions.DoublePrecisionFloatShaderOps;
    if (!doublePrecisionSupported_)
    {
        error.Clear();
        return true;
    }
    ShaderLoadDesc shaderDesc{};
    shaderDesc.mComp.pFileName = "gkcore_model_skinning.comp";
    addShader(renderer_, &shaderDesc, &shader_);
    if (!shader_)
    {
        Shutdown();
        return Fail(error, "The Forgeでload the model skinning compute shader");
    }
    PipelineDesc pipelineDesc{};
    pipelineDesc.mType = PIPELINE_TYPE_COMPUTE;
    pipelineDesc.mComputeDesc.pShaderProgram = shader_;
    pipelineDesc.pName = "gkcore Model Skinning Compute Pipeline";
    addPipeline(renderer_, &pipelineDesc, &pipeline_);
    if (!pipeline_)
    {
        Shutdown();
        return Fail(error, "The Forgeでcreate the model skinning compute pipeline");
    }
    const DescriptorSetDesc descriptorDesc = SRT_SET_DESC(ModelSkinningResources, PerDraw, kFramesInFlight * kDescriptorSlots, 0);
    addDescriptorSet(renderer_, &descriptorDesc, &descriptorSet_);
    if (!descriptorSet_)
    {
        Shutdown();
        return Fail(error, "The Forgeでallocate model skinning descriptors");
    }
    error.Clear();
    return true;
}

bool FModelSkinningRenderer::SupportsDoublePrecision() const
{
    return doublePrecisionSupported_;
}

bool FModelSkinningRenderer::CreateFrameBuffers(uint32_t frameIndex, uint32_t matrixRecordCount, uint32_t faceCount, uint32_t outputFloat4Count, String& error)
{
    const uint32_t requestedMatrices = matrixRecordCount ? matrixRecordCount : 1;
    const uint32_t requestedFaces = faceCount ? faceCount : 1;
    const uint32_t requestedOutput = outputFloat4Count ? outputFloat4Count : 1;
    const uint64_t constantsBytes = static_cast<uint64_t>(kDescriptorSlots) * kConstantSlotBytes;
    const uint64_t precisePositionCount = (static_cast<uint64_t>(requestedOutput) + 2u) / 3u;
    if (requestedMatrices > UINT32_MAX / sizeof(FModelSkinningRecord) || requestedFaces > UINT32_MAX / (sizeof(double) * 4) || requestedOutput > UINT32_MAX / (sizeof(float) * 4) || precisePositionCount > UINT32_MAX / (sizeof(double) * 4) || constantsBytes > UINT32_MAX)
        return Fail(error, "model skinning frame buffer size exceeds the supported range");
    if (matrices_[frameIndex] && outputs_[frameIndex] && precisePositions_[frameIndex] && faceNormals_[frameIndex] && errorFlags_[frameIndex] && errorReadbacks_[frameIndex] && constants_[frameIndex] && matrixCapacities_[frameIndex] >= requestedMatrices && faceCapacities_[frameIndex] >= requestedFaces && outputCapacities_[frameIndex] >= requestedOutput)
        return true;
    const auto createBuffer = [&](const char* name, uint64_t size, ResourceMemoryUsage memory, DescriptorType descriptors, ResourceState state, uint32_t stride, BufferCreationFlags additionalFlags, Buffer** output) -> bool
    {
        BufferLoadDesc desc{};
        desc.mDesc.mSize = size;
        desc.mDesc.mElementCount = stride ? static_cast<uint32_t>(size / stride) : 0;
        desc.mDesc.mFormat = TinyImageFormat_UNDEFINED;
        desc.mDesc.mMemoryUsage = memory;
        const BufferCreationFlags memoryFlags = memory == RESOURCE_MEMORY_USAGE_CPU_TO_GPU || memory == RESOURCE_MEMORY_USAGE_GPU_TO_CPU ? BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT : BUFFER_CREATION_FLAG_NONE;
        desc.mDesc.mFlags = static_cast<BufferCreationFlags>(memoryFlags | additionalFlags);
        desc.mDesc.mDescriptors = descriptors;
        desc.mDesc.mStartState = state;
        desc.mDesc.mQueueType = QUEUE_TYPE_GRAPHICS;
        desc.mDesc.mStructStride = stride;
        desc.mDesc.pName = name;
        desc.ppBuffer = output;
        addResource(&desc, nullptr);
        return *output != nullptr;
    };
    ReleaseFrameBuffers(frameIndex);
    if (!createBuffer("gkcore Skinning Matrices", static_cast<uint64_t>(requestedMatrices) * sizeof(FModelSkinningRecord), RESOURCE_MEMORY_USAGE_CPU_TO_GPU, DESCRIPTOR_TYPE_BUFFER, RESOURCE_STATE_SHADER_RESOURCE, sizeof(FModelSkinningRecord), BUFFER_CREATION_FLAG_NONE, &matrices_[frameIndex]) || !createBuffer("gkcore Skinning Output", static_cast<uint64_t>(requestedOutput) * sizeof(float) * 4, RESOURCE_MEMORY_USAGE_GPU_ONLY, static_cast<DescriptorType>(DESCRIPTOR_TYPE_BUFFER | DESCRIPTOR_TYPE_RW_BUFFER), RESOURCE_STATE_UNORDERED_ACCESS, sizeof(float) * 4, BUFFER_CREATION_FLAG_NONE, &outputs_[frameIndex]) || !createBuffer("gkcore Skinning Precise Positions", precisePositionCount * sizeof(double) * 4, RESOURCE_MEMORY_USAGE_GPU_ONLY, static_cast<DescriptorType>(DESCRIPTOR_TYPE_BUFFER | DESCRIPTOR_TYPE_RW_BUFFER), RESOURCE_STATE_UNORDERED_ACCESS, sizeof(double) * 4, BUFFER_CREATION_FLAG_NONE, &precisePositions_[frameIndex]) || !createBuffer("gkcore Skinning Face Normals", static_cast<uint64_t>(requestedFaces) * sizeof(double) * 4, RESOURCE_MEMORY_USAGE_GPU_ONLY, static_cast<DescriptorType>(DESCRIPTOR_TYPE_BUFFER | DESCRIPTOR_TYPE_RW_BUFFER), RESOURCE_STATE_UNORDERED_ACCESS, sizeof(double) * 4, BUFFER_CREATION_FLAG_NONE, &faceNormals_[frameIndex]) || !createBuffer("gkcore Skinning Errors", sizeof(uint32_t), RESOURCE_MEMORY_USAGE_GPU_ONLY, static_cast<DescriptorType>(DESCRIPTOR_TYPE_BUFFER | DESCRIPTOR_TYPE_RW_BUFFER), RESOURCE_STATE_UNORDERED_ACCESS, sizeof(uint32_t), BUFFER_CREATION_FLAG_NONE, &errorFlags_[frameIndex]) || !createBuffer("gkcore Skinning Error Readback", sizeof(uint32_t), RESOURCE_MEMORY_USAGE_GPU_TO_CPU, DESCRIPTOR_TYPE_UNDEFINED, RESOURCE_STATE_COPY_DEST, sizeof(uint32_t), BUFFER_CREATION_FLAG_NONE, &errorReadbacks_[frameIndex]) || !createBuffer("gkcore Skinning Constants", constantsBytes, RESOURCE_MEMORY_USAGE_CPU_TO_GPU, DESCRIPTOR_TYPE_UNIFORM_BUFFER, RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, 0, BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION, &constants_[frameIndex]))
    {
        waitForAllResourceLoads();
        ReleaseFrameBuffers(frameIndex);
        return Fail(error, "The Forgeでallocate model skinning frame buffer");
    }
    waitForAllResourceLoads();
    if (!matrices_[frameIndex]->pCpuMappedAddress || !errorReadbacks_[frameIndex]->pCpuMappedAddress || !constants_[frameIndex]->pCpuMappedAddress)
    {
        ReleaseFrameBuffers(frameIndex);
        return Fail(error, "The Forge did not map model skinning upload buffers");
    }
    matrixCapacities_[frameIndex] = requestedMatrices;
    outputCapacities_[frameIndex] = requestedOutput;
    faceCapacities_[frameIndex] = requestedFaces;
    return true;
}

void FModelSkinningRenderer::ReleaseFrameBuffers(uint32_t frameIndex)
{
    if (constants_[frameIndex])
        removeResource(constants_[frameIndex]);
    if (outputs_[frameIndex])
        removeResource(outputs_[frameIndex]);
    if (precisePositions_[frameIndex])
        removeResource(precisePositions_[frameIndex]);
    if (faceNormals_[frameIndex])
        removeResource(faceNormals_[frameIndex]);
    if (errorFlags_[frameIndex])
        removeResource(errorFlags_[frameIndex]);
    if (errorReadbacks_[frameIndex])
        removeResource(errorReadbacks_[frameIndex]);
    if (matrices_[frameIndex])
        removeResource(matrices_[frameIndex]);
    constants_[frameIndex] = nullptr;
    outputs_[frameIndex] = nullptr;
    precisePositions_[frameIndex] = nullptr;
    faceNormals_[frameIndex] = nullptr;
    errorFlags_[frameIndex] = nullptr;
    errorReadbacks_[frameIndex] = nullptr;
    matrices_[frameIndex] = nullptr;
    matrixCapacities_[frameIndex] = 0;
    outputCapacities_[frameIndex] = 0;
    faceCapacities_[frameIndex] = 0;
    outputStates_[frameIndex] = RESOURCE_STATE_UNORDERED_ACCESS;
    errorStates_[frameIndex] = RESOURCE_STATE_UNORDERED_ACCESS;
}

bool FModelSkinningRenderer::PrepareFrame(uint32_t frameIndex, const FModelSkinningRecord* matrices, uint32_t matrixRecordCount, Buffer* const* geometryBuffers, const FModelSkinningDispatch* dispatches, uint32_t dispatchCount, uint32_t minimumOutputFloat4Count, String& error)
{
    if (!renderer_ || !doublePrecisionSupported_ || frameIndex >= kFramesInFlight || (matrixRecordCount && !matrices) || (dispatchCount && (!dispatches || !geometryBuffers)) || dispatchCount > kMaximumModelsPerFrame)
        return Fail(error, "model skinning frame inputs are invalid");
    uint32_t outputCount = 0;
    uint32_t faceCount = 0;
    for (uint32_t i = 0; i < dispatchCount; ++i)
    {
        const FModelSkinningDispatch& dispatch = dispatches[i];
        if (dispatch.outputPositionsOffset > UINT32_MAX - dispatch.outputPositionsCount || dispatch.outputNormalsOffset > UINT32_MAX - dispatch.outputNormalsCount)
            return Fail(error, "A model skinning output range overflows");
        uint32_t positionEnd = 0;
        uint32_t normalEnd = 0;
        if (!StridedRangeEnd(dispatch.outputPositionsOffset, dispatch.outputPositionsCount, 3u, positionEnd) || !StridedRangeEnd(dispatch.outputNormalsOffset, dispatch.outputNormalsCount, 3u, normalEnd))
            return Fail(error, "A model skinning output range overflows");
        if (positionEnd > outputCount)
            outputCount = positionEnd;
        if (normalEnd > outputCount)
            outputCount = normalEnd;
        if (dispatch.facesCount > faceCount)
            faceCount = dispatch.facesCount;
        if (!geometryBuffers[i] || dispatch.matricesCount > UINT32_MAX / 6u || !DispatchRangesFit(dispatch, static_cast<uint32_t>(geometryBuffers[i]->mSize / sizeof(FModelSkinningRecord)), matrixRecordCount, UINT32_MAX))
            return Fail(error, "A model skinning dispatch range exceeds its geometry or matrix buffer");
    }
    if (minimumOutputFloat4Count > UINT32_MAX / 16u)
        return Fail(error, "The model skinning output seed range exceeds the supported size");
    if (minimumOutputFloat4Count > outputCount)
        outputCount = minimumOutputFloat4Count;
    if (!CreateFrameBuffers(frameIndex, matrixRecordCount, faceCount, outputCount, error))
        return false;
    if (matrixRecordCount)
        memcpy(matrices_[frameIndex]->pCpuMappedAddress, matrices, static_cast<size_t>(matrixRecordCount) * sizeof(FModelSkinningRecord));
    for (uint32_t model = 0; model < dispatchCount; ++model)
    {
        if (!DispatchRangesFit(dispatches[model], static_cast<uint32_t>(geometryBuffers[model]->mSize / sizeof(FModelSkinningRecord)), matrixRecordCount, outputCount))
        {
            ReleaseFrameBuffers(frameIndex);
            return Fail(error, "A model skinning output range exceeds its frame buffer");
        }
        for (uint32_t phase = 0; phase < kPhasesPerModel; ++phase)
        {
            const uint32_t slot = model * kPhasesPerModel + phase;
            uint8_t* constantBytes = static_cast<uint8_t*>(constants_[frameIndex]->pCpuMappedAddress) + static_cast<size_t>(slot) * kConstantSlotBytes;
            CopyDispatchRanges(*reinterpret_cast<FModelSkinningConstants*>(constantBytes), dispatches[model], phase);
            const uint32_t workCount = phase == 0 ? dispatches[model].positionsCount : phase == 1 ? dispatches[model].facesCount : dispatches[model].outputNormalsCount;
            preparedGroupCounts_[frameIndex][model][phase] = workCount / 64u + (workCount % 64u != 0 ? 1u : 0u);
            DescriptorDataRange constantRange = { slot * kConstantSlotBytes, sizeof(FModelSkinningConstants), 0 };
            DescriptorData data[7]{};
            data[0].mIndex = SRT_RES_IDX(ModelSkinningResources, PerDraw, gSkinRecords);
            data[0].mCount = 1;
            Buffer* geometryBuffer = geometryBuffers[model];
            data[0].ppBuffers = &geometryBuffer;
            data[1].mIndex = SRT_RES_IDX(ModelSkinningResources, PerDraw, gSkinMatrices);
            data[1].mCount = 1;
            data[1].ppBuffers = &matrices_[frameIndex];
            data[2].mIndex = SRT_RES_IDX(ModelSkinningResources, PerDraw, gSkinOutput);
            data[2].mCount = 1;
            data[2].ppBuffers = &outputs_[frameIndex];
            data[3].mIndex = SRT_RES_IDX(ModelSkinningResources, PerDraw, gSkinPrecisePositions);
            data[3].mCount = 1;
            data[3].ppBuffers = &precisePositions_[frameIndex];
            data[4].mIndex = SRT_RES_IDX(ModelSkinningResources, PerDraw, gSkinFaceNormals);
            data[4].mCount = 1;
            data[4].ppBuffers = &faceNormals_[frameIndex];
            data[5].mIndex = SRT_RES_IDX(ModelSkinningResources, PerDraw, gSkinConstants);
            data[5].mCount = 1;
            data[5].pRanges = &constantRange;
            data[5].ppBuffers = &constants_[frameIndex];
            data[6].mIndex = SRT_RES_IDX(ModelSkinningResources, PerDraw, gSkinErrors);
            data[6].mCount = 1;
            data[6].ppBuffers = &errorFlags_[frameIndex];
            updateDescriptorSet(renderer_, frameIndex * kDescriptorSlots + slot, descriptorSet_, 7, data);
        }
    }
    preparedDispatchCounts_[frameIndex] = dispatchCount;
    error.Clear();
    return true;
}

bool FModelSkinningRenderer::SeedOutputBuffer(Cmd* command, uint32_t frameIndex, Buffer* source, uint32_t float4Count, String& error)
{
    if (!renderer_ || !command || frameIndex >= kFramesInFlight || !outputs_[frameIndex] || !errorFlags_[frameIndex] || !matrices_[frameIndex] || !source || !source->mDx.pResource || !outputs_[frameIndex]->mDx.pResource || !errorFlags_[frameIndex]->mDx.pResource || float4Count > outputCapacities_[frameIndex] || static_cast<uint64_t>(float4Count) * 16u > source->mSize)
        return Fail(error, "The model skinning output seed range is invalid");
    BufferBarrier errorBarrier{};
    errorBarrier.pBuffer = errorFlags_[frameIndex];
    errorBarrier.mCurrentState = errorStates_[frameIndex];
    errorBarrier.mNewState = RESOURCE_STATE_COPY_DEST;
    cmdResourceBarrier(command, 1, &errorBarrier, 0, nullptr, 0, nullptr);
    command->mDx.pCmdList->CopyBufferRegion(errorFlags_[frameIndex]->mDx.pResource, 0, matrices_[frameIndex]->mDx.pResource, 0, sizeof(uint32_t));
    errorBarrier.mCurrentState = RESOURCE_STATE_COPY_DEST;
    errorBarrier.mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
    cmdResourceBarrier(command, 1, &errorBarrier, 0, nullptr, 0, nullptr);
    errorStates_[frameIndex] = RESOURCE_STATE_UNORDERED_ACCESS;
    if (float4Count == 0)
    {
        error.Clear();
        return true;
    }
    BufferBarrier outputBarrier{};
    outputBarrier.pBuffer = outputs_[frameIndex];
    outputBarrier.mCurrentState = outputStates_[frameIndex];
    outputBarrier.mNewState = RESOURCE_STATE_COPY_DEST;
    cmdResourceBarrier(command, 1, &outputBarrier, 0, nullptr, 0, nullptr);
    command->mDx.pCmdList->CopyBufferRegion(outputs_[frameIndex]->mDx.pResource, 0, source->mDx.pResource, 0, static_cast<uint64_t>(float4Count) * 16u);
    outputBarrier.mCurrentState = RESOURCE_STATE_COPY_DEST;
    outputBarrier.mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
    cmdResourceBarrier(command, 1, &outputBarrier, 0, nullptr, 0, nullptr);
    outputStates_[frameIndex] = RESOURCE_STATE_UNORDERED_ACCESS;
    error.Clear();
    return true;
}

bool FModelSkinningRenderer::Dispatch(Cmd* command, uint32_t frameIndex, String& error)
{
    if (!renderer_ || !command || frameIndex >= kFramesInFlight || !pipeline_ || !descriptorSet_ || !outputs_[frameIndex] || outputStates_[frameIndex] != RESOURCE_STATE_UNORDERED_ACCESS)
        return Fail(error, "model skinning dispatch parameters are invalid");
    cmdBindPipeline(command, pipeline_);
    for (uint32_t model = 0; model < preparedDispatchCounts_[frameIndex]; ++model)
    {
        for (uint32_t phase = 0; phase < kPhasesPerModel; ++phase)
        {
            const uint32_t slot = model * kPhasesPerModel + phase;
            cmdBindDescriptorSet(command, frameIndex * kDescriptorSlots + slot, descriptorSet_);
            const uint32_t groupCount = preparedGroupCounts_[frameIndex][model][phase];
            if (groupCount)
                cmdDispatch(command, groupCount, 1, 1);
            if (phase == 0)
            {
                BufferBarrier barrier{};
                barrier.pBuffer = outputs_[frameIndex];
                barrier.mCurrentState = RESOURCE_STATE_UNORDERED_ACCESS;
                barrier.mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
                cmdResourceBarrier(command, 1, &barrier, 0, nullptr, 0, nullptr);
                BufferBarrier preciseBarrier{};
                preciseBarrier.pBuffer = precisePositions_[frameIndex];
                preciseBarrier.mCurrentState = RESOURCE_STATE_UNORDERED_ACCESS;
                preciseBarrier.mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
                cmdResourceBarrier(command, 1, &preciseBarrier, 0, nullptr, 0, nullptr);
            }
            else if (phase == 1)
            {
                BufferBarrier faceNormalBarrier{};
                faceNormalBarrier.pBuffer = faceNormals_[frameIndex];
                faceNormalBarrier.mCurrentState = RESOURCE_STATE_UNORDERED_ACCESS;
                faceNormalBarrier.mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
                cmdResourceBarrier(command, 1, &faceNormalBarrier, 0, nullptr, 0, nullptr);
            }
        }
        if (model + 1u < preparedDispatchCounts_[frameIndex])
        {
            // 次modelのphase0・phase1が共有scratchを再利用する前に、現在の読み書きを完了させる。
            BufferBarrier scratchBarriers[2]{};
            scratchBarriers[0].pBuffer = precisePositions_[frameIndex];
            scratchBarriers[0].mCurrentState = RESOURCE_STATE_UNORDERED_ACCESS;
            scratchBarriers[0].mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
            scratchBarriers[1].pBuffer = faceNormals_[frameIndex];
            scratchBarriers[1].mCurrentState = RESOURCE_STATE_UNORDERED_ACCESS;
            scratchBarriers[1].mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
            cmdResourceBarrier(command, 2, scratchBarriers, 0, nullptr, 0, nullptr);
        }
    }
    BufferBarrier outputToShader{};
    outputToShader.pBuffer = outputs_[frameIndex];
    outputToShader.mCurrentState = RESOURCE_STATE_UNORDERED_ACCESS;
    outputToShader.mNewState = RESOURCE_STATE_SHADER_RESOURCE;
    cmdResourceBarrier(command, 1, &outputToShader, 0, nullptr, 0, nullptr);
    outputStates_[frameIndex] = RESOURCE_STATE_SHADER_RESOURCE;
    BufferBarrier errorToCopy{};
    errorToCopy.pBuffer = errorFlags_[frameIndex];
    errorToCopy.mCurrentState = RESOURCE_STATE_UNORDERED_ACCESS;
    errorToCopy.mNewState = RESOURCE_STATE_COPY_SOURCE;
    cmdResourceBarrier(command, 1, &errorToCopy, 0, nullptr, 0, nullptr);
    command->mDx.pCmdList->CopyBufferRegion(errorReadbacks_[frameIndex]->mDx.pResource, 0, errorFlags_[frameIndex]->mDx.pResource, 0, sizeof(uint32_t));
    errorToCopy.mCurrentState = RESOURCE_STATE_COPY_SOURCE;
    errorToCopy.mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
    cmdResourceBarrier(command, 1, &errorToCopy, 0, nullptr, 0, nullptr);
    errorStates_[frameIndex] = RESOURCE_STATE_UNORDERED_ACCESS;
    error.Clear();
    return true;
}

bool FModelSkinningRenderer::ReadErrorFlag(uint32_t frameIndex, Fence* fence, String& error)
{
    if (!renderer_ || frameIndex >= kFramesInFlight || !fence || !errorReadbacks_[frameIndex] || !errorReadbacks_[frameIndex]->pCpuMappedAddress)
        return Fail(error, "The model skinning error readback is unavailable");
    waitForFences(renderer_, 1, &fence);
    const uint32_t flags = *static_cast<const uint32_t*>(errorReadbacks_[frameIndex]->pCpuMappedAddress);
    if (flags & 1u)
        return Fail(error, "GPU model skinning produced a non-finite position");
    if (flags & 2u)
        return Fail(error, "GPU model skinning produced a non-finite face normal");
    if (flags & 4u)
        return Fail(error, "GPU model skinning produced a degenerate referenced normal");
    error.Clear();
    return true;
}

Buffer* FModelSkinningRenderer::OutputBuffer(uint32_t frameIndex) const
{
    return frameIndex < kFramesInFlight ? outputs_[frameIndex] : nullptr;
}

void FModelSkinningRenderer::Shutdown()
{
    if (!renderer_)
        return;
    if (descriptorSet_)
        removeDescriptorSet(renderer_, descriptorSet_);
    descriptorSet_ = nullptr;
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        ReleaseFrameBuffers(frame);
        preparedDispatchCounts_[frame] = 0;
        memset(preparedGroupCounts_[frame], 0, sizeof(preparedGroupCounts_[frame]));
    }
    if (pipeline_)
        removePipeline(renderer_, pipeline_);
    if (shader_)
        removeShader(renderer_, shader_);
    pipeline_ = nullptr;
    shader_ = nullptr;
    renderer_ = nullptr;
    doublePrecisionSupported_ = false;
}

}

#endif
