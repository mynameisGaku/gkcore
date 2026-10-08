// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include "../src/core/Context.h"
#include "../src/internal/Backend.hpp"
#include <filesystem>
#include <float.h>
#include <fstream>
#include <math.h>
#include <stdio.h>

namespace
{
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

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        fprintf(stderr, "%s: %s\n", message, gk::GetLastErrorMessage());
    }
    return condition;
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
    gk::detail::SetBackendForTesting(new AAnimationTestBackend);
    if (!Check(gk::Init() == 0, "Init"))
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
