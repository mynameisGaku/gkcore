// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSnapshot.h"
#include "model/animation/ModelPoseCache.h"
#include "model/animation/ModelSecondaryMotion.h"
#include "model/animation/ModelAnimationBinding.h"
#include "model/animation/ModelPose.h"
#include "model/animation/ModelIk.h"
#include "model/Model.h"
#include <math.h>
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#endif

/**
 * 描画時点のanimation状態から独立した形状を作る処理。
 */
namespace gk::model
{
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
namespace
{

/**
 * animation snapshotの処理時間をcapture専用buildで集計する。
 */
struct FSnapshotProfileState
{
    // QPCの1秒あたりtick数。
    int64_t frequency = 0;
    // 100回分の各phaseのQPC tick。
    uint64_t ticks[7]{};
    // 集計中の姿勢評価数。
    uint32_t calls = 0;
    // 環境変数を確認済みか示す。
    bool initialized = false;
    // 計測が有効か示す。
    bool enabled = false;
    // 集計中のmode。複数modeの場合はmixed。
    const char* mode = nullptr;
};

// 姿勢評価のcapture専用集計状態。
FSnapshotProfileState gSnapshotProfile{};

/**
 * QPC値を取得し、失敗時は0を返す。
 */
int64_t ReadSnapshotProfileCounter()
{
    // 現在の高精度counter値。
    LARGE_INTEGER counter{};
    return QueryPerformanceCounter(&counter) ? counter.QuadPart : 0;
}

/**
 * 環境変数から計測状態を一度だけ初期化する。
 */
bool IsSnapshotProfileEnabled()
{
    if (!gSnapshotProfile.initialized)
    {
        gSnapshotProfile.initialized = true;
        // 計測を有効にする環境変数の値。
        const char* value = getenv("GKCORE_RENDER_PROFILE");
        // QPCの単位変換に使う1秒あたりtick数。
        LARGE_INTEGER frequency{};
        gSnapshotProfile.enabled = value && value[0] == '1' && value[1] == '\0' && QueryPerformanceFrequency(&frequency);
        gSnapshotProfile.frequency = gSnapshotProfile.enabled ? frequency.QuadPart : 0;
    }
    return gSnapshotProfile.enabled;
}

/**
 * 一度の姿勢評価の処理時間をphaseごとに記録する。
 */
class FSnapshotProfileCall
{
  public:
    FSnapshotProfileCall() : enabled_(IsSnapshotProfileEnabled()), totalStart_(enabled_ ? ReadSnapshotProfileCounter() : 0)
    {
    }

    void SetMode(const char* mode)
    {
        if (!enabled_)
            return;
        if (!gSnapshotProfile.mode)
            gSnapshotProfile.mode = mode;
        else if (strcmp(gSnapshotProfile.mode, mode) != 0)
            gSnapshotProfile.mode = "mixed";
    }

    void Start(uint32_t phase)
    {
        if (enabled_ && phase < 6)
            phaseStart_[phase] = ReadSnapshotProfileCounter();
    }

    void Stop(uint32_t phase)
    {
        if (enabled_ && phase < 6)
        {
            // 対象phaseの終了時刻。
            const int64_t end = ReadSnapshotProfileCounter();
            if (end > phaseStart_[phase])
                gSnapshotProfile.ticks[phase] += static_cast<uint64_t>(end - phaseStart_[phase]);
        }
    }

    ~FSnapshotProfileCall()
    {
        if (!enabled_)
            return;
        // 姿勢評価全体の終了時刻。
        const int64_t totalEnd = ReadSnapshotProfileCounter();
        if (totalEnd > totalStart_)
            gSnapshotProfile.ticks[6] += static_cast<uint64_t>(totalEnd - totalStart_);
        ++gSnapshotProfile.calls;
        if (gSnapshotProfile.calls == 100)
            Report();
    }

