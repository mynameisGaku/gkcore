// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include "core/Context.h"
#include "internal/Backend.hpp"
#include "model/Model.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelDeferredPose.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/ModelPose.h"
#include <filesystem>
#include <float.h>
#include <fstream>
#include <math.h>
#include <stdio.h>

namespace
{
// sparse試験sourceが出力する位置成分。
float gDeferredTestPositionX = 0.75f;
// sparse試験sourceが出力する法線成分。
float gDeferredTestNormalX = 0.0f;
// map範囲検査を失敗させる試験条件。
bool gDeferredTestInvalidMapping = false;
uint32_t gDeferredSourceDestructions = 0;
uint32_t gDeferredFullSparseCalls = 0;
bool gDeferredTestGpuPoseSupported = true;
class AAnimationTestBackend;
AAnimationTestBackend* gAnimationTestBackend = nullptr;

/**
 * sparse姿勢ownerの寿命とmaterializeを調べる小さなsource。
 */
class ADeferredPoseTestSource final : public gk::model::AModelAnimationSource
{
  public:
    explicit ADeferredPoseTestSource(bool gpuSkinning = false) : gpuSkinning_(gpuSkinning)
    {
        gk::model::animation::FModelBoneTransform transform{};
        transform.rotation[3] = 1.0f;
        transform.scale[0] = 1.0f;
        transform.scale[1] = 1.0f;
        transform.scale[2] = 1.0f;
        skeleton_.parents.Append(-1);
        skeleton_.restLocalTransforms.Append(transform);
        if (gpuSkinning_)
        {
            for (uint32_t index = 0; index < 4; ++index)
            {
                mapping_.Append({ index, index });
                gk::model::animation::FModelGpuSkinningGeometry::FPosition position{};
                position.value[0] = index == 0 ? 40.0 : static_cast<double>(index);
                position.value[1] = index * 0.25;
                gpuGeometry_.positions.Append(position);
                const gk::model::animation::FModelGpuSkinningGeometry::FInfluenceRange range{ index, index == 0 ? 0u : 1u };
                gpuGeometry_.influenceRanges.Append(range);
                const gk::model::animation::FModelGpuSkinningGeometry::FInfluence influence{ 0, 1.0 };
                gpuGeometry_.influences.Append(influence);
                const gk::model::animation::FModelGpuSkinningGeometry::FNormalGroupRange normalRange{ index, 1 };
                gpuGeometry_.normalGroupRanges.Append(normalRange);
            }
            gk::model::animation::FModelGpuSkinningGeometry::FCluster cluster{};
            cluster.nodeIndex = 0;
            gpuGeometry_.clusters.Append(cluster);
            gk::model::animation::FModelGpuSkinningGeometry::FSegment segment{};
            segment.positionCount = 4;
            segment.clusterCount = 1;
            segment.nodeIndex = 0;
            gpuGeometry_.segments.Append(segment);
        }
        else
            mapping_.Append({ gDeferredTestInvalidMapping ? 1u : 0u, 0 });
    }
    ~ADeferredPoseTestSource() override
    {
        ++gDeferredSourceDestructions;
    }
    gk::model::EModelAnimationFormat Format() const override
    {
        return gk::model::EModelAnimationFormat::Fbx;
    }
    const gk::model::animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton_;
    }
    const char* BoneName(uint32_t bone) const override
    {
        return bone == 0 ? "root" : nullptr;
    }
    const char* MorphName(uint32_t) const override
    {
        return nullptr;
    }
    uint32_t ClipCount() const override
    {
        return 1;
    }
    const char* ClipName(uint32_t clip) const override
    {
        return clip == 0 ? "move" : nullptr;
    }
    double ClipDuration(uint32_t clip) const override
    {
        return clip == 0 ? 1.0 : -1.0;
    }
    bool Sample(uint32_t clip, double seconds, gk::model::animation::FModelPose& output, gk::String& error) const override
    {
        if (clip != 0 || !gk::model::animation::InitializeModelPose(skeleton_, output, error))
            return false;
        output.localTransforms.At(0).position[0] = static_cast<float>(seconds);
        error.Clear();
        return true;
    }
    bool Deform(const gk::model::animation::FModelPose&, gk::detail::ModelResource&, gk::String& error) const override
    {
        error.Clear();
        return true;
    }
    const gk::Array<gk::model::animation::FModelSparseVertexMap>* SparseVertexMap() const override
    {
        return &mapping_;
    }
    const gk::model::animation::FModelGpuSkinningGeometry* GpuSkinningGeometry() const override
    {
        return gpuSkinning_ ? &gpuGeometry_ : nullptr;
    }
    bool SupportsGpuSkinningPose(const gk::model::animation::FModelPose& pose) const override
    {
        return gpuSkinning_ && gDeferredTestGpuPoseSupported && pose.localTransforms.Count() == 1 && pose.morphWeights.Count() == 0;
    }
    bool EvaluateGpuSkinningMatrices(const gk::model::animation::FModelPose& pose, gk::Array<gk::model::animation::FModelGpuSkinningGeometry::FMatrix>& output, gk::String& error) const override
    {
        if (!SupportsGpuSkinningPose(pose))
        {
            error.Assign("test source rejects this GPU pose");
            return false;
        }
        gk::model::animation::FModelGpuSkinningGeometry::FMatrix matrix{};
        matrix.value[0] = 1.0;
        matrix.value[3] = pose.localTransforms.At(0).position[0];
        matrix.value[5] = 1.0;
        matrix.value[10] = 1.0;
        if (!output.Append(matrix))
        {
            error.Assign("test GPU matrix allocation failed");
            return false;
        }
        error.Clear();
        return true;
    }
    bool DeformSparseWithGpuSkinningData(const gk::model::animation::FModelPose& pose, gk::model::animation::FModelSparsePoseGeometry& output, gk::Array<gk::model::animation::FModelGpuSkinningGeometry::FMatrix>& outputMatrices, gk::String& error) const override
    {
        gk::model::animation::FModelSparsePoseGeometry candidate;
        gk::Array<gk::model::animation::FModelGpuSkinningGeometry::FMatrix> matrices;
        if (!DeformSparse(pose, candidate, error) || !EvaluateGpuSkinningMatrices(pose, matrices, error))
            return false;
        output.positions.MoveFrom(candidate.positions);
        output.normals.MoveFrom(candidate.normals);
        outputMatrices.MoveFrom(matrices);
        error.Clear();
        return true;
    }
    bool DeformSparse(const gk::model::animation::FModelPose& pose, gk::model::animation::FModelSparsePoseGeometry& output, gk::String& error) const override
    {
        ++gDeferredFullSparseCalls;
        gk::model::animation::FModelSparsePoseGeometry candidate;
        if (gpuSkinning_)
        {
            for (uint32_t index = 0; index < gpuGeometry_.positions.Count(); ++index)
            {
                const auto& bind = gpuGeometry_.positions.At(index);
                const gk::model::animation::FModelSparsePoseGeometry::FModelVector4 posedPosition{ { static_cast<float>(bind.value[0]) + pose.localTransforms.At(0).position[0], static_cast<float>(bind.value[1]), 0.0f, 1.0f } };
                const gk::model::animation::FModelSparsePoseGeometry::FModelVector4 posedNormal{ { 0.0f, 0.0f, 1.0f, 0.0f } };
                if (!candidate.positions.Append(posedPosition) || !candidate.normals.Append(posedNormal))
                {
                    error.Assign("test full sparse allocation failed");
                    return false;
                }
            }
            output.positions.MoveFrom(candidate.positions);
            output.normals.MoveFrom(candidate.normals);
            error.Clear();
            return true;
        }
        gk::model::animation::FModelSparsePoseGeometry::FModelVector4 position{ { gDeferredTestPositionX, pose.localTransforms.At(0).position[1], 3.0f, 1.0f } };
        gk::model::animation::FModelSparsePoseGeometry::FModelVector4 normal{ { gDeferredTestNormalX, 0.0f, 1.0f, 0.0f } };
        if (!candidate.positions.Append(position) || !candidate.normals.Append(normal))
        {
            error.Assign("test sparse allocation failed");
            return false;
        }
        output.positions.MoveFrom(candidate.positions);
        output.normals.MoveFrom(candidate.normals);
        error.Clear();
        return true;
    }

