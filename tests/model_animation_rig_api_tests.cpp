// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>

#include "../src/core/Context.h"
#include "../src/internal/Backend.hpp"
#include "../src/model/Model.h"
#include "../src/model/animation/AModelAnimationSource.h"
#include "../src/model/animation/FModelAnimationAsset.h"
#include "../src/model/animation/FModelPlayback.h"
#include "../src/model/animation/ModelPose.h"

#include <math.h>
#include <stdio.h>

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
        static const char* names[3] = { "root", "middle", "end" };
        return bone < 3 ? names[bone] : nullptr;
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
    if (!transformAdded || !instance.IsValid() || gk::PlayModelAnimation(instance, 0, false) != 0 || gk::SetModelAnimationBlend(instance, 1, 0.5f) != 0)
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