  private:
    void Report()
    {
        if (gSnapshotProfile.frequency > 0)
        {
            // tickをミリ秒へ変換する係数。
            const double millisecondsPerTick = 1000.0 / static_cast<double>(gSnapshotProfile.frequency);
            // 100回分の平均値を求める除数。
            const double divisor = static_cast<double>(gSnapshotProfile.calls);
            fprintf(stderr, "{\"type\":\"gkcore_model_snapshot_profile\",\"calls\":%u,\"mode\":\"%s\",\"cloneMeanMs\":%.6f,\"primarySampleMeanMs\":%.6f,\"secondarySampleMeanMs\":%.6f,\"blendMeanMs\":%.6f,\"ikMeanMs\":%.6f,\"deformMeanMs\":%.6f,\"totalMeanMs\":%.6f}\n", gSnapshotProfile.calls, gSnapshotProfile.mode ? gSnapshotProfile.mode : "unknown", gSnapshotProfile.ticks[0] * millisecondsPerTick / divisor, gSnapshotProfile.ticks[1] * millisecondsPerTick / divisor, gSnapshotProfile.ticks[2] * millisecondsPerTick / divisor, gSnapshotProfile.ticks[3] * millisecondsPerTick / divisor, gSnapshotProfile.ticks[4] * millisecondsPerTick / divisor, gSnapshotProfile.ticks[5] * millisecondsPerTick / divisor, gSnapshotProfile.ticks[6] * millisecondsPerTick / divisor);
        }
        gSnapshotProfile.ticks[0] = 0;
        gSnapshotProfile.ticks[1] = 0;
        gSnapshotProfile.ticks[2] = 0;
        gSnapshotProfile.ticks[3] = 0;
        gSnapshotProfile.ticks[4] = 0;
        gSnapshotProfile.ticks[5] = 0;
        gSnapshotProfile.ticks[6] = 0;
        gSnapshotProfile.calls = 0;
        gSnapshotProfile.mode = nullptr;
    }