  private:
    bool gpuSkinning_;
    gk::model::animation::FModelSkeleton skeleton_;
    gk::Array<gk::model::animation::FModelSparseVertexMap> mapping_;
    gk::model::animation::FModelGpuSkinningGeometry gpuGeometry_;
};

/**
 * 描画予約の姿勢だけを検査する、ウィンドウを持たないbackend。
 */
class AAnimationTestBackend final : public gk::detail::Backend
{
  public:
    bool Initialize(uint32_t, uint32_t, uint32_t, gk::String&) override
    {
        return true;
    }
    void Shutdown() override
    {
    }
    int ProcessMessage() override
    {
        return 0;
    }
    bool IsKeyDown(uint32_t) const override
    {
        return false;
    }
    bool SupportsSparseModelPoses() const override
    {
        return true;
    }
    bool Present(const gk::detail::FramePacket&, gk::String&) override
    {
        return true;
    }
    bool SupportsGpuModelSkinning() const override
    {
        return supportsGpuModelSkinning_;
    }
    gk::ShaderHandle LoadPixelShader(const char*, gk::String&) override
    {
        return {};
    }
    bool ReleasePixelShader(gk::ShaderHandle, gk::String&) override
    {
        return true;
    }

    bool supportsGpuModelSkinning_ = false;
};

