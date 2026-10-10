// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>

#include "core/Context.h"
#include "internal/Backend.hpp"
#include "model/Model.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/FModelPlayback.h"
#include "model/animation/ModelPose.h"
#include "model/animation/ModelAnimationResources.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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
    // IK検査用の親順3節骨格。
    gk::model::animation::FModelSkeleton skeleton;
    // 対応表検査ではtargetと異なる名前を使う。
    const char* boneNames[3] = { "root", "middle", "end" };

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
        return bone < 3 ? boneNames[bone] : nullptr;
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
        if (clip >= 2 || !isfinite(seconds) || !gk::model::animation::InitializeModelPose(skeleton, output, error))
            return false;
        if (clip == 1)
            output.localTransforms.At(0).position[0] = 2.0f;
        error.Clear();
        return true;
    }

    bool Deform(const gk::model::animation::FModelPose& pose, gk::detail::ModelResource& output, gk::String& error) const override
    {
        gk::Array<float> matrices;
        if (output.vertices.Count() != 3 || !gk::model::animation::EvaluateModelPose(skeleton, pose, matrices, error))
            return false;
        for (uint32_t bone = 0; bone < 3; ++bone)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
                output.vertices.At(bone).position[axis] = matrices.At(bone * 16u + 12u + axis);
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
 * 公開profile API用に、既存fixtureと同じ3骨motionを登録する。
 */
gk::ModelAnimationHandle CreateRigAnimation(const char* const* boneNames = nullptr)
{
    auto* source = new AAnimationRigTestSource;
    if (boneNames)
    {
        for (uint32_t bone = 0; bone < 3; ++bone)
            source->boneNames[bone] = boneNames[bone];
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
        fprintf(stderr, "rig animation registration failed: %s\n", error.CStr());
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
    const bool fixturesReady = WriteHumanoidProfile(targetPath, "Hips\troot\nLeftUpperLeg\tmiddle\nLeftLowerLeg\tend\n", target) && WriteHumanoidProfile(partialPath, "Hips\tsource_a\n", partial) && WriteHumanoidProfile(fullPath, "Hips\tsource_a\nLeftUpperLeg\tsource_b\nLeftLowerLeg\tsource_c\n", full);
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
        fprintf(stderr, "animation mapping diagnostics snapshot or invalid-query contract failed: %s\n", gk::GetLastErrorMessage());
    return passed;
}

bool WriteHumanoidProfile(const std::filesystem::path& path, const char* contents, std::string& utf8Path)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !(file << contents))
        return false;
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
    const bool fixturesReady = WriteHumanoidProfile(profileOnePath, "# 人型profile\r\nHips\troot\r\nLeftUpperLeg\tmiddle\r\nLeftLowerLeg\tend\r\n", profileOne) && WriteHumanoidProfile(profileTwoPath, "Hips\troot\nLeftUpperLeg\tend\nLeftLowerLeg\tmiddle\n", profileTwo) && WriteHumanoidProfile(partialPath, "Hips\troot\n", partial) && WriteHumanoidProfile(unknownPath, "Hips\troot\nLeftUpperLeg\tmissing\n", unknown) && WriteHumanoidProfile(duplicatePath, "Hips\troot\nHips\tmiddle\n", duplicate);
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
        fprintf(stderr, "humanoid profile replacement, failure atomicity, instance isolation, or bind snapshot failed: %s\n", gk::GetLastErrorMessage());
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
            gk::DeleteModel(instance);
        if (model.IsValid())
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
    if (draws.Count() != 1 || !Near(draws.At(0).model->vertices.At(2).position[0], 1.0f) || !Near(draws.At(0).model->vertices.At(2).position[1], 1.0f))
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
