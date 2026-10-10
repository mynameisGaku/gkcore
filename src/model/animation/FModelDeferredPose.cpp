// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FModelDeferredPose.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/ModelSnapshot.h"
#include "model/Model.h"
#include <float.h>
#include <math.h>
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#endif

/**
 * 描画予約時のsparse姿勢を評価し、元モデルを保持する処理。
 */
namespace gk::model
{
namespace
{
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
/**
 * capture専用の処理時間集計を保持する。
 */
struct FDeferredPoseProfileState
{
    // QPCの1秒あたりtick数。
    int64_t frequency = 0;
    // 100回分の姿勢処理と各phaseのtick。
    uint64_t ticks[5]{};
    // 集計中の姿勢評価数。
    uint32_t calls = 0;
    // 環境変数を確認済みか示す。
    bool initialized = false;
    // capture計測が有効か示す。
    bool enabled = false;
};

// deferred pose処理のcapture専用集計。
FDeferredPoseProfileState gDeferredPoseProfile{};

/**
 * QPC値を読み、失敗時は0を返す。
 */
int64_t ReadDeferredPoseCounter()
{
    LARGE_INTEGER counter{};
    return QueryPerformanceCounter(&counter) ? counter.QuadPart : 0;
}

/**
 * 環境変数からcapture計測状態を一度だけ初期化する。
 */
bool IsDeferredPoseProfileEnabled()
{
    if (!gDeferredPoseProfile.initialized)
    {
        gDeferredPoseProfile.initialized = true;
        const char* value = getenv("GK_RENDER_PROFILE");
        LARGE_INTEGER frequency{};
        gDeferredPoseProfile.enabled = value && value[0] == '1' && value[1] == '\0' && QueryPerformanceFrequency(&frequency);
        gDeferredPoseProfile.frequency = gDeferredPoseProfile.enabled ? frequency.QuadPart : 0;
    }
    return gDeferredPoseProfile.enabled;
}

/**
 * deferred pose処理のphaseごとの時間を記録する。
 */
class FDeferredPoseProfileCall
{
  public:
    FDeferredPoseProfileCall() : enabled_(IsDeferredPoseProfileEnabled()), totalStart_(enabled_ ? ReadDeferredPoseCounter() : 0)
    {
    }

    void Start(uint32_t phase)
    {
        if (enabled_ && phase < 4)
            starts_[phase] = ReadDeferredPoseCounter();
    }

    void Stop(uint32_t phase)
    {
        if (!enabled_ || phase >= 4)
            return;
        const int64_t finish = ReadDeferredPoseCounter();
        if (finish > starts_[phase])
            gDeferredPoseProfile.ticks[phase] += static_cast<uint64_t>(finish - starts_[phase]);
    }

    ~FDeferredPoseProfileCall()
    {
        if (!enabled_)
            return;
        const int64_t finish = ReadDeferredPoseCounter();
        if (finish > totalStart_)
            gDeferredPoseProfile.ticks[4] += static_cast<uint64_t>(finish - totalStart_);
        ++gDeferredPoseProfile.calls;
        if (gDeferredPoseProfile.calls == 100)
            Report();
    }

  private:
    void Report()
    {
        if (gDeferredPoseProfile.frequency > 0)
        {
            const double millisecondsPerTick = 1000.0 / static_cast<double>(gDeferredPoseProfile.frequency);
            const double divisor = static_cast<double>(gDeferredPoseProfile.calls);
            fprintf(stderr, "{\"type\":\"gkcore_model_deferred_pose_profile\",\"calls\":%u,\"poseEvaluationMeanMs\":%.6f,\"deformOrMatrixEvaluationMeanMs\":%.6f,\"geometryValidationOrBlendPositionsMeanMs\":%.6f,\"allocationRetentionMeanMs\":%.6f,\"totalMeanMs\":%.6f}\n", gDeferredPoseProfile.calls, gDeferredPoseProfile.ticks[0] * millisecondsPerTick / divisor, gDeferredPoseProfile.ticks[1] * millisecondsPerTick / divisor, gDeferredPoseProfile.ticks[2] * millisecondsPerTick / divisor, gDeferredPoseProfile.ticks[3] * millisecondsPerTick / divisor, gDeferredPoseProfile.ticks[4] * millisecondsPerTick / divisor);
        }
        for (uint32_t phase = 0; phase < 5; ++phase)
            gDeferredPoseProfile.ticks[phase] = 0;
        gDeferredPoseProfile.calls = 0;
    }