/**
 * GPU mock描画で使うblend頂点付きmodelを登録する。
 */
gk::ModelHandle RegisterGpuTestModel(gk::String& error)
{
    auto* model = gk::detail::CreateModelResource();
    if (!model)
        return {};
    model->animation = gk::model::CreateModelAnimationAsset(new ADeferredPoseTestSource(true), error);
    for (uint32_t index = 0; index < 4; ++index)
    {
        gk::detail::ModelVertex vertex{};
        vertex.position[0] = static_cast<float>(index);
        if (!model->vertices.Append(vertex))
        {
            gk::Release(&model->reference);
            return {};
        }
    }
    model->materials.At(0).alphaBlend = true;
    const gk::detail::ModelPrimitive primitive{ 0, 3, 0 };
    const uint32_t indices[3] = { 1, 2, 3 };
    if (!model->animation || !model->primitives.Append(primitive) || !model->indices.AppendRange(indices, 3))
    {
        gk::Release(&model->reference);
        return {};
    }
    const gk::ModelHandle handle = gk::detail::RegisterModelResource(model, error);
    if (!handle.IsValid())
        return {};
    gk::detail::ModelTransform transform{};
    transform.handle = handle;
    transform.scale = { 1.0f, 1.0f, 1.0f };
    if (!gk::detail::GetContext().modelTransforms.Append(transform))
    {
        gk::DeleteModel(handle);
        error.Assign("GPU test model transform allocation failed");
        return {};
    }
    return handle;
}

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        fprintf(stderr, "%s: %s\n", message, gk::GetLastErrorMessage());
    }
    return condition;
}

/**
 * 指定したfloat値をsparse ownerが有限値として受け入れるか調べる。
 */
bool DeferredPoseValueAccepted(float positionX, float normalX, bool* referencesAtomic = nullptr)
{
    gDeferredTestPositionX = positionX;
    gDeferredTestNormalX = normalX;
    auto* source = gk::detail::CreateModelResource();
    if (!source)
        return false;
    gk::String error;
    source->animation = gk::model::CreateModelAnimationAsset(new ADeferredPoseTestSource, error);
    const gk::detail::ModelVertex vertex{};
    if (!source->animation || !source->vertices.Append(vertex))
    {
        gk::Release(&source->reference);
        return false;
    }
    gk::model::FModelPlayback playback;
    playback.clips[0].asset = source->animation;
    if (!gk::Retain(&playback.clips[0].asset->reference))
    {
        playback.clips[0].asset = nullptr;
        gk::Release(&source->reference);
        return false;
    }
    const uint32_t sourceReferenceCount = source->reference.references;
    const uint32_t destructionsBefore = gDeferredSourceDestructions;
    auto* deferred = gk::model::EvaluateDeferredModelPose(*source, &playback, error);
    const bool accepted = deferred && error.Empty();
    bool atomic = source->reference.references == sourceReferenceCount + (deferred ? 1u : 0u);
    if (deferred)
        gk::Release(&deferred->reference);
    atomic = atomic && source->reference.references == sourceReferenceCount;
    gk::Release(&playback.clips[0].asset->reference);
    playback.clips[0].asset = nullptr;
    gk::Release(&source->reference);
    atomic = atomic && gDeferredSourceDestructions == destructionsBefore + 1;
    if (referencesAtomic)
        *referencesAtomic = *referencesAtomic && atomic;
    gDeferredTestPositionX = 0.75f;
    gDeferredTestNormalX = 0.0f;
    return accepted;
}

