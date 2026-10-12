// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>
#include <gkcore/ModelSecondaryMotion.h>

#include "core/Context.h"
#include "internal/Backend.hpp"
#include "model/Model.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/FModelPlayback.h"
#include "model/animation/FModelPoseCache.h"
#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/FModelDeferredPose.h"
#include "model/animation/FModelSparseVertexMap.h"
#include "model/animation/ModelIk.h"
#include "model/animation/ModelPose.h"
#include "model/animation/ModelAnimationResources.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include "model/animation/ModelSecondaryMotionDistances.h"
#include "model/animation/ModelSecondaryMotionLinks.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "foundation/Memory.h"
#include "examples/support/FModelArmIkPreview.h"
#include "examples/support/FModelMotionView.h"
#include "examples/support/ModelSecondaryMotionPreview.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

/**
 * Viewerの頂点接触option契約を同じ軽量な検証実行へ含める。
 */
bool RunSecondaryMotionPreviewContracts();

namespace
{

/**
 * GPUを使わず、描画予約された変形モデルを検査するbackend。
 */
class AAnimationRigTestBackend final : public gk::detail::Backend
{
  public:
    bool supportsSparseModelPoses = false;
    bool supportsGpuModelSkinning = false;

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
    bool Present(const gk::detail::FramePacket&, gk::String&) override
    {
        return true;
    }
    bool SupportsSparseModelPoses() const override
    {
        return supportsSparseModelPoses;
    }
    bool SupportsGpuModelSkinning() const override
    {
        return supportsGpuModelSkinning;
    }
    gk::ShaderHandle LoadPixelShader(const char*, gk::String&) override
    {
        return {};
    }
    bool ReleasePixelShader(gk::ShaderHandle, gk::String&) override
    {
        return true;
    }
};

/**
 * 2つのclipを持ち、poseをbone位置へ写す検査用source。
 */
class AAnimationRigTestSource : public gk::model::AModelAnimationSource
{
  public:
    // IK検査用の親順骨格。
    gk::model::animation::FModelSkeleton skeleton;
    // 対応表検査ではtargetと異なる名前を使う。
    const char* boneNames[7] = { "joint_a", "joint_b", "joint_c", "joint_d", "joint_e", "joint_f", "joint_g" };
    // fixtureに登録した骨数。
    uint32_t boneCount = 3;
    // trueならtranslated clipのroot移動量へ再生時刻を加える。
    bool timeVaryingTranslation = false;
    // 1回のpose評価で各clipを1度だけ読むことを数える。
    mutable uint32_t sampleCount = 0;

    gk::model::EModelAnimationFormat Format() const override
    {
        return gk::model::EModelAnimationFormat::Glb;
    }
    const gk::model::animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton;
    }
    const char* BoneName(uint32_t bone) const override
    {
        return bone < boneCount ? boneNames[bone] : nullptr;
    }
    const char* MorphName(uint32_t) const override
    {
        return nullptr;
    }
    uint32_t ClipCount() const override
    {
        return 2;
    }
    const char* ClipName(uint32_t clip) const override
    {
        return clip < 2 ? (clip == 0 ? "rest" : "translated") : nullptr;
    }
    double ClipDuration(uint32_t clip) const override
    {
        return clip < 2 ? (clip == 1 && timeVaryingTranslation ? 1.0e33 : 1.0) : -1.0;
    }

    bool Sample(uint32_t clip, double seconds, gk::model::animation::FModelPose& output, gk::String& error) const override
    {
        ++sampleCount;
        if (clip >= 2 || !isfinite(seconds) || !gk::model::animation::InitializeModelPose(skeleton, output, error))
        {
            return false;
        }
        if (clip == 1)
        {
            output.localTransforms.At(0).position[0] = 2.0f + (timeVaryingTranslation ? static_cast<float>(seconds) : 0.0f);
        }
        error.Clear();
        return true;
    }

    bool Deform(const gk::model::animation::FModelPose& pose, gk::detail::ModelResource& output, gk::String& error) const override
    {
        gk::Array<float> matrices;
        if (output.vertices.Count() != boneCount || !gk::model::animation::EvaluateModelPose(skeleton, pose, matrices, error))
        {
            return false;
        }
        for (uint32_t bone = 0; bone < boneCount; ++bone)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                output.vertices.At(bone).position[axis] = matrices.At(bone * 16u + 12u + axis);
            }
        }
        error.Clear();
        return true;
    }
};

/**
 * 頂点接触のGPU予約とCPU展開を検査する合成FBX source。
 */
class ASkinPointContactTestSource final : public AAnimationRigTestSource
{
  public:
    gk::model::animation::FModelGpuSkinningGeometry geometry;
    gk::model::animation::FModelGpuSkinningGeometry alternateGeometry;
    const gk::model::animation::FModelGpuSkinningGeometry* gpuGeometryOverride = nullptr;
    mutable uint32_t geometryQueryCount = 0;
    gk::Array<gk::model::animation::FModelSparseVertexMap> sparseMap;

    gk::model::EModelAnimationFormat Format() const override
    {
        return gk::model::EModelAnimationFormat::Fbx;
    }
    const gk::model::animation::FModelGpuSkinningGeometry* GpuSkinningGeometry() const override
    {
        ++geometryQueryCount;
        return gpuGeometryOverride ? gpuGeometryOverride : &geometry;
    }
    const gk::Array<gk::model::animation::FModelSparseVertexMap>* SparseVertexMap() const override
    {
        return &sparseMap;
    }
    bool SparseDeformationIsValidated() const override
    {
        return true;
    }
    bool SupportsGpuSkinningPose(const gk::model::animation::FModelPose&) const override
    {
        return true;
    }
    bool EvaluateGpuSkinningMatrices(const gk::model::animation::FModelPose&, gk::Array<gk::model::animation::FModelGpuSkinningGeometry::FMatrix>& output, gk::String& error) const override
    {
        using FMatrix = gk::model::animation::FModelGpuSkinningGeometry::FMatrix;
        gk::Array<FMatrix> candidate;
        for (uint32_t cluster = 0; cluster < geometry.clusters.Count(); ++cluster)
        {
            FMatrix matrix{};
            matrix.value[0] = 1.0;
            matrix.value[5] = 1.0;
            matrix.value[10] = 1.0;
            if (!candidate.Append(matrix))
            {
                error.Assign("contact fixture matrix allocation failed");
                return false;
            }
        }
        output.MoveFrom(candidate);
        error.Clear();
        return true;
    }
    bool DeformSparse(const gk::model::animation::FModelPose&, gk::model::animation::FModelSparsePoseGeometry& output, gk::String& error) const override
    {
        using FVector4 = gk::model::animation::FModelSparsePoseGeometry::FModelVector4;
        const FVector4 positions[3] = { { { 5.0f, 0.0f, 0.0f, 1.0f } }, { { 5.0f, 1.0f, 0.0f, 1.0f } }, { { 0.0f, 0.5f, 0.0f, 1.0f } } };
        const FVector4 normals[3] = { { { 0.0f, 0.0f, 1.0f, 0.0f } }, { { 0.0f, 0.0f, 1.0f, 0.0f } }, { { 0.0f, 0.0f, 1.0f, 0.0f } } };
        gk::model::animation::FModelSparsePoseGeometry candidate;
        if (!candidate.positions.AppendRange(positions, 3) || !candidate.normals.AppendRange(normals, 3))
        {
            error.Assign("contact fixture sparse geometry allocation failed");
            return false;
        }
        output.positions.MoveFrom(candidate.positions);
        output.normals.MoveFrom(candidate.normals);
        error.Clear();
        return true;
    }
    bool DeformSparseWithGpuSkinningData(const gk::model::animation::FModelPose& pose, gk::model::animation::FModelSparsePoseGeometry& output, gk::Array<gk::model::animation::FModelGpuSkinningGeometry::FMatrix>& matrices, gk::String& error) const override
    {
        return DeformSparse(pose, output, error) && EvaluateGpuSkinningMatrices(pose, matrices, error);
    }
};

/**
 * 変形頂点を読むための単純な比較。
 */
bool Near(float left, float right)
{
    return fabsf(left - right) <= 0.001f;
}

/**
 * preview内の計算結果が有限値か確認する。
 */
bool IsFinitePreview(const gk::examples::FModelArmIkPreview& preview)
{
    for (uint32_t index = 0; index < 3; ++index)
    {
        if (!isfinite(preview.joints[index].x) || !isfinite(preview.joints[index].y) || !isfinite(preview.joints[index].z))
        {
            return false;
        }
    }
    return isfinite(preview.target.x) && isfinite(preview.target.y) && isfinite(preview.target.z) && isfinite(preview.pole.x) && isfinite(preview.pole.y) && isfinite(preview.pole.z) && isfinite(preview.armLength);
}

/**
 * IK previewの全fieldが失敗前と同じか確認する。
 */
bool SamePreview(const gk::examples::FModelArmIkPreview& left, const gk::examples::FModelArmIkPreview& right)
{
    for (uint32_t index = 0; index < 3; ++index)
    {
        if (left.bones[index] != right.bones[index] || left.joints[index].x != right.joints[index].x || left.joints[index].y != right.joints[index].y || left.joints[index].z != right.joints[index].z)
        {
            return false;
        }
    }
    return left.outward.x == right.outward.x && left.outward.y == right.outward.y && left.outward.z == right.outward.z && left.up.x == right.up.x && left.up.y == right.up.y && left.up.z == right.up.z && left.target.x == right.target.x && left.target.y == right.target.y && left.target.z == right.target.z && left.pole.x == right.pole.x && left.pole.y == right.pole.y && left.pole.z == right.pole.z && left.armLength == right.armLength;
}

/**
 * 点をY軸まわりに回転し、移動量を加える。
 */
gk::Vec3 RotateTranslateY(gk::Vec3 point, float angle, gk::Vec3 translation)
{
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    return { point.x * cosine + point.z * sine + translation.x, point.y + translation.y, -point.x * sine + point.z * cosine + translation.z };
}

/**
 * 点をZ軸まわりに回転し、移動量を加える。
 */
gk::Vec3 RotateTranslateZ(gk::Vec3 point, float angle, gk::Vec3 translation)
{
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    return { point.x * cosine - point.y * sine + translation.x, point.x * sine + point.y * cosine + translation.y, point.z + translation.z };
}

/**
 * 点をX軸まわりに回転し、移動量を加える。
 */
gk::Vec3 RotateTranslateX(gk::Vec3 point, float angle, gk::Vec3 translation)
{
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    return { point.x + translation.x, point.y * cosine - point.z * sine + translation.y, point.y * sine + point.z * cosine + translation.z };
}

/**
 * 2つの3次元位置が許容誤差内で一致するか確認する。
 */
bool NearVector(gk::Vec3 left, gk::Vec3 right)
{
    return Near(left.x, right.x) && Near(left.y, right.y) && Near(left.z, right.z);
}

/**
 * model空間の骨位置を、描画変換や再生時刻の副作用なしで取得できることを確認する。
 */