    // この呼び出しでQPCを読むか示す。
    bool enabled_;
    // 姿勢評価全体の開始counter。
    int64_t totalStart_;
    // 各phaseの開始counter。
    int64_t phaseStart_[6]{};
};

}
#endif

#if !defined(_WIN32) || (!defined(GKCORE_TEST_FRAME_CAPTURE) && !defined(GKCORE_RENDER_PERFORMANCE_METRICS))
/**
 * 計測なしbuildで同じ姿勢評価経路を使うための空の状態型。
 */
struct FSnapshotProfileCall
{
};
#endif

/**
 * capture専用計測を有効な場合だけ開始する。
 */
static void StartSnapshotProfile(FSnapshotProfileCall* profile, uint32_t phase)
{
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    if (profile)
        profile->Start(phase);
#else
    (void)profile;
    (void)phase;
#endif
}

/**
 * capture専用計測を有効な場合だけ終了する。
 */
static void StopSnapshotProfile(FSnapshotProfileCall* profile, uint32_t phase)
{
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    if (profile)
        profile->Stop(phase);
#else
    (void)profile;
    (void)phase;
#endif
}

/**
 * model形状を複製し、画像参照を個別に保持する。
 */
detail::ModelResource* CloneModelSnapshot(const detail::ModelResource& source, String& error)
{
    auto* result = detail::CreateModelResource();
    if (!result)
    {
        error.Assign("model snapshot allocation failed");
        return nullptr;
    }
    result->materials.Clear();
    bool success = result->vertices.AppendRange(source.vertices.Data(), source.vertices.Count()) && result->indices.AppendRange(source.indices.Data(), source.indices.Count()) && result->primitives.AppendRange(source.primitives.Data(), source.primitives.Count()) && result->materials.AppendRange(source.materials.Data(), source.materials.Count());
    for (uint32_t i = 0; success && i < source.textures.Count(); ++i)
    {
        auto* texture = source.textures.At(i);
        if (texture && !Retain(&texture->reference))
        {
            success = false;
            break;
        }
        if (!result->textures.Append(texture))
        {
            if (texture)
                Release(&texture->reference);
            success = false;
        }
    }
    if (!success)
    {
        Release(&result->reference);
        error.Assign("model snapshot copy failed");
        return nullptr;
    }
    return result;
}

/**
 * 1再生枠の姿勢を適用先へ評価する。OBJ連番はsource自身が変形する。
 */
static bool EvaluateClip(const detail::ModelResource& source, const FModelClipState& state, animation::FModelPose& pose, String& error)
{
    if (state.asset->source->Format() == EModelAnimationFormat::ObjSequence)
        return state.asset->source->Sample(state.clip, state.seconds, pose, error);
    return SampleBoundClip(state, *source.animation, pose, error);
}

/**
 * 通常の骨格姿勢を共通順序で評価し、失敗時は出力を保つ。
 */
static bool EvaluateModelPlaybackPoseCore(const detail::ModelResource& source, const FModelPlayback& playback, animation::FModelPose& output, String& error, FSnapshotProfileCall* profile, bool includeSecondary = true)
{
    if (!source.animation || !source.animation->source)
    {
        error.Assign("model animation source is missing");
        return false;
    }
    if (!isfinite(playback.blendWeight) || playback.blendWeight < 0.0f || playback.blendWeight > 1.0f)
    {
        error.Assign("model animation blend weight must be finite and in [0, 1]");
        return false;
    }
    const auto* primary = playback.clips[0].asset;
    const bool secondaryActive = playback.clips[1].asset && playback.blendWeight > 0.0f;
    if ((primary && !primary->source) || (secondaryActive && !playback.clips[1].asset->source))
    {
        error.Assign("model animation clip source is invalid");
        return false;
    }
    if ((primary && primary->source->Format() == EModelAnimationFormat::ObjSequence) || (secondaryActive && playback.clips[1].asset->source->Format() == EModelAnimationFormat::ObjSequence))
    {
        error.Assign("OBJ sequence does not use a shared skeletal pose");
        return false;
    }
    animation::FModelPose candidate;
    // 揺れ更新で確定した基準姿勢を借りずに複製し、予約描画を独立して保持する。
    bool cacheHit = false;
    if (!CopyCurrentModelPoseCache(source, playback, candidate, cacheHit, error))
    {
        return false;
    }
    if (cacheHit)
    {
        if (includeSecondary && playback.secondaryMotion && !ApplyModelSecondaryMotion(*playback.secondaryMotion, candidate, error))
        {
            return false;
        }
        output.localTransforms.MoveFrom(candidate.localTransforms);
        output.morphWeights.MoveFrom(candidate.morphWeights);
        error.Clear();
        return true;
    }
    StartSnapshotProfile(profile, 1);
    bool success = primary ? EvaluateClip(source, playback.clips[0], candidate, error) : animation::InitializeModelPose(source.animation->source->Skeleton(), candidate, error);
    StopSnapshotProfile(profile, 1);
    if (success && secondaryActive)
    {
        animation::FModelPose secondary;
        StartSnapshotProfile(profile, 2);
        success = EvaluateClip(source, playback.clips[1], secondary, error);
        StopSnapshotProfile(profile, 2);
        if (success)
        {
            StartSnapshotProfile(profile, 3);
            success = animation::BlendModelPoses(source.animation->source->Skeleton(), candidate, secondary, playback.blendWeight, candidate, error);
            StopSnapshotProfile(profile, 3);
        }
    }
    if (success)
    {
        StartSnapshotProfile(profile, 4);
        const auto& skeleton = source.animation->source->Skeleton();
        for (uint32_t i = 0; success && i < playback.ik.Count(); ++i)
        {
            const auto& command = playback.ik.At(i);
            const auto* chain = playback.ikBones.Data() + command.offset;
            if (command.twoBone)
                success = animation::SolveTwoBoneIk(skeleton, candidate, chain[0], chain[1], chain[2], command.target, command.pole, command.weight, candidate, error);
            else
                success = animation::SolveFabrikIk(skeleton, candidate, chain, command.count, command.target, command.weight, 0.0001f, 64, candidate, error);
        }
        StopSnapshotProfile(profile, 4);
    }
    if (success && includeSecondary && playback.secondaryMotion)
    {
        success = ApplyModelSecondaryMotion(*playback.secondaryMotion, candidate, error);
    }
    if (!success)
        return false;
    output.localTransforms.MoveFrom(candidate.localTransforms);
    output.morphWeights.MoveFrom(candidate.morphWeights);
    error.Clear();
    return true;
}

bool EvaluateModelPlaybackPose(const detail::ModelResource& source, const FModelPlayback& playback, animation::FModelPose& pose, String& error)
{
    return EvaluateModelPlaybackPoseCore(source, playback, pose, error, nullptr);
}

bool EvaluateModelPlaybackBasePose(const detail::ModelResource& source, const FModelPlayback& playback, animation::FModelPose& pose, String& error)
{
    return EvaluateModelPlaybackPoseCore(source, playback, pose, error, nullptr, false);
}

detail::ModelResource* EvaluateModelSnapshot(const detail::ModelResource& source, const FModelPlayback* playback, String& error)
{
    if (playback && (!isfinite(playback->blendWeight) || playback->blendWeight < 0.0f || playback->blendWeight > 1.0f))
    {
        error.Assign("model animation blend weight must be finite and in [0, 1]");
        return nullptr;
    }
    if (playback && ((playback->clips[0].asset && !playback->clips[0].asset->source) || (playback->clips[1].asset && !playback->clips[1].asset->source)))
    {
        error.Assign("model animation clip source is invalid");
        return nullptr;
    }
    // 再生・IKがないモデルは、既存の静的形状をそのまま保持する。
    if (!playback || (!playback->clips[0].asset && playback->ik.Count() == 0 && !playback->secondaryMotion))
    {
        auto* result = const_cast<detail::ModelResource*>(&source);
        if (!Retain(&result->reference))
        {
            error.Assign("model reference limit exceeded");
            return nullptr;
        }
        return result;
    }
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    // animation snapshotごとの処理時間をcapture専用で記録する。
    FSnapshotProfileCall profile;
    profile.SetMode(playback->clips[1].asset && playback->blendWeight > 0.0f ? "blend" : (playback->clips[0].asset ? "single_clip" : "ik_only"));
    profile.Start(0);
#endif
    const auto* primary = playback->clips[0].asset;
    const bool secondarySequenceActive = playback->clips[1].asset && playback->blendWeight > 0.0f && playback->clips[1].asset->source->Format() == EModelAnimationFormat::ObjSequence;
    const bool skeletalPath = (!primary || primary->source->Format() != EModelAnimationFormat::ObjSequence) && !secondarySequenceActive;
    if (!skeletalPath && playback->secondaryMotion)
    {
        error.Assign("secondary motion cannot be combined with an OBJ sequence pose");
        return nullptr;
    }
    if (skeletalPath)
    {
        auto* result = CloneModelSnapshot(source, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
        profile.Stop(0);
#endif
        if (!result)
            return nullptr;
        result->isPoseSnapshot = true;
        if (source.animation && source.animation->source->Format() != EModelAnimationFormat::ObjSequence)
        {
            auto* geometrySource = const_cast<detail::ModelResource*>(&source);
            if (!Retain(&geometrySource->reference))
            {
                Release(&result->reference);
                error.Assign("model geometry source reference limit exceeded");
                return nullptr;
            }
            result->geometrySource = geometrySource;
        }
        animation::FModelPose pose;
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
        bool success = EvaluateModelPlaybackPoseCore(source, *playback, pose, error, &profile);
#else
        bool success = EvaluateModelPlaybackPoseCore(source, *playback, pose, error, nullptr);
#endif
        if (success)
        {
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
            profile.Start(5);
#endif
            success = source.animation && source.animation->source->Deform(pose, *result, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
            profile.Stop(5);
#endif
        }
        if (!success)
        {
            Release(&result->reference);
            return nullptr;
        }
        error.Clear();
        return result;
    }
    auto* result = CloneModelSnapshot(source, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(0);
#endif
    if (!result)
        return nullptr;
    result->isPoseSnapshot = true;
    // GLB/FBXは位置・法線・接線だけを変形し、他属性の元geometryを共有する。
    if (source.animation && source.animation->source->Format() != EModelAnimationFormat::ObjSequence && (!playback->clips[0].asset || playback->clips[0].asset->source->Format() != EModelAnimationFormat::ObjSequence))
    {
        auto* geometrySource = const_cast<detail::ModelResource*>(&source);
        if (!Retain(&geometrySource->reference))
        {
            Release(&result->reference);
            error.Assign("model geometry source reference limit exceeded");
            return nullptr;
        }
        result->geometrySource = geometrySource;
    }
    animation::FModelPose pose;
    const auto* sequencePrimary = playback->clips[0].asset;
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Start(1);
#endif
    bool success = sequencePrimary ? EvaluateClip(source, playback->clips[0], pose, error) : source.animation && animation::InitializeModelPose(source.animation->source->Skeleton(), pose, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
    profile.Stop(1);
#endif
    const bool sequence = sequencePrimary && sequencePrimary->source->Format() == EModelAnimationFormat::ObjSequence;
    if (success && playback->clips[1].asset && playback->blendWeight > 0.0f)
    {
        animation::FModelPose secondary;
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
        profile.Start(2);
#endif
        success = EvaluateClip(source, playback->clips[1], secondary, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
        profile.Stop(2);
#endif
        if (success && !sequence)
        {
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
            profile.Start(3);
#endif
            success = animation::BlendModelPoses(source.animation->source->Skeleton(), pose, secondary, playback->blendWeight, pose, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
            profile.Stop(3);
#endif
        }
        else if (success)
        {
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
            profile.Start(0);
#endif
            auto* other = CloneModelSnapshot(source, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
            profile.Stop(0);
            profile.Start(5);
#endif
            success = other && sequencePrimary->source->Deform(pose, *result, error) && playback->clips[1].asset->source->Deform(secondary, *other, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
            profile.Stop(5);
            profile.Start(3);
#endif
            if (success)
            {
                for (uint32_t i = 0; i < result->vertices.Count(); ++i)
                {
                    auto& vertex = result->vertices.At(i);
                    const auto& second = other->vertices.At(i);
                    double lengthSquared = 0.0;
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        vertex.position[axis] = static_cast<float>(static_cast<double>(vertex.position[axis]) * (1.0 - playback->blendWeight) + static_cast<double>(second.position[axis]) * playback->blendWeight);
                        vertex.normal[axis] = vertex.normal[axis] * (1.0f - playback->blendWeight) + second.normal[axis] * playback->blendWeight;
                        lengthSquared += static_cast<double>(vertex.normal[axis]) * vertex.normal[axis];
                    }
                    const double length = sqrt(lengthSquared);
                    if (length > 0.0)
                        for (uint32_t axis = 0; axis < 3; ++axis)
                            vertex.normal[axis] = static_cast<float>(vertex.normal[axis] / length);
                }
            }
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
            profile.Stop(3);
#endif
            if (other)
                Release(&other->reference);
            if (!success)
            {
                Release(&result->reference);
                return nullptr;
            }
            return result;
        }
    }
    if (success && !sequence)
    {
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
        profile.Start(4);
#endif
        for (uint32_t i = 0; success && i < playback->ik.Count(); ++i)
        {
            const auto& command = playback->ik.At(i);
            const auto* chain = playback->ikBones.Data() + command.offset;
            const auto& skeleton = source.animation->source->Skeleton();
            if (command.twoBone)
                success = animation::SolveTwoBoneIk(skeleton, pose, chain[0], chain[1], chain[2], command.target, command.pole, command.weight, pose, error);
            else
                success = animation::SolveFabrikIk(skeleton, pose, chain, command.count, command.target, command.weight, 0.0001f, 64, pose, error);
        }
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
        profile.Stop(4);
#endif
    }
    if (success)
    {
        const auto* deform = sequence ? sequencePrimary->source : source.animation->source;
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
        profile.Start(5);
#endif
        success = deform->Deform(pose, *result, error);
#if defined(_WIN32) && (defined(GKCORE_TEST_FRAME_CAPTURE) || defined(GKCORE_RENDER_PERFORMANCE_METRICS))
        profile.Stop(5);
#endif
    }
    if (!success)
    {
        Release(&result->reference);
        return nullptr;
    }
    error.Clear();
    return result;
}
}