bool DeferredPoseContract()
{
    gk::String error;
    auto* source = gk::detail::CreateModelResource();
    if (!source)
        return Check(false, "deferred source model allocation");
    source->animation = gk::model::CreateModelAnimationAsset(new ADeferredPoseTestSource, error);
    gk::detail::ModelVertex vertex{};
    vertex.position[0] = -1.0f;
    vertex.normal[2] = -1.0f;
    if (!source->animation || !source->vertices.Append(vertex))
    {
        gk::Release(&source->reference);
        return Check(false, "deferred source setup");
    }
    gDeferredSourceDestructions = 0;
    gk::model::FModelPlayback playback;
    playback.clips[0].asset = source->animation;
    playback.clips[0].seconds = 0.75;
    if (!gk::Retain(&playback.clips[0].asset->reference))
    {
        playback.clips[0].asset = nullptr;
        gk::Release(&source->reference);
        return Check(false, "deferred clip retain");
    }
    auto* deferred = gk::model::EvaluateDeferredModelPose(*source, &playback, error);
    const bool evaluated = deferred && deferred->geometry.positions.Count() == 1 && deferred->geometry.normals.Count() == 1 && fabsf(deferred->geometry.positions.At(0).value[0] - 0.75f) < 1e-6f;
    playback.clips[0].seconds = 0.0;
    const bool frozenAtReservation = deferred && fabsf(deferred->geometry.positions.At(0).value[0] - 0.75f) < 1e-6f;
    gk::Release(&playback.clips[0].asset->reference);
    playback.clips[0].asset = nullptr;
    gk::Release(&source->reference);
    source = nullptr;
    auto* materialized = deferred ? gk::model::MaterializeDeferredModelPose(*deferred, error) : nullptr;
    const bool expanded = materialized && materialized->isPoseSnapshot && materialized->vertices.Count() == 1 && fabsf(materialized->vertices.At(0).position[0] - 0.75f) < 1e-6f && fabsf(materialized->vertices.At(0).normal[2] - 1.0f) < 1e-6f;
    if (materialized)
        gk::Release(&materialized->reference);
    const bool retainedUntilOwnerRelease = gDeferredSourceDestructions == 0;
    if (deferred)
        gk::Release(&deferred->reference);
    const bool releasedWithOwner = gDeferredSourceDestructions == 1;
    auto* unsupportedSource = gk::detail::CreateModelResource();
    error.Assign("stale error");
    const bool unsupportedFallsBack = unsupportedSource && !gk::model::EvaluateDeferredModelPose(*unsupportedSource, nullptr, error) && error.Length() == 0;
    if (unsupportedSource)
        gk::Release(&unsupportedSource->reference);
    bool finiteReferencesAtomic = true;
    const bool finiteBoundary = DeferredPoseValueAccepted(FLT_MAX, 0.0f, &finiteReferencesAtomic) && DeferredPoseValueAccepted(-FLT_MAX, 0.0f, &finiteReferencesAtomic) && DeferredPoseValueAccepted(-0.0f, -0.0f, &finiteReferencesAtomic) && DeferredPoseValueAccepted(1.0e30f, 1.0e30f, &finiteReferencesAtomic);
    bool invalidReferencesAtomic = true;
    const bool nonFiniteRejected = !DeferredPoseValueAccepted(NAN, 0.0f, &invalidReferencesAtomic) && !DeferredPoseValueAccepted(INFINITY, 0.0f, &invalidReferencesAtomic) && !DeferredPoseValueAccepted(-INFINITY, 0.0f, &invalidReferencesAtomic) && !DeferredPoseValueAccepted(0.0f, NAN, &invalidReferencesAtomic) && !DeferredPoseValueAccepted(0.0f, INFINITY, &invalidReferencesAtomic) && !DeferredPoseValueAccepted(0.0f, -INFINITY, &invalidReferencesAtomic);
    gDeferredTestInvalidMapping = true;
    bool mappingReferencesAtomic = true;
    const bool invalidMappingRejected = !DeferredPoseValueAccepted(0.0f, 0.0f, &mappingReferencesAtomic);
    gDeferredTestInvalidMapping = false;
    return Check(evaluated && frozenAtReservation && expanded && retainedUntilOwnerRelease && releasedWithOwner && unsupportedFallsBack && finiteBoundary && finiteReferencesAtomic && nonFiniteRejected && invalidReferencesAtomic && invalidMappingRejected && mappingReferencesAtomic, "deferred pose ownership, fallback, finite-value, and atomic-failure contract");
}