    // この呼び出しでQPCを読むか示す。
    bool enabled_;
    // 姿勢評価全体の開始時刻。
    int64_t totalStart_;
    // 各phaseの開始時刻。
    int64_t starts_[4]{};
};
#endif

/**
 * 最終packet参照を失った姿勢と保持中の元モデルを破棄する。
 */
void DestroyDeferredPose(RefCounted* reference)
{
    auto* pose = reinterpret_cast<FModelDeferredPose*>(reference);
    if (pose->source)
        Release(&pose->source->reference);
    delete pose;
}

/**
 * unsupported条件を描画側の通常snapshot fallbackへ伝える。
 */
FModelDeferredPose* UnsupportedPose(String& error)
{
    error.Clear();
    return nullptr;
}

/**
 * CRT呼び出しを避けてfloat32の有限値を確認する。
 */
bool IsFiniteDeferredValue(float value)
{
    return value >= -FLT_MAX && value <= FLT_MAX;
}

/**
 * unique geometryは各値を一度だけ、source mapはindexだけを調べる。
 */
bool ValidateSparseGeometry(const Array<animation::FModelSparseVertexMap>& sparseMap, uint32_t sourceVertexCount, const animation::FModelSparsePoseGeometry& geometry, String& error)
{
    if (sparseMap.Count() != sourceVertexCount)
    {
        error.Assign("deferred model sparse vertex map count is invalid");
        return false;
    }
    for (uint32_t index = 0; index < geometry.positions.Count(); ++index)
    {
        for (uint32_t component = 0; component < 4; ++component)
        {
            if (!IsFiniteDeferredValue(geometry.positions.At(index).value[component]))
            {
                error.Assign("deferred model sparse position is non-finite");
                return false;
            }
        }
    }
    for (uint32_t index = 0; index < geometry.normals.Count(); ++index)
    {
        for (uint32_t component = 0; component < 4; ++component)
        {
            if (!IsFiniteDeferredValue(geometry.normals.At(index).value[component]))
            {
                error.Assign("deferred model sparse normal is non-finite");
                return false;
            }
        }
    }
    for (uint32_t vertex = 0; vertex < sparseMap.Count(); ++vertex)
    {
        const auto& mapping = sparseMap.At(vertex);
        if (mapping.positionIndex >= geometry.positions.Count() || mapping.normalIndex >= geometry.normals.Count())
        {
            error.Assign("deferred model sparse vertex index is invalid");
            return false;
        }
    }
    error.Clear();
    return true;
}

/**
 * 固定長のゼロ領域を小さな連続copyで追加する。
 */
template <class T> bool AppendZeroValues(Array<T>& output, uint32_t count)
{
    static const T zeroValues[256]{};
    while (count)
    {
        const uint32_t chunkCount = count < 256 ? count : 256;
        if (!output.AppendRange(zeroValues, chunkCount))
            return false;
        count -= chunkCount;
    }
    return true;
}

/**
 * BLENDが参照するunique位置だけを倍精度skin行列で計算し、GPU用の空き領域を揃える。
 */
bool BuildGpuEvaluationGeometry(const detail::ModelResource& source, const Array<animation::FModelSparseVertexMap>& sparseMap, bool sparseMapValidated, const animation::FModelGpuSkinningGeometry& gpuGeometry, const Array<animation::FModelGpuSkinningGeometry::FMatrix>& matrices, animation::FModelSparsePoseGeometry& output, String& error)
{
    using FVector4 = animation::FModelSparsePoseGeometry::FModelVector4;
    if (sparseMap.Count() != source.vertices.Count() || gpuGeometry.positions.Count() == 0 || gpuGeometry.influenceRanges.Count() != gpuGeometry.positions.Count() || gpuGeometry.clusters.Count() != matrices.Count() || !gpuGeometry.normalGroupRanges.Count())
    {
        error.Assign("GPU deferred model geometry dimensions are invalid");
        return false;
    }
    Array<uint8_t> requiredPositions;
    Array<uint32_t> requestedPositionIndices;
    animation::FModelSparsePoseGeometry candidate;
    if (!requiredPositions.Reserve(gpuGeometry.positions.Count()) || !requestedPositionIndices.Reserve(gpuGeometry.positions.Count()) || !candidate.positions.Reserve(gpuGeometry.positions.Count()) || !candidate.normals.Reserve(gpuGeometry.normalGroupRanges.Count()))
    {
        error.Assign("GPU deferred model geometry allocation failed");
        return false;
    }
    if (!AppendZeroValues(requiredPositions, gpuGeometry.positions.Count()) || !AppendZeroValues(candidate.positions, gpuGeometry.positions.Count()) || !AppendZeroValues(candidate.normals, gpuGeometry.normalGroupRanges.Count()))
    {
        error.Assign("GPU deferred model placeholder allocation failed");
        return false;
    }
    for (uint32_t vertex = 0; !sparseMapValidated && vertex < sparseMap.Count(); ++vertex)
    {
        const auto& mapping = sparseMap.At(vertex);
        if (mapping.positionIndex >= candidate.positions.Count() || mapping.normalIndex >= candidate.normals.Count())
        {
            error.Assign("GPU deferred model sparse vertex index is invalid");
            return false;
        }
    }
    const detail::ModelPrimitive* primitives = source.primitives.Data();
    const uint32_t* sourceIndices = source.indices.Data();
    const animation::FModelSparseVertexMap* sparseMapValues = sparseMap.Data();
    uint8_t* requiredPositionValues = requiredPositions.Data();
    const animation::FModelGpuSkinningGeometry::FPosition* bindPositions = gpuGeometry.positions.Data();
    const animation::FModelGpuSkinningGeometry::FInfluenceRange* influenceRanges = gpuGeometry.influenceRanges.Data();
    const animation::FModelGpuSkinningGeometry::FInfluence* influences = gpuGeometry.influences.Data();
    const animation::FModelGpuSkinningGeometry::FMatrix* matrixValues = matrices.Data();
    const animation::FModelGpuSkinningGeometry::FSegment* segments = gpuGeometry.segments.Data();
    FVector4* posedPositions = candidate.positions.Data();
    for (uint32_t primitiveIndex = 0; primitiveIndex < source.primitives.Count(); ++primitiveIndex)
    {
        const detail::ModelPrimitive& primitive = primitives[primitiveIndex];
        if (primitive.materialIndex < 0)
            continue;
        if (static_cast<uint32_t>(primitive.materialIndex) >= source.materials.Count())
        {
            error.Assign("GPU deferred model material index is invalid");
            return false;
        }
        if (!source.materials.At(static_cast<uint32_t>(primitive.materialIndex)).alphaBlend)
            continue;
        if (primitive.firstIndex > source.indices.Count() || primitive.indexCount > source.indices.Count() - primitive.firstIndex)
        {
            error.Assign("GPU deferred model BLEND index range is invalid");
            return false;
        }
        for (uint32_t offset = 0; offset < primitive.indexCount; ++offset)
        {
            const uint32_t vertex = sourceIndices[primitive.firstIndex + offset];
            if (vertex >= sparseMap.Count())
            {
                error.Assign("GPU deferred model BLEND vertex index is invalid");
                return false;
            }
            const uint32_t positionIndex = sparseMapValues[vertex].positionIndex;
            if (positionIndex >= gpuGeometry.positions.Count())
            {
                error.Assign("GPU deferred model BLEND position index is invalid");
                return false;
            }
            requiredPositionValues[positionIndex] = 1;
        }
    }
    for (uint32_t positionIndex = 0; positionIndex < requiredPositions.Count(); ++positionIndex)
    {
        if (requiredPositionValues[positionIndex] && !requestedPositionIndices.Append(positionIndex))
        {
            error.Assign("GPU deferred model BLEND position list allocation failed");
            return false;
        }
    }
    for (uint32_t requestIndex = 0; requestIndex < requestedPositionIndices.Count(); ++requestIndex)
    {
        const uint32_t positionIndex = requestedPositionIndices.Data()[requestIndex];
        const auto& bindPosition = bindPositions[positionIndex];
        const auto& influenceRange = influenceRanges[positionIndex];
        if (influenceRange.firstInfluence > gpuGeometry.influences.Count() || influenceRange.influenceCount > gpuGeometry.influences.Count() - influenceRange.firstInfluence)
        {
            error.Assign("GPU deferred model BLEND position has an invalid skin influence range");
            return false;
        }
        double result[3]{};
        double totalWeight = 0.0;
        for (uint32_t influenceIndex = 0; influenceIndex < influenceRange.influenceCount; ++influenceIndex)
        {
            const auto& influence = influences[influenceRange.firstInfluence + influenceIndex];
            if (influence.clusterIndex >= matrices.Count() || !isfinite(influence.weight) || influence.weight < 0.0)
            {
                error.Assign("GPU deferred model BLEND influence is invalid");
                return false;
            }
            totalWeight += influence.weight;
            const double* matrix = matrixValues[influence.clusterIndex].value;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const uint32_t row = axis * 4;
                const double transformed = matrix[row] * bindPosition.value[0] + matrix[row + 1] * bindPosition.value[1] + matrix[row + 2] * bindPosition.value[2] + matrix[row + 3];
                result[axis] += transformed * influence.weight;
            }
        }
        if (!isfinite(totalWeight))
        {
            error.Assign("GPU deferred model BLEND influence weight is non-finite");
            return false;
        }
        if (totalWeight <= 0.0)
        {
            const animation::FModelGpuSkinningGeometry::FSegment* segment = nullptr;
            for (uint32_t segmentIndex = 0; segmentIndex < gpuGeometry.segments.Count(); ++segmentIndex)
            {
                const auto& candidateSegment = segments[segmentIndex];
                if (positionIndex >= candidateSegment.firstPosition && positionIndex - candidateSegment.firstPosition < candidateSegment.positionCount)
                {
                    segment = &candidateSegment;
                    break;
                }
            }
            if (!segment || segment->clusterCount == 0 || segment->firstCluster > matrices.Count() || segment->clusterCount > matrices.Count() - segment->firstCluster)
            {
                error.Assign("GPU deferred model BLEND fallback segment is invalid");
                return false;
            }
            const double* matrix = matrixValues[segment->firstCluster + segment->clusterCount - 1].value;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const uint32_t row = axis * 4;
                result[axis] = matrix[row] * bindPosition.value[0] + matrix[row + 1] * bindPosition.value[1] + matrix[row + 2] * bindPosition.value[2] + matrix[row + 3];
            }
        }
        FVector4 posed{};
        posed.value[3] = 1.0f;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(result[axis]) || fabs(result[axis]) > FLT_MAX)
            {
                error.Assign("GPU deferred model BLEND position is non-finite");
                return false;
            }
            posed.value[axis] = static_cast<float>(result[axis]);
        }
        posedPositions[positionIndex] = posed;
    }
    output.positions.MoveFrom(candidate.positions);
    output.normals.MoveFrom(candidate.normals);
    error.Clear();
    return true;
}

