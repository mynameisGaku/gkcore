// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>

#include "core/Context.h"
#include "internal/Backend.hpp"
#include "model/Model.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/FModelPlayback.h"
#include "model/animation/ModelIk.h"
#include "model/animation/ModelPose.h"
#include "model/animation/ModelAnimationResources.h"
#include "examples/support/FModelArmIkPreview.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <math.h>
#include <stdio.h>
#include <string>

namespace
{

/**
 * GPUを使わず、描画予約された変形モデルを検査するbackend。
 */
class AAnimationRigTestBackend final : public gk::detail::Backend
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
    bool Present(const gk::detail::FramePacket&, gk::String&) override
    {
        return true;
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
class AAnimationRigTestSource final : public gk::model::AModelAnimationSource
{
  public:
    // IK検査用の親順骨格。
    gk::model::animation::FModelSkeleton skeleton;
    // 対応表検査ではtargetと異なる名前を使う。
    const char* boneNames[4] = { "joint_a", "joint_b", "joint_c", "joint_d" };
    // fixtureに登録した骨数。
    uint32_t boneCount = 3;
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
        return clip < 2 ? 1.0 : -1.0;
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
            output.localTransforms.At(0).position[0] = 2.0f;
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
        const auto rotatePoint = [axis, angle, translation](gk::Vec3 point)
        {
            return axis == 'y' ? RotateTranslateY(point, angle, translation) : (axis == 'z' ? RotateTranslateZ(point, angle, translation) : RotateTranslateX(point, angle, translation));
        };
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
    if (!TestModelBonePositionQuery(model, instance) || !TestModelBonePositionsQuery(instance, *source) || !TestModelArmIkPreview() || !TestMotionFollowingArmIkPreview() || !TestBuildArmIkTorsoFallback())
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

int main()
{
    return TestBlendThenIkSnapshot() ? 0 : 1;
}