/**
 * GPU skinning予約がBLEND位置だけを計算し、fallbackも同じposeを使うことを確認する。
 */
bool DeferredGpuPoseContract()
{
    AAnimationTestBackend capabilityProbe;
    const bool defaultCapabilityDisabled = !capabilityProbe.SupportsGpuModelSkinning();
    capabilityProbe.supportsGpuModelSkinning_ = true;
    const bool gpuCapabilityCanBeEnabled = capabilityProbe.SupportsGpuModelSkinning();
    capabilityProbe.supportsGpuModelSkinning_ = false;
    gk::String error;
    auto* source = gk::detail::CreateModelResource();
    if (!source)
        return Check(false, "GPU deferred source model allocation");
    source->animation = gk::model::CreateModelAnimationAsset(new ADeferredPoseTestSource(true), error);
    for (uint32_t index = 0; index < 4; ++index)
    {
        gk::detail::ModelVertex vertex{};
        vertex.position[0] = static_cast<float>(index);
        if (!source->vertices.Append(vertex))
        {
            gk::Release(&source->reference);
            return Check(false, "GPU deferred source vertices");
        }
    }
    source->materials.At(0).alphaBlend = true;
    const gk::detail::ModelPrimitive primitive{ 0, 3, 0 };
    const uint32_t blendIndices[3] = { 0, 2, 3 };
    if (!source->animation || !source->primitives.Append(primitive) || !source->indices.AppendRange(blendIndices, 3))
    {
        gk::Release(&source->reference);
        return Check(false, "GPU deferred source materials and indices");
    }
    gk::model::FModelPlayback playback;
    playback.clips[0].asset = source->animation;
    playback.clips[0].seconds = 0.75;
    if (!gk::Retain(&playback.clips[0].asset->reference))
    {
        playback.clips[0].asset = nullptr;
        gk::Release(&source->reference);
        return Check(false, "GPU deferred clip retain");
    }
    gDeferredFullSparseCalls = 0;
    auto* deferred = gk::model::EvaluateGpuDeferredModelPose(*source, &playback, error);
    const bool evaluatedOnlyBlendPositions = deferred && deferred->gpuEvaluationOnly && deferred->frozenPose.localTransforms.Count() == 1 && deferred->geometry.positions.Count() == 4 && deferred->geometry.normals.Count() == 4 && fabsf(deferred->geometry.positions.At(0).value[0] - 40.75f) < 1e-6f && deferred->geometry.positions.At(1).value[0] == 0.0f && fabsf(deferred->geometry.positions.At(3).value[0] - 3.75f) < 1e-6f && deferred->gpuSkinningMatrices.Count() == 1 && gDeferredFullSparseCalls == 0;
    gDeferredTestGpuPoseSupported = false;
    auto* cpuFallback = gk::model::EvaluateGpuDeferredModelPose(*source, &playback, error);
    auto* sparseFallback = gk::model::EvaluateDeferredModelPose(*source, &playback, error);
    const bool unsupportedPoseFallsBack = !cpuFallback && sparseFallback && error.Empty() && gDeferredFullSparseCalls == 1;
    gDeferredTestGpuPoseSupported = true;
    if (cpuFallback)
        gk::Release(&cpuFallback->reference);
    if (sparseFallback)
        gk::Release(&sparseFallback->reference);
    playback.clips[0].seconds = 0.0;
    gk::Release(&playback.clips[0].asset->reference);
    playback.clips[0].asset = nullptr;
    gk::Release(&source->reference);
    source = nullptr;
    auto* materialized = deferred ? gk::model::MaterializeDeferredModelPose(*deferred, error) : nullptr;
    const bool fallbackUsesFrozenPose = materialized && materialized->vertices.Count() == 4 && fabsf(materialized->vertices.At(0).position[0] - 40.75f) < 1e-6f && fabsf(materialized->vertices.At(3).position[0] - 3.75f) < 1e-6f && gDeferredFullSparseCalls == 2;
    if (materialized)
        gk::Release(&materialized->reference);
    if (deferred)
        gk::Release(&deferred->reference);
    bool cpuCapabilityFallback = false;
    bool gpuCapabilityUsesFrozenPose = false;
    if (gAnimationTestBackend)
    {
        gAnimationTestBackend->supportsGpuModelSkinning_ = false;
        const auto cpuHandle = RegisterGpuTestModel(error);
        if (cpuHandle.IsValid() && gk::PlayModelAnimation(cpuHandle, 0, false) == 0 && gk::SetModelAnimationTime(cpuHandle, 0.75) == 0 && gk::BeginFrame() == 0 && gk::DrawModel(cpuHandle) == 0)
        {
            const auto& draws = gk::detail::GetContext().frame.draws;
            cpuCapabilityFallback = draws.Count() == 1 && draws.At(0).deferredPose && !draws.At(0).deferredPose->gpuEvaluationOnly && gDeferredFullSparseCalls == 3;
            gk::DeleteModel(cpuHandle);
            cpuCapabilityFallback = cpuCapabilityFallback && gk::Present() == 0;
        }
        gAnimationTestBackend->supportsGpuModelSkinning_ = true;
        const auto gpuHandle = RegisterGpuTestModel(error);
        if (gpuHandle.IsValid() && gk::PlayModelAnimation(gpuHandle, 0, false) == 0 && gk::SetModelAnimationTime(gpuHandle, 0.75) == 0 && gk::BeginFrame() == 0 && gk::DrawModel(gpuHandle) == 0)
        {
            const auto& draws = gk::detail::GetContext().frame.draws;
            gpuCapabilityUsesFrozenPose = draws.Count() == 1 && draws.At(0).deferredPose && draws.At(0).deferredPose->gpuEvaluationOnly && draws.At(0).deferredPose->geometry.positions.At(0).value[0] == 0.0f && gDeferredFullSparseCalls == 3;
            gk::SetModelAnimationTime(gpuHandle, 0.0);
            gpuCapabilityUsesFrozenPose = gpuCapabilityUsesFrozenPose && fabsf(draws.At(0).deferredPose->geometry.positions.At(1).value[0] - 1.75f) < 1e-6f;
            gk::DeleteModel(gpuHandle);
            gpuCapabilityUsesFrozenPose = gpuCapabilityUsesFrozenPose && gk::Present() == 0;
        }
        gAnimationTestBackend->supportsGpuModelSkinning_ = false;
    }
    return Check(defaultCapabilityDisabled && gpuCapabilityCanBeEnabled && evaluatedOnlyBlendPositions && unsupportedPoseFallsBack && fallbackUsesFrozenPose && cpuCapabilityFallback && gpuCapabilityUsesFrozenPose, "GPU deferred capability, BLEND subset, frozen pose, and CPU materialization");
}