/**
 * capture検証ではCPU参照を残し、通常runtimeだけ全頂点skin計算を省く。
 */
bool VerifyGpuSkinningReferenceEnabled()
{
#if defined(GKCORE_TEST_FRAME_CAPTURE)
    const char* value = getenv("GKCORE_VERIFY_GPU_SKINNING");
    return value && value[0] == '1' && value[1] == '\0';
#else
    return false;
#endif
}
}

FModelDeferredPose* EvaluateDeferredModelPose(const detail::ModelResource& source, const FModelPlayback* playback, String& error)
{
    if (!playback)
        return UnsupportedPose(error);
    if (!isfinite(playback->blendWeight) || playback->blendWeight < 0.0f || playback->blendWeight > 1.0f)
    {
        error.Assign("model animation blend weight must be finite and in [0, 1]");
        return nullptr;
    }
    if (!playback->clips[0].asset && playback->ik.Count() == 0 && !playback->secondaryMotion)
        return UnsupportedPose(error);
    if (!source.animation || !source.animation->source || (playback->clips[0].asset && !playback->clips[0].asset->source) || (playback->clips[1].asset && !playback->clips[1].asset->source))
    {
        error.Assign("deferred model animation source is invalid");
        return nullptr;
    }
    const auto* animationSource = source.animation->source;
    const auto* sparseMap = animationSource->SparseVertexMap();
    if (!sparseMap || animationSource->Format() == EModelAnimationFormat::ObjSequence)
        return UnsupportedPose(error);
    if (sparseMap->Count() != source.vertices.Count())
    {
        error.Assign("deferred model sparse vertex map count is invalid");
        return nullptr;
    }
    if ((playback->clips[0].asset && playback->clips[0].asset->source->Format() == EModelAnimationFormat::ObjSequence) || (playback->clips[1].asset && playback->blendWeight > 0.0f && playback->clips[1].asset->source->Format() == EModelAnimationFormat::ObjSequence))
        return UnsupportedPose(error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    FDeferredPoseProfileCall profile;
    profile.Start(0);
#endif
    animation::FModelPose sampled;
    if (!EvaluateModelPlaybackPose(source, *playback, sampled, error))
        return nullptr;

#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(0);
    profile.Start(1);
#endif
    animation::FModelSparsePoseGeometry geometry;
    Array<animation::FModelGpuSkinningGeometry::FMatrix> gpuSkinningMatrices;
    const bool hasGpuSkinningGeometry = animationSource->GpuSkinningGeometry() != nullptr && animationSource->SupportsGpuSkinningPose(sampled);
    const bool deformed = hasGpuSkinningGeometry ? animationSource->DeformSparseWithGpuSkinningData(sampled, geometry, gpuSkinningMatrices, error) : animationSource->DeformSparse(sampled, geometry, error);
    if (!deformed)
    {
        if (error.Empty())
            error.Assign("sparse model deformation failed");
        return nullptr;
    }
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(1);
    profile.Start(2);
#endif
    if (!animationSource->SparseDeformationIsValidated() && !ValidateSparseGeometry(*sparseMap, source.vertices.Count(), geometry, error))
        return nullptr;
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(2);
    profile.Start(3);
#endif
    FModelDeferredPose* result = nullptr;
    try
    {
        result = new FModelDeferredPose;
    }
    catch (...)
    {
        error.Assign("deferred model pose allocation failed");
        return nullptr;
    }
    if (!Retain(&const_cast<detail::ModelResource&>(source).reference))
    {
        delete result;
        error.Assign("deferred model source reference limit exceeded");
        return nullptr;
    }
    result->source = const_cast<detail::ModelResource*>(&source);
    result->geometry.positions.MoveFrom(geometry.positions);
    result->geometry.normals.MoveFrom(geometry.normals);
    result->gpuSkinningMatrices.MoveFrom(gpuSkinningMatrices);
    result->frozenPose.localTransforms.MoveFrom(sampled.localTransforms);
    result->frozenPose.morphWeights.MoveFrom(sampled.morphWeights);
    result->reference = { 1, DestroyDeferredPose };
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(3);
#endif
    error.Clear();
    return result;
}

FModelDeferredPose* EvaluateGpuDeferredModelPose(const detail::ModelResource& source, const FModelPlayback* playback, String& error)
{
    if (!playback)
        return UnsupportedPose(error);
    if (!isfinite(playback->blendWeight) || playback->blendWeight < 0.0f || playback->blendWeight > 1.0f)
    {
        error.Assign("model animation blend weight must be finite and in [0, 1]");
        return nullptr;
    }
    if (!playback->clips[0].asset && playback->ik.Count() == 0 && !playback->secondaryMotion)
        return UnsupportedPose(error);
    if (!source.animation || !source.animation->source || (playback->clips[0].asset && !playback->clips[0].asset->source) || (playback->clips[1].asset && !playback->clips[1].asset->source))
    {
        error.Assign("GPU deferred model animation source is invalid");
        return nullptr;
    }
    const auto* animationSource = source.animation->source;
    const auto* sparseMap = animationSource->SparseVertexMap();
    const auto* gpuGeometry = animationSource->GpuSkinningGeometry();
    if (!sparseMap || !gpuGeometry || animationSource->Format() == EModelAnimationFormat::ObjSequence)
    {
        error.Clear();
        return nullptr;
    }
    if (sparseMap->Count() != source.vertices.Count())
    {
        error.Assign("GPU deferred model sparse vertex map count is invalid");
        return nullptr;
    }
    if ((playback->clips[0].asset && playback->clips[0].asset->source->Format() == EModelAnimationFormat::ObjSequence) || (playback->clips[1].asset && playback->blendWeight > 0.0f && playback->clips[1].asset->source->Format() == EModelAnimationFormat::ObjSequence))
        return UnsupportedPose(error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    FDeferredPoseProfileCall profile;
    profile.Start(0);
#endif
    animation::FModelPose sampled;
    if (!EvaluateModelPlaybackPose(source, *playback, sampled, error))
        return nullptr;
    if (!animationSource->SupportsGpuSkinningPose(sampled))
        return UnsupportedPose(error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(0);
    profile.Start(1);
#endif
    Array<animation::FModelGpuSkinningGeometry::FMatrix> matrices;
    if (!animationSource->EvaluateGpuSkinningMatrices(sampled, matrices, error))
    {
        if (error.Empty())
            error.Assign("GPU deferred model skinning matrix evaluation failed");
        return nullptr;
    }
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(1);
    profile.Start(2);
#endif
    animation::FModelSparsePoseGeometry geometry;
    const bool verifyReference = VerifyGpuSkinningReferenceEnabled();
    if (verifyReference)
    {
        if (!animationSource->DeformSparseWithGpuSkinningData(sampled, geometry, matrices, error))
        {
            if (error.Empty())
                error.Assign("GPU deferred model CPU reference deformation failed");
            return nullptr;
        }
        if (!animationSource->SparseDeformationIsValidated() && !ValidateSparseGeometry(*sparseMap, source.vertices.Count(), geometry, error))
            return nullptr;
    }
    else if (!BuildGpuEvaluationGeometry(source, *sparseMap, animationSource->SparseDeformationIsValidated(), *gpuGeometry, matrices, geometry, error))
        return nullptr;
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(2);
    profile.Start(3);
#endif
    FModelDeferredPose* result = nullptr;
    try
    {
        result = new FModelDeferredPose;
    }
    catch (...)
    {
        error.Assign("GPU deferred model pose allocation failed");
        return nullptr;
    }
    if (!Retain(&const_cast<detail::ModelResource&>(source).reference))
    {
        delete result;
        error.Assign("GPU deferred model source reference limit exceeded");
        return nullptr;
    }
    result->source = const_cast<detail::ModelResource*>(&source);
    result->geometry.positions.MoveFrom(geometry.positions);
    result->geometry.normals.MoveFrom(geometry.normals);
    result->gpuSkinningMatrices.MoveFrom(matrices);
    result->frozenPose.localTransforms.MoveFrom(sampled.localTransforms);
    result->frozenPose.morphWeights.MoveFrom(sampled.morphWeights);
    result->gpuEvaluationOnly = !verifyReference;
    result->reference = { 1, DestroyDeferredPose };
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(3);
#endif
    error.Clear();
    return result;
}

detail::ModelResource* MaterializeDeferredModelPose(const FModelDeferredPose& pose, String& error)
{
    if (!pose.source || !pose.source->animation || !pose.source->animation->source)
    {
        error.Assign("deferred model source is missing");
        return nullptr;
    }
    const auto* sparseMap = pose.source->animation->source->SparseVertexMap();
    if (!sparseMap)
    {
        error.Assign("deferred model sparse vertex map is invalid");
        return nullptr;
    }
    animation::FModelSparsePoseGeometry evaluatedGeometry;
    const animation::FModelSparsePoseGeometry* geometry = &pose.geometry;
    if (pose.gpuEvaluationOnly)
    {
        if (!pose.source->animation->source->DeformSparse(pose.frozenPose, evaluatedGeometry, error))
        {
            if (error.Empty())
                error.Assign("GPU deferred model CPU fallback deformation failed");
            return nullptr;
        }
        geometry = &evaluatedGeometry;
    }
    if (!pose.source->animation->source->SparseDeformationIsValidated() && !ValidateSparseGeometry(*sparseMap, pose.source->vertices.Count(), *geometry, error))
        return nullptr;
    auto* result = CloneModelSnapshot(*pose.source, error);
    if (!result)
        return nullptr;
    for (uint32_t i = 0; i < sparseMap->Count(); ++i)
    {
        const auto& mapping = sparseMap->At(i);
        const auto& position = geometry->positions.At(mapping.positionIndex).value;
        const auto& normal = geometry->normals.At(mapping.normalIndex).value;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            result->vertices.At(i).position[axis] = position[axis];
            result->vertices.At(i).normal[axis] = normal[axis];
        }
    }
    result->isPoseSnapshot = true;
    error.Clear();
    return result;
}
}