bool TestModelBonePositionQuery(gk::ModelHandle model, gk::ModelHandle instance)
{
    gk::Vec3 position{ 91.0f, 92.0f, 93.0f };
    bool passed = gk::ClearModelIk(instance) == 0 && gk::StopModelAnimation(instance) == 0;
    passed = gk::GetModelBonePosition(instance, 2, position) == 0 && Near(position.x, 2.0f) && Near(position.y, 0.0f) && Near(position.z, 0.0f) && passed;
    passed = gk::PlayModelAnimation(instance, 0, false) == 0 && gk::SetModelAnimationBlend(instance, 1, 0.5f) == 0 && passed;
    passed = gk::GetModelBonePosition(instance, 2, position) == 0 && Near(position.x, 3.0f) && Near(position.y, 0.0f) && Near(position.z, 0.0f) && passed;
    const double time0 = gk::GetModelAnimationTime(instance, 0);
    const double time1 = gk::GetModelAnimationTime(instance, 1);
    const auto* transform = gk::detail::FindModelTransform(instance);
    const auto* playback = transform ? transform->playback : nullptr;
    const uint32_t ikCount = playback ? playback->ik.Count() : 0;
    const gk::Vec3 target{ 1.0f, 1.0f, 0.0f };
    passed = gk::SetModelTwoBoneIk(instance, 0, 1, 2, target, { 0.0f, 1.0f, 0.0f }) == 0 && passed;
    passed = gk::GetModelBonePosition(instance, 2, position) == 0 && Near(position.x, target.x) && Near(position.y, target.y) && Near(position.z, target.z) && passed;
    passed = gk::SetModelPosition(instance, { 20.0f, 30.0f, 40.0f }) == 0 && gk::SetModelRotation(instance, { 0.2f, 0.3f, 0.4f }) == 0 && gk::SetModelScale(instance, { 2.0f, 3.0f, 4.0f }) == 0 && passed;
    passed = gk::GetModelBonePosition(instance, 2, position) == 0 && Near(position.x, target.x) && Near(position.y, target.y) && Near(position.z, target.z) && passed;
    passed = gk::GetModelAnimationTime(instance, 0) == time0 && gk::GetModelAnimationTime(instance, 1) == time1 && passed;
    transform = gk::detail::FindModelTransform(instance);
    playback = transform ? transform->playback : nullptr;
    passed = playback && playback->ik.Count() == ikCount + 1 && playback->ik.At(playback->ik.Count() - 1).target[0] == target.x && playback->ik.At(playback->ik.Count() - 1).target[1] == target.y && playback->ik.At(playback->ik.Count() - 1).target[2] == target.z && passed;
    position = { 91.0f, 92.0f, 93.0f };
    passed = gk::GetModelBonePosition(instance, 3, position) == -1 && Near(position.x, 91.0f) && Near(position.y, 92.0f) && Near(position.z, 93.0f) && passed;
    passed = gk::GetModelBonePosition({}, 0, position) == -1 && Near(position.x, 91.0f) && Near(position.y, 92.0f) && Near(position.z, 93.0f) && passed;
    const auto stale = gk::CreateModelInstance(model);
    passed = stale.IsValid() && gk::DeleteModel(stale) == 0 && gk::GetModelBonePosition(stale, 0, position) == -1 && Near(position.x, 91.0f) && Near(position.y, 92.0f) && Near(position.z, 93.0f) && passed;
    passed = gk::ClearModelIk(instance) == 0 && gk::StopModelAnimation(instance) == 0 && gk::SetModelPosition(instance, { 0.0f, 0.0f, 0.0f }) == 0 && gk::SetModelRotation(instance, { 0.0f, 0.0f, 0.0f }) == 0 && gk::SetModelScale(instance, { 1.0f, 1.0f, 1.0f }) == 0 && gk::PlayModelAnimation(instance, 0, false) == 0 && gk::SetModelAnimationBlend(instance, 1, 0.5f) == 0 && passed;
    if (!passed)
    {
        fprintf(stderr, "model-space current bone position query contract failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * 複数骨の現在姿勢を一括取得し、blend・IK・失敗時保持を確認する。
 */
bool TestModelBonePositionsQuery(gk::ModelHandle instance, AAnimationRigTestSource& source)
{
    const uint32_t bones[3] = { 2, 0, 2 };
    gk::Vec3 positions[3] = { { 91.0f, 92.0f, 93.0f }, { 81.0f, 82.0f, 83.0f }, { 71.0f, 72.0f, 73.0f } };
    const double time0 = gk::GetModelAnimationTime(instance, 0);
    const double time1 = gk::GetModelAnimationTime(instance, 1);
    const gk::Vec3 goal{ 1.0f, 1.0f, 0.0f };
    bool passed = gk::ClearModelIk(instance) == 0 && gk::SetModelPosition(instance, { 20.0f, 30.0f, 40.0f }) == 0 && gk::SetModelRotation(instance, { 0.2f, 0.3f, 0.4f }) == 0 && gk::SetModelScale(instance, { 2.0f, 3.0f, 4.0f }) == 0;
    source.sampleCount = 0;
    passed = gk::GetModelBonePositions(instance, bones, 3, positions) == 0 && source.sampleCount == 2 && Near(positions[0].x, 3.0f) && Near(positions[1].x, 1.0f) && Near(positions[2].x, 3.0f) && passed;
    passed = gk::PlayModelAnimation(instance, 1, false) == 0 && gk::ClearModelIk(instance) == 0 && passed;
    source.sampleCount = 0;
    passed = gk::GetModelBonePositions(instance, bones, 3, positions) == 0 && source.sampleCount == 1 && Near(positions[0].x, 4.0f) && Near(positions[1].x, 2.0f) && Near(positions[2].x, 4.0f) && passed;
    passed = gk::PlayModelAnimation(instance, 0, false) == 0 && gk::SetModelAnimationBlend(instance, 1, 0.5f) == 0 && passed;
    const uint32_t ikBones[3] = { 0, 1, 2 };
    gk::Vec3 ikPositions[3]{};
    for (uint32_t frame = 0; frame < 3; ++frame)
    {
        // 同じ時刻でIKを消して再評価し、前frameの結果が次の基準姿勢へ混ざらないことを確認する。
        passed = gk::SetModelAnimationTime(instance, 0.0, 0) == 0 && gk::SetModelAnimationTime(instance, 0.0, 1) == 0 && gk::UpdateModelAnimation(instance, 0.0) == 0 && gk::ClearModelIk(instance) == 0 && gk::GetModelBonePositions(instance, ikBones, 3, ikPositions) == 0 && Near(ikPositions[0].x, 1.0f) && Near(ikPositions[2].x, 3.0f) && passed;
        passed = gk::SetModelTwoBoneIk(instance, 0, 1, 2, goal, { 0.0f, 1.0f, 0.0f }) == 0 && gk::GetModelBonePositions(instance, ikBones, 3, ikPositions) == 0 && Near(ikPositions[0].x, 1.0f) && Near(ikPositions[2].x, goal.x) && Near(ikPositions[2].y, goal.y) && passed;
    }
    passed = gk::GetModelAnimationTime(instance, 0) == time0 && gk::GetModelAnimationTime(instance, 1) == time1 && passed;
    positions[0] = { 91.0f, 92.0f, 93.0f };
    positions[1] = { 81.0f, 82.0f, 83.0f };
    positions[2] = { 71.0f, 72.0f, 73.0f };
    const uint32_t invalidBones[3] = { 0, 3, 2 };
    passed = gk::GetModelBonePositions(instance, invalidBones, 3, positions) == -1 && Near(positions[0].x, 91.0f) && Near(positions[1].x, 81.0f) && Near(positions[2].x, 71.0f) && passed;
    passed = gk::GetModelBonePositions(instance, bones, 0, positions) == -1 && Near(positions[0].x, 91.0f) && passed;
    passed = gk::GetModelBonePositions(instance, nullptr, 3, positions) == -1 && Near(positions[0].x, 91.0f) && passed;
    passed = gk::GetModelBonePositions(instance, bones, 3, nullptr) == -1 && Near(positions[0].x, 91.0f) && passed;
    passed = gk::GetModelBonePositions(instance, bones, 4, positions) == -1 && Near(positions[0].x, 91.0f) && passed;
    passed = gk::ClearModelIk(instance) == 0 && gk::SetModelPosition(instance, { 0.0f, 0.0f, 0.0f }) == 0 && gk::SetModelRotation(instance, { 0.0f, 0.0f, 0.0f }) == 0 && gk::SetModelScale(instance, { 1.0f, 1.0f, 1.0f }) == 0 && passed;
    if (!passed)
    {
        fprintf(stderr, "batched model-space bone position query contract failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * 揺れもの鎖の更新順、照会の非更新性、instance分離、失敗時保持を確認する。
 */
bool TestModelSecondaryMotion(gk::ModelHandle model, gk::ModelHandle instance)
{
    const uint32_t bones[3] = { 0, 1, 2 };
    const uint32_t disconnected[2] = { 0, 2 };
    const uint32_t duplicate[3] = { 0, 1, 1 };
    const uint32_t overlapping[2] = { 1, 2 };
    const uint32_t queryBones[3] = { 0, 1, 2 };
    gk::Vec3 basePose[3]{};
    gk::Vec3 currentPose[3]{};
    gk::Vec3 repeatedPose[3]{};
    gk::FModelSecondaryMotionSettings settings{};
    settings.frequencyHz = 6.0f;
    settings.dampingRatio = 0.8f;
    settings.gravity = { 0.0f, 0.0f, -9.81f };
    settings.endOffset = { 0.1f, 0.0f, 0.0f };
    settings.maxAngleDegrees = 60.0f;
    settings.teleportDistance = 0.5f;
    settings.constraintIterations = 8;
    bool passed = gk::ClearModelSecondaryMotion(model) == 0 && gk::ClearModelSecondaryMotion(instance) == 0;
    passed = gk::ClearModelIk(instance) == 0 && gk::StopModelAnimation(instance) == 0 && passed;
    passed = gk::GetModelBonePositions(model, queryBones, 3, basePose) == 0 && gk::GetModelBonePositions(instance, queryBones, 3, currentPose) == 0 && NearVector(basePose[0], currentPose[0]) && NearVector(basePose[1], currentPose[1]) && NearVector(basePose[2], currentPose[2]) && passed;
    passed = gk::SetModelSecondaryMotionChain(instance, bones, 3, settings) == 0 && passed;
    for (uint32_t step = 0; step < 18; ++step)
    {
        passed = gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == 0 && passed;
    }
    passed = gk::GetModelBonePositions(instance, queryBones, 3, currentPose) == 0 && passed;
    passed = fabsf(currentPose[2].z - basePose[2].z) > 0.001f && passed;
    passed = gk::GetModelBonePositions(instance, queryBones, 3, repeatedPose) == 0 && currentPose[0].x == repeatedPose[0].x && currentPose[0].y == repeatedPose[0].y && currentPose[0].z == repeatedPose[0].z && currentPose[1].x == repeatedPose[1].x && currentPose[1].y == repeatedPose[1].y && currentPose[1].z == repeatedPose[1].z && currentPose[2].x == repeatedPose[2].x && currentPose[2].y == repeatedPose[2].y && currentPose[2].z == repeatedPose[2].z && passed;
    gk::Vec3 untouched[3] = { currentPose[0], currentPose[1], currentPose[2] };
    gk::FModelSecondaryMotionSettings invalidSettings = settings;
    invalidSettings.frequencyHz = std::numeric_limits<float>::quiet_NaN();
    passed = gk::SetModelSecondaryMotionChain(instance, disconnected, 2, settings) == -1 && passed;
    passed = gk::SetModelSecondaryMotionChain(instance, duplicate, 3, settings) == -1 && passed;
    passed = gk::SetModelSecondaryMotionChain(instance, overlapping, 2, settings) == -1 && passed;
    passed = gk::SetModelSecondaryMotionChain(instance, bones, 3, invalidSettings) == -1 && passed;
    passed = gk::GetModelBonePositions(instance, queryBones, 3, currentPose) == 0 && currentPose[0].x == untouched[0].x && currentPose[0].y == untouched[0].y && currentPose[0].z == untouched[0].z && currentPose[1].x == untouched[1].x && currentPose[1].y == untouched[1].y && currentPose[1].z == untouched[1].z && currentPose[2].x == untouched[2].x && currentPose[2].y == untouched[2].y && currentPose[2].z == untouched[2].z && passed;
    passed = gk::GetModelBonePositions(model, queryBones, 3, repeatedPose) == 0 && NearVector(repeatedPose[0], basePose[0]) && NearVector(repeatedPose[1], basePose[1]) && NearVector(repeatedPose[2], basePose[2]) && passed;

    // 予約済み描画は、その後のsimulation更新で書き換わらない。
    passed = gk::BeginFrame() == 0 && gk::DrawModel(instance) == 0 && passed;
    const auto& draws = gk::detail::GetContext().frame.draws;
    const float queuedEndX = draws.Count() == 1 ? draws.At(0).model->vertices.At(2).position[0] : 0.0f;
    const float queuedEndY = draws.Count() == 1 ? draws.At(0).model->vertices.At(2).position[1] : 0.0f;
    const float queuedEndZ = draws.Count() == 1 ? draws.At(0).model->vertices.At(2).position[2] : 0.0f;
    passed = draws.Count() == 1 && currentPose[2].x == queuedEndX && currentPose[2].y == queuedEndY && currentPose[2].z == queuedEndZ && gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == 0 && gk::DrawModel(instance) == 0 && draws.Count() == 2 && draws.At(0).model->vertices.At(2).position[0] == queuedEndX && draws.At(0).model->vertices.At(2).position[1] == queuedEndY && draws.At(0).model->vertices.At(2).position[2] == queuedEndZ && passed;
    passed = gk::Present() == 0 && passed;

    // animation blendとIKの後に更新し、anchorは合成後のroot位置を使う。
    passed = gk::ClearModelSecondaryMotion(instance) == 0 && gk::PlayModelAnimation(instance, 0, false) == 0 && gk::SetModelAnimationBlend(instance, 1, 0.5f) == 0 && gk::SetModelTwoBoneIk(instance, 0, 1, 2, { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) == 0 && gk::SetModelSecondaryMotionChain(instance, bones, 3, settings) == 0 && gk::UpdateModelAnimation(instance, 0.0) == 0 && gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == 0 && passed;
    passed = gk::GetModelBonePositions(instance, queryBones, 3, currentPose) == 0 && Near(currentPose[0].x, 1.0f) && passed;
    passed = gk::ResetModelSecondaryMotion(instance) == 0 && gk::GetModelBonePositions(instance, queryBones, 3, currentPose) == 0 && Near(currentPose[0].x, 1.0f) && Near(currentPose[2].x, 1.0f) && Near(currentPose[2].y, 1.0f) && passed;
    passed = gk::UpdateModelSecondaryMotion(instance, std::numeric_limits<double>::quiet_NaN()) == -1 && passed;
    passed = gk::ClearModelSecondaryMotion(instance) == 0 && gk::GetModelBonePositions(instance, queryBones, 3, currentPose) == 0 && Near(currentPose[0].x, 1.0f) && Near(currentPose[2].x, 1.0f) && Near(currentPose[2].y, 1.0f) && passed;
    passed = gk::ResetModelSecondaryMotion(model) == 0 && gk::ClearModelSecondaryMotion(model) == 0 && gk::ClearModelSecondaryMotion(instance) == 0 && gk::ClearModelIk(instance) == 0 && gk::StopModelAnimation(instance) == 0 && gk::PlayModelAnimation(instance, 0, false) == 0 && gk::SetModelAnimationBlend(instance, 1, 0.5f) == 0 && passed;
    if (!passed)
    {
        fprintf(stderr, "model secondary motion update, isolation, failure atomicity, or snapshot contract failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * 身体接触設定の検証、instance分離、更新順、解除後の鎖更新を確認する。
 */
bool TestModelSecondaryMotionColliders(gk::ModelHandle model, gk::ModelHandle instance, AAnimationRigTestSource& source)
{
    const uint32_t chainBones[2] = { 1, 2 };
    gk::FModelSecondaryMotionSettings settings{};
    settings.frequencyHz = 6.0f;
    settings.dampingRatio = 0.8f;
    settings.gravity = { 0.0f, 0.0f, -9.81f };
    settings.endOffset = { 0.1f, 0.0f, 0.0f };
    settings.maxAngleDegrees = 60.0f;
    settings.teleportDistance = 0.5f;
    settings.constraintIterations = 8;
    gk::FModelSecondaryMotionCollider collider{};
    collider.bone = 0;
    collider.start = { 3.0f, 0.0f, 0.0f };
    collider.end = { 3.0f, 0.5f, 0.0f };
    collider.radius = 0.05f;
    gk::FModelSecondaryMotionCollider tooMany[65]{};
    for (uint32_t index = 0; index < 65; ++index)
    {
        tooMany[index] = collider;
    }
    bool passed = gk::ClearModelSecondaryMotion(model) == 0 && gk::ClearModelSecondaryMotion(instance) == 0;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1) == -1 && passed;
    passed = gk::SetModelSecondaryMotionChain(instance, chainBones, 2, settings) == 0 && passed;

    // 骨格に追従する身体capsuleを登録する。
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.01f) == 0 && passed;
    auto* instanceTransform = gk::detail::FindModelTransform(instance);
    auto* state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->colliders.Count() == 1 && state->previousCollisionShapes.Count() == 1 && Near(state->previousCollisionShapes.At(0).radius, 0.06f) && passed;

    // count、bone、半径、余白の不正値は失敗し、直前の接触設定を保つ。
    gk::FModelSecondaryMotionCollider invalid = collider;
    invalid.bone = 99;
    passed = gk::SetModelSecondaryMotionColliders(instance, nullptr, 1) == -1 && passed;
    passed = gk::SetModelSecondaryMotionColliders(instance, tooMany, 65) == -1 && passed;
    passed = gk::SetModelSecondaryMotionColliders(instance, &invalid, 1) == -1 && passed;
    invalid = collider;
    invalid.radius = 0.0f;
    passed = gk::SetModelSecondaryMotionColliders(instance, &invalid, 1) == -1 && passed;
    invalid = collider;
    invalid.radius = std::numeric_limits<float>::quiet_NaN();
    passed = gk::SetModelSecondaryMotionColliders(instance, &invalid, 1) == -1 && passed;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, -0.001f) == -1 && passed;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, std::numeric_limits<float>::infinity()) == -1 && passed;

    // 揺れる鎖のroot自身と、その子孫には身体形状を取り付けられない。
    invalid = collider;
    invalid.bone = 1;
    passed = gk::SetModelSecondaryMotionColliders(instance, &invalid, 1) == -1 && passed;
    invalid.bone = 2;
    passed = gk::SetModelSecondaryMotionColliders(instance, &invalid, 1) == -1 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->colliders.Count() == 1 && state->colliders.At(0).bone == collider.bone && state->colliders.At(0).radius == collider.radius && state->colliderMargin == 0.01f && state->previousCollisionShapes.Count() == 1 && Near(state->previousCollisionShapes.At(0).start.x, 4.0f) && Near(state->previousCollisionShapes.At(0).radius, 0.06f) && passed;

    // 有効な一様scaleは端点と半径へ反映し、わずかな軸差も許す。
    passed = gk::SetModelAnimationBlendWeight(instance, 0.0f) == 0 && passed;
    source.skeleton.restLocalTransforms.At(0).scale[0] = 2.0f;
    source.skeleton.restLocalTransforms.At(0).scale[1] = 2.0f;
    source.skeleton.restLocalTransforms.At(0).scale[2] = 2.0f;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.01f) == 0 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->previousCollisionShapes.Count() == 1 && Near(state->previousCollisionShapes.At(0).start.x, 6.0f) && Near(state->previousCollisionShapes.At(0).end.y, 1.0f) && Near(state->previousCollisionShapes.At(0).radius, 0.11f) && passed;
    source.skeleton.restLocalTransforms.At(0).scale[1] = 2.00001f;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.01f) == 0 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->previousCollisionShapes.Count() == 1 && Near(state->previousCollisionShapes.At(0).start.x, 6.0f) && Near(state->previousCollisionShapes.At(0).radius, 0.11f) && passed;

    // zero・負・目立つ非一様scaleは拒否し、直前の配置を保つ。
    gk::model::animation::FModelSecondaryMotionCollisionShape preservedShape{};
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    if (state && state->previousCollisionShapes.Count() == 1)
    {
        preservedShape = state->previousCollisionShapes.At(0);
    }
    source.skeleton.restLocalTransforms.At(0).scale[0] = 0.0f;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.01f) == -1 && passed;
    source.skeleton.restLocalTransforms.At(0).scale[0] = -2.0f;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.01f) == -1 && passed;
    source.skeleton.restLocalTransforms.At(0).scale[0] = 2.1f;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.01f) == -1 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->previousCollisionShapes.Count() == 1 && Near(state->previousCollisionShapes.At(0).start.x, preservedShape.start.x) && Near(state->previousCollisionShapes.At(0).end.y, preservedShape.end.y) && Near(state->previousCollisionShapes.At(0).radius, preservedShape.radius) && passed;
    source.skeleton.restLocalTransforms.At(0).scale[0] = 1.0f;
    source.skeleton.restLocalTransforms.At(0).scale[1] = 1.0f;
    source.skeleton.restLocalTransforms.At(0).scale[2] = 1.0f;

    // 身体に形状を取り付けた後、そのboneを揺れ鎖に含める変更は失敗し設定を保つ。
    const uint32_t collidingChain[3] = { 0, 1, 2 };
    passed = gk::SetModelSecondaryMotionChain(instance, collidingChain, 3, settings) == -1 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->chains.Count() == 1 && state->chains.At(0)->bones.At(0) == chainBones[0] && state->colliders.Count() == 1 && passed;

    // クリップのroot移動後、明示更新で保存された形状も同じ移動量だけ追従する。
    gk::Vec3 before[3]{};
    gk::Vec3 after[3]{};
    gk::Vec3 repeated[3]{};
    gk::Vec3 basePose[3]{};
    const uint32_t queryBones[3] = { 0, 1, 2 };
    passed = gk::ClearModelSecondaryMotion(instance) == 0 && passed;
    passed = gk::SetModelSecondaryMotionChain(instance, chainBones, 2, settings) == 0 && passed;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1) == 0 && passed;
    passed = gk::SetModelAnimationBlendWeight(instance, 0.0f) == 0 && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    const float stationaryStartX = state && state->previousCollisionShapes.Count() == 1 ? state->previousCollisionShapes.At(0).start.x : -1000.0f;
    passed = gk::SetModelAnimationBlendWeight(instance, 1.0f) == 0 && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->previousCollisionShapes.Count() == 1 && Near(state->previousCollisionShapes.At(0).start.x - stationaryStartX, 2.0f) && passed;
    // 純粋なbone照会は保存済み身体形状を更新しない。
    passed = gk::GetModelBonePositions(model, queryBones, 3, basePose) == 0 && gk::GetModelBonePositions(instance, queryBones, 3, before) == 0 && passed;
    passed = gk::GetModelBonePositions(instance, queryBones, 3, after) == 0 && passed;
    passed = gk::GetModelBonePositions(instance, queryBones, 3, repeated) == 0 && NearVector(after[0], repeated[0]) && NearVector(after[1], repeated[1]) && NearVector(after[2], repeated[2]) && passed;
    passed = gk::GetModelBonePositions(model, queryBones, 3, repeated) == 0 && NearVector(basePose[0], repeated[0]) && NearVector(basePose[1], repeated[1]) && NearVector(basePose[2], repeated[2]) && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->previousCollisionShapes.Count() == 1 && Near(state->previousCollisionShapes.At(0).start.x, stationaryStartX + 2.0f) && passed;

    // IKによる身体bone回転で、局所capsuleの軸方向も追従する。
    collider.start = { 3.0f, 0.0f, 0.0f };
    collider.end = { 3.0f, 0.5f, 0.0f };
    passed = gk::SetModelTwoBoneIk(instance, 0, 1, 2, { 2.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) == 0 && gk::SetModelAnimationBlendWeight(instance, 0.0f) == 0 && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    const gk::Vec3 straightAxisEnd = state && state->previousCollisionShapes.Count() == 1 ? state->previousCollisionShapes.At(0).end : gk::Vec3{};
    passed = gk::SetModelTwoBoneIk(instance, 0, 1, 2, { 0.0f, 2.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }) == 0 && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->previousCollisionShapes.Count() == 1 && (fabsf(state->previousCollisionShapes.At(0).end.x - straightAxisEnd.x) > 0.1f || fabsf(state->previousCollisionShapes.At(0).end.y - straightAxisEnd.y) > 0.1f) && passed;

    // 予約済みsnapshotは後続のIK・物理更新で変わらず、形状は明示更新時だけ進む。
    passed = gk::BeginFrame() == 0 && gk::DrawModel(instance) == 0 && passed;
    const auto& draws = gk::detail::GetContext().frame.draws;
    const float queuedVertexX = draws.Count() == 1 ? draws.At(0).model->vertices.At(2).position[0] : -1000.0f;
    const float queuedVertexY = draws.Count() == 1 ? draws.At(0).model->vertices.At(2).position[1] : -1000.0f;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    const gk::Vec3 queuedShapeEnd = state && state->previousCollisionShapes.Count() == 1 ? state->previousCollisionShapes.At(0).end : gk::Vec3{};
    passed = gk::SetModelTwoBoneIk(instance, 0, 1, 2, { 2.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) == 0 && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && gk::DrawModel(instance) == 0 && draws.Count() == 2 && Near(draws.At(0).model->vertices.At(2).position[0], queuedVertexX) && Near(draws.At(0).model->vertices.At(2).position[1], queuedVertexY) && passed;
    state = instanceTransform && instanceTransform->playback ? instanceTransform->playback->secondaryMotion : nullptr;
    passed = state && state->previousCollisionShapes.Count() == 1 && (fabsf(state->previousCollisionShapes.At(0).end.x - queuedShapeEnd.x) > 0.1f || fabsf(state->previousCollisionShapes.At(0).end.y - queuedShapeEnd.y) > 0.1f) && gk::Present() == 0 && passed;

    // 接触だけを解除しても鎖設定は残り、重力による更新を続ける。
    passed = gk::SetModelSecondaryMotionColliders(instance, nullptr, 0) == 0 && passed;
    passed = gk::ClearModelIk(instance) == 0 && gk::StopModelAnimation(instance) == 0 && gk::SetModelSecondaryMotionChain(instance, chainBones, 2, settings) == 0 && passed;
    for (uint32_t step = 0; step < 8; ++step)
    {
        passed = gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == 0 && passed;
    }
    passed = gk::GetModelBonePositions(instance, queryBones, 3, after) == 0 && passed;
    passed = fabsf(after[2].z - basePose[2].z) > 0.001f && passed;
    passed = gk::ClearModelSecondaryMotion(instance) == 0 && passed;

    // GLBでは鎖と身体形状が揃っていても頂点接触を有効にできない。
    passed = gk::SetModelSecondaryMotionChain(instance, chainBones, 2, settings) == 0 && passed;
    passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.0f) == 0 && passed;
    passed = gk::SetModelSecondaryMotionVertexContactsEnabled(instance, true) == -1 && passed;
    passed = gk::SetModelSecondaryMotionVertexContactsEnabled(instance, false) == 0 && passed;
    passed = gk::ClearModelSecondaryMotion(instance) == 0 && passed;
    if (!passed)
    {
        fprintf(stderr, "secondary motion collider validation, ownership, or contact clear contract failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * 接触検査に使う三角形、bone weight、sparse対応を作る。
 */
bool InitializeSkinPointContactGeometry(ASkinPointContactTestSource& source)
{
    using FGeometry = gk::model::animation::FModelGpuSkinningGeometry;
    const FGeometry::FPosition positions[3] = { { { 5.0, 0.0, 0.0 } }, { { 5.0, 1.0, 0.0 } }, { { 0.0, 0.5, 0.0 } } };
    const FGeometry::FInfluenceRange ranges[3] = { { 0, 1 }, { 1, 1 }, { 2, 1 } };
    const FGeometry::FInfluence influences[3] = { { 0, 1.0 }, { 1, 1.0 }, { 2, 1.0 } };
    const FGeometry::FFace face{ 0, 3 };
    const FGeometry::FCorner corners[3] = { { 0, 0 }, { 1, 1 }, { 2, 2 } };
    const FGeometry::FNormalGroupRange normalRanges[3] = { { 0, 1 }, { 1, 1 }, { 2, 1 } };
    const uint32_t normalFaces[3] = { 0, 0, 0 };
    FGeometry::FSegment segment{};
    segment.positionCount = 3;
    segment.clusterCount = 3;
    segment.faceCount = 1;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        segment.geometryToNode[axis * 4u + axis] = 1.0;
    }
    const FGeometry::FCluster clusters[3] = { { 0, {} }, { 0, {} }, { 2, {} } };
    const gk::model::animation::FModelSparseVertexMap sparseMap[3] = { { 0, 0 }, { 1, 1 }, { 2, 2 } };
    return source.geometry.positions.AppendRange(positions, 3) && source.geometry.influenceRanges.AppendRange(ranges, 3) && source.geometry.influences.AppendRange(influences, 3) && source.geometry.clusters.AppendRange(clusters, 3) && source.geometry.faces.Append(face) && source.geometry.corners.AppendRange(corners, 3) && source.geometry.normalGroupRanges.AppendRange(normalRanges, 3) && source.geometry.normalFaceIds.AppendRange(normalFaces, 3) && source.geometry.segments.Append(segment) && source.sparseMap.AppendRange(sparseMap, 3);
}

/**
 * 身体だけにweightを持つ点を保ち、cloth点だけの接触を描画予約へ固定する。
 */
bool TestSkinPointContactsSnapshot()
{
    auto* backend = static_cast<AAnimationRigTestBackend*>(gk::detail::GetContext().backend);
    if (!backend)
    {
        return false;
    }
    backend->supportsSparseModelPoses = true;
    backend->supportsGpuModelSkinning = true;
    auto* resource = gk::detail::CreateModelResource();
    if (!resource)
    {
        backend->supportsSparseModelPoses = false;
        backend->supportsGpuModelSkinning = false;
        return false;
    }
    auto* source = new ASkinPointContactTestSource;
    const int32_t parents[3] = { -1, 0, 1 };
    gk::model::animation::FModelBoneTransform rest[3]{};
    rest[2].position[1] = 1.0f;
    bool passed = source->skeleton.parents.AppendRange(parents, 3) && source->skeleton.restLocalTransforms.AppendRange(rest, 3) && InitializeSkinPointContactGeometry(*source);
    if (!passed)
    {
        fprintf(stderr, "skin contact fixture geometry setup failed\n");
    }
    gk::String error;
    resource->animation = passed ? gk::model::CreateModelAnimationAsset(source, error) : nullptr;
    if (!passed)
    {
        delete source;
    }
    gk::detail::ModelVertex vertices[3]{};
    passed = passed && resource->animation && resource->vertices.AppendRange(vertices, 3);
    if (!passed)
    {
        fprintf(stderr, "skin contact fixture animation or vertices failed: %s\n", error.CStr());
    }
    const bool resourceRegistered = passed;
    const auto model = passed ? gk::detail::RegisterModelResource(resource, error) : gk::ModelHandle{};
    gk::detail::ModelTransform baseTransform{};
    baseTransform.handle = model;
    baseTransform.scale = { 1.0f, 1.0f, 1.0f };
    const bool transformAdded = model.IsValid() && gk::detail::GetContext().modelTransforms.Append(baseTransform);
    const auto first = transformAdded ? gk::CreateModelInstance(model) : gk::ModelHandle{};
    const auto second = first.IsValid() ? gk::CreateModelInstance(model) : gk::ModelHandle{};
    if (!first.IsValid() || !second.IsValid())
    {
        passed = false;
        fprintf(stderr, "skin contact fixture instance creation failed: %s\n", gk::GetLastErrorMessage());
    }
    const uint32_t chain[2] = { 1, 2 };
    gk::FModelSecondaryMotionSettings settings{};
    settings.gravity = {};
    settings.windAcceleration = {};
    settings.endOffset = { 0.0f, 1.0f, 0.0f };
    settings.maxAngleDegrees = 60.0f;
    settings.constraintIterations = 8;
    gk::FModelSecondaryMotionCollider collider{};
    collider.bone = 0;
    collider.radius = 0.75f;
    passed = gk::SetModelSecondaryMotionVertexContactsEnabled({}, true) == -1 && gk::SetModelSecondaryMotionVertexContactsEnabled({}, false) == -1 && passed;
    passed = gk::SetModelSecondaryMotionVertexContactsEnabled(first, false) == 0 && passed;
    passed = gk::SetModelSecondaryMotionVertexContactsEnabled(first, true) == -1 && passed;
    if (!passed)
    {
        fprintf(stderr, "skin contact API invalid-handle or unconfigured checks failed\n");
    }
    passed = gk::PlayModelAnimation(first, 0, false) == 0 && gk::SetModelSecondaryMotionChain(first, chain, 2, settings) == 0 && gk::SetModelSecondaryMotionColliders(first, &collider, 1, 0.005f) == 0 && passed;
    passed = gk::SetModelSecondaryMotionVertexContactsEnabled(first, true) == 0 && passed;
    passed = gk::SetModelSecondaryMotionChain(second, chain, 2, settings) == 0 && gk::SetModelSecondaryMotionColliders(second, &collider, 1, 0.005f) == 0 && passed;
    if (!passed)
    {
        fprintf(stderr, "skin contact chain/collider enable setup failed: %s\n", gk::GetLastErrorMessage());
    }
    auto* firstTransform = gk::detail::FindModelTransform(first);
    auto* firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
    auto* secondTransform = gk::detail::FindModelTransform(second);
    auto* secondState = secondTransform && secondTransform->playback ? secondTransform->playback->secondaryMotion : nullptr;
    passed = firstState && firstState->skinPointContactsEnabled && firstState->skinContactGeometry == &source->geometry && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && passed;
    passed = secondState && !secondState->skinPointContactsEnabled && secondState->skinContactPositionIds.Count() == 0 && secondState->skinContactGeometry == nullptr && passed;
    if (!passed)
    {
        fprintf(stderr, "skin contact enable did not select only the cloth-weighted position: %s\n", gk::GetLastErrorMessage());
    }
    gk::FModelSecondaryMotionCollider invalidCollider = collider;
    invalidCollider.radius = std::numeric_limits<float>::quiet_NaN();
    passed = gk::SetModelSecondaryMotionColliders(first, &invalidCollider, 1, 0.005f) == -1 && firstState && firstState->skinPointContactsEnabled && firstState->skinContactGeometry == &source->geometry && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && passed;
    source->geometryQueryCount = 0;
    const int32_t resetResult = gk::ResetModelSecondaryMotion(first);
    const uint32_t resetGeometryQueries = source->geometryQueryCount;
    passed = resetResult == 0 && resetGeometryQueries == 0 && passed;
    firstTransform = gk::detail::FindModelTransform(first);
    firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
    passed = firstState && firstState->skinPointContactsEnabled && firstState->skinContactGeometry == &source->geometry && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && passed;
    source->geometryQueryCount = 0;
    const int32_t updateResult = gk::UpdateModelSecondaryMotion(first, 0.0);
    const uint32_t updateGeometryQueries = source->geometryQueryCount;
    passed = updateResult == 0 && updateGeometryQueries == 0 && passed;
    firstTransform = gk::detail::FindModelTransform(first);
    firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
    passed = firstState && firstState->skinPointContactsEnabled && firstState->skinContactGeometry == &source->geometry && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && passed;
    // chain再登録が古い選別結果を再利用せず、body-only位置を除いて再選別することを確かめる。
    if (firstState)
    {
        firstState->skinContactPositionIds.Clear();
        passed = firstState->skinContactPositionIds.Append(0) && passed;
    }
    else
    {
        passed = false;
    }
    source->geometryQueryCount = 0;
    const int32_t reselectResult = gk::SetModelSecondaryMotionChain(first, chain, 2, settings);
    const uint32_t reselectGeometryQueries = source->geometryQueryCount;
    passed = reselectResult == 0 && reselectGeometryQueries > 0 && passed;
    firstTransform = gk::detail::FindModelTransform(first);
    firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
    passed = firstState && firstState->skinPointContactsEnabled && firstState->skinContactGeometry == &source->geometry && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && passed;
    if (!passed)
    {
        fprintf(stderr, "skin contact reset/update/chain replacement did not preserve enable state: %s\n", gk::GetLastErrorMessage());
    }

    bool frameOpen = false;
    if (passed)
    {
        passed = gk::BeginFrame() == 0;
        frameOpen = passed;
    }
    if (passed)
    {
        gk::Array<gk::model::animation::FModelSecondaryMotionPoint> simulationSnapshot;
        const auto& pointsBeforeMismatch = firstState->chains.At(0)->simulation.points;
        const uint32_t pointCountBeforeMismatch = pointsBeforeMismatch.Count();
        const bool snapshotReady = !pointCountBeforeMismatch || simulationSnapshot.AppendRange(pointsBeforeMismatch.Data(), pointCountBeforeMismatch);
        const auto* stateBeforeMismatch = firstState;
        source->gpuGeometryOverride = &source->alternateGeometry;
        const int32_t mismatchDraw = gk::DrawModel(first);
        const auto& drawsAfterMismatch = gk::detail::GetContext().frame.draws;
        firstTransform = gk::detail::FindModelTransform(first);
        firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
        const auto* pointsAfterMismatch = firstState && firstState->chains.Count() > 0 ? &firstState->chains.At(0)->simulation.points : nullptr;
        const bool stateUnchanged = firstState == stateBeforeMismatch && firstState->skinContactGeometry == &source->geometry && firstState->skinPointContactsEnabled && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && pointsAfterMismatch && pointsAfterMismatch->Count() == pointCountBeforeMismatch && (!pointCountBeforeMismatch || memcmp(pointsAfterMismatch->Data(), simulationSnapshot.Data(), static_cast<size_t>(pointCountBeforeMismatch) * sizeof(gk::model::animation::FModelSecondaryMotionPoint)) == 0);
        source->gpuGeometryOverride = nullptr;
        passed = mismatchDraw == -1 && drawsAfterMismatch.Count() == 0 && snapshotReady && stateUnchanged && passed;
        if (!passed)
        {
            fprintf(stderr, "skin geometry identity mismatch did not fail draw atomically: %s\n", gk::GetLastErrorMessage());
        }
    }
    if (passed)
    {
        passed = gk::DrawModel(first) == 0 && gk::DrawModel(second) == 0;
    }
    if (!passed)
    {
        fprintf(stderr, "skin contact GPU deferred draw reservation failed: %s\n", gk::GetLastErrorMessage());
    }
    auto& draws = gk::detail::GetContext().frame.draws;
    gk::model::FModelDeferredPose* firstSnapshot = passed && draws.Count() == 2 ? draws.At(0).deferredPose : nullptr;
    gk::model::FModelDeferredPose* secondSnapshot = passed && draws.Count() == 2 ? draws.At(1).deferredPose : nullptr;
    gk::model::animation::FModelSkinPointCorrection frozenCorrection{};
    gk::model::animation::FModelSparsePoseGeometry::FModelVector4 frozenPoint{};
    if (!firstSnapshot || !secondSnapshot || firstSnapshot->skinPointCorrections.Count() != 1 || firstSnapshot->skinPointCorrections.At(0).positionId != 2 || secondSnapshot->skinPointCorrections.Count() != 0 || firstSnapshot->geometry.positions.Count() != 3 || firstSnapshot->geometry.normals.Count() != 3)
    {
        passed = false;
        fprintf(stderr, "skin contact deferred snapshot mask/count failed (first=%u second=%u)\n", firstSnapshot ? firstSnapshot->skinPointCorrections.Count() : 0u, secondSnapshot ? secondSnapshot->skinPointCorrections.Count() : 0u);
    }
    if (passed)
    {
        frozenCorrection = firstSnapshot->skinPointCorrections.At(0);
        frozenPoint = firstSnapshot->geometry.positions.At(2);
        const auto& clothPoint = firstSnapshot->geometry.positions.At(2);
        const double clothRadius = sqrt(static_cast<double>(clothPoint.value[0]) * clothPoint.value[0] + static_cast<double>(clothPoint.value[1]) * clothPoint.value[1] + static_cast<double>(clothPoint.value[2]) * clothPoint.value[2]);
        // GPU packetは不透明位置を0で保持するため、body-only点の実座標はCPU materialize後に検査する。
        passed = clothRadius >= 0.75 && clothRadius < 0.8;
        if (!passed)
        {
            fprintf(stderr, "skin contact projected geometry mismatch (clothRadius=%f)\n", clothRadius);
        }
        for (uint32_t normal = 0; normal < firstSnapshot->geometry.normals.Count(); ++normal)
        {
            for (uint32_t axis = 0; axis < 4; ++axis)
            {
                passed = isfinite(firstSnapshot->geometry.normals.At(normal).value[axis]) && passed;
            }
        }
    }
    gk::String materializeError;
    gk::detail::ModelResource* materialized = firstSnapshot ? gk::model::MaterializeDeferredModelPose(*firstSnapshot, materializeError) : nullptr;
    if (!materialized || materialized->vertices.Count() != 3 || !Near(materialized->vertices.At(0).position[0], 5.0f) || !Near(materialized->vertices.At(1).position[0], 5.0f) || !Near(sqrtf(materialized->vertices.At(2).position[0] * materialized->vertices.At(2).position[0] + materialized->vertices.At(2).position[1] * materialized->vertices.At(2).position[1] + materialized->vertices.At(2).position[2] * materialized->vertices.At(2).position[2]), 0.755f) || !isfinite(materialized->vertices.At(2).normal[0]) || !isfinite(materialized->vertices.At(2).normal[1]) || !isfinite(materialized->vertices.At(2).normal[2]))
    {
        passed = false;
        fprintf(stderr, "skin contact deferred materialization failed (ptr=%p count=%u error=%s)\n", materialized, materialized ? materialized->vertices.Count() : 0u, materializeError.CStr());
    }
    if (materialized)
    {
        gk::Release(&materialized->reference);
    }

    collider.radius = 0.8f;
    passed = gk::SetModelSecondaryMotionColliders(first, &collider, 1, 0.005f) == 0 && passed;
    firstTransform = gk::detail::FindModelTransform(first);
    firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
    passed = firstState && firstState->skinPointContactsEnabled && firstState->skinContactGeometry == &source->geometry && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && passed;
    passed = gk::DrawModel(first) == 0 && draws.Count() == 3 && draws.At(2).deferredPose && draws.At(2).deferredPose->skinPointCorrections.Count() == 1 && draws.At(2).deferredPose->skinPointCorrections.At(0).position[1] > frozenCorrection.position[1] && passed;
    passed = firstSnapshot && memcmp(&firstSnapshot->skinPointCorrections.At(0), &frozenCorrection, sizeof(frozenCorrection)) == 0 && memcmp(&firstSnapshot->geometry.positions.At(2), &frozenPoint, sizeof(frozenPoint)) == 0 && passed;
    if (firstSnapshot)
    {
        materialized = gk::model::MaterializeDeferredModelPose(*firstSnapshot, materializeError);
        const bool oldPacketMaterialized = materialized && Near(sqrtf(materialized->vertices.At(2).position[0] * materialized->vertices.At(2).position[0] + materialized->vertices.At(2).position[1] * materialized->vertices.At(2).position[1] + materialized->vertices.At(2).position[2] * materialized->vertices.At(2).position[2]), 0.755f);
        passed = oldPacketMaterialized && passed;
        if (!oldPacketMaterialized)
        {
            fprintf(stderr, "frozen skin contact packet changed after replacing shape (ptr=%p error=%s)\n", materialized, materializeError.CStr());
        }
        if (materialized)
        {
            gk::Release(&materialized->reference);
        }
    }
    passed = gk::SetModelSecondaryMotionChain(first, chain, 2, settings) == 0 && passed;
    firstTransform = gk::detail::FindModelTransform(first);
    firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
    passed = firstState && firstState->skinPointContactsEnabled && firstState->skinContactGeometry == &source->geometry && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && passed;
    passed = gk::DrawModel(first) == 0 && draws.Count() == 4 && draws.At(3).deferredPose && draws.At(3).deferredPose->skinPointCorrections.Count() == 1 && draws.At(3).deferredPose->skinPointCorrections.At(0).positionId == 2 && passed;
    passed = firstSnapshot && memcmp(&firstSnapshot->skinPointCorrections.At(0), &frozenCorrection, sizeof(frozenCorrection)) == 0 && memcmp(&firstSnapshot->geometry.positions.At(2), &frozenPoint, sizeof(frozenPoint)) == 0 && passed;
    if (draws.Count() == 4 && draws.At(3).deferredPose)
    {
        materialized = gk::model::MaterializeDeferredModelPose(*draws.At(3).deferredPose, materializeError);
        const bool bodyOnlyPositionsKept = materialized && materialized->vertices.Count() == 3 && Near(materialized->vertices.At(0).position[0], 5.0f) && Near(materialized->vertices.At(0).position[1], 0.0f) && Near(materialized->vertices.At(1).position[0], 5.0f) && Near(materialized->vertices.At(1).position[1], 1.0f);
        passed = bodyOnlyPositionsKept && passed;
        if (!bodyOnlyPositionsKept)
        {
            fprintf(stderr, "chain reselection changed body-only weighted positions: %s\n", materializeError.CStr());
        }
        if (materialized)
        {
            gk::Release(&materialized->reference);
        }
    }
    passed = gk::SetModelSecondaryMotionVertexContactsEnabled(first, false) == 0 && firstState && !firstState->skinPointContactsEnabled && firstState->skinContactGeometry == nullptr && firstState->skinContactPositionIds.Count() == 0 && gk::DrawModel(first) == 0 && draws.Count() == 5 && draws.At(4).deferredPose && draws.At(4).deferredPose->skinPointCorrections.Count() == 0 && passed;
    if (!passed)
    {
        fprintf(stderr, "skin contact replacement, frozen snapshot, or disable contract failed: %s\n", gk::GetLastErrorMessage());
    }
    if (frameOpen)
    {
        const bool presented = gk::Present() == 0;
        frameOpen = false;
        passed = presented && passed;
    }
    backend->supportsSparseModelPoses = false;
    backend->supportsGpuModelSkinning = false;

    // CPU draw fallbackも頂点補正を展開し、補正済み法線を描画packetへ写す。
    if (passed)
    {
        passed = gk::SetModelSecondaryMotionVertexContactsEnabled(first, true) == 0;
        firstTransform = gk::detail::FindModelTransform(first);
        firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
        passed = firstState && firstState->skinContactGeometry == &source->geometry && firstState->skinContactPositionIds.Count() == 1 && firstState->skinContactPositionIds.At(0) == 2 && passed;
        if (!passed)
        {
            fprintf(stderr, "skin contact re-enable before CPU fallback failed: %s\n", gk::GetLastErrorMessage());
        }
        if (passed)
        {
            frameOpen = gk::BeginFrame() == 0;
            passed = frameOpen && gk::DrawModel(first) == 0;
        }
        const auto& cpuDraws = gk::detail::GetContext().frame.draws;
        passed = passed && cpuDraws.Count() == 1 && !cpuDraws.At(0).deferredPose && Near(sqrtf(cpuDraws.At(0).model->vertices.At(2).position[0] * cpuDraws.At(0).model->vertices.At(2).position[0] + cpuDraws.At(0).model->vertices.At(2).position[1] * cpuDraws.At(0).model->vertices.At(2).position[1] + cpuDraws.At(0).model->vertices.At(2).position[2] * cpuDraws.At(0).model->vertices.At(2).position[2]), 0.805f) && isfinite(cpuDraws.At(0).model->vertices.At(2).normal[0]) && isfinite(cpuDraws.At(0).model->vertices.At(2).normal[1]) && isfinite(cpuDraws.At(0).model->vertices.At(2).normal[2]);
        if (!passed)
        {
            const float measuredRadius = cpuDraws.Count() == 1 ? sqrtf(cpuDraws.At(0).model->vertices.At(2).position[0] * cpuDraws.At(0).model->vertices.At(2).position[0] + cpuDraws.At(0).model->vertices.At(2).position[1] * cpuDraws.At(0).model->vertices.At(2).position[1] + cpuDraws.At(0).model->vertices.At(2).position[2] * cpuDraws.At(0).model->vertices.At(2).position[2]) : -1.0f;
            fprintf(stderr, "skin contact CPU fallback draw failed (draws=%u radius=%f deferred=%d): %s\n", cpuDraws.Count(), measuredRadius, cpuDraws.Count() == 1 && cpuDraws.At(0).deferredPose ? 1 : 0, gk::GetLastErrorMessage());
        }
    }
    if (frameOpen)
    {
        const bool presented = gk::Present() == 0;
        frameOpen = false;
        passed = presented && passed;
    }
    // 骨の半径設定が鎖の再登録と状態更新を越えて保たれ、queryが純粋であることを調べる。
    if (passed)
    {
        gk::Vec3 beforeRadius[2]{};
        gk::Vec3 afterRadius[2]{};
        const bool beforePoseReady = gk::GetModelBonePositions(first, chain, 2, beforeRadius) == 0;
        const int32_t radiusSet = gk::SetModelSecondaryMotionChainCollisionRadius(first, chain[0], 0.05f);
        const bool afterSetterPoseReady = gk::GetModelBonePositions(first, chain, 2, afterRadius) == 0;
        firstTransform = gk::detail::FindModelTransform(first);
        firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
        const bool setterStored = radiusSet == 0 && firstState && firstState->chains.Count() == 1 && firstState->chains.At(0)->collisionRadius == 0.05f;
        const bool setterDidNotMovePose = beforePoseReady && afterSetterPoseReady && memcmp(beforeRadius, afterRadius, sizeof(beforeRadius)) == 0;
        const bool invalidRadiusAtomic = gk::SetModelSecondaryMotionChainCollisionRadius(first, chain[0], -0.01f) == -1 && gk::SetModelSecondaryMotionChainCollisionRadius(first, chain[0], std::numeric_limits<float>::quiet_NaN()) == -1 && firstState && firstState->chains.At(0)->collisionRadius == 0.05f;
        const bool unknownChainRejected = gk::SetModelSecondaryMotionChainCollisionRadius(first, 0, 0.1f) == -1 && firstState && firstState->chains.At(0)->collisionRadius == 0.05f;
        bool lifecycleKeptRadius = gk::SetModelSecondaryMotionChain(first, chain, 2, settings) == 0;
        firstTransform = gk::detail::FindModelTransform(first);
        firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
        lifecycleKeptRadius = lifecycleKeptRadius && firstState && firstState->chains.Count() == 1 && firstState->chains.At(0)->collisionRadius == 0.05f;
        lifecycleKeptRadius = gk::ResetModelSecondaryMotion(first) == 0 && lifecycleKeptRadius;
        firstTransform = gk::detail::FindModelTransform(first);
        firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
        lifecycleKeptRadius = lifecycleKeptRadius && firstState && firstState->chains.Count() == 1 && firstState->chains.At(0)->collisionRadius == 0.05f;
        lifecycleKeptRadius = gk::UpdateModelSecondaryMotion(first, 0.0) == 0 && lifecycleKeptRadius;
        firstTransform = gk::detail::FindModelTransform(first);
        firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
        lifecycleKeptRadius = lifecycleKeptRadius && firstState && firstState->chains.Count() == 1 && firstState->chains.At(0)->collisionRadius == 0.05f;
        passed = setterStored && setterDidNotMovePose && invalidRadiusAtomic && unknownChainRejected && lifecycleKeptRadius && passed;
        if (!passed)
        {
            fprintf(stderr, "bone collision radius setter or lifecycle contract failed: %s\n", gk::GetLastErrorMessage());
        }

        gk::FModelSecondaryMotionDebugShape debugShapes[4]{};
        const int32_t requiredShapes = gk::GetModelSecondaryMotionDebugShapes(first, nullptr, 0);
        const bool querySize = requiredShapes == 4;
        const gk::FModelSecondaryMotionDebugShape sentinel{ { 17.0f, 18.0f, 19.0f }, { 20.0f, 21.0f, 22.0f }, 23.0f, 24, true, true, true };
        const gk::FModelSecondaryMotionDebugShape sentinelOutput[4] = { sentinel, sentinel, sentinel, sentinel };
        gk::FModelSecondaryMotionDebugShape shortOutput[4] = { sentinel, sentinel, sentinel, sentinel };
        const bool shortCapacityRejected = gk::GetModelSecondaryMotionDebugShapes(first, shortOutput, 3) == -1 && memcmp(shortOutput, sentinelOutput, sizeof(shortOutput)) == 0;
        gk::Vec3 beforeQueries[2]{};
        gk::Vec3 afterQueries[2]{};
        const bool beforeQueryPoseReady = gk::GetModelBonePositions(first, chain, 2, beforeQueries) == 0;
        const int32_t copiedShapes = gk::GetModelSecondaryMotionDebugShapes(first, debugShapes, 4);
        gk::FModelSecondaryMotionDebugShape repeatedShapes[4]{};
        const int32_t repeatedShapeCount = gk::GetModelSecondaryMotionDebugShapes(first, repeatedShapes, 4);
        const bool afterQueryPoseReady = gk::GetModelBonePositions(first, chain, 2, afterQueries) == 0;
        const bool queryDidNotAdvancePose = beforeQueryPoseReady && afterQueryPoseReady && memcmp(beforeQueries, afterQueries, sizeof(beforeQueries)) == 0 && copiedShapes == repeatedShapeCount;
        const bool invalidHandleRejected = gk::GetModelSecondaryMotionDebugShapes({}, shortOutput, 4) == -1 && memcmp(shortOutput, sentinelOutput, sizeof(shortOutput)) == 0;
        bool sawBody = false;
        bool sawFixedBone = false;
        bool sawChildBone = false;
        bool sawVirtualTip = false;
        for (uint32_t index = 0; index < 4; ++index)
        {
            const gk::FModelSecondaryMotionDebugShape& shape = debugShapes[index];
            if (shape.body)
            {
                sawBody = shape.bone == 0 && Near(shape.radius, 0.805f) && Near(shape.start.x, 0.0f) && Near(shape.start.y, 0.0f) && Near(shape.start.z, 0.0f) && Near(shape.end.x, shape.start.x) && Near(shape.end.y, shape.start.y) && Near(shape.end.z, shape.start.z) && !shape.fixed && !shape.virtualTip;
            }
            else if (shape.virtualTip)
            {
                sawVirtualTip = shape.bone == chain[1] && Near(shape.radius, 0.05f) && Near(shape.start.x, 0.0f) && Near(shape.start.y, 2.0f) && Near(shape.start.z, 0.0f) && Near(shape.end.x, shape.start.x) && Near(shape.end.y, shape.start.y) && Near(shape.end.z, shape.start.z) && !shape.fixed;
            }
            else if (shape.bone == chain[0])
            {
                sawFixedBone = shape.fixed && Near(shape.radius, 0.05f) && Near(shape.start.x, 0.0f) && Near(shape.start.y, 0.0f) && Near(shape.start.z, 0.0f) && Near(shape.end.x, shape.start.x) && Near(shape.end.y, shape.start.y) && Near(shape.end.z, shape.start.z);
            }
            else if (shape.bone == chain[1])
            {
                sawChildBone = !shape.fixed && Near(shape.radius, 0.05f) && Near(shape.start.x, 0.0f) && Near(shape.start.y, 1.0f) && Near(shape.start.z, 0.0f) && Near(shape.end.x, shape.start.x) && Near(shape.end.y, shape.start.y) && Near(shape.end.z, shape.start.z);
            }
        }
        passed = querySize && copiedShapes == 4 && shortCapacityRejected && invalidHandleRejected && queryDidNotAdvancePose && sawBody && sawFixedBone && sawChildBone && sawVirtualTip && passed;
        if (!passed)
        {
            fprintf(stderr, "secondary-motion debug-shape query contract failed (required=%d copied=%d body=%d root=%d child=%d tip=%d): %s\n", requiredShapes, copiedShapes, sawBody ? 1 : 0, sawFixedBone ? 1 : 0, sawChildBone ? 1 : 0, sawVirtualTip ? 1 : 0, gk::GetLastErrorMessage());
        }
    }

    if (first.IsValid())
    {
        const bool secondaryCleared = gk::ClearModelSecondaryMotion(first) == 0;
        firstTransform = gk::detail::FindModelTransform(first);
        firstState = firstTransform && firstTransform->playback ? firstTransform->playback->secondaryMotion : nullptr;
        passed = secondaryCleared && (!firstState || firstState->chains.Count() == 0) && passed;
        passed = gk::DeleteModel(first) == 0 && passed;
    }
    if (second.IsValid())
    {
        passed = gk::DeleteModel(second) == 0 && passed;
    }
    if (model.IsValid())
    {
        passed = gk::DeleteModel(model) == 0 && passed;
    }
    else if (!resourceRegistered)
    {
        gk::Release(&resource->reference);
    }
    if (!passed)
    {
        fprintf(stderr, "skin point contact API, mask, frozen snapshot, or CPU materialization failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * 腕IK preview計算の独立した幾何期待値と失敗時保持を確認する。
 */
bool TestModelArmIkPreview()
{
    const gk::Vec3 joints[3] = { { -1.0f, 1.0f, 0.0f }, { -2.0f, 1.0f, 0.0f }, { -3.0f, 1.0f, 0.0f } };
    gk::examples::FModelArmIkPreview preview{};
    bool passed = gk::examples::MakeModelArmIkPreview({ 0.0f, 1.0f, 0.0f }, joints, preview) && IsFinitePreview(preview);
    passed = passed && Near(preview.joints[0].x, -1.0f) && Near(preview.joints[0].y, 1.0f) && Near(preview.joints[0].z, 0.0f) && Near(preview.joints[1].x, -2.0f) && Near(preview.joints[1].y, 1.0f) && Near(preview.joints[1].z, 0.0f) && Near(preview.joints[2].x, -3.0f) && Near(preview.joints[2].y, 1.0f) && Near(preview.joints[2].z, 0.0f);
    passed = passed && Near(preview.target.x, -2.5f) && Near(preview.target.y, 0.5f) && Near(preview.target.z, 0.0f) && Near(preview.pole.x, -1.0f) && Near(preview.pole.y, 1.0f) && Near(preview.pole.z, 2.0f) && Near(preview.armLength, 2.0f);
    gk::examples::FModelArmIkPreview mirrored{};
    const gk::Vec3 mirroredJoints[3] = { { 1.0f, 1.0f, 0.0f }, { 2.0f, 1.0f, 0.0f }, { 3.0f, 1.0f, 0.0f } };
    passed = gk::examples::MakeModelArmIkPreview({ 0.0f, 1.0f, 0.0f }, mirroredJoints, mirrored) && IsFinitePreview(mirrored) && Near(mirrored.target.x, 2.5f) && Near(mirrored.target.y, 0.5f) && Near(mirrored.target.z, 0.0f) && Near(mirrored.pole.x, 1.0f) && Near(mirrored.pole.y, 1.0f) && Near(mirrored.pole.z, 2.0f) && Near(mirrored.armLength, 2.0f) && passed;
    for (float scale : { 0.0001f, 0.01f, 0.5f, 10.0f, 100.0f, 10000.0f })
    {
        gk::Vec3 scaled[3] = { { joints[0].x * scale, joints[0].y * scale, joints[0].z * scale }, { joints[1].x * scale, joints[1].y * scale, joints[1].z * scale }, { joints[2].x * scale, joints[2].y * scale, joints[2].z * scale } };
        gk::examples::FModelArmIkPreview scaledPreview{};
        passed = gk::examples::MakeModelArmIkPreview({ 0.0f, scale, 0.0f }, scaled, scaledPreview) && IsFinitePreview(scaledPreview) && Near(scaledPreview.target.x / scale, -2.5f) && Near(scaledPreview.target.y / scale, 0.5f) && Near(scaledPreview.pole.x / scale, -1.0f) && Near(scaledPreview.pole.y / scale, 1.0f) && Near(scaledPreview.pole.z / scale, 2.0f) && Near(scaledPreview.armLength / scale, 2.0f) && passed;
    }
    const gk::examples::FModelArmIkPreview original = preview;
    const gk::Vec3 degenerate[3] = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
    passed = !gk::examples::MakeModelArmIkPreview({ 0.0f, 1.0f, 0.0f }, degenerate, preview) && Near(preview.target.x, original.target.x) && Near(preview.target.y, original.target.y) && Near(preview.pole.x, original.pole.x) && Near(preview.armLength, original.armLength) && passed;
    const gk::Vec3 zeroLength[3] = { joints[0], joints[0], joints[2] };
    passed = !gk::examples::MakeModelArmIkPreview({ 0.0f, 1.0f, 0.0f }, zeroLength, preview) && Near(preview.target.x, original.target.x) && Near(preview.target.y, original.target.y) && Near(preview.pole.x, original.pole.x) && Near(preview.armLength, original.armLength) && passed;
    const gk::Vec3 unbalanced[3] = { { -1.0f, 1.0f, 0.0f }, { -100.0f, 1.0f, 0.0f }, { -101.0f, 1.0f, 0.0f } };
    passed = !gk::examples::MakeModelArmIkPreview({ 0.0f, 1.0f, 0.0f }, unbalanced, preview) && SamePreview(preview, original) && passed;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const gk::Vec3 nanJoint[3] = { { nan, 1.0f, 0.0f }, joints[1], joints[2] };
    passed = !gk::examples::MakeModelArmIkPreview({ 0.0f, 1.0f, 0.0f }, nullptr, preview) && SamePreview(preview, original) && passed;
    passed = !gk::examples::MakeModelArmIkPreview({ nan, 1.0f, 0.0f }, joints, preview) && SamePreview(preview, original) && passed;
    passed = !gk::examples::MakeModelArmIkPreview({ 0.0f, 1.0f, 0.0f }, nanJoint, preview) && SamePreview(preview, original) && passed;
    passed = !gk::examples::BuildModelArmIkPreview({}, preview) && SamePreview(preview, original) && passed;
    if (!passed)
    {
        fprintf(stderr, "model arm IK preview geometry or failure atomicity failed\n");
    }
    return passed;
}

/**
 * 現在の肘の曲げ面を保ち、手先を腕の届く範囲で少し動かす目標を確認する。
 */
bool TestMotionFollowingArmIkPreview()
{
    // 胴体は上向き、右腕は-Xへ伸び、肘を下へ曲げた検査姿勢。
    const gk::Vec3 hips{ 0.0f, 0.0f, 0.0f };
    const gk::Vec3 torso{ 0.0f, 1.0f, 0.0f };
    const gk::Vec3 leftShoulder{ 1.0f, 1.0f, 0.0f };
    const gk::Vec3 bentJoints[3] = { { -1.0f, 1.0f, 0.0f }, { -2.0f, 1.0f, 0.0f }, { -2.0f, 0.0f, 0.0f } };
    gk::examples::FModelArmIkPreview original{};
    // 既定の0.05L分だけ手先を胴体前方向へ動かす。
    bool passed = gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, bentJoints, original) && NearVector(original.joints[0], bentJoints[0]) && NearVector(original.joints[1], bentJoints[1]) && NearVector(original.joints[2], bentJoints[2]) && NearVector(original.target, { -2.0f, 0.0f, 0.1f }) && Near(original.armLength, 2.0f);
    // rootから目標・poleへの方向が腕長と元の曲げ面を保つか調べる。
    const gk::Vec3 targetVector{ original.target.x - bentJoints[0].x, original.target.y - bentJoints[0].y, original.target.z - bentJoints[0].z };
    const gk::Vec3 poleVector{ original.pole.x - bentJoints[0].x, original.pole.y - bentJoints[0].y, original.pole.z - bentJoints[0].z };
    const float targetDistance = sqrtf(targetVector.x * targetVector.x + targetVector.y * targetVector.y + targetVector.z * targetVector.z);
    const float poleDistance = sqrtf(poleVector.x * poleVector.x + poleVector.y * poleVector.y + poleVector.z * poleVector.z);
    const float bendNormalZ = poleVector.x * targetVector.y - poleVector.y * targetVector.x;
    passed = Near(targetDistance, 1.4177447f) && Near(poleDistance, original.armLength) && bendNormalZ > 1.0f && passed;

    // offset 0では元の手首位置と曲げ側を保つ。
    gk::examples::FModelArmIkPreview unchangedGoal{};
    passed = gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, bentJoints, unchangedGoal, 0.0f) && NearVector(unchangedGoal.target, bentJoints[2]) && unchangedGoal.pole.x < bentJoints[0].x && unchangedGoal.pole.y > bentJoints[0].y && passed;

    // 直線腕はすでに最大距離なので、前方向への移動量を0へ切り詰める。
    const gk::Vec3 straightJoints[3] = { { -1.0f, 1.0f, 0.0f }, { -2.0f, 1.0f, 0.0f }, { -3.0f, 1.0f, 0.0f } };
    gk::examples::FModelArmIkPreview clipped{};
    passed = gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, straightJoints, clipped) && NearVector(clipped.target, straightJoints[2]) && Near(clipped.armLength, 2.0f) && passed;

    // 腕とbody-forwardが-XZを含めて同じ-Z方向でもpoleを有限値で作る。
    const gk::Vec3 forwardAlignedJoints[3] = { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, -1.0f }, { 1.0f, 1.0f, -2.0f } };
    gk::examples::FModelArmIkPreview forwardAligned{};
    const bool forwardAlignedBuilt = gk::examples::MakeMotionFollowingArmIkPreview({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, forwardAlignedJoints, forwardAligned);
    const gk::Vec3 forwardAlignedPole{ forwardAligned.pole.x - forwardAlignedJoints[0].x, forwardAligned.pole.y - forwardAlignedJoints[0].y, forwardAligned.pole.z - forwardAlignedJoints[0].z };
    const float forwardAlignedPoleLength = sqrtf(forwardAlignedPole.x * forwardAlignedPole.x + forwardAlignedPole.y * forwardAlignedPole.y + forwardAlignedPole.z * forwardAlignedPole.z);
    if (!forwardAlignedBuilt)
    {
        fprintf(stderr, "body-forward-parallel straight arm was rejected at max reach\n");
    }
    passed = forwardAlignedBuilt && NearVector(forwardAligned.target, forwardAlignedJoints[2]) && isfinite(forwardAlignedPole.x) && isfinite(forwardAlignedPole.y) && isfinite(forwardAlignedPole.z) && Near(forwardAlignedPoleLength, 2.0f) && passed;

    // yaw、pitch、rollと平行移動の共通変換で共変性を確かめる。
    const float halfPi = 1.57079632679f;
    const float pi = 3.14159265359f;
    const gk::Vec3 translation{ 5.0f, -3.0f, 7.0f };
    struct FRotationCase
    {
        // 共通の剛体変換に使う回転軸と角度。
        char axis;
        float angle;
    };
    const float epsilon = 0.00001f;
    const FRotationCase rotations[10] = { { 'y', halfPi }, { 'y', pi }, { 'y', epsilon }, { 'y', -epsilon }, { 'y', pi - epsilon }, { 'y', -pi + epsilon }, { 'z', halfPi }, { 'x', halfPi }, { 'x', -halfPi }, { 'z', -halfPi } };
    for (uint32_t index = 0; index < 10; ++index)
    {
        const char axis = rotations[index].axis;
        const float angle = rotations[index].angle;
        const auto rotatePoint = [axis, angle, translation](gk::Vec3 point) { return axis == 'y' ? RotateTranslateY(point, angle, translation) : (axis == 'z' ? RotateTranslateZ(point, angle, translation) : RotateTranslateX(point, angle, translation)); };
        const gk::Vec3 rotatedJoints[3] = { rotatePoint(bentJoints[0]), rotatePoint(bentJoints[1]), rotatePoint(bentJoints[2]) };
        gk::examples::FModelArmIkPreview rotated{};
        const bool built = gk::examples::MakeMotionFollowingArmIkPreview(rotatePoint(hips), rotatePoint(torso), rotatePoint(leftShoulder), rotatedJoints, rotated);
        passed = built && NearVector(rotated.target, rotatePoint(original.target)) && NearVector(rotated.pole, rotatePoint(original.pole)) && Near(rotated.armLength, original.armLength) && passed;
    }

    // 無効な比率や関節値で出力が変わらないことを確認する。
    gk::examples::FModelArmIkPreview output = original;
    const gk::examples::FModelArmIkPreview saved = output;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const gk::Vec3 nonFiniteJoints[3] = { { nan, 1.0f, 0.0f }, bentJoints[1], bentJoints[2] };
    const gk::Vec3 collapsedJoints[3] = { bentJoints[0], bentJoints[0], bentJoints[2] };
    passed = !gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, bentJoints, output, -0.01f) && SamePreview(output, saved) && passed;
    passed = !gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, bentJoints, output, 1.01f) && SamePreview(output, saved) && passed;
    passed = !gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, bentJoints, output, nan) && SamePreview(output, saved) && passed;
    passed = !gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, nullptr, output) && SamePreview(output, saved) && passed;
    passed = !gk::examples::MakeMotionFollowingArmIkPreview(hips, hips, leftShoulder, bentJoints, output) && SamePreview(output, saved) && passed;
    passed = !gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, { -1.0f, 0.0f, 0.0f }, bentJoints, output) && SamePreview(output, saved) && passed;
    passed = !gk::examples::MakeMotionFollowingArmIkPreview({ nan, 0.0f, 0.0f }, torso, leftShoulder, bentJoints, output) && SamePreview(output, saved) && passed;
    passed = !gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, collapsedJoints, output) && SamePreview(output, saved) && passed;
    passed = !gk::examples::MakeMotionFollowingArmIkPreview(hips, torso, leftShoulder, nonFiniteJoints, output) && SamePreview(output, saved) && passed;

    // 手首の元位置を目標にし、肘面を示すpoleだけで元の関節回転を保つ。
    // 90度曲がった元姿勢の2本腕でIKを解き、中央関節の回転を照合する。
    gk::model::animation::FModelSkeleton skeleton;
    const int32_t parents[3] = { -1, 0, 1 };
    gk::model::animation::FModelBoneTransform rest[3]{};
    rest[1].position[0] = 1.0f;
    rest[1].rotation[2] = 0.70710678118f;
    rest[1].rotation[3] = 0.70710678118f;
    rest[2].position[0] = 1.0f;
    gk::model::animation::FModelPose sourcePose;
    gk::model::animation::FModelPose solvedPose;
    gk::Array<float> sourceWorld;
    gk::String error;
    const bool poseReady = skeleton.parents.AppendRange(parents, 3) && skeleton.restLocalTransforms.AppendRange(rest, 3) && gk::model::animation::InitializeModelPose(skeleton, sourcePose, error);
    const gk::Vec3 sourceJoints[3] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    const bool sourceEvaluated = poseReady && gk::model::animation::EvaluateModelPose(skeleton, sourcePose, sourceWorld, error) && sourceWorld.Count() == 48 && Near(sourceWorld.At(12), sourceJoints[0].x) && Near(sourceWorld.At(13), sourceJoints[0].y) && Near(sourceWorld.At(14), sourceJoints[0].z) && Near(sourceWorld.At(28), sourceJoints[1].x) && Near(sourceWorld.At(29), sourceJoints[1].y) && Near(sourceWorld.At(30), sourceJoints[1].z) && Near(sourceWorld.At(44), sourceJoints[2].x) && Near(sourceWorld.At(45), sourceJoints[2].y) && Near(sourceWorld.At(46), sourceJoints[2].z);
    gk::examples::FModelArmIkPreview sourcePreview{};
    gk::model::animation::FModelPose sourceDrivenPose;
    const bool previewBuilt = sourceEvaluated && gk::examples::MakeMotionFollowingArmIkPreview({ -1.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, sourceJoints, sourcePreview, 0.0f) && NearVector(sourcePreview.target, sourceJoints[2]);
    const float ikTarget[3] = { sourcePreview.target.x, sourcePreview.target.y, sourcePreview.target.z };
    const float ikPole[3] = { sourcePreview.pole.x, sourcePreview.pole.y, sourcePreview.pole.z };
    bool hingePassed = previewBuilt && gk::model::animation::SolveTwoBoneIk(skeleton, sourcePose, 0, 1, 2, ikTarget, ikPole, 1.0f, sourceDrivenPose, error);
    if (hingePassed)
    {
        const float* expected = sourcePose.localTransforms.At(1).rotation;
        const float* actual = sourceDrivenPose.localTransforms.At(1).rotation;
        double directError = 0.0;
        double negatedError = 0.0;
        for (uint32_t index = 0; index < 4; ++index)
        {
            const double directDelta = static_cast<double>(expected[index]) - actual[index];
            const double negatedDelta = static_cast<double>(expected[index]) + actual[index];
            directError += directDelta * directDelta;
            negatedError += negatedDelta * negatedDelta;
        }
        hingePassed = sqrt(directError < negatedError ? directError : negatedError) <= 0.001;
    }
    passed = hingePassed && passed;
    if (!passed)
    {
        fprintf(stderr, "motion-following arm IK target or source bend plane failed\n");
    }
    return passed;
}