bool Contract(const char* directory)
{
    const auto root = std::filesystem::u8path(directory);
    std::filesystem::create_directories(root);
    const auto first = root / "sequence-0.obj";
    const auto second = root / "sequence-1.obj";
    const auto wrong = root / "sequence-wrong.obj";
    std::ofstream(first) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    std::ofstream(second) << "v 2 0 0\nv 3 0 0\nv 2 1 0\nf 1 2 3\n";
    std::ofstream(wrong) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 3 2\n";
    const auto firstName = first.u8string();
    const auto secondName = second.u8string();
    const auto wrongName = wrong.u8string();
    const char* paths[2] = { firstName.c_str(), secondName.c_str() };
    gAnimationTestBackend = new AAnimationTestBackend;
    gk::detail::SetBackendForTesting(gAnimationTestBackend);
    if (!Check(gk::Init() == 0, "Init"))
        return false;
    if (!DeferredPoseContract() || !DeferredGpuPoseContract())
        return false;
    const auto model = gk::LoadModelSequence(paths, 2, 1.0f);
    if (!Check(model.IsValid() && gk::GetModelAnimationCount(model) == 1, "sequence load"))
        return false;
    if (!Check(fabs(gk::GetModelAnimationDuration(model, 0) - 1.0) < 1e-6, "duration"))
        return false;
    const auto instance = gk::CreateModelInstance(model);
    if (!Check(instance.IsValid() && gk::PlayModelAnimation(model, 0, false) == 0, "independent instance"))
        return false;
    if (!Check(gk::SetModelAnimationTime(model, 0.5) == 0 && gk::BeginFrame() == 0 && gk::DrawModel(model) == 0, "first snapshot"))
        return false;
    const auto& draws = gk::detail::GetContext().frame.draws;
    const auto* frozen = draws.At(0).model;
    if (!Check(fabsf(frozen->vertices.At(0).position[0] - 1.0f) < 1e-5f, "midpoint position"))
        return false;
    if (!Check(gk::SetModelAnimationTime(model, 1.0) == 0 && gk::DrawModel(model) == 0 && gk::DrawModel(instance) == 0, "later independent poses"))
        return false;
    if (!Check(fabsf(frozen->vertices.At(0).position[0] - 1.0f) < 1e-5f && fabsf(draws.At(1).model->vertices.At(0).position[0] - 2.0f) < 1e-5f && fabsf(draws.At(2).model->vertices.At(0).position[0]) < 1e-5f, "frozen pose and instance isolation"))
        return false;
    if (!Check(gk::DeleteModel(model) == 0 && gk::Present() == 0, "queued model lifetime"))
        return false;
    const auto animation = gk::LoadModelSequenceAnimation(paths, 2, 1.0f);
    if (!Check(animation.IsValid() && gk::ApplyModelAnimation(instance, animation) == 0 && gk::DeleteModelAnimation(animation) == 0, "external clip lifetime"))
        return false;
    if (!Check(gk::SetModelAnimationTime(instance, 0.5) == 0 && gk::BeginFrame() == 0 && gk::DrawModel(instance) == 0 && fabsf(draws.At(0).model->vertices.At(0).position[0] - 1.0f) < 1e-5f && gk::Present() == 0, "external sequence midpoint"))
        return false;
    if (!Check(gk::SetModelAnimationTime(instance, NAN) == -1 && gk::SetModelAnimationSpeed(instance, INFINITY) == -1, "invalid clock"))
        return false;
    if (!Check(gk::SetModelAnimationLoop(instance, true) == 0 && gk::SetModelAnimationTime(instance, 0.75) == 0 && gk::UpdateModelAnimation(instance, 0.5) == 0 && fabs(gk::GetModelAnimationTime(instance) - 0.25) < 1e-6, "loop clock"))
        return false;
    if (!Check(gk::SetModelAnimationSpeed(instance, -1.0) == 0 && gk::UpdateModelAnimation(instance, 0.5) == 0 && fabs(gk::GetModelAnimationTime(instance) - 0.75) < 1e-6, "reverse playback"))
        return false;
    if (!Check(gk::SetModelAnimationBlend(instance, 0, 0.5f) == 0 && gk::SetModelAnimationLoop(instance, false, 0) == 0 && gk::SetModelAnimationLoop(instance, false, 1) == 0 && gk::SetModelAnimationTime(instance, 0.0) == 0 && gk::SetModelAnimationTime(instance, 1.0, 1) == 0, "independent blend clocks"))
        return false;
    if (!Check(gk::BeginFrame() == 0 && gk::DrawModel(instance) == 0 && fabsf(draws.At(0).model->vertices.At(0).position[0] - 1.0f) < 1e-5f && gk::SetModelAnimationBlendWeight(instance, 1.0f) == 0 && gk::DrawModel(instance) == 0 && fabsf(draws.At(1).model->vertices.At(0).position[0] - 2.0f) < 1e-5f && gk::SetModelAnimationBlendWeight(instance, NAN) == -1 && gk::Present() == 0, "sequence blend and frozen blend weight"))
        return false;
    // 負時刻はloop中に折り返し、loop停止中は先頭へ制限する。
    if (!Check(gk::SetModelAnimationLoop(instance, true, 0) == 0 && gk::SetModelAnimationTime(instance, -0.25) == 0 && fabs(gk::GetModelAnimationTime(instance, 0) - 0.75) < 1e-6 && gk::SetModelAnimationLoop(instance, false, 0) == 0 && gk::SetModelAnimationTime(instance, -0.25) == 0 && fabs(gk::GetModelAnimationTime(instance, 0)) < 1e-6, "negative time under both loop policies"))
        return false;
    // 極大有限倍率で更新がoverflowしても、両再生枠の時刻を途中変更しない。
    if (!Check(gk::SetModelAnimationTime(instance, 0.2, 0) == 0 && gk::SetModelAnimationTime(instance, 0.6, 1) == 0 && gk::SetModelAnimationSpeed(instance, DBL_MAX, 0) == 0 && gk::SetModelAnimationSpeed(instance, -DBL_MAX, 1) == 0, "overflow clock fixture"))
        return false;
    if (!Check(gk::UpdateModelAnimation(instance, 2.0) == -1 && fabs(gk::GetModelAnimationTime(instance, 0) - 0.2) < 1e-6 && fabs(gk::GetModelAnimationTime(instance, 1) - 0.6) < 1e-6, "overflow clock update is atomic"))
        return false;
    if (!Check(gk::SetModelAnimationSpeed(instance, 1.0, 0) == 0 && gk::SetModelAnimationSpeed(instance, 1.0, 1) == 0, "reset clock speeds"))
        return false;
    if (!Check(gk::StopModelAnimation(instance) == 0 && gk::BeginFrame() == 0 && gk::DrawModel(instance) == 0 && fabsf(draws.At(0).model->vertices.At(0).position[0]) < 1e-5f && gk::Present() == 0, "stop restores rest pose"))
        return false;
    paths[1] = wrongName.c_str();
    if (!Check(!gk::LoadModelSequence(paths, 2, 1.0f).IsValid(), "topology mismatch rejection"))
        return false;
    // 1frame sequenceは長さ0として扱い、時刻指定と更新を0へ保つ。
    const char* singlePath[1] = { firstName.c_str() };
    const auto singleFrame = gk::LoadModelSequence(singlePath, 1, 24.0f);
    if (!Check(singleFrame.IsValid() && gk::GetModelAnimationDuration(singleFrame, 0) == 0.0 && gk::PlayModelAnimation(singleFrame, 0, true) == 0 && gk::SetModelAnimationTime(singleFrame, -10.0) == 0 && gk::GetModelAnimationTime(singleFrame) == 0.0 && gk::UpdateModelAnimation(singleFrame, 10.0) == 0 && gk::GetModelAnimationTime(singleFrame) == 0.0, "zero-duration sequence clock"))
        return false;
    if (!Check(gk::DeleteModel(singleFrame) == 0, "zero-duration sequence cleanup"))
        return false;
    gk::Shutdown();
    return true;
}
}

int main(int argc, char** argv)
{
    if (argc != 2 || !Contract(argv[1]))
    {
        gk::Shutdown();
        return 1;
    }
    return 0;
}