/**
 * 複数揺れ鎖の更新がFKを共有し、身体接触時だけ描画姿勢の検算を加える。
 */
bool TestSecondaryMotionPoseEvaluationBudget()
{
    auto* resource = gk::detail::CreateModelResource();
    if (!resource)
    {
        return false;
    }
    auto* source = new AAnimationRigTestSource;
    source->boneCount = 7;
    const int32_t parents[7] = { -1, 0, 1, 0, 3, 0, 0 };
    gk::model::animation::FModelBoneTransform rest[7]{};
    rest[1].position[0] = 1.0f;
    rest[2].position[0] = 1.0f;
    rest[3].position[1] = 1.0f;
    rest[4].position[0] = 1.0f;
    rest[5].position[0] = 10.0f;
    rest[6].position[1] = -10.0f;
    if (!source->skeleton.parents.AppendRange(parents, 7) || !source->skeleton.restLocalTransforms.AppendRange(rest, 7))
    {
        delete source;
        gk::Release(&resource->reference);
        return false;
    }
    gk::String error;
    resource->animation = gk::model::CreateModelAnimationAsset(source, error);
    gk::detail::ModelVertex vertices[7]{};
    if (!resource->animation || !resource->vertices.AppendRange(vertices, 7))
    {
        gk::Release(&resource->reference);
        return false;
    }
    const auto model = gk::detail::RegisterModelResource(resource, error);
    gk::detail::ModelTransform baseTransform{};
    baseTransform.handle = model;
    baseTransform.scale = { 1.0f, 1.0f, 1.0f };
    const bool transformAdded = model.IsValid() && gk::detail::GetContext().modelTransforms.Append(baseTransform);
    const auto instance = transformAdded ? gk::CreateModelInstance(model) : gk::ModelHandle{};
    if (!transformAdded || !instance.IsValid())
    {
        if (instance.IsValid())
        {
            gk::DeleteModel(instance);
        }
        if (model.IsValid())
        {
            gk::DeleteModel(model);
        }
        else
        {
            gk::Release(&resource->reference);
        }
        fprintf(stderr, "secondary-motion pose-budget fixture setup failed: %s\n", error.CStr());
        return false;
    }
    const uint32_t firstChain[2] = { 1, 2 };
    const uint32_t secondChain[2] = { 3, 4 };
    gk::FModelSecondaryMotionSettings settings{};
    settings.gravity = {};
    settings.windAcceleration = {};
    settings.endOffset = { 0.0f, 0.25f, 0.0f };
    settings.maxAngleDegrees = 60.0f;
    bool passed = gk::SetModelSecondaryMotionChain(instance, firstChain, 2, settings) == 0 && gk::SetModelSecondaryMotionChain(instance, secondChain, 2, settings) == 0;
    passed = gk::SetModelSecondaryMotionChainCollisionRadius(instance, firstChain[0], 0.05f) == 0 && gk::SetModelSecondaryMotionChainCollisionRadius(instance, secondChain[0], 0.15f) == 0 && passed;
    auto* radiusTransform = gk::detail::FindModelTransform(instance);
    auto* radiusState = radiusTransform && radiusTransform->playback ? radiusTransform->playback->secondaryMotion : nullptr;
    bool separateRadiiStayIndependent = radiusState && radiusState->chains.Count() == 2;
    if (separateRadiiStayIndependent)
    {
        separateRadiiStayIndependent = radiusState->chains.At(0)->collisionRadius == 0.05f && radiusState->chains.At(1)->collisionRadius == 0.15f;
    }
    passed = separateRadiiStayIndependent && passed;
    const auto ReadSecondaryState = [](gk::ModelHandle handle)
    {
        auto* currentTransform = gk::detail::FindModelTransform(handle);
        return currentTransform && currentTransform->playback ? currentTransform->playback->secondaryMotion : nullptr;
    };
    const auto ReportLinkStage = [&](bool result, const char* stage)
    {
        if (!result)
        {
            const auto* currentState = ReadSecondaryState(instance);
            fprintf(stderr, "secondary-motion link contract failed at %s (links=%u iterations=%u): %s\n", stage, currentState ? currentState->links.Count() : 0u, currentState ? currentState->linkIterations : 0u, gk::GetLastErrorMessage());
        }
        passed = result && passed;
        return result;
    };
    if (passed)
    {
        gk::FModelSecondaryMotionLink links[1]{};
        links[0].firstBone = 2;
        links[0].secondBone = 4;
        links[0].radius = 0.125f;
        const int32_t debugCountBeforeLinks = gk::GetModelSecondaryMotionDebugShapes(instance, nullptr, 0);
        const bool linkSet = gk::SetModelSecondaryMotionLinks(instance, links, 1, 12) == 0;
        auto* currentState = ReadSecondaryState(instance);
        const bool copiedLink = currentState && currentState->chains.Count() == 2 && currentState->links.Count() == 1 && currentState->links.At(0).firstBone == 2 && currentState->links.At(0).secondBone == 4 && currentState->links.At(0).radius == 0.125f && currentState->linkIterations == 12;
        ReportLinkStage(linkSet && copiedLink, "copies a link between free bones in different chains and stores iterations");
        links[0].firstBone = 4;
        links[0].secondBone = 2;
        links[0].radius = 9.0f;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(currentState && currentState->links.Count() == 1 && currentState->links.At(0).firstBone == 2 && currentState->links.At(0).secondBone == 4 && currentState->links.At(0).radius == 0.125f, "caller mutation does not change copied link settings");
        const int32_t debugCountWithLink = gk::GetModelSecondaryMotionDebugShapes(instance, nullptr, 0);
        ReportLinkStage(debugCountBeforeLinks >= 0 && debugCountWithLink == debugCountBeforeLinks + 1, "debug shape count includes the registered link");

        gk::FModelSecondaryMotionCollider replacementCollider{};
        replacementCollider.bone = 0;
        replacementCollider.start = { 10.0f, 10.0f, 10.0f };
        replacementCollider.end = replacementCollider.start;
        replacementCollider.radius = 0.15f;
        const bool bodyReplacementSucceeded = gk::SetModelSecondaryMotionColliders(instance, &replacementCollider, 1, 0.01f) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(bodyReplacementSucceeded && currentState && currentState->colliders.Count() == 1 && currentState->links.Count() == 1 && currentState->links.At(0).firstBone == 2 && currentState->links.At(0).secondBone == 4 && currentState->links.At(0).radius == 0.125f && currentState->linkIterations == 12, "body collider replacement preserves links and iteration settings");
        const bool bodyRemovalSucceeded = gk::SetModelSecondaryMotionColliders(instance, nullptr, 0) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(bodyRemovalSucceeded && currentState && currentState->colliders.Count() == 0 && currentState->links.Count() == 1 && currentState->links.At(0).firstBone == 2 && currentState->links.At(0).secondBone == 4 && currentState->links.At(0).radius == 0.125f && currentState->linkIterations == 12, "clearing body colliders preserves links and iteration settings");

        const uint32_t queriedBones[7] = { 0, 1, 2, 3, 4, 5, 6 };
        gk::Vec3 beforeLinkUpdate[7]{};
        gk::Vec3 afterLinkUpdate[7]{};
        const bool beforeQuery = gk::GetModelBonePositions(instance, queriedBones, 7, beforeLinkUpdate) == 0;
        const bool resetKeptLink = gk::ResetModelSecondaryMotion(instance) == 0;
        currentState = ReadSecondaryState(instance);
        const bool resetStateKeptLink = currentState && currentState->links.Count() == 1 && currentState->links.At(0).radius == 0.125f && currentState->linkIterations == 12 && currentState->chains.Count() == 2;
        const bool updateKeptLink = gk::UpdateModelSecondaryMotion(instance, 0.0) == 0;
        currentState = ReadSecondaryState(instance);
        const bool updateStateKeptLink = currentState && currentState->links.Count() == 1 && currentState->links.At(0).radius == 0.125f && currentState->linkIterations == 12 && currentState->chains.Count() == 2;
        const bool afterQuery = gk::GetModelBonePositions(instance, queriedBones, 7, afterLinkUpdate) == 0;
        ReportLinkStage(beforeQuery && resetKeptLink && resetStateKeptLink && updateKeptLink && updateStateKeptLink && afterQuery && memcmp(beforeLinkUpdate, afterLinkUpdate, sizeof(beforeLinkUpdate)) == 0, "reset and zero-delta update keep links and leave the no-collider pose unchanged");

        gk::FModelSecondaryMotionLink invalidLink = { 2, 5, 0.1f };
        gk::FModelSecondaryMotionLink tooManyLinks[1025]{};
        for (uint32_t index = 0; index < 1025; ++index)
        {
            tooManyLinks[index] = { 2, 4, 0.1f };
        }
        gk::FModelSecondaryMotionLink duplicateLinks[2] = { { 2, 4, 0.1f }, { 4, 2, 0.2f } };
        gk::FModelSecondaryMotionLink sameChainLink = { 2, 2, 0.1f };
        gk::FModelSecondaryMotionLink fixedRootLink = { 1, 4, 0.1f };
        gk::FModelSecondaryMotionLink invalidRadiusLink = { 2, 4, std::numeric_limits<float>::quiet_NaN() };
        const auto RejectWithoutReplacingLink = [&](const gk::FModelSecondaryMotionLink* candidate, uint32_t count, uint32_t iterations)
        {
            const bool rejected = gk::SetModelSecondaryMotionLinks(instance, candidate, count, iterations) == -1;
            const auto* preserved = ReadSecondaryState(instance);
            return rejected && preserved && preserved->chains.Count() == 2 && preserved->links.Count() == 1 && preserved->links.At(0).firstBone == 2 && preserved->links.At(0).secondBone == 4 && preserved->links.At(0).radius == 0.125f && preserved->linkIterations == 12;
        };
        const gk::FModelSecondaryMotionLink validLink = { 2, 4, 0.125f };
        const bool invalidsAtomic = RejectWithoutReplacingLink(nullptr, 1, 8) && RejectWithoutReplacingLink(tooManyLinks, 1025, 8) && RejectWithoutReplacingLink(&invalidLink, 1, 8) && RejectWithoutReplacingLink(duplicateLinks, 2, 8) && RejectWithoutReplacingLink(&sameChainLink, 1, 8) && RejectWithoutReplacingLink(&fixedRootLink, 1, 8) && RejectWithoutReplacingLink(&invalidRadiusLink, 1, 8) && RejectWithoutReplacingLink(&validLink, 1, 0) && RejectWithoutReplacingLink(&validLink, 1, 33);
        ReportLinkStage(invalidsAtomic, "invalid count, iterations, endpoints, duplicate pair, or radius preserve current state");

        const uint32_t replacementMissingEndpoint[2] = { 1, 5 };
        const bool missingEndpointRejected = gk::SetModelSecondaryMotionChain(instance, replacementMissingEndpoint, 2, settings) == -1;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(missingEndpointRejected && currentState && currentState->chains.Count() == 2 && currentState->links.Count() == 1 && currentState->links.At(0).firstBone == 2 && currentState->links.At(0).secondBone == 4 && currentState->linkIterations == 12, "failed same-root chain replacement keeps referenced link state");
        const uint32_t shortenedFirstChain[1] = { 1 };
        const bool shortenedChainRejected = gk::SetModelSecondaryMotionChain(instance, shortenedFirstChain, 1, settings) == -1;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(shortenedChainRejected && currentState && currentState->chains.Count() == 2 && currentState->chains.At(0)->bones.Count() == 2 && currentState->links.Count() == 1 && currentState->links.At(0).firstBone == 2 && currentState->links.At(0).secondBone == 4 && currentState->linkIterations == 12, "same-root chain shortening cannot remove a referenced free bone");

        const bool clearedLinkBeforeDistance = gk::SetModelSecondaryMotionLinks(instance, nullptr, 0) == 0;
        const int32_t debugCountBeforeDistance = gk::GetModelSecondaryMotionDebugShapes(instance, nullptr, 0);
        gk::FModelSecondaryMotionDistanceConstraint distanceConstraint = { 2, 4, 0.0f, 0.0f };
        const bool distanceSet = clearedLinkBeforeDistance && gk::SetModelSecondaryMotionDistanceConstraints(instance, &distanceConstraint, 1, 16) == 0;
        currentState = ReadSecondaryState(instance);
        const double baseDistance = sqrt(2.0);
        const bool derivedDistanceCopied = currentState && currentState->chains.Count() == 2 && currentState->links.Count() == 0 && currentState->distanceConstraints.Count() == 1 && currentState->distanceLambdas.Count() == 1 && fabs(currentState->distanceConstraints.At(0).restDistance - baseDistance) < 1.0e-6 && currentState->distanceConstraints.At(0).firstBone == 2 && currentState->distanceConstraints.At(0).secondBone == 4 && currentState->linkIterations == 16;
        ReportLinkStage(distanceSet && derivedDistanceCopied, "distance registration resolves zero rest length from the initial pose and stores shared iterations");
        const gk::FModelSecondaryMotionDistanceConstraint distanceInputSnapshot = distanceConstraint;
        distanceConstraint.firstBone = 4;
        distanceConstraint.secondBone = 2;
        distanceConstraint.restDistance = 9.0f;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(currentState && currentState->distanceConstraints.Count() == 1 && currentState->distanceConstraints.At(0).firstBone == 2 && currentState->distanceConstraints.At(0).secondBone == 4 && fabs(currentState->distanceConstraints.At(0).restDistance - baseDistance) < 1.0e-6, "caller edits do not alter copied distance settings");
        distanceConstraint = distanceInputSnapshot;

        const gk::FModelSecondaryMotionDistanceConstraint unregisteredDistance = { 2, 5, 0.5f, 0.0f };
        const gk::FModelSecondaryMotionDistanceConstraint sameChainDistance = { 2, 1, 0.5f, 0.0f };
        const gk::FModelSecondaryMotionDistanceConstraint fixedRootDistance = { 1, 4, 0.5f, 0.0f };
        const gk::FModelSecondaryMotionDistanceConstraint invalidRestDistance = { 2, 4, -0.5f, 0.0f };
        const gk::FModelSecondaryMotionDistanceConstraint invalidComplianceDistance = { 2, 4, 0.5f, -0.01f };
        const gk::FModelSecondaryMotionDistanceConstraint nonFiniteDistance = { 2, 4, std::numeric_limits<float>::quiet_NaN(), 0.0f };
        const gk::FModelSecondaryMotionDistanceConstraint duplicateDistances[2] = { { 2, 4, 0.5f, 0.0f }, { 4, 2, 0.75f, 0.01f } };
        gk::FModelSecondaryMotionDistanceConstraint tooManyDistances[1025]{};
        for (uint32_t index = 0; index < 1025; ++index)
        {
            tooManyDistances[index] = { 2, 4, 0.5f, 0.0f };
        }
        const auto RejectWithoutReplacingDistance = [&](const gk::FModelSecondaryMotionDistanceConstraint* candidate, uint32_t count, uint32_t iterations)
        {
            const auto* before = ReadSecondaryState(instance);
            std::vector<gk::FModelSecondaryMotionDistanceConstraint> beforeConstraints;
            std::vector<double> beforeLambdas;
            if (before && before->distanceConstraints.Count() > 0)
            {
                beforeConstraints.assign(before->distanceConstraints.Data(), before->distanceConstraints.Data() + before->distanceConstraints.Count());
            }
            if (before && before->distanceLambdas.Count() > 0)
            {
                beforeLambdas.assign(before->distanceLambdas.Data(), before->distanceLambdas.Data() + before->distanceLambdas.Count());
            }
            const double beforeStep = before ? before->distanceStepSeconds : 0.0;
            const bool rejected = gk::SetModelSecondaryMotionDistanceConstraints(instance, candidate, count, iterations) == -1;
            const auto* preserved = ReadSecondaryState(instance);
            const bool arraysPreserved = preserved && preserved->distanceConstraints.Count() == beforeConstraints.size() && preserved->distanceLambdas.Count() == beforeLambdas.size() && (beforeConstraints.empty() || memcmp(preserved->distanceConstraints.Data(), beforeConstraints.data(), beforeConstraints.size() * sizeof(beforeConstraints[0])) == 0) && (beforeLambdas.empty() || memcmp(preserved->distanceLambdas.Data(), beforeLambdas.data(), beforeLambdas.size() * sizeof(beforeLambdas[0])) == 0);
            return rejected && preserved == before && arraysPreserved && preserved->distanceStepSeconds == beforeStep && preserved->chains.Count() == 2 && preserved->links.Count() == 0 && preserved->distanceConstraints.Count() == 1 && preserved->distanceLambdas.Count() == 1 && fabs(preserved->distanceConstraints.At(0).restDistance - baseDistance) < 1.0e-6 && preserved->distanceConstraints.At(0).compliance == 0.0f && preserved->linkIterations == 16;
        };
        const bool invalidDistanceSettingsAtomic = RejectWithoutReplacingDistance(nullptr, 1, 8) && RejectWithoutReplacingDistance(tooManyDistances, 1025, 8) && RejectWithoutReplacingDistance(&unregisteredDistance, 1, 8) && RejectWithoutReplacingDistance(&sameChainDistance, 1, 8) && RejectWithoutReplacingDistance(&fixedRootDistance, 1, 8) && RejectWithoutReplacingDistance(&invalidRestDistance, 1, 8) && RejectWithoutReplacingDistance(&invalidComplianceDistance, 1, 8) && RejectWithoutReplacingDistance(&nonFiniteDistance, 1, 8) && RejectWithoutReplacingDistance(duplicateDistances, 2, 8) && RejectWithoutReplacingDistance(&distanceConstraint, 1, 0) && RejectWithoutReplacingDistance(&distanceConstraint, 1, 33);
        ReportLinkStage(invalidDistanceSettingsAtomic, "invalid distance count, endpoints, duplicates, parameters, or iteration limits preserve the prior settings");

        const bool compatibleChainReplacement = gk::SetModelSecondaryMotionChain(instance, firstChain, 2, settings) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(compatibleChainReplacement && currentState && currentState->chains.Count() == 2 && currentState->distanceConstraints.Count() == 1 && fabs(currentState->distanceConstraints.At(0).restDistance - baseDistance) < 1.0e-6 && currentState->linkIterations == 16, "re-registering a referenced chain retains its distance settings");

        const uint32_t removeDistanceEndpoint[1] = { 1 };
        const bool removeDistanceEndpointRejected = gk::SetModelSecondaryMotionChain(instance, removeDistanceEndpoint, 1, settings) == -1;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(removeDistanceEndpointRejected && currentState && currentState->chains.Count() == 2 && currentState->distanceConstraints.Count() == 1 && fabs(currentState->distanceConstraints.At(0).restDistance - baseDistance) < 1.0e-6 && currentState->linkIterations == 16, "chain replacement cannot remove a bone referenced by a distance constraint");

        const uint32_t distanceBones[7] = { 0, 1, 2, 3, 4, 5, 6 };
        const auto DistanceForConstraint = [](gk::Vec3 first, gk::Vec3 second)
        {
            const double x = static_cast<double>(first.x) - second.x;
            const double y = static_cast<double>(first.y) - second.y;
            const double z = static_cast<double>(first.z) - second.z;
            return sqrt(x * x + y * y + z * z);
        };
        const auto DistancePoseConstraintsHold = [&](const gk::Vec3* positions)
        {
            const double firstLength = DistanceForConstraint(positions[1], positions[2]);
            const double secondLength = DistanceForConstraint(positions[3], positions[4]);
            return fabs(positions[1].x - 1.0f) <= 0.001 && fabs(positions[1].y) <= 0.001 && fabs(positions[3].x) <= 0.001 && fabs(positions[3].y - 1.0f) <= 0.001 && fabs(firstLength - 1.0) <= 0.001 && fabs(secondLength - 1.0) <= 0.001 && (positions[2].x - positions[1].x) / firstLength >= 0.5 - 0.001 && (positions[4].x - positions[3].x) / secondLength >= 0.5 - 0.001;
        };
        bool noBodyDistanceReady = gk::ResetModelSecondaryMotion(instance) == 0;
        currentState = ReadSecondaryState(instance);
        if (noBodyDistanceReady && currentState && currentState->chains.Count() == 2)
        {
            currentState->chains.At(0)->simulation.points.At(1).position.x += 0.5f;
            currentState->chains.At(1)->simulation.points.At(1).position.x -= 0.5f;
        }
        else
        {
            noBodyDistanceReady = false;
        }
        gk::Vec3 noBodySolvedPositions[7]{};
        const bool noBodyDistanceUpdate = noBodyDistanceReady && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && gk::GetModelBonePositions(instance, distanceBones, 7, noBodySolvedPositions) == 0;
        const double noBodyDistance = noBodyDistanceUpdate ? DistanceForConstraint(noBodySolvedPositions[2], noBodySolvedPositions[4]) : 0.0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(noBodyDistanceUpdate && fabs(noBodyDistance - baseDistance) <= baseDistance * 0.001 && DistancePoseConstraintsHold(noBodySolvedPositions) && currentState && currentState->colliders.Count() == 0 && currentState->distanceStepSeconds == 1.0 / 120.0, "distance-only update restores separation, bone lengths, and angle without body shapes");
        const auto* stateBeforeDistanceDebug = currentState;
        const double lambdaBeforeDistanceDebug = currentState && currentState->distanceLambdas.Count() == 1 ? currentState->distanceLambdas.At(0) : 0.0;
        const double stepBeforeDistanceDebug = currentState ? currentState->distanceStepSeconds : 0.0;
        const int32_t debugCountWithDistance = gk::GetModelSecondaryMotionDebugShapes(instance, nullptr, 0);
        gk::FModelSecondaryMotionDebugShape distanceDebugShapes[16]{};
        const int32_t copiedDistanceDebugShapes = debugCountWithDistance > 0 && debugCountWithDistance <= 16 ? gk::GetModelSecondaryMotionDebugShapes(instance, distanceDebugShapes, 16) : -1;
        const uint32_t distanceDebugBones[2] = { 2, 4 };
        gk::Vec3 queriedDistanceBones[2]{};
        const bool distanceDebugPoseRead = gk::GetModelBonePositions(instance, distanceDebugBones, 2, queriedDistanceBones) == 0;
        currentState = ReadSecondaryState(instance);
        const auto& distanceDebugLine = distanceDebugShapes[debugCountWithDistance > 0 && debugCountWithDistance <= 16 ? debugCountWithDistance - 1 : 0];
        const bool distanceDebugMatchesBones = copiedDistanceDebugShapes == debugCountWithDistance && distanceDebugPoseRead && !distanceDebugLine.body && !distanceDebugLine.fixed && !distanceDebugLine.virtualTip && distanceDebugLine.radius == 0.0f && distanceDebugLine.bone == 2 && NearVector(distanceDebugLine.start, queriedDistanceBones[0]) && NearVector(distanceDebugLine.end, queriedDistanceBones[1]);
        ReportLinkStage(debugCountBeforeDistance >= 0 && debugCountWithDistance == debugCountBeforeDistance + 1 && distanceDebugMatchesBones && currentState && currentState == stateBeforeDistanceDebug && currentState->distanceLambdas.Count() == 1 && currentState->distanceLambdas.At(0) == lambdaBeforeDistanceDebug && currentState->distanceStepSeconds == stepBeforeDistanceDebug, "distance debug query adds a zero-radius endpoint line without mutating solved state");

        gk::FModelSecondaryMotionCollider distantCollider{};
        distantCollider.bone = 0;
        distantCollider.start = { 100.0f, 100.0f, 100.0f };
        distantCollider.end = distantCollider.start;
        distantCollider.radius = 0.25f;
        const bool distantBodySet = gk::SetModelSecondaryMotionColliders(instance, &distantCollider, 1, 0.0f) == 0;
        currentState = ReadSecondaryState(instance);
        const bool bodyKeepsDistance = currentState && currentState->colliders.Count() == 1 && currentState->distanceConstraints.Count() == 1 && fabs(currentState->distanceConstraints.At(0).restDistance - baseDistance) < 1.0e-6 && currentState->linkIterations == 16;
        bool distanceUpdateReady = distantBodySet && bodyKeepsDistance && gk::ResetModelSecondaryMotion(instance) == 0;
        currentState = ReadSecondaryState(instance);
        if (distanceUpdateReady && currentState && currentState->chains.Count() == 2)
        {
            currentState->chains.At(0)->simulation.points.At(1).position.x += 0.5f;
            currentState->chains.At(1)->simulation.points.At(1).position.x -= 0.5f;
        }
        else
        {
            distanceUpdateReady = false;
        }
        gk::Vec3 distanceSolvedPositions[7]{};
        const bool distanceUpdate = distanceUpdateReady && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && gk::GetModelBonePositions(instance, distanceBones, 7, distanceSolvedPositions) == 0;
        const double solvedDistance = distanceUpdate ? DistanceForConstraint(distanceSolvedPositions[2], distanceSolvedPositions[4]) : 0.0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(distanceUpdate && fabs(solvedDistance - baseDistance) <= baseDistance * 0.001 && DistancePoseConstraintsHold(distanceSolvedPositions) && currentState && currentState->distanceConstraints.Count() == 1 && currentState->distanceLambdas.Count() == 1 && currentState->distanceStepSeconds == 1.0 / 120.0 && currentState->linkIterations == 16, "distance constraints restore separation and chain constraints with a body configured");

        bool positiveDistanceReady = gk::ResetModelSecondaryMotion(instance) == 0;
        currentState = ReadSecondaryState(instance);
        if (positiveDistanceReady && currentState && currentState->chains.Count() == 2)
        {
            currentState->chains.At(0)->simulation.points.At(1).position.x += 0.25f;
            currentState->chains.At(1)->simulation.points.At(1).position.x -= 0.25f;
        }
        else
        {
            positiveDistanceReady = false;
        }
        gk::Vec3 positiveSolvedPositions[7]{};
        const bool positiveDistanceUpdate = positiveDistanceReady && gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == 0 && gk::GetModelBonePositions(instance, distanceBones, 7, positiveSolvedPositions) == 0;
        const double positiveSolvedDistance = positiveDistanceUpdate ? DistanceForConstraint(positiveSolvedPositions[2], positiveSolvedPositions[4]) : 0.0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(positiveDistanceUpdate && fabs(positiveSolvedDistance - baseDistance) <= baseDistance * 0.001 && DistancePoseConstraintsHold(positiveSolvedPositions) && currentState && fabs(currentState->distanceStepSeconds - 1.0 / 60.0) <= 1.0e-12, "positive-delta distance update stores its physical step and keeps both chain constraints");

        const gk::FModelSecondaryMotionDistanceConstraint softDistance = { 2, 4, static_cast<float>(baseDistance), 0.02f };
        bool softDistanceReady = gk::SetModelSecondaryMotionDistanceConstraints(instance, &softDistance, 1, 32) == 0 && gk::ResetModelSecondaryMotion(instance) == 0;
        currentState = ReadSecondaryState(instance);
        if (softDistanceReady && currentState && currentState->chains.Count() == 2)
        {
            currentState->chains.At(0)->simulation.points.At(1).position.y += 0.05f;
            currentState->chains.At(1)->simulation.points.At(1).position.y -= 0.05f;
        }
        else
        {
            softDistanceReady = false;
        }
        const uint32_t softDistanceBones[2] = { 2, 4 };
        gk::Vec3 softSolvedBones[2]{};
        const bool softDistanceUpdate = softDistanceReady && gk::UpdateModelSecondaryMotion(instance, 1.0e-5) == 0 && gk::GetModelBonePositions(instance, softDistanceBones, 2, softSolvedBones) == 0;
        const double softSolvedDistance = softDistanceUpdate ? DistanceForConstraint(softSolvedBones[0], softSolvedBones[1]) : 0.0;
        currentState = ReadSecondaryState(instance);
        const double softStep = currentState ? currentState->distanceStepSeconds : 0.0;
        const double softLambda = currentState && currentState->distanceLambdas.Count() == 1 ? currentState->distanceLambdas.At(0) : 0.0;
        const double softAlpha = softStep > 0.0 ? static_cast<double>(softDistance.compliance) / (softStep * softStep) : 0.0;
        const double softResidual = softSolvedDistance - softDistance.restDistance - softAlpha * softLambda;
        ReportLinkStage(softDistanceUpdate && currentState && currentState->distanceConstraints.Count() == 1 && currentState->distanceConstraints.At(0).compliance == softDistance.compliance && fabs(softStep - 1.0e-5) <= 1.0e-15 && isfinite(softLambda) && isfinite(softResidual) && fabs(softResidual) <= static_cast<double>(softDistance.restDistance) * 0.001 && fabs(softSolvedDistance - softDistance.restDistance) > static_cast<double>(softDistance.restDistance) * 0.001, "small positive step is retained and the compliant result satisfies its lambda-weighted residual");
        const bool restoredRigidDistance = gk::SetModelSecondaryMotionDistanceConstraints(instance, &distanceConstraint, 1, 32) == 0 && gk::ResetModelSecondaryMotion(instance) == 0 && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && gk::GetModelBonePositions(instance, distanceBones, 7, positiveSolvedPositions) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(restoredRigidDistance && currentState && currentState->distanceConstraints.Count() == 1 && currentState->distanceConstraints.At(0).compliance == 0.0f && fabs(DistanceForConstraint(positiveSolvedPositions[2], positiveSolvedPositions[4]) - baseDistance) <= baseDistance * 0.001, "reset and next update restore the original rigid distance setting");

        const gk::FModelSecondaryMotionDistanceConstraint impossibleDistance = { 2, 4, 10.0f, 0.0f };
        const bool impossibleDistanceSet = gk::SetModelSecondaryMotionDistanceConstraints(instance, &impossibleDistance, 1, 32) == 0;
        currentState = ReadSecondaryState(instance);
        std::vector<gk::model::animation::FModelSecondaryMotionPoint> firstDistancePoints;
        std::vector<gk::model::animation::FModelSecondaryMotionPoint> secondDistancePoints;
        if (currentState && currentState->chains.Count() == 2)
        {
            firstDistancePoints.assign(currentState->chains.At(0)->simulation.points.Data(), currentState->chains.At(0)->simulation.points.Data() + currentState->chains.At(0)->simulation.points.Count());
            secondDistancePoints.assign(currentState->chains.At(1)->simulation.points.Data(), currentState->chains.At(1)->simulation.points.Data() + currentState->chains.At(1)->simulation.points.Count());
        }
        gk::Vec3 beforeImpossibleDistancePose[7]{};
        const bool impossibleSnapshotReady = impossibleDistanceSet && currentState && currentState->distanceConstraints.Count() == 1 && currentState->distanceConstraints.At(0).restDistance == 10.0f && currentState->distanceLambdas.Count() == 1 && currentState->chains.Count() == 2 && !firstDistancePoints.empty() && !secondDistancePoints.empty() && gk::GetModelBonePositions(instance, distanceBones, 7, beforeImpossibleDistancePose) == 0;
        const double lambdaBeforeImpossibleDistance = impossibleSnapshotReady ? currentState->distanceLambdas.At(0) : 0.0;
        const double stepBeforeImpossibleDistance = impossibleSnapshotReady ? currentState->distanceStepSeconds : 0.0;
        const int impossibleDistanceResult = impossibleSnapshotReady ? gk::UpdateModelSecondaryMotion(instance, 0.0) : 0;
        const auto* afterImpossibleDistance = ReadSecondaryState(instance);
        gk::Vec3 afterImpossibleDistancePose[7]{};
        bool impossibleDistanceAtomic = impossibleSnapshotReady && impossibleDistanceResult == -1 && gk::GetLastErrorMessage()[0] != '\0' && afterImpossibleDistance == currentState && afterImpossibleDistance->distanceConstraints.Count() == 1 && afterImpossibleDistance->distanceConstraints.At(0).restDistance == 10.0f && afterImpossibleDistance->distanceLambdas.Count() == 1 && afterImpossibleDistance->distanceLambdas.At(0) == lambdaBeforeImpossibleDistance && afterImpossibleDistance->distanceStepSeconds == stepBeforeImpossibleDistance && afterImpossibleDistance->chains.Count() == 2 && afterImpossibleDistance->chains.At(0)->simulation.points.Count() == firstDistancePoints.size() && afterImpossibleDistance->chains.At(1)->simulation.points.Count() == secondDistancePoints.size();
        if (impossibleDistanceAtomic)
        {
            impossibleDistanceAtomic = memcmp(afterImpossibleDistance->chains.At(0)->simulation.points.Data(), firstDistancePoints.data(), firstDistancePoints.size() * sizeof(firstDistancePoints[0])) == 0 && memcmp(afterImpossibleDistance->chains.At(1)->simulation.points.Data(), secondDistancePoints.data(), secondDistancePoints.size() * sizeof(secondDistancePoints[0])) == 0 && gk::GetModelBonePositions(instance, distanceBones, 7, afterImpossibleDistancePose) == 0 && memcmp(beforeImpossibleDistancePose, afterImpossibleDistancePose, sizeof(beforeImpossibleDistancePose)) == 0;
        }
        ReportLinkStage(impossibleDistanceAtomic, "an infeasible rigid distance update reports failure without publishing candidate points, lambda, or FK pose");
        const bool restoreFeasibleDistance = gk::SetModelSecondaryMotionDistanceConstraints(instance, &distanceConstraint, 1, 32) == 0;
        ReportLinkStage(restoreFeasibleDistance, "restore a feasible distance after atomic failure");

        const bool clearDistanceOnly = gk::SetModelSecondaryMotionDistanceConstraints(instance, nullptr, 0) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(clearDistanceOnly && currentState && currentState->distanceConstraints.Count() == 0 && currentState->distanceLambdas.Count() == 0 && currentState->chains.Count() == 2 && currentState->colliders.Count() == 1, "zero count clears distance constraints while retaining chains and body shapes");
        const bool clearDistanceBody = gk::SetModelSecondaryMotionColliders(instance, nullptr, 0) == 0;
        ReportLinkStage(clearDistanceBody, "clear the temporary distant body before link-contact integration");

        gk::FModelSecondaryMotionCollider linkCollider{};
        linkCollider.bone = 0;
        linkCollider.start = { 1.5f, 0.5f, 0.0f };
        linkCollider.end = linkCollider.start;
        linkCollider.radius = 0.15f;
        const gk::FModelSecondaryMotionLink contactLink = { 2, 4, 0.02f };
        const gk::FModelSecondaryMotionDistanceConstraint contactDistance = { 2, 4, static_cast<float>(baseDistance), 0.0f };
        bool contactReady = gk::SetModelSecondaryMotionColliders(instance, &linkCollider, 1, 0.005f) == 0 && gk::SetModelSecondaryMotionLinks(instance, &contactLink, 1, 31) == 0 && gk::SetModelSecondaryMotionDistanceConstraints(instance, &contactDistance, 1, 32) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(contactReady && currentState && currentState->linkIterations == 32 && currentState->links.Count() == 1 && currentState->distanceConstraints.Count() == 1, "the last distance setter controls the shared link iteration budget");
        gk::Vec3 contactReference[7]{};
        const uint32_t allBones[7] = { 0, 1, 2, 3, 4, 5, 6 };
        contactReady = gk::GetModelBonePositions(instance, allBones, 7, contactReference) == 0 && contactReady;
        const auto Distance3 = [](gk::Vec3 first, gk::Vec3 second)
        {
            const double x = static_cast<double>(first.x) - second.x;
            const double y = static_cast<double>(first.y) - second.y;
            const double z = static_cast<double>(first.z) - second.z;
            return sqrt(x * x + y * y + z * z);
        };
        const auto Dot3 = [](gk::Vec3 first, gk::Vec3 second) { return static_cast<double>(first.x) * second.x + static_cast<double>(first.y) * second.y + static_cast<double>(first.z) * second.z; };
        const auto LinkAndChainConstraintsHold = [&](const gk::Vec3* positions)
        {
            auto* state = ReadSecondaryState(instance);
            if (!state || state->links.Count() != 1 || state->previousCollisionShapes.Count() != 1)
            {
                return false;
            }
            const gk::Vec3 bodyCenter = state->previousCollisionShapes.At(0).start;
            const gk::Vec3 linkFirst = positions[2];
            const gk::Vec3 linkSecond = positions[4];
            const gk::Vec3 linkAxis{ linkSecond.x - linkFirst.x, linkSecond.y - linkFirst.y, linkSecond.z - linkFirst.z };
            const gk::Vec3 centerOffset{ bodyCenter.x - linkFirst.x, bodyCenter.y - linkFirst.y, bodyCenter.z - linkFirst.z };
            const double axisLengthSquared = Dot3(linkAxis, linkAxis);
            const double amount = axisLengthSquared > 0.0 ? fmax(0.0, fmin(1.0, Dot3(centerOffset, linkAxis) / axisLengthSquared)) : 0.0;
            const gk::Vec3 closest{ linkFirst.x + static_cast<float>(amount) * linkAxis.x, linkFirst.y + static_cast<float>(amount) * linkAxis.y, linkFirst.z + static_cast<float>(amount) * linkAxis.z };
            const double bodyDistance = Distance3(bodyCenter, closest);
            const double requiredDistance = static_cast<double>(state->previousCollisionShapes.At(0).radius) + state->links.At(0).radius;
            // 候補の接触誤差は許容幅の半分以下に収め、残りをFKの丸めに残す。
            const auto& simulationFirst = state->chains.At(0)->simulation.points.At(1).position;
            const auto& simulationSecond = state->chains.At(1)->simulation.points.At(1).position;
            const double ax = static_cast<double>(simulationSecond.x) - simulationFirst.x, ay = static_cast<double>(simulationSecond.y) - simulationFirst.y, az = static_cast<double>(simulationSecond.z) - simulationFirst.z;
            const double ox = static_cast<double>(bodyCenter.x) - simulationFirst.x, oy = static_cast<double>(bodyCenter.y) - simulationFirst.y, oz = static_cast<double>(bodyCenter.z) - simulationFirst.z;
            const double simulationDenominator = ax * ax + ay * ay + az * az;
            const double simulationAmount = simulationDenominator > 0.0 ? fmax(0.0, fmin(1.0, (ox * ax + oy * ay + oz * az) / simulationDenominator)) : 0.0;
            const double dx = ox - simulationAmount * ax;
            const double dy = oy - simulationAmount * ay;
            const double dz = oz - simulationAmount * az;
            if (sqrt(dx * dx + dy * dy + dz * dz) < requiredDistance * (1.0 - 0.0005))
            {
                fprintf(stderr, "candidate link entered the numerical tolerance band: distance %.17g radius %.17g\n", sqrt(dx * dx + dy * dy + dz * dz), requiredDistance);
                return false;
            }
            if (bodyDistance + 0.001 < requiredDistance || !NearVector(positions[1], contactReference[1]) || !NearVector(positions[3], contactReference[3]))
            {
                return false;
            }
            for (uint32_t chainIndex = 0; chainIndex < 2; ++chainIndex)
            {
                const uint32_t rootBone = chainIndex == 0 ? 1u : 3u;
                const uint32_t freeBone = chainIndex == 0 ? 2u : 4u;
                const gk::Vec3 targetAxis{ contactReference[freeBone].x - contactReference[rootBone].x, contactReference[freeBone].y - contactReference[rootBone].y, contactReference[freeBone].z - contactReference[rootBone].z };
                const gk::Vec3 solvedAxis{ positions[freeBone].x - positions[rootBone].x, positions[freeBone].y - positions[rootBone].y, positions[freeBone].z - positions[rootBone].z };
                const double targetLength = Distance3(contactReference[rootBone], contactReference[freeBone]);
                const double solvedLength = Distance3(positions[rootBone], positions[freeBone]);
                if (!(solvedLength > 0.0) || fabs(solvedLength - targetLength) > targetLength * 0.001 || Dot3(solvedAxis, targetAxis) / (solvedLength * targetLength) < cos(settings.maxAngleDegrees * 3.14159265358979323846 / 180.0) - 0.002)
                {
                    return false;
                }
            }
            for (uint32_t chainIndex = 0; chainIndex < state->chains.Count(); ++chainIndex)
            {
                const auto* chain = state->chains.At(chainIndex);
                if (!chain)
                {
                    return false;
                }
                for (uint32_t pointIndex = 0; pointIndex < chain->simulation.points.Count(); ++pointIndex)
                {
                    const auto& point = chain->simulation.points.At(pointIndex);
                    if (!isfinite(point.position.x) || !isfinite(point.position.y) || !isfinite(point.position.z) || !isfinite(point.velocity.x) || !isfinite(point.velocity.y) || !isfinite(point.velocity.z))
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        gk::Vec3 solvedPositions[7]{};
        for (uint32_t iteration = 0; iteration < 3 && contactReady; ++iteration)
        {
            gk::model::ResetSecondaryMotionLinkPreparationCounts();
            const bool updateSucceeded = gk::UpdateModelSecondaryMotion(instance, 0.0) == 0 && gk::GetModelBonePositions(instance, allBones, 7, solvedPositions) == 0 && LinkAndChainConstraintsHold(solvedPositions);
            currentState = ReadSecondaryState(instance);
            contactReady = updateSucceeded && fabs(Distance3(solvedPositions[2], solvedPositions[4]) - baseDistance) <= 0.001 && currentState && currentState->distanceConstraints.Count() == 1 && currentState->distanceLambdas.Count() == 1;
            ReportLinkStage(gk::model::GetSecondaryMotionLinkResolutionCount() == 1 && gk::model::GetSecondaryMotionLinkShapePreparationCount() == 1, "a link resolves once and prepares one body shape across all constraint passes");
        }
        ReportLinkStage(contactReady, "body sphere pushes the free-bone link clear while preserving roots, lengths, angles, and finite velocity");
        const bool clearedCoupledDistance = gk::SetModelSecondaryMotionDistanceConstraints(instance, nullptr, 0) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(clearedCoupledDistance && currentState && currentState->distanceConstraints.Count() == 0 && currentState->links.Count() == 1 && currentState->colliders.Count() == 1, "clear the temporary distance setting after coupled body-link integration");

        struct FFailedLinkUpdateSnapshot
        {
            struct FChain
            {
                std::vector<uint32_t> bones;
                std::vector<gk::model::animation::FModelSecondaryMotionPoint> points;
                std::vector<gk::Vec3> previousTargets;
                std::vector<gk::model::animation::FModelBoneTransform> transforms;
                gk::FModelSecondaryMotionSettings settings{};
                float collisionRadius = 0.0f;
            };
            std::vector<FChain> chains;
            std::vector<gk::model::animation::FModelSecondaryMotionCollisionShape> previousShapes;
            std::vector<gk::FModelSecondaryMotionLink> links;
            std::vector<gk::FModelSecondaryMotionCollider> colliders;
            std::vector<gk::model::animation::FModelBoneTransform> cachedTransforms;
            const gk::model::FModelSecondaryMotionState* state = nullptr;
            gk::model::FModelPoseCache* cache = nullptr;
            uint64_t poseRevision = 0;
            uint64_t cacheRevision = 0;
            float colliderMargin = 0.0f;
        } failedUpdateSnapshot;
        const gk::FModelSecondaryMotionLink infeasibleLink = { 2, 4, 10.0f };
        bool failedUpdateSnapshotReady = gk::SetModelSecondaryMotionLinks(instance, &infeasibleLink, 1, 12) == 0;
        auto* failedUpdateTransform = gk::detail::FindModelTransform(instance);
        auto* failedUpdatePlayback = failedUpdateTransform ? failedUpdateTransform->playback : nullptr;
        const auto* stateBeforeFailedLinkUpdate = ReadSecondaryState(instance);
        failedUpdateSnapshotReady = failedUpdateSnapshotReady && failedUpdatePlayback && stateBeforeFailedLinkUpdate && stateBeforeFailedLinkUpdate->chains.Count() == 2 && stateBeforeFailedLinkUpdate->links.Count() == 1 && failedUpdatePlayback->basePoseCache;
        if (failedUpdateSnapshotReady)
        {
            failedUpdateSnapshot.state = stateBeforeFailedLinkUpdate;
            failedUpdateSnapshot.cache = failedUpdatePlayback->basePoseCache;
            failedUpdateSnapshot.poseRevision = failedUpdatePlayback->poseRevision;
            failedUpdateSnapshot.cacheRevision = failedUpdatePlayback->basePoseCache->revision;
            failedUpdateSnapshot.colliderMargin = stateBeforeFailedLinkUpdate->colliderMargin;
            failedUpdateSnapshot.chains.reserve(stateBeforeFailedLinkUpdate->chains.Count());
            for (uint32_t chainIndex = 0; chainIndex < stateBeforeFailedLinkUpdate->chains.Count(); ++chainIndex)
            {
                const auto* chain = stateBeforeFailedLinkUpdate->chains.At(chainIndex);
                if (!chain)
                {
                    failedUpdateSnapshotReady = false;
                    break;
                }
                FFailedLinkUpdateSnapshot::FChain copy{};
                if (chain->bones.Count() > 0)
                {
                    copy.bones.assign(chain->bones.Data(), chain->bones.Data() + chain->bones.Count());
                }
                if (chain->simulation.points.Count() > 0)
                {
                    copy.points.assign(chain->simulation.points.Data(), chain->simulation.points.Data() + chain->simulation.points.Count());
                }
                if (chain->simulation.previousTargets.Count() > 0)
                {
                    copy.previousTargets.assign(chain->simulation.previousTargets.Data(), chain->simulation.previousTargets.Data() + chain->simulation.previousTargets.Count());
                }
                if (chain->transforms.Count() > 0)
                {
                    copy.transforms.assign(chain->transforms.Data(), chain->transforms.Data() + chain->transforms.Count());
                }
                copy.settings = chain->settings;
                copy.collisionRadius = chain->collisionRadius;
                failedUpdateSnapshot.chains.push_back(copy);
            }
            if (stateBeforeFailedLinkUpdate->previousCollisionShapes.Count() > 0)
            {
                failedUpdateSnapshot.previousShapes.assign(stateBeforeFailedLinkUpdate->previousCollisionShapes.Data(), stateBeforeFailedLinkUpdate->previousCollisionShapes.Data() + stateBeforeFailedLinkUpdate->previousCollisionShapes.Count());
            }
            if (stateBeforeFailedLinkUpdate->links.Count() > 0)
            {
                failedUpdateSnapshot.links.assign(stateBeforeFailedLinkUpdate->links.Data(), stateBeforeFailedLinkUpdate->links.Data() + stateBeforeFailedLinkUpdate->links.Count());
            }
            if (stateBeforeFailedLinkUpdate->colliders.Count() > 0)
            {
                failedUpdateSnapshot.colliders.assign(stateBeforeFailedLinkUpdate->colliders.Data(), stateBeforeFailedLinkUpdate->colliders.Data() + stateBeforeFailedLinkUpdate->colliders.Count());
            }
            if (failedUpdatePlayback->basePoseCache->pose.localTransforms.Count() > 0)
            {
                failedUpdateSnapshot.cachedTransforms.assign(failedUpdatePlayback->basePoseCache->pose.localTransforms.Data(), failedUpdatePlayback->basePoseCache->pose.localTransforms.Data() + failedUpdatePlayback->basePoseCache->pose.localTransforms.Count());
            }
        }
        gk::Vec3 fkBeforeFailedLinkUpdate[7]{};
        failedUpdateSnapshotReady = failedUpdateSnapshotReady && gk::GetModelBonePositions(instance, allBones, 7, fkBeforeFailedLinkUpdate) == 0;
        const int infeasibleUpdateResult = failedUpdateSnapshotReady ? gk::UpdateModelSecondaryMotion(instance, 0.0) : 0;
        const auto* stateAfterFailedLinkUpdate = ReadSecondaryState(instance);
        auto* transformAfterFailedLinkUpdate = gk::detail::FindModelTransform(instance);
        auto* playbackAfterFailedLinkUpdate = transformAfterFailedLinkUpdate ? transformAfterFailedLinkUpdate->playback : nullptr;
        bool failedUpdateValuesPreserved = failedUpdateSnapshotReady && infeasibleUpdateResult == -1 && gk::GetLastErrorMessage()[0] != '\0' && stateAfterFailedLinkUpdate == failedUpdateSnapshot.state && playbackAfterFailedLinkUpdate && playbackAfterFailedLinkUpdate->basePoseCache == failedUpdateSnapshot.cache && playbackAfterFailedLinkUpdate->poseRevision == failedUpdateSnapshot.poseRevision && failedUpdateSnapshot.cache->revision == failedUpdateSnapshot.cacheRevision && stateAfterFailedLinkUpdate->colliderMargin == failedUpdateSnapshot.colliderMargin && stateAfterFailedLinkUpdate->chains.Count() == failedUpdateSnapshot.chains.size() && stateAfterFailedLinkUpdate->previousCollisionShapes.Count() == failedUpdateSnapshot.previousShapes.size() && stateAfterFailedLinkUpdate->links.Count() == failedUpdateSnapshot.links.size() && stateAfterFailedLinkUpdate->colliders.Count() == failedUpdateSnapshot.colliders.size();
        for (uint32_t chainIndex = 0; chainIndex < stateAfterFailedLinkUpdate->chains.Count() && failedUpdateValuesPreserved; ++chainIndex)
        {
            const auto* chain = stateAfterFailedLinkUpdate->chains.At(chainIndex);
            const auto& saved = failedUpdateSnapshot.chains[chainIndex];
            failedUpdateValuesPreserved = chain && chain->bones.Count() == saved.bones.size() && chain->simulation.points.Count() == saved.points.size() && chain->simulation.previousTargets.Count() == saved.previousTargets.size() && chain->transforms.Count() == saved.transforms.size() && chain->collisionRadius == saved.collisionRadius && memcmp(&chain->settings, &saved.settings, sizeof(saved.settings)) == 0 && memcmp(chain->bones.Data(), saved.bones.data(), saved.bones.size() * sizeof(uint32_t)) == 0 && memcmp(chain->simulation.points.Data(), saved.points.data(), saved.points.size() * sizeof(saved.points[0])) == 0 && memcmp(chain->simulation.previousTargets.Data(), saved.previousTargets.data(), saved.previousTargets.size() * sizeof(saved.previousTargets[0])) == 0 && memcmp(chain->transforms.Data(), saved.transforms.data(), saved.transforms.size() * sizeof(saved.transforms[0])) == 0;
        }
        if (failedUpdateValuesPreserved)
        {
            failedUpdateValuesPreserved = memcmp(stateAfterFailedLinkUpdate->previousCollisionShapes.Data(), failedUpdateSnapshot.previousShapes.data(), failedUpdateSnapshot.previousShapes.size() * sizeof(failedUpdateSnapshot.previousShapes[0])) == 0 && memcmp(stateAfterFailedLinkUpdate->links.Data(), failedUpdateSnapshot.links.data(), failedUpdateSnapshot.links.size() * sizeof(failedUpdateSnapshot.links[0])) == 0 && memcmp(stateAfterFailedLinkUpdate->colliders.Data(), failedUpdateSnapshot.colliders.data(), failedUpdateSnapshot.colliders.size() * sizeof(failedUpdateSnapshot.colliders[0])) == 0 && memcmp(failedUpdateSnapshot.cache->pose.localTransforms.Data(), failedUpdateSnapshot.cachedTransforms.data(), failedUpdateSnapshot.cachedTransforms.size() * sizeof(failedUpdateSnapshot.cachedTransforms[0])) == 0;
        }
        gk::Vec3 fkAfterFailedLinkUpdate[7]{};
        failedUpdateValuesPreserved = failedUpdateValuesPreserved && gk::GetModelBonePositions(instance, allBones, 7, fkAfterFailedLinkUpdate) == 0 && memcmp(fkBeforeFailedLinkUpdate, fkAfterFailedLinkUpdate, sizeof(fkBeforeFailedLinkUpdate)) == 0;
        ReportLinkStage(failedUpdateValuesPreserved, "infeasible finite link update reports failure and preserves state, chains, pose cache, and FK pose atomically");
        const bool restoredContactLinkAfterFailure = gk::SetModelSecondaryMotionLinks(instance, &contactLink, 1, 32) == 0 && gk::ResetModelSecondaryMotion(instance) == 0;
        ReportLinkStage(restoredContactLinkAfterFailure, "feasible link settings can be restored after an atomic failed update");

        const bool retainedLinkIterations = gk::SetModelSecondaryMotionLinks(instance, &contactLink, 1, 32) == 0 && gk::ResetModelSecondaryMotion(instance) == 0;
        const bool positiveDeltaUpdate = retainedLinkIterations && gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == 0 && gk::GetModelBonePositions(instance, allBones, 7, solvedPositions) == 0 && LinkAndChainConstraintsHold(solvedPositions);
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(positiveDeltaUpdate && currentState && currentState->links.Count() == 1 && currentState->links.At(0).radius == contactLink.radius && currentState->linkIterations == 32, "positive-delta link update preserves the contact-solving budget and resolves FK span contact");

        const auto AllSimulationVelocitiesZero = [&](const gk::model::FModelSecondaryMotionState* state)
        {
            if (!state || state->links.Count() != 1 || state->linkIterations != 32 || state->chains.Count() != 2)
            {
                return false;
            }
            for (uint32_t chainIndex = 0; chainIndex < state->chains.Count(); ++chainIndex)
            {
                const auto* chain = state->chains.At(chainIndex);
                if (!chain)
                {
                    return false;
                }
                for (uint32_t pointIndex = 0; pointIndex < chain->simulation.points.Count(); ++pointIndex)
                {
                    const auto& point = chain->simulation.points.At(pointIndex);
                    if (!isfinite(point.position.x) || !isfinite(point.position.y) || !isfinite(point.position.z) || !isfinite(point.velocity.x) || !isfinite(point.velocity.y) || !isfinite(point.velocity.z) || fabsf(point.velocity.x) > 1.0e-6f || fabsf(point.velocity.y) > 1.0e-6f || fabsf(point.velocity.z) > 1.0e-6f)
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        const bool hitchUpdate = positiveDeltaUpdate && gk::UpdateModelSecondaryMotion(instance, 0.3) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(hitchUpdate && AllSimulationVelocitiesZero(currentState), "hitch reset zeros every link-connected simulation velocity");

        const bool teleportBaseReady = gk::PlayModelAnimation(instance, 0, false) == 0 && gk::UpdateModelAnimation(instance, 0.0) == 0 && gk::ResetModelSecondaryMotion(instance) == 0 && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0;
        gk::Vec3 teleportBasePosition{};
        const bool readTeleportBase = teleportBaseReady && gk::GetModelBonePosition(instance, 5, teleportBasePosition) == 0;
        const bool teleportAnimationReady = readTeleportBase && gk::PlayModelAnimation(instance, 1, false) == 0 && gk::UpdateModelAnimation(instance, 0.0) == 0;
        gk::Vec3 teleportPosePosition{};
        const bool readTeleportPose = teleportAnimationReady && gk::GetModelBonePosition(instance, 5, teleportPosePosition) == 0;
        const double teleportDistance = readTeleportPose ? Distance3(teleportBasePosition, teleportPosePosition) : 0.0;
        const bool teleportUpdate = teleportDistance > settings.teleportDistance && gk::UpdateModelSecondaryMotion(instance, 0.0) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(teleportUpdate && AllSimulationVelocitiesZero(currentState), "animated root teleport beyond threshold zeros connected simulation velocities");
        const bool restoreBaseClip = gk::PlayModelAnimation(instance, 0, false) == 0 && gk::UpdateModelAnimation(instance, 0.0) == 0;

        const bool contactCleared = gk::SetModelSecondaryMotionLinks(instance, nullptr, 0) == 0 && gk::SetModelSecondaryMotionColliders(instance, nullptr, 0) == 0 && gk::ResetModelSecondaryMotion(instance) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(restoreBaseClip && contactCleared && currentState && currentState->links.Count() == 0 && currentState->colliders.Count() == 0 && currentState->chains.Count() == 2, "body-link contact fixture clears temporary link and body settings after restoring base clip");
        const bool restoredMainLink = gk::SetModelSecondaryMotionLinks(instance, &validLink, 1, 12) == 0;
        ReportLinkStage(restoredMainLink, "restore the link lifecycle settings after body integration");

        const auto linkInstance = gk::CreateModelInstance(model);
        const gk::FModelSecondaryMotionLink independentLink = { 2, 4, 0.35f };
        bool instanceIndependent = linkInstance.IsValid() && gk::SetModelSecondaryMotionChain(linkInstance, firstChain, 2, settings) == 0 && gk::SetModelSecondaryMotionChain(linkInstance, secondChain, 2, settings) == 0;
        auto* independentState = linkInstance.IsValid() ? ReadSecondaryState(linkInstance) : nullptr;
        instanceIndependent = instanceIndependent && independentState && independentState->links.Count() == 0;
        instanceIndependent = instanceIndependent && gk::SetModelSecondaryMotionLinks(linkInstance, &independentLink, 1, 5) == 0;
        independentState = linkInstance.IsValid() ? ReadSecondaryState(linkInstance) : nullptr;
        currentState = ReadSecondaryState(instance);
        instanceIndependent = instanceIndependent && independentState && independentState->links.Count() == 1 && independentState->links.At(0).radius == 0.35f && independentState->linkIterations == 5 && currentState && currentState->links.Count() == 1 && currentState->links.At(0).radius == 0.125f && currentState->linkIterations == 12;
        if (linkInstance.IsValid())
        {
            instanceIndependent = gk::DeleteModel(linkInstance) == 0 && instanceIndependent;
        }
        ReportLinkStage(instanceIndependent, "link settings belong to their model instance");

        const bool clearedLinks = gk::SetModelSecondaryMotionLinks(instance, nullptr, 0) == 0;
        currentState = ReadSecondaryState(instance);
        ReportLinkStage(clearedLinks && currentState && currentState->chains.Count() == 2 && currentState->links.Count() == 0, "zero count clears links while retaining both chains");
        const bool republishedLinks = gk::SetModelSecondaryMotionLinks(instance, &independentLink, 1, 7) == 0;
        const bool clearedMotion = gk::ClearModelSecondaryMotion(instance) == 0;
        ReportLinkStage(republishedLinks && clearedMotion && ReadSecondaryState(instance) == nullptr, "clearing secondary motion releases chains and links together");
        const bool restoredChains = gk::SetModelSecondaryMotionChain(instance, firstChain, 2, settings) == 0 && gk::SetModelSecondaryMotionChain(instance, secondChain, 2, settings) == 0 && gk::SetModelSecondaryMotionChainCollisionRadius(instance, firstChain[0], 0.05f) == 0 && gk::SetModelSecondaryMotionChainCollisionRadius(instance, secondChain[0], 0.15f) == 0;
        ReportLinkStage(restoredChains, "restore the shared pose-budget chains after lifecycle checks");
    }
    if (passed)
    {
        namespace fs = std::filesystem;
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto configDirectory = fs::temp_directory_path() / ("gkcore-secondary-motion-radius-" + std::to_string(suffix));
        fs::create_directories(configDirectory);
        const auto configPath = configDirectory / "motion.txt";
        const std::string configUtf8Path = configPath.string();
        const auto WriteConfig = [&](const char* contents)
        {
            std::ofstream file(configPath, std::ios::binary | std::ios::trunc);
            file << contents;
            return file.good();
        };
        const auto ReadFirstChain = [&]()
        {
            auto* currentTransform = gk::detail::FindModelTransform(instance);
            auto* currentState = currentTransform && currentTransform->playback ? currentTransform->playback->secondaryMotion : nullptr;
            if (!currentState || currentState->chains.Count() != 2)
            {
                return static_cast<gk::model::FModelSecondaryMotionChain*>(nullptr);
            }
            for (uint32_t chainIndex = 0; chainIndex < currentState->chains.Count(); ++chainIndex)
            {
                if (currentState->chains.At(chainIndex)->bones.Count() > 0 && currentState->chains.At(chainIndex)->bones.At(0) == firstChain[0])
                {
                    return currentState->chains.At(chainIndex);
                }
            }
            return static_cast<gk::model::FModelSecondaryMotionChain*>(nullptr);
        };
        bool configPassed = true;
        const auto CheckRadiusConfigStage = [&](bool result, const char* stage)
        {
            if (!result)
            {
                auto* current = ReadFirstChain();
                fprintf(stderr, "secondary-motion radius config failed at %s (chain=%p radius=%.9g frequency=%.9g iterations=%u): %s\n", stage, static_cast<void*>(current), current ? current->collisionRadius : -1.0f, current ? current->settings.frequencyHz : -1.0f, current ? current->settings.constraintIterations : 0u, gk::GetLastErrorMessage());
            }
            configPassed = result && configPassed;
            return result;
        };
        bool stagePassed = WriteConfig("4 0.8 60 0 0.25 0 0 0 0 8 0.008 | joint_b joint_c\n") && gk::examples::ApplySecondaryMotionPreview(instance, configUtf8Path.c_str());
        auto* configuredFirstChain = ReadFirstChain();
        CheckRadiusConfigStage(stagePassed && configuredFirstChain && configuredFirstChain->collisionRadius == 0.008f && configuredFirstChain->settings.frequencyHz == 4.0f && configuredFirstChain->settings.constraintIterations == 8, "11-number radius row");
        stagePassed = WriteConfig("4 0.8 60 0 0.25 0 0 0 0 | joint_b joint_c\n") && gk::examples::ApplySecondaryMotionPreview(instance, configUtf8Path.c_str());
        configuredFirstChain = ReadFirstChain();
        CheckRadiusConfigStage(stagePassed && configuredFirstChain && configuredFirstChain->collisionRadius == 0.008f && configuredFirstChain->settings.constraintIterations == 8, "legacy 9-number row preserves radius");
        stagePassed = WriteConfig("4 0.8 60 0 0.25 0 0 0 0 8 | joint_b joint_c\n") && gk::examples::ApplySecondaryMotionPreview(instance, configUtf8Path.c_str());
        configuredFirstChain = ReadFirstChain();
        CheckRadiusConfigStage(stagePassed && configuredFirstChain && configuredFirstChain->collisionRadius == 0.008f && configuredFirstChain->settings.constraintIterations == 8, "legacy 10-number row preserves radius");
        const char* invalidConfigs[5] = { "4 0.8 60 0 0.25 0 0 0 0 8 nan | joint_b joint_c\n", "4 0.8 60 0 0.25 0 0 0 0 8 -0.008 | joint_b joint_c\n", "4 0.8 60 0 0.25 0 0 0 0 8 0.008 1 | joint_b joint_c\n", "4 0.8 60 0 0.25 0 0 0 0 8 0.008 joint_b joint_c\n", "4 0.8 60 0 0.25 0 0 0 0 8.5 | joint_b joint_c\n" };
        for (uint32_t index = 0; index < 5; ++index)
        {
            stagePassed = WriteConfig(invalidConfigs[index]) && !gk::examples::ApplySecondaryMotionPreview(instance, configUtf8Path.c_str());
            configuredFirstChain = ReadFirstChain();
            const char* stage = index == 0 ? "NaN radius rejected atomically" : (index == 1 ? "negative radius rejected atomically" : (index == 2 ? "extra field rejected atomically" : (index == 3 ? "missing separator rejected atomically" : "fractional iterations rejected atomically")));
            CheckRadiusConfigStage(stagePassed && configuredFirstChain && configuredFirstChain->collisionRadius == 0.008f && configuredFirstChain->settings.frequencyHz == 4.0f && configuredFirstChain->settings.constraintIterations == 8, stage);
        }
        fs::remove_all(configDirectory);
        passed = configPassed && passed;
        if (!configPassed)
        {
            fprintf(stderr, "bone-radius config contract failed\n");
        }
    }
    if (passed)
    {
        gk::model::animation::ResetModelPoseWorkForTesting();
        passed = gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == 0 && gk::model::animation::GetModelPoseEvaluationCountForTesting() == 2u;
    }
    gk::FModelSecondaryMotionCollider collider{};
    collider.bone = 5;
    collider.radius = 0.1f;
    if (passed)
    {
        passed = gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.0f) == 0;
    }
    if (passed)
    {
        gk::model::animation::ResetModelPoseWorkForTesting();
        passed = gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == 0 && gk::model::animation::GetModelPoseEvaluationCountForTesting() == 3u;
    }
    if (passed)
    {
        const auto CheckCacheStage = [&](bool result, const char* stage)
        {
            if (!result)
            {
                fprintf(stderr, "secondary motion pose-cache contract failed at %s (sampleCount=%u): %s\n", stage, source->sampleCount, gk::GetLastErrorMessage());
            }
            passed = result && passed;
            return result;
        };

        // 更新済みのclip姿勢をcacheへ保存し、描画と照会が再利用できることを調べる。
        const bool cachePlaybackReady = gk::PlayModelAnimation(instance, 0, false) == 0 && gk::SetModelAnimationBlend(instance, 1, 0.5f) == 0;
        CheckCacheStage(cachePlaybackReady, "cache warmup setup");
        source->sampleCount = 0;
        const bool cachePublished = gk::UpdateModelSecondaryMotion(instance, 0.0) == 0;
        CheckCacheStage(cachePublished && source->sampleCount > 0, "successful secondary update publishes sampled base pose");
        source->sampleCount = 0;
        const bool repeatedUpdateSucceeded = gk::UpdateModelSecondaryMotion(instance, 0.0) == 0;
        CheckCacheStage(repeatedUpdateSucceeded && source->sampleCount == 0, "same-condition secondary update reuses base pose");

        // 同じclip・時刻・blend・IKではqueryと複数drawでclipを再標本化しない。
        const uint32_t queryBones[3] = { 0, 2, 4 };
        gk::Vec3 queried[3]{};
        source->sampleCount = 0;
        const bool querySucceeded = gk::GetModelBonePositions(instance, queryBones, 3, queried) == 0;
        if (!CheckCacheStage(querySucceeded && source->sampleCount == 0, "bone query reuses cached base pose"))
        {
            fprintf(stderr, "  query source samples: %u\n", source->sampleCount);
        }
        const bool frameStarted = gk::BeginFrame() == 0;
        const bool firstDrawSucceeded = frameStarted && gk::DrawModel(instance) == 0;
        const bool secondDrawSucceeded = firstDrawSucceeded && gk::DrawModel(instance) == 0;
        CheckCacheStage(secondDrawSucceeded && source->sampleCount == 0, "repeated CPU draws reuse cached base pose");
        const auto& draws = gk::detail::GetContext().frame.draws;
        const bool snapshotsMatch = draws.Count() == 2 && draws.At(0).model != draws.At(1).model && Near(draws.At(0).model->vertices.At(2).position[0], queried[1].x) && Near(draws.At(0).model->vertices.At(2).position[1], queried[1].y) && Near(draws.At(0).model->vertices.At(2).position[2], queried[1].z);
        CheckCacheStage(snapshotsMatch, "cached query and independent draw snapshots agree");
        const float queuedX = draws.Count() > 0 ? draws.At(0).model->vertices.At(2).position[0] : 0.0f;
        const float queuedY = draws.Count() > 0 ? draws.At(0).model->vertices.At(2).position[1] : 0.0f;
        const float queuedZ = draws.Count() > 0 ? draws.At(0).model->vertices.At(2).position[2] : 0.0f;

        // 再生時刻、blend、IKの変更はcacheを無効にして新しい姿勢を評価する。
        source->sampleCount = 0;
        const bool timeDrawSucceeded = gk::SetModelAnimationTime(instance, 0.25, 0) == 0 && gk::DrawModel(instance) == 0;
        CheckCacheStage(timeDrawSucceeded && source->sampleCount > 0, "animation time mutation invalidates cache");
        source->sampleCount = 0;
        const bool changedTimeUpdateSucceeded = gk::UpdateModelSecondaryMotion(instance, 0.0) == 0;
        CheckCacheStage(changedTimeUpdateSucceeded && source->sampleCount > 0, "secondary update resamples after animation time mutation");
        source->sampleCount = 0;
        const bool blendDrawSucceeded = gk::SetModelAnimationBlendWeight(instance, 1.0f) == 0 && gk::DrawModel(instance) == 0;
        CheckCacheStage(blendDrawSucceeded && source->sampleCount > 0, "blend mutation invalidates cache");
        source->sampleCount = 0;
        const bool ikDrawSucceeded = gk::SetModelTwoBoneIk(instance, 0, 1, 2, { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) == 0 && gk::DrawModel(instance) == 0;
        CheckCacheStage(ikDrawSucceeded && source->sampleCount > 0, "IK insertion invalidates cache");
        source->sampleCount = 0;
        const bool clearIkDrawSucceeded = gk::ClearModelIk(instance) == 0 && gk::DrawModel(instance) == 0;
        CheckCacheStage(clearIkDrawSucceeded && source->sampleCount > 0, "IK removal invalidates cache");
        const bool queuedSnapshotStable = draws.Count() >= 6 && Near(draws.At(0).model->vertices.At(2).position[0], queuedX) && Near(draws.At(0).model->vertices.At(2).position[1], queuedY) && Near(draws.At(0).model->vertices.At(2).position[2], queuedZ);
        CheckCacheStage(queuedSnapshotStable, "queued draw snapshot remains immutable after invalidation");

        // 失敗したsetterはcacheを無効化せず、揺れもの専用操作も基準姿勢を再標本化しない。
        source->sampleCount = 0;
        CheckCacheStage(gk::UpdateModelSecondaryMotion(instance, 0.0) == 0, "cache refresh after valid mutations");
        source->sampleCount = 0;
        const bool failedSetterPreservedCache = gk::SetModelAnimationTime(instance, std::numeric_limits<double>::quiet_NaN(), 0) == -1 && gk::DrawModel(instance) == 0;
        CheckCacheStage(failedSetterPreservedCache && source->sampleCount == 0, "failed setter preserves matching cache");
        source->sampleCount = 0;
        const bool secondaryOnlyOperationsSucceeded = gk::ResetModelSecondaryMotion(instance) == 0 && gk::SetModelSecondaryMotionColliders(instance, &collider, 1, 0.0f) == 0 && gk::SetModelSecondaryMotionChain(instance, secondChain, 2, settings) == 0 && gk::DrawModel(instance) == 0;
        auto* currentTransformAfterChainReplacement = gk::detail::FindModelTransform(instance);
        auto* currentStateAfterChainReplacement = currentTransformAfterChainReplacement && currentTransformAfterChainReplacement->playback ? currentTransformAfterChainReplacement->playback->secondaryMotion : nullptr;
        bool firstChainRadiusSurvived = false;
        bool secondChainRadiusSurvived = false;
        if (currentStateAfterChainReplacement && currentStateAfterChainReplacement->chains.Count() == 2)
        {
            for (uint32_t chainIndex = 0; chainIndex < currentStateAfterChainReplacement->chains.Count(); ++chainIndex)
            {
                const auto* currentChain = currentStateAfterChainReplacement->chains.At(chainIndex);
                if (currentChain->bones.Count() > 0 && currentChain->bones.At(0) == firstChain[0] && currentChain->collisionRadius == 0.008f)
                {
                    firstChainRadiusSurvived = true;
                }
                if (currentChain->bones.Count() > 0 && currentChain->bones.At(0) == secondChain[0] && currentChain->collisionRadius == 0.15f)
                {
                    secondChainRadiusSurvived = true;
                }
            }
        }
        const bool radiiSurvivedLifecycle = firstChainRadiusSurvived && secondChainRadiusSurvived;
        CheckCacheStage(secondaryOnlyOperationsSucceeded && source->sampleCount == 0 && radiiSurvivedLifecycle, "secondary-only changes preserve base-pose cache and independent chain radii");

        const auto GetPlayback = [&]()
        {
            auto* currentTransform = gk::detail::FindModelTransform(instance);
            return currentTransform ? currentTransform->playback : nullptr;
        };
        const auto WarmCache = [&](const char* stage)
        {
            source->sampleCount = 0;
            const bool updated = gk::UpdateModelSecondaryMotion(instance, 0.0) == 0;
            const auto* currentPlayback = GetPlayback();
            return CheckCacheStage(updated && currentPlayback && currentPlayback->basePoseCache && currentPlayback->basePoseCache->revision == currentPlayback->poseRevision, stage);
        };
        const auto CheckMutationMiss = [&](bool setterSucceeded, const char* stage)
        {
            source->sampleCount = 0;
            const bool drawn = gk::DrawModel(instance) == 0;
            return CheckCacheStage(setterSucceeded && drawn && source->sampleCount > 0, stage);
        };
        const auto CheckQueryFallback = [&](const char* stage)
        {
            const uint32_t bone = 2;
            gk::Vec3 position{};
            source->sampleCount = 0;
            const bool queriedSuccessfully = gk::GetModelBonePosition(instance, bone, position) == 0;
            const bool finitePosition = isfinite(position.x) && isfinite(position.y) && isfinite(position.z);
            return CheckCacheStage(queriedSuccessfully && finitePosition && source->sampleCount > 0, stage);
        };

        // NaNや配列数が崩れたcacheは読まず、正しい姿勢を評価し直す。
        WarmCache("warm cache before private-state validation");
        auto* cachePlayback = GetPlayback();
        CheckCacheStage(cachePlayback && cachePlayback->basePoseCache, "private cache exists before validation");
        if (cachePlayback && cachePlayback->basePoseCache)
        {
            cachePlayback->basePoseCache->source = nullptr;
            CheckQueryFallback("source identity mismatch falls back to fresh evaluation");
            WarmCache("replace source-mismatched cache");
            cachePlayback = GetPlayback();
            if (cachePlayback && cachePlayback->basePoseCache && cachePlayback->basePoseCache->pose.localTransforms.Count() > 0)
            {
                cachePlayback->basePoseCache->pose.localTransforms.At(0).position[0] = std::numeric_limits<float>::quiet_NaN();
                CheckQueryFallback("NaN cached transform falls back to fresh evaluation");
            }
            else
            {
                CheckCacheStage(false, "cache pose exists before NaN validation");
            }
            WarmCache("replace nonfinite cache");
            cachePlayback = GetPlayback();
            if (cachePlayback && cachePlayback->basePoseCache)
            {
                cachePlayback->basePoseCache->pose.localTransforms.Clear();
                CheckQueryFallback("cached transform count mismatch falls back to fresh evaluation");
            }
            else
            {
                CheckCacheStage(false, "cache exists before count validation");
            }
            WarmCache("replace count-mismatched cache");
        }

        // 失敗した揺れ更新は、直前に公開したcacheとsimulation状態を保つ。
        auto* failedUpdatePlayback = GetPlayback();
        auto* const cacheBeforeFailedUpdate = failedUpdatePlayback ? failedUpdatePlayback->basePoseCache : nullptr;
        const auto* const secondaryBeforeFailedUpdate = failedUpdatePlayback ? failedUpdatePlayback->secondaryMotion : nullptr;
        const uint64_t revisionBeforeFailedUpdate = failedUpdatePlayback ? failedUpdatePlayback->poseRevision : 0;
        source->sampleCount = 0;
        const bool failedUpdateRejected = gk::UpdateModelSecondaryMotion(instance, std::numeric_limits<double>::quiet_NaN()) == -1;
        failedUpdatePlayback = GetPlayback();
        CheckCacheStage(failedUpdateRejected && failedUpdatePlayback && failedUpdatePlayback->basePoseCache == cacheBeforeFailedUpdate && failedUpdatePlayback->secondaryMotion == secondaryBeforeFailedUpdate && failedUpdatePlayback->poseRevision == revisionBeforeFailedUpdate, "failed secondary update preserves cache and simulation");
        source->sampleCount = 0;
        CheckCacheStage(gk::DrawModel(instance) == 0 && source->sampleCount == 0, "failed update leaves reusable cache intact");

        // cache世代番号の上限では古いcacheを破棄し、番号を安全な値へ戻す。
        WarmCache("warm cache before revision overflow");
        auto* overflowPlayback = GetPlayback();
        if (overflowPlayback)
        {
            overflowPlayback->poseRevision = std::numeric_limits<uint64_t>::max();
        }
        source->sampleCount = 0;
        const bool overflowSetterSucceeded = gk::SetModelAnimationSpeed(instance, 0.75, 0) == 0;
        overflowPlayback = GetPlayback();
        CheckCacheStage(overflowSetterSucceeded && overflowPlayback && overflowPlayback->poseRevision == 1 && overflowPlayback->basePoseCache == nullptr, "revision overflow drops stale cache");
        CheckMutationMiss(overflowSetterSucceeded, "revision overflow setter forces fresh sampling");

        // speed、loop、bone role、humanoid map、Stopの成功後は古いcacheを使わない。
        WarmCache("warm cache before speed setter");
        CheckMutationMiss(gk::SetModelAnimationSpeed(instance, 0.5, 0) == 0, "speed setter invalidates cache");
        WarmCache("warm cache before loop setter");
        CheckMutationMiss(gk::SetModelAnimationLoop(instance, true, 0) == 0, "loop setter invalidates cache");
        WarmCache("warm cache before bone role setter");
        CheckMutationMiss(gk::SetModelBoneRole(instance, 0, gk::EHumanoidBone::Hips) == 0, "model bone role setter invalidates cache");
        WarmCache("warm cache before humanoid auto-map");
        const char* savedBoneNames[3] = { source->boneNames[0], source->boneNames[1], source->boneNames[2] };
        source->boneNames[0] = "Hips";
        source->boneNames[1] = "LeftUpperLeg";
        source->boneNames[2] = "LeftLowerLeg";
        const int autoMapResult = gk::AutoMapModelHumanoidBones(instance);
        source->boneNames[0] = savedBoneNames[0];
        source->boneNames[1] = savedBoneNames[1];
        source->boneNames[2] = savedBoneNames[2];
        CheckMutationMiss(autoMapResult == 0, "successful humanoid auto-map invalidates cache");
        WarmCache("warm cache before stop");
        const auto* beforeStopPlayback = GetPlayback();
        const uint64_t revisionBeforeStop = beforeStopPlayback ? beforeStopPlayback->poseRevision : 0;
        const bool stopped = gk::StopModelAnimation(instance) == 0;
        const auto* afterStopPlayback = GetPlayback();
        CheckCacheStage(stopped && afterStopPlayback && afterStopPlayback->poseRevision != revisionBeforeStop, "StopModelAnimation invalidates cache revision");
        source->sampleCount = 0;
        const bool restartedAfterStop = gk::PlayModelAnimation(instance, 0, false) == 0 && gk::DrawModel(instance) == 0;
        CheckCacheStage(restartedAfterStop && source->sampleCount > 0, "animation restart after stop evaluates a fresh pose");
        WarmCache("publish cache after animation restart");

        // 新instanceはcacheを共有せず、一方の失効がもう一方のcacheへ波及しない。
        const auto separateInstance = gk::CreateModelInstance(model);
        const uint32_t separateChain[2] = { 1, 2 };
        const bool separateCreated = separateInstance.IsValid() && gk::SetModelSecondaryMotionChain(separateInstance, separateChain, 2, settings) == 0 && gk::PlayModelAnimation(separateInstance, 0, false) == 0;
        const auto* mainPlayback = GetPlayback();
        const auto* separateTransform = gk::detail::FindModelTransform(separateInstance);
        const auto* separatePlayback = separateTransform ? separateTransform->playback : nullptr;
        CheckCacheStage(separateCreated && mainPlayback && mainPlayback->basePoseCache && separatePlayback && separatePlayback->basePoseCache == nullptr, "new instance starts without shared pose cache");
        source->sampleCount = 0;
        const bool separateUpdateSucceeded = separateCreated && gk::UpdateModelSecondaryMotion(separateInstance, 0.0) == 0;
        separateTransform = gk::detail::FindModelTransform(separateInstance);
        separatePlayback = separateTransform ? separateTransform->playback : nullptr;
        CheckCacheStage(separateUpdateSucceeded && mainPlayback && separatePlayback && separatePlayback->basePoseCache && separatePlayback->basePoseCache != mainPlayback->basePoseCache, "instances own distinct pose caches");
        source->sampleCount = 0;
        const bool changedSeparateTime = gk::SetModelAnimationTime(separateInstance, 0.5, 0) == 0;
        const bool mainCacheStillHits = gk::DrawModel(instance) == 0 && source->sampleCount == 0;
        CheckCacheStage(changedSeparateTime && mainCacheStillHits, "one instance mutation preserves the other instance cache");
        source->sampleCount = 0;
        CheckCacheStage(gk::DrawModel(separateInstance) == 0 && source->sampleCount > 0, "mutated instance misses its own cache");
        CheckCacheStage(gk::DeleteModel(separateInstance) == 0, "separate instance cleanup");

        // 最後にcacheを伴うinstanceを消し、clear後も基準姿勢を使えることを確認する。
        source->sampleCount = 0;
        const bool clearedSecondary = gk::ClearModelSecondaryMotion(instance) == 0 && gk::DrawModel(instance) == 0;
        CheckCacheStage(clearedSecondary && source->sampleCount == 0, "clearing secondary motion preserves cached base pose");
    }
    if (!passed)
    {
        fprintf(stderr, "multiple secondary chains exceeded the shared FK budget or base-pose cache contract: %s\n", gk::GetLastErrorMessage());
    }
    if (gk::detail::GetContext().frameOpen)
    {
        passed = gk::Present() == 0 && passed;
    }
    passed = gk::ClearModelSecondaryMotion(instance) == 0 && passed;
    passed = gk::DeleteModel(instance) == 0 && passed;
    passed = gk::DeleteModel(model) == 0 && passed;
    return passed;
}

/**
 * UpperChest未設定時にChestへ胴体位置を探し直してpreviewを作る。
 */
bool TestBuildArmIkTorsoFallback()
{
    auto* resource = gk::detail::CreateModelResource();
    if (!resource)
    {
        return false;
    }
    auto* source = new AAnimationRigTestSource;
    source->boneCount = 4;
    const int32_t parents[4] = { -1, 0, 1, 2 };
    gk::model::animation::FModelBoneTransform rest[4]{};
    rest[0].position[1] = 1.0f;
    rest[1].position[0] = -1.0f;
    rest[2].position[0] = -1.0f;
    rest[3].position[0] = -1.0f;
    if (!source->skeleton.parents.AppendRange(parents, 4) || !source->skeleton.restLocalTransforms.AppendRange(rest, 4))
    {
        delete source;
        gk::Release(&resource->reference);
        return false;
    }
    gk::String error;
    resource->animation = gk::model::CreateModelAnimationAsset(source, error);
    gk::detail::ModelVertex vertices[4]{};
    if (!resource->animation || !resource->vertices.AppendRange(vertices, 4))
    {
        gk::Release(&resource->reference);
        return false;
    }
    const auto model = gk::detail::RegisterModelResource(resource, error);
    gk::detail::ModelTransform transform{};
    transform.handle = model;
    transform.scale = { 1.0f, 1.0f, 1.0f };
    if (!model.IsValid() || !gk::detail::GetContext().modelTransforms.Append(transform))
    {
        if (model.IsValid())
        {
            gk::DeleteModel(model);
        }
        return false;
    }
    bool passed = gk::SetModelBoneRole(model, 0, gk::EHumanoidBone::Chest) == 0 && gk::SetModelBoneRole(model, 1, gk::EHumanoidBone::RightUpperArm) == 0 && gk::SetModelBoneRole(model, 2, gk::EHumanoidBone::RightLowerArm) == 0 && gk::SetModelBoneRole(model, 3, gk::EHumanoidBone::RightHand) == 0;
    passed = gk::PlayModelAnimation(model, 1, false) == 0 && passed;
    const uint32_t allBones[4] = { 0, 1, 2, 3 };
    gk::Vec3 animatedBones[4]{};
    source->sampleCount = 0;
    passed = gk::GetModelBonePositions(model, allBones, 4, animatedBones) == 0 && source->sampleCount == 1 && Near(animatedBones[0].x, 2.0f) && Near(animatedBones[1].x, 1.0f) && Near(animatedBones[2].x, 0.0f) && Near(animatedBones[3].x, -1.0f) && passed;
    for (uint32_t frame = 0; frame < 3; ++frame)
    {
        // 同じclip時刻から毎回previewとIKを作り直し、前回のIKが胸や手首の基準へ混ざらないことを確認する。
        gk::examples::FModelArmIkPreview framePreview{};
        passed = gk::ClearModelIk(model) == 0 && gk::SetModelAnimationTime(model, 0.0, 0) == 0 && gk::UpdateModelAnimation(model, 0.0) == 0 && gk::examples::BuildModelArmIkPreview(model, framePreview) && Near(framePreview.joints[0].x, 1.0f) && Near(framePreview.joints[2].x, -1.0f) && passed;
        passed = gk::SetModelTwoBoneIk(model, framePreview.bones[0], framePreview.bones[1], framePreview.bones[2], framePreview.target, framePreview.pole) == 0 && gk::GetModelBonePositions(model, allBones, 4, animatedBones) == 0 && Near(animatedBones[0].x, 2.0f) && Near(animatedBones[3].x, framePreview.target.x) && Near(animatedBones[3].y, framePreview.target.y) && passed;
    }
    passed = gk::ClearModelIk(model) == 0 && gk::StopModelAnimation(model) == 0 && passed;
    gk::examples::FModelArmIkPreview preview{};
    preview.target = { 71.0f, 72.0f, 73.0f };
    passed = gk::examples::BuildModelArmIkPreview(model, preview) && Near(preview.joints[0].x, -1.0f) && Near(preview.joints[0].y, 1.0f) && Near(preview.joints[1].x, -2.0f) && Near(preview.joints[1].y, 1.0f) && Near(preview.joints[2].x, -3.0f) && Near(preview.joints[2].y, 1.0f) && Near(preview.target.x, -2.5f) && Near(preview.target.y, 0.5f) && Near(preview.target.z, 0.0f) && preview.bones[0] == 1 && preview.bones[1] == 2 && preview.bones[2] == 3 && passed;
    const auto snapshotInstance = gk::CreateModelInstance(model);
    bool snapshotPassed = snapshotInstance.IsValid() && gk::BeginFrame() == 0 && gk::DrawModel(snapshotInstance) == 0;
    auto& draws = gk::detail::GetContext().frame.draws;
    const gk::Vec3 queuedHand = snapshotPassed ? gk::Vec3{ draws.At(0).model->vertices.At(3).position[0], draws.At(0).model->vertices.At(3).position[1], draws.At(0).model->vertices.At(3).position[2] } : gk::Vec3{};
    snapshotPassed = snapshotPassed && gk::PlayModelAnimation(snapshotInstance, 1, false) == 0 && gk::SetModelTwoBoneIk(snapshotInstance, 1, 2, 3, { 0.0f, 2.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) == 0 && gk::DeleteModel(snapshotInstance) == 0 && draws.Count() == 1 && Near(draws.At(0).model->vertices.At(3).position[0], queuedHand.x) && Near(draws.At(0).model->vertices.At(3).position[1], queuedHand.y) && Near(draws.At(0).model->vertices.At(3).position[2], queuedHand.z) && snapshotPassed;
    snapshotPassed = gk::Present() == 0 && snapshotPassed;
    passed = snapshotPassed && passed;
    if (gk::DeleteModel(model) != 0)
    {
        passed = false;
    }
    if (!passed)
    {
        fprintf(stderr, "arm IK preview did not fall back from UpperChest to Chest: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * 事前解決した6骨bindingを使うpreviewが役割変更後も保持され、再解決と現在poseへ追随することを確認する。
 */
bool TestAnimatedArmIkCachedBinding()
{
    auto* resource = gk::detail::CreateModelResource();
    if (!resource)
    {
        return false;
    }
    auto* source = new AAnimationRigTestSource;
    source->boneCount = 7;
    source->timeVaryingTranslation = true;
    const int32_t parents[7] = { -1, 0, 1, 0, 0, 0, 0 };
    gk::model::animation::FModelBoneTransform rest[7]{};
    rest[0].position[1] = 1.0f;
    rest[1].position[0] = -1.0f;
    rest[2].position[0] = -1.0f;
    rest[3].position[0] = 0.0f;
    rest[4].position[1] = 1.0f;
    rest[5].position[0] = 1.0f;
    rest[6].position[0] = 0.5f;
    if (!source->skeleton.parents.AppendRange(parents, 7) || !source->skeleton.restLocalTransforms.AppendRange(rest, 7))
    {
        delete source;
        gk::Release(&resource->reference);
        return false;
    }
    gk::String error;
    resource->animation = gk::model::CreateModelAnimationAsset(source, error);
    gk::detail::ModelVertex vertices[7]{};
    if (!resource->animation || !resource->vertices.AppendRange(vertices, 7))
    {
        gk::Release(&resource->reference);
        return false;
    }
    const auto model = gk::detail::RegisterModelResource(resource, error);
    gk::detail::ModelTransform transform{};
    transform.handle = model;
    transform.scale = { 1.0f, 1.0f, 1.0f };
    if (!model.IsValid() || !gk::detail::GetContext().modelTransforms.Append(transform))
    {
        if (model.IsValid())
        {
            gk::DeleteModel(model);
        }
        return false;
    }
    const gk::EHumanoidBone roles[6] = { gk::EHumanoidBone::RightUpperArm, gk::EHumanoidBone::RightLowerArm, gk::EHumanoidBone::RightHand, gk::EHumanoidBone::Hips, gk::EHumanoidBone::Chest, gk::EHumanoidBone::LeftUpperArm };
    bool passed = true;
    for (uint32_t index = 0; index < 6; ++index)
    {
        passed = gk::SetModelBoneRole(model, index, roles[index]) == 0 && passed;
    }
    gk::examples::FModelMotionView motionView;
    const gk::Vec3 viewCenter{ 5.0f, 6.0f, 7.0f };
    passed = motionView.Initialize(model, viewCenter) && passed;
    gk::Vec3 observedCenter{};
    passed = motionView.GetCenter(observedCenter) && NearVector(observedCenter, viewCenter) && passed;
    passed = gk::SetModelPosition(model, { 20.0f, 30.0f, 40.0f }) == 0 && gk::SetModelRotation(model, { 0.2f, 0.3f, 0.4f }) == 0 && gk::SetModelScale(model, { 2.0f, 3.0f, 4.0f }) == 0 && passed;
    observedCenter = {};
    passed = motionView.GetCenter(observedCenter) && NearVector(observedCenter, viewCenter) && passed;
    gk::examples::FModelMotionView overflowView;
    const float largestFloat = std::numeric_limits<float>::max();
    passed = overflowView.Initialize(model, { largestFloat, 0.0f, 0.0f }) && passed;
    gk::Vec3 retainedCenter{ 81.0f, 82.0f, 83.0f };
    passed = !motionView.Initialize({}, { 8.0f, 9.0f, 10.0f }) && motionView.GetCenter(retainedCenter) && NearVector(retainedCenter, viewCenter) && passed;
    passed = !motionView.Initialize(model, { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f }) && motionView.GetCenter(retainedCenter) && NearVector(retainedCenter, viewCenter) && passed;
    auto* modelTransform = gk::detail::FindModelTransform(model);
    passed = modelTransform && modelTransform->playback && modelTransform->playback->roles.Count() == 7 && passed;
    if (modelTransform && modelTransform->playback && modelTransform->playback->roles.Count() == 7)
    {
        modelTransform->playback->roles.At(6) = static_cast<uint16_t>(gk::EHumanoidBone::Hips);
    }
    passed = !motionView.Initialize(model, { 11.0f, 12.0f, 13.0f }) && motionView.GetCenter(retainedCenter) && NearVector(retainedCenter, viewCenter) && passed;
    if (modelTransform && modelTransform->playback && modelTransform->playback->roles.Count() == 7)
    {
        modelTransform->playback->roles.At(6) = static_cast<uint16_t>(gk::EHumanoidBone::None);
    }
    passed = gk::SetModelBoneRole(model, 3, gk::EHumanoidBone::None) == 0 && passed;
    passed = !motionView.Initialize(model, { 11.0f, 12.0f, 13.0f }) && motionView.GetCenter(retainedCenter) && NearVector(retainedCenter, viewCenter) && passed;
    passed = gk::SetModelBoneRole(model, 3, gk::EHumanoidBone::Hips) == 0 && passed;
    uint32_t binding[6]{};
    const uint32_t expectedBinding[6] = { 0, 1, 2, 3, 4, 5 };
    passed = gk::examples::ResolveAnimatedModelArmIkBones(model, binding) && memcmp(binding, expectedBinding, sizeof(binding)) == 0 && passed;
    gk::examples::FModelArmIkPreview cached{};
    gk::examples::FModelArmIkPreview dynamic{};
    passed = gk::examples::BuildAnimatedModelArmIkPreview(model, binding, cached) && gk::examples::BuildAnimatedModelArmIkPreview(model, dynamic) && SamePreview(cached, dynamic) && passed;
    passed = gk::PlayModelAnimation(model, 1, false) == 0 && gk::SetModelAnimationTime(model, 0.0, 0) == 0 && gk::UpdateModelAnimation(model, 0.0) == 0 && passed;
    gk::examples::FModelArmIkPreview animated{};
    passed = gk::examples::BuildAnimatedModelArmIkPreview(model, binding, animated) && Near(animated.joints[0].x, cached.joints[0].x + 2.0f) && passed;
    passed = gk::SetModelAnimationTime(model, 0.5, 0) == 0 && gk::UpdateModelAnimation(model, 0.0) == 0 && passed;
    gk::examples::FModelArmIkPreview retimed{};
    passed = gk::examples::BuildAnimatedModelArmIkPreview(model, binding, retimed) && Near(retimed.joints[0].x, cached.joints[0].x + 2.5f) && passed;
    observedCenter = {};
    passed = motionView.GetCenter(observedCenter) && NearVector(observedCenter, { 7.5f, 6.0f, 7.0f }) && passed;
    passed = gk::SetModelAnimationTime(model, 1.0e32, 0) == 0 && gk::UpdateModelAnimation(model, 0.0) == 0 && passed;
    retainedCenter = { 92.0f, 93.0f, 94.0f };
    passed = !overflowView.GetCenter(retainedCenter) && NearVector(retainedCenter, { 92.0f, 93.0f, 94.0f }) && passed;
    passed = gk::SetModelAnimationTime(model, 0.5, 0) == 0 && gk::UpdateModelAnimation(model, 0.0) == 0 && passed;
    passed = gk::SetModelBoneRole(model, 0, gk::EHumanoidBone::None) == 0 && passed;
    gk::examples::FModelArmIkPreview stillCached{};
    uint32_t resolvedAgain[6]{};
    const uint32_t savedFailedBinding[6] = { 51, 52, 53, 54, 55, 56 };
    uint32_t unresolvedBinding[6];
    memcpy(unresolvedBinding, savedFailedBinding, sizeof(unresolvedBinding));
    passed = gk::examples::BuildAnimatedModelArmIkPreview(model, binding, stillCached) && stillCached.bones[0] == 0 && !gk::examples::ResolveAnimatedModelArmIkBones(model, unresolvedBinding) && memcmp(unresolvedBinding, savedFailedBinding, sizeof(unresolvedBinding)) == 0 && passed;
    passed = gk::SetModelBoneRole(model, 6, gk::EHumanoidBone::RightUpperArm) == 0 && gk::examples::ResolveAnimatedModelArmIkBones(model, resolvedAgain) && resolvedAgain[0] == 6 && passed;
    gk::examples::FModelArmIkPreview rebound{};
    passed = gk::examples::BuildAnimatedModelArmIkPreview(model, resolvedAgain, rebound) && rebound.bones[0] == 6 && passed;
    const gk::examples::FModelArmIkPreview savedPreview = rebound;
    const uint32_t savedBinding[6] = { 41, 42, 43, 44, 45, 46 };
    uint32_t failedBinding[6];
    memcpy(failedBinding, savedBinding, sizeof(failedBinding));
    passed = !gk::examples::ResolveAnimatedModelArmIkBones({}, failedBinding) && memcmp(failedBinding, savedBinding, sizeof(failedBinding)) == 0 && passed;
    passed = !gk::examples::ResolveAnimatedModelArmIkBones(model, nullptr) && passed;
    gk::examples::FModelArmIkPreview failedPreview = savedPreview;
    passed = !gk::examples::BuildAnimatedModelArmIkPreview(model, nullptr, failedPreview) && SamePreview(failedPreview, savedPreview) && passed;
    const uint32_t invalidBinding[6] = { 99, 1, 2, 3, 4, 5 };
    failedPreview = savedPreview;
    passed = !gk::examples::BuildAnimatedModelArmIkPreview(model, invalidBinding, failedPreview) && SamePreview(failedPreview, savedPreview) && passed;
    passed = gk::DeleteModel(model) == 0 && passed;
    retainedCenter = { 101.0f, 102.0f, 103.0f };
    passed = !motionView.GetCenter(retainedCenter) && NearVector(retainedCenter, { 101.0f, 102.0f, 103.0f }) && passed;
    if (!passed)
    {
        fprintf(stderr, "cached animated arm IK binding or failure atomicity failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * viewer option解析が位置引数とmaterials設定を保ち、follow flagの重複失敗を変更なしで返す。
 */
bool TestParseMotionViewOptions()
{
    char app[] = "viewer";
    char model[] = "motion.glb";
    char scale[] = "1";
    char centerX[] = "0";
    char centerY[] = "1";
    char centerZ[] = "2";
    char motionFlag[] = "--motion-bones";
    char motionPath[] = "bones.glb";
    char followFlag[] = "--follow-motion";
    char materialsFlag[] = "--materials";
    char materialsValue[] = "--follow-motion";
    char* arguments[12] = { app, model, scale, centerX, centerY, centerZ, motionFlag, motionPath, followFlag, materialsFlag, materialsValue, nullptr };
    int argumentCount = 11;
    bool followMotion = false;
    bool passed = gk::examples::ParseMotionViewOptions(argumentCount, arguments, followMotion) && followMotion && argumentCount == 10 && arguments[6] == motionFlag && arguments[7] == motionPath && arguments[8] == materialsFlag && arguments[9] == materialsValue && arguments[10] == nullptr;

    char duplicateApp[] = "viewer";
    char duplicateModel[] = "motion.glb";
    char duplicateScale[] = "1";
    char duplicateX[] = "0";
    char duplicateY[] = "1";
    char duplicateZ[] = "2";
    char duplicateFirst[] = "--follow-motion";
    char duplicateSecond[] = "--follow-motion";
    char* duplicateArguments[9] = { duplicateApp, duplicateModel, duplicateScale, duplicateX, duplicateY, duplicateZ, duplicateFirst, duplicateSecond, nullptr };
    char* originalArguments[9];
    memcpy(originalArguments, duplicateArguments, sizeof(duplicateArguments));
    int duplicateCount = 8;
    bool duplicateFollowMotion = true;
    passed = !gk::examples::ParseMotionViewOptions(duplicateCount, duplicateArguments, duplicateFollowMotion) && duplicateCount == 8 && duplicateFollowMotion && memcmp(originalArguments, duplicateArguments, sizeof(duplicateArguments)) == 0 && passed;
    if (!passed)
    {
        fprintf(stderr, "motion-view command line option contract failed\n");
    }
    return passed;
}

/**
 * IK目標markerがUI層へ8個の塗り三角形として正しいworld位置で積まれることを確認する。
 */
bool TestModelArmIkTargetMarker()
{
    const gk::Vec3 joints[3] = { { -1.0f, 1.0f, 0.0f }, { -2.0f, 1.0f, 0.0f }, { -3.0f, 1.0f, 0.0f } };
    gk::examples::FModelArmIkPreview preview{};
    if (!gk::examples::MakeModelArmIkPreview({ 0.0f, 1.0f, 0.0f }, joints, preview))
    {
        return false;
    }
    auto& context = gk::detail::GetContext();
    const uint32_t firstMarkerDraw = context.frame.draws.Count();
    bool passed = gk::SetDrawLayer(gk::DrawLayer::UI) == 0 && gk::examples::DrawModelArmIkTarget(preview, 2.0f, { 1.0f, 2.0f, 3.0f }, 1.57079632679f) && context.frame.draws.Count() == firstMarkerDraw + 8;
    const uint8_t expectedFlags = static_cast<uint8_t>(gk::detail::DrawFilled);
    const uint8_t expectedLayer = static_cast<uint8_t>(gk::DrawLayer::UI);
    const uint32_t expectedColor = gk::ColorRGB(255, 150, 35);
    for (uint32_t index = 0; index < 8 && passed; ++index)
    {
        const auto& draw = context.frame.draws.At(firstMarkerDraw + index);
        passed = draw.kind == gk::detail::DrawKind::Triangle3D && draw.flags == expectedFlags && draw.layer == expectedLayer && draw.color == expectedColor;
        for (uint32_t point = 0; point < 3 && passed; ++point)
        {
            passed = draw.points[point].x >= -6.0241f && draw.points[point].x <= -5.9759f && draw.points[point].y >= -3.0241f && draw.points[point].y <= -2.9759f && draw.points[point].z >= 6.9759f && draw.points[point].z <= 7.0241f;
        }
    }
    passed = gk::SetDrawLayer(gk::DrawLayer::Scene) == 0 && passed;
    if (!passed)
    {
        fprintf(stderr, "model arm IK target marker packet, layer, fill, or transformed position failed\n");
    }
    return passed;
}

/**
 * 公開profile API用に、既存fixtureと同じ3骨motionを登録する。
 */
gk::ModelAnimationHandle CreateRigAnimation(const char* const* boneNames = nullptr)
{
    auto* source = new AAnimationRigTestSource;
    if (boneNames)
    {
        for (uint32_t bone = 0; bone < 3; ++bone)
        {
            source->boneNames[bone] = boneNames[bone];
        }
    }
    const int32_t parents[3] = { -1, 0, 1 };
    gk::model::animation::FModelBoneTransform rest[3]{};
    rest[1].position[0] = 1.0f;
    rest[2].position[0] = 1.0f;
    if (!source->skeleton.parents.AppendRange(parents, 3) || !source->skeleton.restLocalTransforms.AppendRange(rest, 3))
    {
        delete source;
        return {};
    }
    gk::String error;
    auto* asset = gk::model::CreateModelAnimationAsset(source, error);
    const auto handle = gk::model::RegisterAnimation(asset, error);
    if (!handle.IsValid())
    {
        fprintf(stderr, "rig animation registration failed: %s\n", error.CStr());
    }
    return handle;
}

/**
 * UTF-8 profileを指定先へ書き、APIへ渡すUTF-8パスを返す。
 */
bool WriteHumanoidProfile(const std::filesystem::path& path, const char* contents, std::string& utf8Path);

/**
 * 適用時の対応状況、再生枠ごとの独立性、無効時の出力保持を確認する。
 */
bool TestMappingDiagnostics(gk::ModelHandle model)
{
    namespace fs = std::filesystem;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = fs::temp_directory_path() / ("gkcore-animation-mapping-" + std::to_string(suffix));
    fs::create_directories(directory);
    const auto targetPath = directory / "target.txt";
    const auto partialPath = directory / "partial.txt";
    const auto fullPath = directory / "full.txt";
    std::string target, partial, full;
    const bool fixturesReady = WriteHumanoidProfile(targetPath, "Hips\tjoint_a\nLeftUpperLeg\tjoint_b\nLeftLowerLeg\tjoint_c\n", target) && WriteHumanoidProfile(partialPath, "Hips\tsource_a\n", partial) && WriteHumanoidProfile(fullPath, "Hips\tsource_a\nLeftUpperLeg\tsource_b\nLeftLowerLeg\tsource_c\n", full);
    if (!fixturesReady)
    {
        fs::remove_all(directory);
        fprintf(stderr, "animation mapping fixtures could not be written\n");
        return false;
    }
    const char* sourceNames[3] = { "source_a", "source_b", "source_c" };
    const auto animation = CreateRigAnimation(sourceNames);
    if (!animation.IsValid())
    {
        fs::remove_all(directory);
        return false;
    }
    bool passed = true;
    passed = gk::SetModelHumanoidBoneMap(model, target.c_str()) == 0 && gk::SetAnimationHumanoidBoneMap(animation, partial.c_str()) == 0 && passed;
    passed = gk::PlayModelAnimation(model, 0, false) == 0 && passed;
    gk::FModelAnimationMappingInfo identityInfo{};
    passed = gk::GetModelAnimationMappingInfo(model, identityInfo, 0) == 0 && identityInfo.targetBoneCount == 3 && identityInfo.mappedBoneCount == 3 && identityInfo.humanoidBoneCount == 3 && identityInfo.mappedHumanoidBoneCount == 3 && passed;
    passed = gk::StopModelAnimation(model) == 0 && passed;
    passed = gk::ApplyModelAnimation(model, animation, 0, false) == 0 && passed;
    gk::FModelAnimationMappingInfo partialInfo{};
    passed = gk::GetModelAnimationMappingInfo(model, partialInfo, 0) == 0 && partialInfo.targetBoneCount == 3 && partialInfo.mappedBoneCount == 1 && partialInfo.humanoidBoneCount == 3 && partialInfo.mappedHumanoidBoneCount == 1 && passed;
    passed = gk::GetModelAnimationMissingHumanoidRole(model, 0, 0) == gk::EHumanoidBone::LeftUpperLeg && gk::GetModelAnimationMissingHumanoidRole(model, 1, 0) == gk::EHumanoidBone::LeftLowerLeg && gk::GetModelAnimationMissingHumanoidRole(model, 2, 0) == gk::EHumanoidBone::None && passed;
    passed = gk::SetAnimationHumanoidBoneMap(animation, full.c_str()) == 0 && gk::SetModelAnimationBlend(model, animation, 0, 0.5f) == 0 && passed;
    gk::FModelAnimationMappingInfo fullInfo{};
    passed = gk::GetModelAnimationMappingInfo(model, fullInfo, 1) == 0 && fullInfo.targetBoneCount == 3 && fullInfo.mappedBoneCount == 3 && fullInfo.humanoidBoneCount == 3 && fullInfo.mappedHumanoidBoneCount == 3 && passed;
    passed = gk::GetModelAnimationMappingInfo(model, partialInfo, 0) == 0 && partialInfo.mappedBoneCount == 1 && partialInfo.mappedHumanoidBoneCount == 1 && passed;
    passed = gk::GetModelAnimationMissingHumanoidRole(model, 3, 0) == gk::EHumanoidBone::None && passed;
    passed = gk::SetModelBoneRole(model, 0, gk::EHumanoidBone::None) == 0 && gk::SetModelBoneRole(model, 1, gk::EHumanoidBone::None) == 0 && gk::SetModelBoneRole(model, 2, gk::EHumanoidBone::None) == 0 && passed;
    gk::FModelAnimationMappingInfo snapshotInfo{};
    passed = gk::GetModelAnimationMappingInfo(model, snapshotInfo, 1) == 0 && snapshotInfo.humanoidBoneCount == 3 && snapshotInfo.mappedHumanoidBoneCount == 3 && passed;
    passed = gk::GetModelAnimationMissingHumanoidRole(model, 0, 1) == gk::EHumanoidBone::None && passed;
    passed = gk::ApplyModelAnimation(model, animation, 0, false) == -1 && passed;
    passed = gk::GetModelAnimationMappingInfo(model, partialInfo, 0) == 0 && partialInfo.humanoidBoneCount == 3 && partialInfo.mappedHumanoidBoneCount == 1 && passed;
    passed = gk::DeleteModelAnimation(animation) == 0 && passed;
    passed = gk::GetModelAnimationMappingInfo(model, snapshotInfo, 1) == 0 && snapshotInfo.mappedBoneCount == 3 && passed;
    const auto nameAnimation = CreateRigAnimation();
    if (!nameAnimation.IsValid())
    {
        fs::remove_all(directory);
        return false;
    }
    passed = gk::ApplyModelAnimation(model, nameAnimation, 0, false) == 0 && passed;
    gk::FModelAnimationMappingInfo reboundInfo{};
    passed = gk::GetModelAnimationMappingInfo(model, reboundInfo, 0) == 0 && reboundInfo.targetBoneCount == 3 && reboundInfo.mappedBoneCount == 3 && reboundInfo.humanoidBoneCount == 0 && reboundInfo.mappedHumanoidBoneCount == 0 && passed;
    passed = gk::GetModelAnimationMappingInfo(model, snapshotInfo, 1) == -1 && passed;
    passed = gk::SetModelHumanoidBoneMap(model, target.c_str()) == 0 && passed;
    passed = gk::GetModelAnimationMappingInfo(model, reboundInfo, 0) == 0 && reboundInfo.humanoidBoneCount == 0 && passed;
    passed = gk::ApplyModelAnimation(model, nameAnimation, 0, false) == 0 && passed;
    passed = gk::GetModelAnimationMappingInfo(model, reboundInfo, 0) == 0 && reboundInfo.targetBoneCount == 3 && reboundInfo.mappedBoneCount == 3 && reboundInfo.humanoidBoneCount == 3 && reboundInfo.mappedHumanoidBoneCount == 0 && passed;
    passed = gk::GetModelAnimationMissingHumanoidRole(model, 0, 0) == gk::EHumanoidBone::Hips && gk::GetModelAnimationMissingHumanoidRole(model, 1, 0) == gk::EHumanoidBone::LeftUpperLeg && gk::GetModelAnimationMissingHumanoidRole(model, 2, 0) == gk::EHumanoidBone::LeftLowerLeg && passed;
    passed = gk::GetModelAnimationMappingInfo(model, snapshotInfo, 1) == -1 && passed;
    passed = gk::DeleteModelAnimation(nameAnimation) == 0 && passed;
    passed = gk::StopModelAnimation(model) == 0 && passed;
    gk::FModelAnimationMappingInfo unchanged{ 11, 12, 13, 14 };
    passed = gk::GetModelAnimationMappingInfo(model, unchanged, 0) == -1 && unchanged.targetBoneCount == 11 && unchanged.mappedBoneCount == 12 && unchanged.humanoidBoneCount == 13 && unchanged.mappedHumanoidBoneCount == 14 && passed;
    passed = gk::GetModelAnimationMissingHumanoidRole(model, 0, 0) == gk::EHumanoidBone::None && passed;
    passed = gk::GetModelAnimationMappingInfo({}, unchanged, 0) == -1 && unchanged.targetBoneCount == 11 && unchanged.mappedBoneCount == 12 && unchanged.humanoidBoneCount == 13 && unchanged.mappedHumanoidBoneCount == 14 && passed;
    passed = gk::GetModelAnimationMappingInfo(model, unchanged, 2) == -1 && unchanged.targetBoneCount == 11 && unchanged.mappedBoneCount == 12 && unchanged.humanoidBoneCount == 13 && unchanged.mappedHumanoidBoneCount == 14 && passed;
    fs::remove_all(directory);
    if (!passed)
    {
        fprintf(stderr, "animation mapping diagnostics snapshot or invalid-query contract failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

bool WriteHumanoidProfile(const std::filesystem::path& path, const char* contents, std::string& utf8Path)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !(file << contents))
    {
        return false;
    }
    file.close();
    const auto encoded = path.u8string();
    utf8Path.assign(encoded.begin(), encoded.end());
    return true;
}

/**
 * 全置換、失敗時保持、instance分離、bind時点固定を公開APIで確認する。
 */
bool TestHumanoidProfiles(gk::ModelHandle model, gk::ModelHandle instance)
{
    namespace fs = std::filesystem;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = fs::temp_directory_path() / ("gkcore-humanoid-profile-" + std::to_string(suffix));
    fs::create_directories(directory);
    const auto profileOnePath = directory / "profile-one.txt";
    const auto profileTwoPath = directory / "profile-two.txt";
    const auto partialPath = directory / "partial.txt";
    const auto unknownPath = directory / "unknown.txt";
    const auto duplicatePath = directory / "duplicate.txt";
    std::string profileOne, profileTwo, partial, unknown, duplicate;
    const bool fixturesReady = WriteHumanoidProfile(profileOnePath, "# 人型profile\r\nHips\tjoint_a\r\nLeftUpperLeg\tjoint_b\r\nLeftLowerLeg\tjoint_c\r\n", profileOne) && WriteHumanoidProfile(profileTwoPath, "Hips\tjoint_a\nLeftUpperLeg\tjoint_c\nLeftLowerLeg\tjoint_b\n", profileTwo) && WriteHumanoidProfile(partialPath, "Hips\tjoint_a\n", partial) && WriteHumanoidProfile(unknownPath, "Hips\tjoint_a\nLeftUpperLeg\tmissing\n", unknown) && WriteHumanoidProfile(duplicatePath, "Hips\tjoint_a\nHips\tjoint_b\n", duplicate);
    if (!fixturesReady)
    {
        fs::remove_all(directory);
        fprintf(stderr, "humanoid profile fixtures could not be written\n");
        return false;
    }
    const auto animation = CreateRigAnimation();
    if (!animation.IsValid())
    {
        fs::remove_all(directory);
        return false;
    }
    bool passed = true;
    passed = gk::SetModelHumanoidBoneMap(model, profileOne.c_str()) == 0 && passed;
    passed = gk::GetModelBoneRole(model, 0) == gk::EHumanoidBone::Hips && gk::GetModelBoneRole(model, 1) == gk::EHumanoidBone::LeftUpperLeg && gk::GetModelBoneRole(model, 2) == gk::EHumanoidBone::LeftLowerLeg && passed;
    passed = gk::SetModelHumanoidBoneMap(model, partial.c_str()) == 0 && passed;
    passed = gk::GetModelBoneRole(model, 0) == gk::EHumanoidBone::Hips && gk::GetModelBoneRole(model, 1) == gk::EHumanoidBone::None && gk::GetModelBoneRole(model, 2) == gk::EHumanoidBone::None && passed;
    passed = gk::SetModelHumanoidBoneMap(model, profileOne.c_str()) == 0 && passed;
    passed = gk::SetAnimationHumanoidBoneMap(animation, profileOne.c_str()) == 0 && passed;
    passed = gk::GetAnimationBoneRole(animation, 0) == gk::EHumanoidBone::Hips && gk::GetAnimationBoneRole(animation, 1) == gk::EHumanoidBone::LeftUpperLeg && gk::GetAnimationBoneRole(animation, 2) == gk::EHumanoidBone::LeftLowerLeg && passed;
    passed = gk::SetAnimationHumanoidBoneMap(animation, partial.c_str()) == 0 && passed;
    passed = gk::GetAnimationBoneRole(animation, 0) == gk::EHumanoidBone::Hips && gk::GetAnimationBoneRole(animation, 1) == gk::EHumanoidBone::None && gk::GetAnimationBoneRole(animation, 2) == gk::EHumanoidBone::None && passed;
    passed = gk::SetAnimationHumanoidBoneMap(animation, profileOne.c_str()) == 0 && passed;
    passed = gk::SetModelHumanoidBoneMap({}, profileOne.c_str()) == -1 && gk::SetAnimationHumanoidBoneMap({}, profileOne.c_str()) == -1 && passed;
    passed = gk::SetModelHumanoidBoneMap(model, unknown.c_str()) == -1 && gk::SetModelHumanoidBoneMap(model, duplicate.c_str()) == -1 && passed;
    passed = gk::GetModelBoneRole(model, 0) == gk::EHumanoidBone::Hips && gk::GetModelBoneRole(model, 1) == gk::EHumanoidBone::LeftUpperLeg && gk::GetModelBoneRole(model, 2) == gk::EHumanoidBone::LeftLowerLeg && passed;
    passed = gk::SetAnimationHumanoidBoneMap(animation, unknown.c_str()) == -1 && gk::SetAnimationHumanoidBoneMap(animation, duplicate.c_str()) == -1 && passed;
    passed = gk::GetAnimationBoneRole(animation, 0) == gk::EHumanoidBone::Hips && gk::GetAnimationBoneRole(animation, 1) == gk::EHumanoidBone::LeftUpperLeg && gk::GetAnimationBoneRole(animation, 2) == gk::EHumanoidBone::LeftLowerLeg && passed;
    passed = gk::SetModelHumanoidBoneMap(instance, profileTwo.c_str()) == 0 && passed;
    passed = gk::GetModelBoneRole(model, 1) == gk::EHumanoidBone::LeftUpperLeg && gk::GetModelBoneRole(instance, 1) == gk::EHumanoidBone::LeftLowerLeg && passed;
    passed = gk::ApplyModelAnimation(model, animation, 0, false) == 0 && passed;
    passed = gk::GetModelAnimationSourceBone(model, 0) == 0 && gk::GetModelAnimationSourceBone(model, 1) == 1 && gk::GetModelAnimationSourceBone(model, 2) == 2 && passed;
    passed = gk::SetModelHumanoidBoneMap(model, profileTwo.c_str()) == 0 && passed;
    passed = gk::GetModelAnimationSourceBone(model, 0) == 0 && gk::GetModelAnimationSourceBone(model, 1) == 1 && gk::GetModelAnimationSourceBone(model, 2) == 2 && passed;
    passed = gk::ApplyModelAnimation(model, animation, 0, false) == 0 && passed;
    passed = gk::GetModelAnimationSourceBone(model, 0) == 0 && gk::GetModelAnimationSourceBone(model, 1) == 2 && gk::GetModelAnimationSourceBone(model, 2) == 1 && passed;
    passed = gk::SetModelHumanoidBoneMap(model, profileOne.c_str()) == 0 && passed;
    passed = gk::SetAnimationHumanoidBoneMap(animation, profileTwo.c_str()) == 0 && passed;
    passed = gk::GetModelAnimationSourceBone(model, 1) == 2 && gk::GetModelAnimationSourceBone(model, 2) == 1 && passed;
    passed = gk::ApplyModelAnimation(model, animation, 0, false) == 0 && passed;
    passed = gk::GetModelAnimationSourceBone(model, 0) == 0 && gk::GetModelAnimationSourceBone(model, 1) == 2 && gk::GetModelAnimationSourceBone(model, 2) == 1 && passed;
    fs::remove(profileOnePath);
    fs::remove(profileTwoPath);
    fs::remove(partialPath);
    fs::remove(unknownPath);
    fs::remove(duplicatePath);
    passed = gk::GetModelBoneRole(model, 1) == gk::EHumanoidBone::LeftUpperLeg && gk::GetAnimationBoneRole(animation, 1) == gk::EHumanoidBone::LeftLowerLeg && gk::GetAnimationBoneRole(animation, 2) == gk::EHumanoidBone::LeftUpperLeg && passed;
    passed = gk::GetModelAnimationSourceBone(model, 0) == 0 && gk::GetModelAnimationSourceBone(model, 1) == 2 && gk::GetModelAnimationSourceBone(model, 2) == 1 && passed;
    passed = gk::ApplyModelAnimation(model, animation, 0, false) == 0 && passed;
    passed = gk::DeleteModelAnimation(animation) == 0 && passed;
    passed = gk::ApplyModelAnimation(model, animation, 0, false) == -1 && passed;
    passed = gk::GetModelAnimationSourceBone(model, 0) == 0 && gk::GetModelAnimationSourceBone(model, 1) == 2 && gk::GetModelAnimationSourceBone(model, 2) == 1 && passed;
    fs::remove_all(directory);
    if (!passed)
    {
        fprintf(stderr, "humanoid profile replacement, failure atomicity, instance isolation, or bind snapshot failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * 名前に依存しない役割指定、描画結果、失敗時保持、instanceごとの分離を確認する。
 */
bool TestHumanoidTwoBoneIk(gk::ModelHandle model, gk::ModelHandle instance)
{
    const gk::EHumanoidBone modelRoles[3] = { gk::EHumanoidBone::Hips, gk::EHumanoidBone::LeftUpperLeg, gk::EHumanoidBone::LeftLowerLeg };
    const gk::EHumanoidBone instanceRoles[3] = { gk::EHumanoidBone::Spine, gk::EHumanoidBone::Chest, gk::EHumanoidBone::Neck };
    for (uint32_t bone = 0; bone < 3; ++bone)
    {
        if (gk::SetModelBoneRole(model, bone, modelRoles[bone]) != 0 || gk::SetModelBoneRole(instance, bone, instanceRoles[bone]) != 0)
        {
            return false;
        }
    }
    const gk::Vec3 pole{ 0.0f, 1.0f, 0.0f };
    const gk::Vec3 target{ 1.0f, 1.0f, 0.0f };
    bool passed = gk::SetModelHumanoidTwoBoneIk(model, modelRoles[0], modelRoles[1], modelRoles[2], target, pole) == 0;
    passed = gk::SetModelHumanoidTwoBoneIk(instance, instanceRoles[0], instanceRoles[1], instanceRoles[2], target, pole) == 0 && passed;
    const auto* modelTransform = gk::detail::FindModelTransform(model);
    const auto* instanceTransform = gk::detail::FindModelTransform(instance);
    const auto* modelPlayback = modelTransform ? modelTransform->playback : nullptr;
    const auto* instancePlayback = instanceTransform ? instanceTransform->playback : nullptr;
    if (!modelPlayback || !instancePlayback || modelPlayback->ik.Count() != 1 || instancePlayback->ik.Count() != 1 || modelPlayback->ikBones.Count() != 3 || instancePlayback->ikBones.Count() != 3)
    {
        passed = false;
    }
    else
        passed = modelPlayback->ikBones.At(0) == 0 && modelPlayback->ikBones.At(1) == 1 && modelPlayback->ikBones.At(2) == 2 && instancePlayback->ikBones.At(0) == 0 && instancePlayback->ikBones.At(1) == 1 && instancePlayback->ikBones.At(2) == 2 && passed;

    // 違うinstanceにだけ割り当てた役割は、このmodelでは解決できない。
    passed = gk::SetModelHumanoidTwoBoneIk(model, instanceRoles[0], instanceRoles[1], instanceRoles[2], { 0.0f, 1.0f, 0.0f }, pole) == -1 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk(instance, modelRoles[0], modelRoles[1], modelRoles[2], { 0.0f, 1.0f, 0.0f }, pole) == -1 && passed;
    // None、終端値、範囲外値、同じ役割の再指定は、既存IKを保つ。
    passed = gk::SetModelHumanoidTwoBoneIk(model, gk::EHumanoidBone::None, modelRoles[1], modelRoles[2], {}, pole) == -1 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk(model, static_cast<gk::EHumanoidBone>(gk::EHumanoidBone::Count), modelRoles[1], modelRoles[2], {}, pole) == -1 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk(model, static_cast<gk::EHumanoidBone>(0xffffu), modelRoles[1], modelRoles[2], {}, pole) == -1 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk(model, modelRoles[0], modelRoles[0], modelRoles[2], {}, pole) == -1 && passed;
    // NaNの入力と範囲外weightも失敗し、直前のIKを保つ。
    passed = gk::SetModelHumanoidTwoBoneIk(model, modelRoles[0], modelRoles[1], modelRoles[2], { std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f }, pole) == -1 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk(model, modelRoles[0], modelRoles[1], modelRoles[2], target, { 0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f }) == -1 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk(model, modelRoles[0], modelRoles[1], modelRoles[2], target, pole, -0.01f) == -1 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk(model, modelRoles[0], modelRoles[1], modelRoles[2], target, pole, 1.01f) == -1 && passed;
    // 役割未設定の新instanceと、削除済みhandleを受け付けない。
    const auto unmappedInstance = gk::CreateModelInstance(model);
    passed = unmappedInstance.IsValid() && gk::SetModelHumanoidTwoBoneIk(unmappedInstance, modelRoles[0], modelRoles[1], modelRoles[2], target, pole) == -1 && passed;
    const auto deletedInstance = unmappedInstance;
    passed = gk::DeleteModel(unmappedInstance) == 0 && gk::SetModelHumanoidTwoBoneIk(deletedInstance, modelRoles[0], modelRoles[1], modelRoles[2], target, pole) == -1 && passed;
    // 役割は割り当て済みでも、親子順でない3節は受け付けない。
    passed = gk::SetModelBoneRole(model, 1, gk::EHumanoidBone::None) == 0 && gk::SetModelBoneRole(model, 2, gk::EHumanoidBone::None) == 0 && passed;
    passed = gk::SetModelBoneRole(model, 1, modelRoles[2]) == 0 && gk::SetModelBoneRole(model, 2, modelRoles[1]) == 0 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk(model, modelRoles[0], modelRoles[1], modelRoles[2], {}, pole) == -1 && passed;
    passed = gk::SetModelBoneRole(model, 1, gk::EHumanoidBone::None) == 0 && gk::SetModelBoneRole(model, 2, gk::EHumanoidBone::None) == 0 && passed;
    passed = gk::SetModelBoneRole(model, 1, modelRoles[1]) == 0 && gk::SetModelBoneRole(model, 2, modelRoles[2]) == 0 && passed;
    // 呼び出し失敗は直前の目標と役割列を変えない。
    modelTransform = gk::detail::FindModelTransform(model);
    modelPlayback = modelTransform ? modelTransform->playback : nullptr;
    passed = modelPlayback && modelPlayback->ik.Count() == 1 && modelPlayback->ikBones.Count() == 3 && modelPlayback->ik.At(0).target[0] == target.x && modelPlayback->ik.At(0).target[1] == target.y && modelPlayback->ik.At(0).target[2] == target.z && modelPlayback->ikBones.At(0) == 0 && modelPlayback->ikBones.At(1) == 1 && modelPlayback->ikBones.At(2) == 2 && passed;
    passed = gk::SetModelHumanoidTwoBoneIk({}, modelRoles[0], modelRoles[1], modelRoles[2], target, pole) == -1 && passed;

    // 成功後に役割を解除しても、確定済みの骨番号で到達点を維持する。
    for (uint32_t bone = 0; bone < 3; ++bone)
    {
        passed = gk::SetModelBoneRole(model, bone, gk::EHumanoidBone::None) == 0 && gk::SetModelBoneRole(instance, bone, gk::EHumanoidBone::None) == 0 && passed;
    }
    // 独立した幾何期待値として、2節長が1の鎖の到達点を描画頂点で確認する。
    passed = gk::BeginFrame() == 0 && gk::DrawModel(model) == 0 && gk::DrawModel(instance) == 0 && passed;
    const auto& draws = gk::detail::GetContext().frame.draws;
    passed = draws.Count() == 2 && Near(draws.At(0).model->vertices.At(2).position[0], 1.0f) && Near(draws.At(0).model->vertices.At(2).position[1], 1.0f) && Near(draws.At(0).model->vertices.At(2).position[2], 0.0f) && Near(draws.At(1).model->vertices.At(2).position[0], 1.0f) && Near(draws.At(1).model->vertices.At(2).position[1], 1.0f) && Near(draws.At(1).model->vertices.At(2).position[2], 0.0f) && passed;
    passed = TestModelArmIkTargetMarker() && passed;
    if (!passed)
    {
        fprintf(stderr, "humanoid two-bone IK role resolution, failure atomicity, isolation, or endpoint contract failed: %s\n", gk::GetLastErrorMessage());
    }
    return passed;
}

/**
 * blend後のposeへIKを適用し、予約済みframe snapshotが変わらないことを確認する。
 */
bool TestBlendThenIkSnapshot()
{
    // 描画APIを初期化するテストbackend。
    gk::detail::SetBackendForTesting(new AAnimationRigTestBackend);
    if (gk::Init() != 0)
    {
        fprintf(stderr, "animation rig API Init failed: %s\n", gk::GetLastErrorMessage());
        return false;
    }
    // 頂点描画から独立した3節モデルresource。
    auto* resource = gk::detail::CreateModelResource();
    if (!resource)
    {
        fprintf(stderr, "animation rig model allocation failed\n");
        gk::Shutdown();
        return false;
    }
    const int32_t parents[3] = { -1, 0, 1 };
    gk::model::animation::FModelBoneTransform rest[3]{};
    rest[1].position[0] = 1.0f;
    rest[2].position[0] = 1.0f;
    auto* source = new AAnimationRigTestSource;
    if (!source->skeleton.parents.AppendRange(parents, 3) || !source->skeleton.restLocalTransforms.AppendRange(rest, 3))
    {
        delete source;
        gk::Release(&resource->reference);
        fprintf(stderr, "animation rig skeleton allocation failed\n");
        gk::Shutdown();
        return false;
    }
    gk::String error;
    resource->animation = gk::model::CreateModelAnimationAsset(source, error);
    gk::detail::ModelVertex vertices[3]{};
    if (!resource->animation || !resource->vertices.AppendRange(vertices, 3))
    {
        gk::Release(&resource->reference);
        fprintf(stderr, "animation rig model payload allocation failed: %s\n", error.CStr());
        gk::Shutdown();
        return false;
    }
    // 直接登録したbase modelにもinstance API用のtransform entryを用意する。
    const auto model = gk::detail::RegisterModelResource(resource, error);
    gk::detail::ModelTransform baseTransform{};
    baseTransform.handle = model;
    baseTransform.scale = { 1.0f, 1.0f, 1.0f };
    const bool transformAdded = model.IsValid() && gk::detail::GetContext().modelTransforms.Append(baseTransform);
    const auto instance = transformAdded ? gk::CreateModelInstance(model) : gk::ModelHandle{};
    if (!transformAdded || !instance.IsValid() || !TestHumanoidProfiles(model, instance) || !TestMappingDiagnostics(model) || gk::PlayModelAnimation(instance, 0, false) != 0 || gk::SetModelAnimationBlend(instance, 1, 0.5f) != 0)
    {
        fprintf(stderr, "animation rig playback setup failed: %s\n", gk::GetLastErrorMessage());
        if (instance.IsValid())
        {
            gk::DeleteModel(instance);
        }
        if (model.IsValid())
        {
            gk::DeleteModel(model);
        }
        gk::Shutdown();
        return false;
    }
    if (!TestModelSecondaryMotion(model, instance) || !TestModelSecondaryMotionColliders(model, instance, *source) || !TestSkinPointContactsSnapshot() || !TestSecondaryMotionPoseEvaluationBudget() || !TestModelBonePositionQuery(model, instance) || !TestModelBonePositionsQuery(instance, *source) || !TestModelArmIkPreview() || !TestMotionFollowingArmIkPreview() || !TestBuildArmIkTorsoFallback() || !TestAnimatedArmIkCachedBinding() || !TestParseMotionViewOptions())
    {
        gk::DeleteModel(instance);
        gk::DeleteModel(model);
        gk::Shutdown();
        return false;
    }
    const gk::Vec3 pole{ 0.0f, 1.0f, 0.0f };
    if (gk::SetModelTwoBoneIk(instance, 0, 1, 2, { 1.0f, 1.0f, 0.0f }, pole) != 0 || gk::BeginFrame() != 0 || gk::DrawModel(instance) != 0)
    {
        fprintf(stderr, "animation rig first IK snapshot failed: %s\n", gk::GetLastErrorMessage());
        gk::DeleteModel(instance);
        gk::DeleteModel(model);
        gk::Shutdown();
        return false;
    }
    const auto& draws = gk::detail::GetContext().frame.draws;
    gk::Vec3 queriedEndpoint{};
    if (draws.Count() != 1 || gk::GetModelBonePosition(instance, 2, queriedEndpoint) != 0 || !Near(draws.At(0).model->vertices.At(2).position[0], 1.0f) || !Near(draws.At(0).model->vertices.At(2).position[1], 1.0f) || !Near(draws.At(0).model->vertices.At(2).position[0], queriedEndpoint.x) || !Near(draws.At(0).model->vertices.At(2).position[1], queriedEndpoint.y) || !Near(draws.At(0).model->vertices.At(2).position[2], queriedEndpoint.z))
    {
        fprintf(stderr, "IK was not evaluated after clip blending\n");
        gk::DeleteModel(instance);
        gk::DeleteModel(model);
        gk::Shutdown();
        return false;
    }
    if (gk::SetModelTwoBoneIk(instance, 0, 1, 2, { 0.0f, 1.0f, 0.0f }, pole) != 0 || gk::DrawModel(instance) != 0 || draws.Count() != 2 || !Near(draws.At(0).model->vertices.At(2).position[0], 1.0f) || !Near(draws.At(0).model->vertices.At(2).position[1], 1.0f) || !Near(draws.At(1).model->vertices.At(2).position[0], 0.0f) || !Near(draws.At(1).model->vertices.At(2).position[1], 1.0f))
    {
        const auto* transform = gk::detail::FindModelTransform(instance);
        const auto* command = transform && transform->playback && transform->playback->ik.Count() ? &transform->playback->ik.At(0) : nullptr;
        fprintf(stderr, "queued animation snapshot changed: target=(%.3f,%.3f) first=(%.3f,%.3f) second root=(%.3f,%.3f) middle=(%.3f,%.3f) end=(%.3f,%.3f) count=%u\n", command ? command->target[0] : -99.0f, command ? command->target[1] : -99.0f, draws.Count() > 0 ? draws.At(0).model->vertices.At(2).position[0] : -99.0f, draws.Count() > 0 ? draws.At(0).model->vertices.At(2).position[1] : -99.0f, draws.Count() > 1 ? draws.At(1).model->vertices.At(0).position[0] : -99.0f, draws.Count() > 1 ? draws.At(1).model->vertices.At(0).position[1] : -99.0f, draws.Count() > 1 ? draws.At(1).model->vertices.At(1).position[0] : -99.0f, draws.Count() > 1 ? draws.At(1).model->vertices.At(1).position[1] : -99.0f, draws.Count() > 1 ? draws.At(1).model->vertices.At(2).position[0] : -99.0f, draws.Count() > 1 ? draws.At(1).model->vertices.At(2).position[1] : -99.0f, draws.Count());
        gk::DeleteModel(instance);
        gk::DeleteModel(model);
        gk::Shutdown();
        return false;
    }
    if (gk::Present() != 0 || gk::ClearModelIk(model) != 0 || gk::ClearModelIk(instance) != 0)
    {
        fprintf(stderr, "animation rig snapshot presentation or IK reset failed: %s\n", gk::GetLastErrorMessage());
        gk::DeleteModel(instance);
        gk::DeleteModel(model);
        gk::Shutdown();
        return false;
    }
    if (!TestHumanoidTwoBoneIk(model, instance))
    {
        gk::DeleteModel(instance);
        gk::DeleteModel(model);
        gk::Shutdown();
        return false;
    }
    const bool presented = gk::Present() == 0;
    const bool deleted = gk::DeleteModel(instance) == 0 && gk::DeleteModel(model) == 0;
    gk::Shutdown();
    if (!presented || !deleted)
    {
        fprintf(stderr, "animation rig frame cleanup failed: %s\n", gk::GetLastErrorMessage());
        return false;
    }
    return true;
}

}

/**
 * 複数接続の形状準備が半径の種類数だけになり、接続なしでは増えないことを確かめる。
 */
static bool TestSecondaryMotionLinkPreparationBudget()
{
    gk::model::FModelSecondaryMotionState candidate;
    gk::model::FModelSecondaryMotionState previous;
    gk::String error;
    for (uint32_t index = 0; index < 3; ++index)
    {
        auto* chain = new gk::model::FModelSecondaryMotionChain;
        const uint32_t bones[2] = { index * 2, index * 2 + 1 };
        const float origin = static_cast<float>(index * 3);
        const gk::Vec3 targets[3] = { { origin, 0.0f, 0.0f }, { origin + 1.0f, 0.0f, 0.0f }, { origin + 2.0f, 0.0f, 0.0f } };
        chain->settings.endOffset = { 1.0f, 0.0f, 0.0f };
        chain->settings.gravity = {};
        if (!chain->bones.AppendRange(bones, 2) || !gk::model::animation::ResetSecondaryMotionChain(targets, 3, chain->simulation, error) || !candidate.chains.Append(chain))
        {
            delete chain;
            return false;
        }
    }
    const gk::model::animation::FModelSecondaryMotionCollisionShape bodies[2] = { { { 100.0f, 0.0f, 0.0f }, { 100.0f, 1.0f, 0.0f }, 0.5f }, { { 200.0f, 0.0f, 0.0f }, { 200.0f, 0.0f, 0.0f }, 0.5f } };
    if (!candidate.links.Append(gk::FModelSecondaryMotionLink{ 1, 3, 0.1f }) || !candidate.links.Append(gk::FModelSecondaryMotionLink{ 3, 5, 0.1f }))
    {
        return false;
    }
    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    gk::model::animation::ResetSecondaryMotionCollisionPrepareCount();
    if (!gk::model::ConstrainSecondaryMotionLinks(candidate, previous, bodies, 2, 0.0, error) || gk::model::GetSecondaryMotionLinkResolutionCount() != 2 || gk::model::GetSecondaryMotionLinkShapePreparationCount() != 2 || gk::model::GetSecondaryMotionLinkChainConstraintCount() != 3)
    {
        fprintf(stderr, "same-radius links did not reuse two prepared bodies: %s\n", error.CStr());
        return false;
    }
    // 接続用2件、共有guard形状2件、保存後のraw検査2件×3鎖だけを準備する。
    if (gk::model::animation::GetSecondaryMotionCollisionPrepareCount() != 10)
    {
        fprintf(stderr, "linked chains did not share their step-shape preparation: count=%llu\n", static_cast<unsigned long long>(gk::model::animation::GetSecondaryMotionCollisionPrepareCount()));
        return false;
    }
    if (!candidate.links.Append(gk::FModelSecondaryMotionLink{ 5, 1, 0.2f }))
    {
        return false;
    }
    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    if (!gk::model::ConstrainSecondaryMotionLinks(candidate, previous, bodies, 2, 0.0, error) || gk::model::GetSecondaryMotionLinkResolutionCount() != 3 || gk::model::GetSecondaryMotionLinkShapePreparationCount() != 4 || gk::model::GetSecondaryMotionLinkChainConstraintCount() != 3)
    {
        fprintf(stderr, "different link radii did not prepare separate body groups: %s\n", error.CStr());
        return false;
    }
    const gk::Vec3 before = candidate.chains.At(0)->simulation.points.At(1).position;
    error.Assign("temporary capacity for the expected allocation-failure diagnostic, retained when the prepared-link stage clears this message");
    gk::SetAllocationFailureAfterForTesting(0);
    const bool allocationRejected = !gk::model::ConstrainSecondaryMotionLinks(candidate, previous, bodies, 2, 0.0, error);
    gk::ResetAllocationFailureForTesting();
    if (!allocationRejected || error.Empty() || memcmp(&before, &candidate.chains.At(0)->simulation.points.At(1).position, sizeof(before)) != 0)
    {
        fprintf(stderr, "link-cache allocation failure changed position or lost the error: %s\n", error.CStr());
        return false;
    }
    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    if (!gk::model::ConstrainSecondaryMotionLinks(candidate, previous, nullptr, 0, 0.0, error) || gk::model::GetSecondaryMotionLinkResolutionCount() != 0 || gk::model::GetSecondaryMotionLinkShapePreparationCount() != 0)
    {
        return false;
    }
    candidate.links.Clear();
    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    return gk::model::ConstrainSecondaryMotionLinks(candidate, previous, bodies, 2, 0.0, error) && gk::model::GetSecondaryMotionLinkResolutionCount() == 0 && gk::model::GetSecondaryMotionLinkShapePreparationCount() == 0;
}

/**
 * 接触鎖と遠い線分鎖を同じlink更新へ登録する検査状態を作る。
 */
static bool MakeSelectiveLinkConstraintFixture(gk::model::FModelSecondaryMotionState& state, uint32_t chainCount)
{
    if (chainCount != 2 && chainCount != 4)
    {
        return false;
    }
    for (uint32_t index = 0; index < chainCount; ++index)
    {
        auto* chain = new gk::model::FModelSecondaryMotionChain;
        const uint32_t bones[2] = { index * 2u, index * 2u + 1u };
        const float originX = index < 2 ? 1.0f : 10.0f;
        const float laneY = static_cast<float>(index % 2u);
        const gk::Vec3 targets[3] = { { originX, laneY, 0.0f }, { originX + 1.0f, laneY, 0.0f }, { originX + 2.0f, laneY, 0.0f } };
        chain->settings.gravity = {};
        chain->settings.maxAngleDegrees = 90.0f;
        chain->settings.endOffset = { 1.0f, 0.0f, 0.0f };
        chain->settings.constraintIterations = 8;
        gk::String error;
        const bool chainReady = chain->bones.AppendRange(bones, 2) && gk::model::animation::ResetSecondaryMotionChain(targets, 3, chain->simulation, error) && state.chains.Append(chain);
        if (!chainReady)
        {
            delete chain;
            return false;
        }
    }
    const gk::FModelSecondaryMotionLink contactLink{ 1, 3, 0.02f };
    if (!state.links.Append(contactLink))
    {
        return false;
    }
    if (chainCount == 4)
    {
        const gk::FModelSecondaryMotionLink clearLink{ 5, 7, 0.02f };
        if (!state.links.Append(clearLink))
        {
            return false;
        }
    }
    state.linkIterations = 32;
    return true;
}

/**
 * 遠いlinkでは初回だけ制約を解き、接触鎖の結果は追加鎖の有無で変えない。
 */
static bool TestSecondaryMotionLinkConstraintOnlyChangedChains()
{
    using namespace gk::model::animation;
    gk::model::FModelSecondaryMotionState contactOnly;
    gk::model::FModelSecondaryMotionState combined;
    if (!MakeSelectiveLinkConstraintFixture(contactOnly, 2) || !MakeSelectiveLinkConstraintFixture(combined, 4))
    {
        fprintf(stderr, "could not initialize selective-link constraint fixtures\n");
        return false;
    }
    const FModelSecondaryMotionCollisionShape body = { { 2.0f, 0.5f, 0.0f }, { 2.0f, 0.5f, 0.0f }, 0.2f };
    gk::String error;
    gk::model::FModelSecondaryMotionState previous;
    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    if (!gk::model::ConstrainSecondaryMotionLinks(contactOnly, previous, &body, 1, 0.0, error))
    {
        fprintf(stderr, "contact-only link fixture did not converge: %s\n", error.CStr());
        return false;
    }
    const uint64_t contactOnlyCalls = gk::model::GetSecondaryMotionLinkChainConstraintCount();
    if (contactOnlyCalls < 4)
    {
        fprintf(stderr, "contact-only link fixture did not require multiple passes: calls=%llu\n", static_cast<unsigned long long>(contactOnlyCalls));
        return false;
    }

    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    if (!gk::model::ConstrainSecondaryMotionLinks(combined, previous, &body, 1, 0.0, error))
    {
        fprintf(stderr, "combined selective-link fixture did not converge: %s\n", error.CStr());
        return false;
    }
    const uint64_t combinedCalls = gk::model::GetSecondaryMotionLinkChainConstraintCount();
    if (combinedCalls != contactOnlyCalls + 2u)
    {
        fprintf(stderr, "clear-link chains were constrained on later passes: contact-only=%llu combined=%llu\n", static_cast<unsigned long long>(contactOnlyCalls), static_cast<unsigned long long>(combinedCalls));
        return false;
    }

    const auto SameChainSimulation = [](const gk::model::FModelSecondaryMotionChain& first, const gk::model::FModelSecondaryMotionChain& second) { return first.simulation.points.Count() == second.simulation.points.Count() && first.simulation.previousTargets.Count() == second.simulation.previousTargets.Count() && memcmp(first.simulation.points.Data(), second.simulation.points.Data(), static_cast<size_t>(first.simulation.points.Count()) * sizeof(FModelSecondaryMotionPoint)) == 0 && memcmp(first.simulation.previousTargets.Data(), second.simulation.previousTargets.Data(), static_cast<size_t>(first.simulation.previousTargets.Count()) * sizeof(gk::Vec3)) == 0; };
    if (!SameChainSimulation(*contactOnly.chains.At(0), *combined.chains.At(0)) || !SameChainSimulation(*contactOnly.chains.At(1), *combined.chains.At(1)))
    {
        fprintf(stderr, "far clear-link chains changed the connected contact-chain state\n");
        return false;
    }

    const auto Distance3 = [](gk::Vec3 first, gk::Vec3 second)
    {
        const double x = static_cast<double>(first.x) - second.x;
        const double y = static_cast<double>(first.y) - second.y;
        const double z = static_cast<double>(first.z) - second.z;
        return sqrt(x * x + y * y + z * z);
    };
    const auto Dot3 = [](gk::Vec3 first, gk::Vec3 second) { return static_cast<double>(first.x) * second.x + static_cast<double>(first.y) * second.y + static_cast<double>(first.z) * second.z; };
    for (uint32_t chainIndex = 0; chainIndex < 4; ++chainIndex)
    {
        const auto& chain = *combined.chains.At(chainIndex);
        const float originX = chainIndex < 2 ? 1.0f : 10.0f;
        const float laneY = static_cast<float>(chainIndex % 2u);
        const gk::Vec3 expectedTargets[3] = { { originX, laneY, 0.0f }, { originX + 1.0f, laneY, 0.0f }, { originX + 2.0f, laneY, 0.0f } };
        if (chain.simulation.points.Count() != 3 || chain.simulation.previousTargets.Count() != 3)
        {
            fprintf(stderr, "selective-link chain state has an invalid point count: chain=%u\n", chainIndex);
            return false;
        }
        for (uint32_t point = 0; point < 3; ++point)
        {
            const auto& value = chain.simulation.points.At(point);
            const auto& target = chain.simulation.previousTargets.At(point);
            if (!isfinite(value.position.x) || !isfinite(value.position.y) || !isfinite(value.position.z) || !isfinite(value.velocity.x) || !isfinite(value.velocity.y) || !isfinite(value.velocity.z) || memcmp(&target, &expectedTargets[point], sizeof(target)) != 0)
            {
                fprintf(stderr, "selective-link chain has invalid values or changed targets: chain=%u point=%u\n", chainIndex, point);
                return false;
            }
            if (chainIndex >= 2 && (memcmp(&value.position, &expectedTargets[point], sizeof(value.position)) != 0 || value.velocity.x != 0.0f || value.velocity.y != 0.0f || value.velocity.z != 0.0f))
            {
                fprintf(stderr, "far clear-link chain position or velocity changed: chain=%u point=%u\n", chainIndex, point);
                return false;
            }
        }
        for (uint32_t segment = 1; segment < 3; ++segment)
        {
            const auto& first = chain.simulation.points.At(segment - 1u).position;
            const auto& second = chain.simulation.points.At(segment).position;
            const auto& firstTarget = chain.simulation.previousTargets.At(segment - 1u);
            const auto& secondTarget = chain.simulation.previousTargets.At(segment);
            const double solvedLength = Distance3(first, second);
            const double targetLength = Distance3(firstTarget, secondTarget);
            const gk::Vec3 solvedAxis{ second.x - first.x, second.y - first.y, second.z - first.z };
            const gk::Vec3 targetAxis{ secondTarget.x - firstTarget.x, secondTarget.y - firstTarget.y, secondTarget.z - firstTarget.z };
            if (!(solvedLength > 0.0) || fabs(solvedLength - targetLength) > targetLength * 0.001 || Dot3(solvedAxis, targetAxis) / (solvedLength * targetLength) < -0.002)
            {
                fprintf(stderr, "selective-link chain violated length or angle: chain=%u segment=%u\n", chainIndex, segment);
                return false;
            }
        }
        const gk::FVector3d points[3] = { { { chain.simulation.points.At(0).position.x, chain.simulation.points.At(0).position.y, chain.simulation.points.At(0).position.z } }, { { chain.simulation.points.At(1).position.x, chain.simulation.points.At(1).position.y, chain.simulation.points.At(1).position.z } }, { { chain.simulation.points.At(2).position.x, chain.simulation.points.At(2).position.y, chain.simulation.points.At(2).position.z } } };
        if (!CheckSecondaryMotionContacts(&body, 1, points, 3, error))
        {
            fprintf(stderr, "selective-link chain left body contact: chain=%u error=%s\n", chainIndex, error.CStr());
            return false;
        }
    }
    const auto& firstLink = combined.chains.At(0)->simulation.points.At(1).position;
    const auto& secondLink = combined.chains.At(1)->simulation.points.At(1).position;
    const gk::Vec3 linkAxis{ secondLink.x - firstLink.x, secondLink.y - firstLink.y, secondLink.z - firstLink.z };
    const gk::Vec3 linkOffset{ body.start.x - firstLink.x, body.start.y - firstLink.y, body.start.z - firstLink.z };
    const double linkAxisLengthSquared = Dot3(linkAxis, linkAxis);
    const double linkAmount = linkAxisLengthSquared > 0.0 ? fmax(0.0, fmin(1.0, Dot3(linkOffset, linkAxis) / linkAxisLengthSquared)) : 0.0;
    const gk::Vec3 linkClosest{ firstLink.x + static_cast<float>(linkAmount) * linkAxis.x, firstLink.y + static_cast<float>(linkAmount) * linkAxis.y, firstLink.z + static_cast<float>(linkAmount) * linkAxis.z };
    if (Distance3(body.start, linkClosest) < (static_cast<double>(body.radius) + 0.02) * 0.999)
    {
        fprintf(stderr, "independent link distance remained inside the expanded sphere\n");
        return false;
    }

    // 正のdeltaでは接触鎖だけ補正速度を受け、遠い鎖の目標と速度を保つ。
    gk::model::FModelSecondaryMotionState movingCandidate;
    gk::model::FModelSecondaryMotionState movingPrevious;
    if (!MakeSelectiveLinkConstraintFixture(movingCandidate, 4) || !MakeSelectiveLinkConstraintFixture(movingPrevious, 4))
    {
        fprintf(stderr, "could not initialize positive-delta link fixtures\n");
        return false;
    }
    movingCandidate.chains.At(2)->simulation.points.At(2).velocity.x = -0.0f;
    movingPrevious.chains.At(2)->simulation.points.At(2).velocity.x = -0.0f;
    movingCandidate.chains.At(2)->simulation.points.At(2).velocity.y = 3.25f;
    movingPrevious.chains.At(2)->simulation.points.At(2).velocity.y = 3.25f;
    if (!ConstrainSecondaryMotionLinks(movingCandidate, movingPrevious, &body, 1, 1.0 / 60.0, error))
    {
        fprintf(stderr, "positive-delta link fixture did not converge: %s\n", error.CStr());
        return false;
    }
    for (uint32_t chainIndex = 2; chainIndex < 4; ++chainIndex)
    {
        const auto& candidateChain = *movingCandidate.chains.At(chainIndex);
        const auto& previousChain = *movingPrevious.chains.At(chainIndex);
        for (uint32_t point = 0; point < candidateChain.simulation.points.Count(); ++point)
        {
            const auto& value = candidateChain.simulation.points.At(point);
            const auto& oldValue = previousChain.simulation.points.At(point);
            if (memcmp(&candidateChain.simulation.previousTargets.At(point), &previousChain.simulation.previousTargets.At(point), sizeof(gk::Vec3)) != 0 || memcmp(&value.position, &oldValue.position, sizeof(gk::Vec3)) != 0 || !isfinite(value.velocity.x) || !isfinite(value.velocity.y) || !isfinite(value.velocity.z) || value.velocity.x != oldValue.velocity.x || value.velocity.y != oldValue.velocity.y || value.velocity.z != oldValue.velocity.z)
            {
                fprintf(stderr, "positive-delta far chain changed targets or velocity: chain=%u point=%u\n", chainIndex, point);
                return false;
            }
        }
        const auto& newVelocity = candidateChain.simulation.points.At(2).velocity;
        // 従来の補正速度加算による -0 + 0 の結果も、skip対象の遠い鎖へ適用する。
        const float positiveZero = 0.0f;
        if (memcmp(&newVelocity.x, &positiveZero, sizeof(positiveZero)) != 0)
        {
            fprintf(stderr, "positive-delta far chain skipped the signed-zero velocity addition: chain=%u\n", chainIndex);
            return false;
        }
    }

    // 初回passでは接続した遠い鎖も一度制約し、わずかな長さ崩れを戻す。
    gk::model::FModelSecondaryMotionState restoredFarChain;
    if (!MakeSelectiveLinkConstraintFixture(restoredFarChain, 4))
    {
        return false;
    }
    restoredFarChain.chains.At(2)->simulation.points.At(2).position.x += 0.2f;
    if (!ConstrainSecondaryMotionLinks(restoredFarChain, movingPrevious, &body, 1, 0.0, error))
    {
        fprintf(stderr, "first-pass far-chain restoration fixture failed: %s\n", error.CStr());
        return false;
    }
    const auto& restoredPoint = restoredFarChain.chains.At(2)->simulation.points.At(2).position;
    const auto& restoredTarget = restoredFarChain.chains.At(2)->simulation.previousTargets.At(2);
    if (memcmp(&restoredPoint, &restoredTarget, sizeof(restoredPoint)) != 0)
    {
        fprintf(stderr, "first link pass did not restore the far chain target\n");
        return false;
    }

    // 独立した2本が同時に不成立でも、両方の接触を同じ反復枠で解く。
    gk::model::FModelSecondaryMotionState simultaneousContacts;
    if (!MakeSelectiveLinkConstraintFixture(simultaneousContacts, 4))
    {
        return false;
    }
    const FModelSecondaryMotionCollisionShape bodies[2] = { body, { { 11.0f, 0.5f, 0.0f }, { 11.0f, 0.5f, 0.0f }, 0.2f } };
    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    if (!gk::model::ConstrainSecondaryMotionLinks(simultaneousContacts, previous, bodies, 2, 0.0, error) || gk::model::GetSecondaryMotionLinkChainConstraintCount() != contactOnlyCalls * 2u)
    {
        fprintf(stderr, "simultaneous link contacts did not restore both components: calls=%llu error=%s\n", static_cast<unsigned long long>(gk::model::GetSecondaryMotionLinkChainConstraintCount()), error.CStr());
        return false;
    }
    for (uint32_t pair = 0; pair < 2; ++pair)
    {
        const auto& first = simultaneousContacts.chains.At(pair * 2)->simulation.points.At(1).position;
        const auto& second = simultaneousContacts.chains.At(pair * 2 + 1)->simulation.points.At(1).position;
        if (!CheckSecondaryMotionFreeSegmentContacts(bodies, 2, 0.02f, { { first.x, first.y, first.z } }, { { second.x, second.y, second.z } }, error))
        {
            fprintf(stderr, "one simultaneous link contact remained unresolved: pair=%u error=%s\n", pair, error.CStr());
            return false;
        }
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    gk::model::FModelSecondaryMotionState invalidSettings;
    if (!MakeSelectiveLinkConstraintFixture(invalidSettings, 4))
    {
        return false;
    }
    invalidSettings.chains.At(2)->settings.frequencyHz = nan;
    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    if (gk::model::ConstrainSecondaryMotionLinks(invalidSettings, previous, &body, 1, 0.0, error) || error.Empty() || gk::model::GetSecondaryMotionLinkChainConstraintCount() < 3)
    {
        fprintf(stderr, "first pass skipped invalid settings on an otherwise clear link: calls=%llu error=%s\n", static_cast<unsigned long long>(gk::model::GetSecondaryMotionLinkChainConstraintCount()), error.CStr());
        return false;
    }

    gk::model::FModelSecondaryMotionState invalidState;
    if (!MakeSelectiveLinkConstraintFixture(invalidState, 4))
    {
        return false;
    }
    invalidState.chains.At(2)->simulation.points.At(2).velocity.x = nan;
    gk::model::ResetSecondaryMotionLinkPreparationCounts();
    if (gk::model::ConstrainSecondaryMotionLinks(invalidState, previous, &body, 1, 0.0, error) || error.Empty() || gk::model::GetSecondaryMotionLinkChainConstraintCount() < 3)
    {
        fprintf(stderr, "first pass skipped invalid state on an otherwise clear link: calls=%llu error=%s\n", static_cast<unsigned long long>(gk::model::GetSecondaryMotionLinkChainConstraintCount()), error.CStr());
        return false;
    }
    return true;
}

/**
 * body接触後の鎖復元と距離制約が有限反復で両立する既知可行姿勢を確かめる。
 */
static bool TestSecondaryMotionLinkLocalStallFeasiblePose()
{
    using namespace gk::model::animation;
    const double angle = 20.0 * 3.14159265358979323846 / 180.0;
    const double sine = sin(angle);
    const double cosine = cos(angle);
    const FModelSecondaryMotionCollisionShape body = { { 0.0f, -0.1f, 0.0f }, { 0.0f, 0.1f, 0.0f }, 0.2f };
    gk::String error;
    const auto Distance = [](const gk::Vec3& first, const gk::Vec3& second)
    {
        const double x = static_cast<double>(second.x) - first.x;
        const double y = static_cast<double>(second.y) - first.y;
        const double z = static_cast<double>(second.z) - first.z;
        return sqrt(x * x + y * y + z * z);
    };
    const auto Dot = [](const gk::Vec3& first, const gk::Vec3& second) { return static_cast<double>(first.x) * second.x + static_cast<double>(first.y) * second.y + static_cast<double>(first.z) * second.z; };
    const auto MakeFixture = [&](gk::model::FModelSecondaryMotionState& state, bool withDistance, bool reverseLink, const gk::Vec3& endOffset, const auto& transform)
    {
        for (uint32_t chainIndex = 0; chainIndex < 2; ++chainIndex)
        {
            auto* chain = new gk::model::FModelSecondaryMotionChain;
            const uint32_t bones[2] = { chainIndex * 2u, chainIndex * 2u + 1u };
            const float side = chainIndex == 0 ? -0.5f : 0.5f;
            const gk::Vec3 baseTargets[3] = { { side, 0.0f, -1.0f }, { side, 0.0f, 0.0f }, { side, 0.0f, 1.0f } };
            gk::Vec3 targets[3];
            for (uint32_t point = 0; point < 3; ++point)
            {
                targets[point] = transform(baseTargets[point]);
            }
            chain->settings.gravity = {};
            chain->settings.maxAngleDegrees = 20.0f;
            chain->settings.endOffset = endOffset;
            chain->settings.constraintIterations = 8;
            const bool ready = chain->bones.AppendRange(bones, 2) && ResetSecondaryMotionChain(targets, 3, chain->simulation, error) && state.chains.Append(chain);
            if (!ready)
            {
                delete chain;
                return false;
            }
        }
        const gk::FModelSecondaryMotionLink link = reverseLink ? gk::FModelSecondaryMotionLink{ 3, 1, 0.0f } : gk::FModelSecondaryMotionLink{ 1, 3, 0.0f };
        if (!state.links.Append(link))
        {
            return false;
        }
        state.linkIterations = 32;
        if (withDistance)
        {
            const gk::FModelSecondaryMotionDistanceConstraint constraint = reverseLink ? gk::FModelSecondaryMotionDistanceConstraint{ 3, 1, 1.0f, 0.001f } : gk::FModelSecondaryMotionDistanceConstraint{ 1, 3, 1.0f, 0.001f };
            if (!state.distanceConstraints.Append(constraint) || !state.distanceLambdas.Append(0.0))
            {
                return false;
            }
        }
        return true;
    };
    // ±20度で同じ高さへ傾けた2端点は、長さ1を保ちつつbody軸とlink半径0の両方を避ける。
    const gk::Vec3 feasibleFirstRoot = { -0.5f, 0.0f, -1.0f };
    const gk::Vec3 feasibleFirst = { -0.5f, static_cast<float>(sine), static_cast<float>(-1.0 + cosine) };
    const gk::Vec3 feasibleFirstTip = { -0.5f, static_cast<float>(2.0 * sine), static_cast<float>(-1.0 + 2.0 * cosine) };
    const gk::Vec3 feasibleSecondRoot = { 0.5f, 0.0f, -1.0f };
    const gk::Vec3 feasibleSecond = { 0.5f, static_cast<float>(sine), static_cast<float>(-1.0 + cosine) };
    const gk::Vec3 feasibleSecondTip = { 0.5f, static_cast<float>(2.0 * sine), static_cast<float>(-1.0 + 2.0 * cosine) };
    if (fabs(Distance(feasibleFirstRoot, feasibleFirst) - 1.0) > 1.0e-6 || fabs(Distance(feasibleFirst, feasibleFirstTip) - 1.0) > 1.0e-6 || fabs(Distance(feasibleSecondRoot, feasibleSecond) - 1.0) > 1.0e-6 || fabs(Distance(feasibleSecond, feasibleSecondTip) - 1.0) > 1.0e-6)
    {
        fprintf(stderr, "known feasible local-stall endpoints did not preserve unit bone lengths\n");
        return false;
    }
    if (fabs(Distance(feasibleFirst, feasibleSecond) - 1.0) > 1.0e-6 || fabs(feasibleFirst.x - body.start.x) <= body.radius || fabs(feasibleSecond.x - body.start.x) <= body.radius || fabs(feasibleFirstRoot.x - body.start.x) <= body.radius || fabs(feasibleSecondRoot.x - body.start.x) <= body.radius || fabs(feasibleFirstTip.x - body.start.x) <= body.radius || fabs(feasibleSecondTip.x - body.start.x) <= body.radius)
    {
        fprintf(stderr, "known feasible local-stall points did not preserve distance or point clearance\n");
        return false;
    }
    const gk::Vec3 firstDirection = { 0.0f, feasibleFirst.y - feasibleFirstRoot.y, feasibleFirst.z - feasibleFirstRoot.z };
    const gk::Vec3 secondDirection = { 0.0f, feasibleSecond.y - feasibleSecondRoot.y, feasibleSecond.z - feasibleSecondRoot.z };
    const double firstDirectionLength = sqrt(Dot(firstDirection, firstDirection));
    const double secondDirectionLength = sqrt(Dot(secondDirection, secondDirection));
    if (fabs(firstDirection.z / firstDirectionLength - cosine) > 1.0e-6 || fabs(secondDirection.z / secondDirectionLength - cosine) > 1.0e-6)
    {
        fprintf(stderr, "known feasible local-stall endpoints exceeded their 20-degree cone\n");
        return false;
    }
    const gk::Vec3 closestLinkPoint = { 0.0f, static_cast<float>(sine), static_cast<float>(-1.0 + cosine) };
    const double closestBodyDistance = sqrt(static_cast<double>(closestLinkPoint.y - 0.1f) * (closestLinkPoint.y - 0.1f) + static_cast<double>(closestLinkPoint.z) * closestLinkPoint.z);
    if (closestBodyDistance <= static_cast<double>(body.radius) + 0.001)
    {
        fprintf(stderr, "known feasible local-stall link still intersects the body capsule\n");
        return false;
    }

    const auto ValidateSolvedCandidate = [&](const gk::model::FModelSecondaryMotionState& candidate, const FModelSecondaryMotionCollisionShape& activeBody, bool withDistance, const char* label)
    {
        for (uint32_t chainIndex = 0; chainIndex < 2; ++chainIndex)
        {
            const auto& simulation = candidate.chains.At(chainIndex)->simulation;
            for (uint32_t segment = 1; segment < 3; ++segment)
            {
                const auto& first = simulation.points.At(segment - 1u).position;
                const auto& second = simulation.points.At(segment).position;
                const auto& firstTarget = simulation.previousTargets.At(segment - 1u);
                const auto& secondTarget = simulation.previousTargets.At(segment);
                const double solvedLength = Distance(first, second);
                const double targetLength = Distance(firstTarget, secondTarget);
                const gk::Vec3 solvedAxis = { second.x - first.x, second.y - first.y, second.z - first.z };
                const gk::Vec3 targetAxis = { secondTarget.x - firstTarget.x, secondTarget.y - firstTarget.y, secondTarget.z - firstTarget.z };
                if (fabs(solvedLength - targetLength) > targetLength * 0.001 || Dot(solvedAxis, targetAxis) / (solvedLength * targetLength) < cos(angle) - 0.001)
                {
                    fprintf(stderr, "%s violated bone length or cone: chain=%u segment=%u\n", label, chainIndex, segment);
                    return false;
                }
            }
            gk::FVector3d points[3];
            for (uint32_t point = 0; point < 3; ++point)
            {
                const auto& position = simulation.points.At(point).position;
                points[point] = { { position.x, position.y, position.z } };
            }
            if (!CheckSecondaryMotionContacts(&activeBody, 1, points, 3, error))
            {
                fprintf(stderr, "%s left a chain point inside the body: %s\n", label, error.CStr());
                return false;
            }
        }
        const auto& first = candidate.chains.At(0)->simulation.points.At(1).position;
        const auto& second = candidate.chains.At(1)->simulation.points.At(1).position;
        if (!CheckSecondaryMotionFreeSegmentContacts(&activeBody, 1, 0.0f, { { first.x, first.y, first.z } }, { { second.x, second.y, second.z } }, error))
        {
            fprintf(stderr, "%s left the body-link segment inside the capsule: %s\n", label, error.CStr());
            return false;
        }
        if (withDistance && (candidate.distanceConstraints.Count() != 1 || !CheckSecondaryMotionDistance(candidate.distanceConstraints.At(0).restDistance, candidate.distanceConstraints.At(0).compliance, candidate.distanceStepSeconds, candidate.distanceLambdas.At(0), { { first.x, first.y, first.z } }, { { second.x, second.y, second.z } }, error)))
        {
            fprintf(stderr, "%s violated its compliant distance residual: %s\n", label, error.CStr());
            return false;
        }
        return true;
    };

    for (uint32_t withDistance = 0; withDistance < 2; ++withDistance)
    {
        gk::model::FModelSecondaryMotionState candidate;
        gk::model::FModelSecondaryMotionState previous;
        const auto Identity = [](gk::Vec3 point) { return point; };
        if (!MakeFixture(candidate, withDistance != 0, false, { 0.0f, 0.0f, 1.0f }, Identity))
        {
            fprintf(stderr, "could not build the local-stall fixture: %s\n", error.CStr());
            return false;
        }
        if (!ConstrainSecondaryMotionLinks(candidate, previous, &body, 1, 0.0, error))
        {
            fprintf(stderr, "known feasible body-link pose did not converge in 32 iterations (distance=%u): %s\n", withDistance, error.CStr());
            return false;
        }
        if (!ValidateSolvedCandidate(candidate, body, withDistance != 0, "local-stall result"))
        {
            return false;
        }
    }

    // 任意軸回転と平行移動を形状・鎖・可行姿勢へ共通適用し、端点順も反転する。
    const double inverseAxisLength = 1.0 / sqrt(14.0);
    const gk::Vec3 rotationAxis = { static_cast<float>(inverseAxisLength), static_cast<float>(2.0 * inverseAxisLength), static_cast<float>(3.0 * inverseAxisLength) };
    const double rotationAngle = 0.73;
    const double rotationCosine = cos(rotationAngle);
    const double rotationSine = sin(rotationAngle);
    const gk::Vec3 translation = { 3.0f, -2.0f, 1.5f };
    const auto Transform = [&](gk::Vec3 point)
    {
        const double axisDot = static_cast<double>(rotationAxis.x) * point.x + static_cast<double>(rotationAxis.y) * point.y + static_cast<double>(rotationAxis.z) * point.z;
        const gk::Vec3 cross = { rotationAxis.y * point.z - rotationAxis.z * point.y, rotationAxis.z * point.x - rotationAxis.x * point.z, rotationAxis.x * point.y - rotationAxis.y * point.x };
        return gk::Vec3{ static_cast<float>(point.x * rotationCosine + cross.x * rotationSine + rotationAxis.x * axisDot * (1.0 - rotationCosine) + translation.x), static_cast<float>(point.y * rotationCosine + cross.y * rotationSine + rotationAxis.y * axisDot * (1.0 - rotationCosine) + translation.y), static_cast<float>(point.z * rotationCosine + cross.z * rotationSine + rotationAxis.z * axisDot * (1.0 - rotationCosine) + translation.z) };
    };
    const auto TransformVector = [&](gk::Vec3 vector)
    {
        const gk::Vec3 transformed = Transform(vector);
        return gk::Vec3{ transformed.x - translation.x, transformed.y - translation.y, transformed.z - translation.z };
    };
    const FModelSecondaryMotionCollisionShape rotatedBody = { Transform({ body.start.x, body.start.y, body.start.z }), Transform({ body.end.x, body.end.y, body.end.z }), body.radius };
    const gk::Vec3 rotatedFeasibleFirst = Transform(feasibleFirst);
    const gk::Vec3 rotatedFeasibleSecond = Transform(feasibleSecond);
    const gk::Vec3 rotatedFirstRoot = Transform(feasibleFirstRoot);
    const gk::Vec3 rotatedFirstTip = Transform(feasibleFirstTip);
    const gk::Vec3 rotatedSecondRoot = Transform(feasibleSecondRoot);
    const gk::Vec3 rotatedSecondTip = Transform(feasibleSecondTip);
    const gk::FVector3d rotatedKnownPoints[6] = { { { rotatedFirstRoot.x, rotatedFirstRoot.y, rotatedFirstRoot.z } }, { { rotatedFeasibleFirst.x, rotatedFeasibleFirst.y, rotatedFeasibleFirst.z } }, { { rotatedFirstTip.x, rotatedFirstTip.y, rotatedFirstTip.z } }, { { rotatedSecondRoot.x, rotatedSecondRoot.y, rotatedSecondRoot.z } }, { { rotatedFeasibleSecond.x, rotatedFeasibleSecond.y, rotatedFeasibleSecond.z } }, { { rotatedSecondTip.x, rotatedSecondTip.y, rotatedSecondTip.z } } };
    if (fabs(Distance(rotatedFeasibleFirst, rotatedFeasibleSecond) - 1.0) > 1.0e-6 || !CheckSecondaryMotionContacts(&rotatedBody, 1, rotatedKnownPoints, 6, error) || !CheckSecondaryMotionFreeSegmentContacts(&rotatedBody, 1, 0.0f, { { rotatedFeasibleFirst.x, rotatedFeasibleFirst.y, rotatedFeasibleFirst.z } }, { { rotatedFeasibleSecond.x, rotatedFeasibleSecond.y, rotatedFeasibleSecond.z } }, error))
    {
        fprintf(stderr, "rigidly transformed known feasible link failed its independent contact check: %s\n", error.CStr());
        return false;
    }
    for (uint32_t withDistance = 0; withDistance < 2; ++withDistance)
    {
        gk::model::FModelSecondaryMotionState candidate;
        gk::model::FModelSecondaryMotionState previous;
        if (!MakeFixture(candidate, withDistance != 0, true, TransformVector({ 0.0f, 0.0f, 1.0f }), Transform) || !ConstrainSecondaryMotionLinks(candidate, previous, &rotatedBody, 1, 0.0, error) || !ValidateSolvedCandidate(candidate, rotatedBody, withDistance != 0, "rotated reversed-link result"))
        {
            fprintf(stderr, "rigidly transformed reversed link did not converge (distance=%u): %s\n", withDistance, error.CStr());
            return false;
        }
    }

    gk::model::FModelSecondaryMotionState impossible;
    gk::model::FModelSecondaryMotionState previous;
    const auto Identity = [](gk::Vec3 point) { return point; };
    if (!MakeFixture(impossible, false, true, { 0.0f, 0.0f, 1.0f }, Identity))
    {
        return false;
    }
    for (uint32_t chainIndex = 0; chainIndex < impossible.chains.Count(); ++chainIndex)
    {
        impossible.chains.At(chainIndex)->settings.maxAngleDegrees = 0.0f;
    }
    if (ConstrainSecondaryMotionLinks(impossible, previous, &body, 1, 0.0, error) || error.Empty())
    {
        fprintf(stderr, "zero-angle infeasible link contact was falsely accepted\n");
        return false;
    }
    return true;
}

/**
 * 姿勢の向き補正をFKへ戻した線分が、投影候補の許容境界を跨ぐことを確認する。
 */
static bool TestSecondaryMotionLinkFkRoundTripToleranceBoundary()
{
    using namespace gk::model::animation;
    FModelSkeleton skeleton;
    const int32_t parents[5] = { -1, 0, 1, 0, 3 };
    FModelBoneTransform transforms[5]{};
    transforms[0].position[0] = 1.0f;
    transforms[1].position[1] = 0.02f;
    transforms[2].position[1] = 0.01f;
    transforms[3].position[1] = -0.02f;
    transforms[4].position[1] = -0.01f;
    gk::String error;
    if (!skeleton.parents.AppendRange(parents, 5) || !skeleton.restLocalTransforms.AppendRange(transforms, 5))
    {
        fprintf(stderr, "could not create the FK-roundtrip skeleton\n");
        return false;
    }

    FModelPose basePose;
    if (!InitializeModelPose(skeleton, basePose, error))
    {
        fprintf(stderr, "could not initialize the FK-roundtrip pose: %s\n", error.CStr());
        return false;
    }
    const uint32_t firstBones[2] = { 1, 2 };
    const uint32_t secondBones[2] = { 3, 4 };
    const gk::Vec3 firstPoints[3] = { { 1.0f, 0.02f, 0.0f }, { 1.00963557f, 0.0226749871f, 0.0f }, { 1.00963557f + static_cast<float>(sin(1.3) * 0.01), 0.0226749871f + static_cast<float>(cos(1.3) * 0.01), 0.0f } };
    const gk::Vec3 secondPoints[3] = { { 1.0f, -0.02f, 0.0f }, { 0.990400016f, -0.0228000209f, 0.0f }, { 0.990400016f + static_cast<float>(sin(1.2869999999999999) * 0.01), -0.0228000209f + static_cast<float>(cos(1.2869999999999999) * 0.01), 0.0f } };
    const FModelPoseChain chains[2] = { { firstBones, 2, { 0.0f, 0.01f, 0.0f }, firstPoints }, { secondBones, 2, { 0.0f, 0.01f, 0.0f }, secondPoints } };
    FModelPose orientedPose;
    if (!OrientModelPoseChains(skeleton, basePose, chains, 2, orientedPose, error))
    {
        fprintf(stderr, "could not orient the FK-roundtrip chains: %s\n", error.CStr());
        return false;
    }
    gk::Array<float> renderedMatrices;
    if (!EvaluateModelPose(skeleton, orientedPose, renderedMatrices, error))
    {
        fprintf(stderr, "could not evaluate the FK-roundtrip pose: %s\n", error.CStr());
        return false;
    }

    const FModelSecondaryMotionCollisionShape sphere = { { 0.999097705f, 0.00032666701f, 0.0f }, { 0.999097705f, 0.00032666701f, 0.0f }, 0.00100001157f };
    const gk::FVector3d candidateFirst{ { firstPoints[1].x, firstPoints[1].y, firstPoints[1].z } };
    const gk::FVector3d candidateSecond{ { secondPoints[1].x, secondPoints[1].y, secondPoints[1].z } };
    const gk::FVector3d renderedFirst{ { renderedMatrices.At(2u * 16u + 12u), renderedMatrices.At(2u * 16u + 13u), renderedMatrices.At(2u * 16u + 14u) } };
    const gk::FVector3d renderedSecond{ { renderedMatrices.At(4u * 16u + 12u), renderedMatrices.At(4u * 16u + 13u), renderedMatrices.At(4u * 16u + 14u) } };
    const gk::FVector3d center{ { sphere.start.x, sphere.start.y, sphere.start.z } };
    const auto SphereSegmentDistance = [](const gk::FVector3d& first, const gk::FVector3d& second, const gk::FVector3d& point)
    {
        const double x = second.value[0] - first.value[0];
        const double y = second.value[1] - first.value[1];
        const double z = second.value[2] - first.value[2];
        const double denominator = x * x + y * y + z * z;
        const double amount = denominator > 0.0 ? fmax(0.0, fmin(1.0, ((point.value[0] - first.value[0]) * x + (point.value[1] - first.value[1]) * y + (point.value[2] - first.value[2]) * z) / denominator)) : 0.0;
        const double dx = first.value[0] + amount * x - point.value[0];
        const double dy = first.value[1] + amount * y - point.value[1];
        const double dz = first.value[2] + amount * z - point.value[2];
        return sqrt(dx * dx + dy * dy + dz * dz);
    };
    const double candidateDistance = SphereSegmentDistance(candidateFirst, candidateSecond, center);
    const double renderedDistance = SphereSegmentDistance(renderedFirst, renderedSecond, center);
    const double threshold = static_cast<double>(sphere.radius) * 0.999;
    const double distanceShift = candidateDistance - renderedDistance;
    if (!(candidateDistance >= threshold) || !(renderedDistance < threshold) || !(distanceShift > 0.0) || distanceShift >= static_cast<double>(sphere.radius) * 0.0005)
    {
        fprintf(stderr, "FK-roundtrip oracle did not cross the expected boundary: candidate=%.17g threshold=%.17g rendered=%.17g shift=%.17g\n", candidateDistance, threshold, renderedDistance, distanceShift);
        return false;
    }

    gk::Array<FPreparedSecondaryMotionCollisionShape> prepared;
    if (!PrepareSecondaryMotionCollisionShapes(&sphere, 1, prepared, error) || prepared.Count() != 1)
    {
        fprintf(stderr, "could not prepare the FK-roundtrip body sphere: %s\n", error.CStr());
        return false;
    }
    if (!CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), candidateFirst, candidateSecond, error, 1.0) || CheckPreparedSecondaryMotionFreeSegmentContacts(prepared.Data(), prepared.Count(), candidateFirst, candidateSecond, error, 0.5))
    {
        fprintf(stderr, "candidate did not pass the regular and fail the strict prepared check: %s\n", error.CStr());
        return false;
    }

    gk::model::FModelSecondaryMotionState state;
    const gk::FModelSecondaryMotionLink link{ 2, 4, 0.0f };
    if (!state.links.Append(link))
    {
        fprintf(stderr, "could not create the FK-roundtrip rendered link\n");
        return false;
    }
    if (gk::model::CheckRenderedSecondaryMotionLinks(state, renderedMatrices, &sphere, 1, error) || error.Empty() || std::strstr(error.CStr(), "(rendered link)") == nullptr)
    {
        fprintf(stderr, "rendered FK link was not rejected with its context: %s\n", error.CStr());
        return false;
    }
    return true;
}

int main()
{
    return TestBlendThenIkSnapshot() && RunSecondaryMotionPreviewContracts() && TestSecondaryMotionLinkPreparationBudget() && TestSecondaryMotionLinkConstraintOnlyChangedChains() && TestSecondaryMotionLinkLocalStallFeasiblePose() && TestSecondaryMotionLinkFkRoundTripToleranceBoundary() ? 0 : 1;
}
