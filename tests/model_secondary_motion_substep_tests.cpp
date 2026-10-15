// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include <gkcore/ModelAnimation.h>
#include <gkcore/ModelSecondaryMotion.h>

#include "core/Context.h"
#include "foundation/Memory.h"
#include "model/Model.h"
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelPlayback.h"
#include "model/animation/FModelPoseCache.h"
#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/ModelAnimationResources.h"
#include "model/animation/ModelPose.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/ModelSecondaryMotionStep.h"
#include "model/animation/ModelSecondaryMotionContactPoints.h"
#include "model/animation/ModelSecondaryMotionAttachments.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

namespace
{
/**
 * 身体と二本の鎖を別々の等速移動で評価する、外部asset不要のsource。
 */
class ASubstepTestSource final : public gk::model::AModelAnimationSource
{
  public:
    // 身体、左右の固定root、各自由boneを親順で所有する。
    gk::model::animation::FModelSkeleton skeleton;
    // 有効時は鎖の固定rootを一定角速度で回し、子boneに円弧を描かせる。
    bool rotatingRoots = false;
    // 鎖の外側の親を回し、固定root自身にも円弧を描かせる。
    bool rotatingPivot = false;
    // 同じ回転幅を異なる更新間隔で検査するための角速度倍率。
    double rotationRateScale = 1.0;

    /**
     * この合成sourceの骨格形式を返す。
     */
    gk::model::EModelAnimationFormat Format() const override
    {
        return gk::model::EModelAnimationFormat::Glb;
    }
    /**
     * 検査が所有する親順の骨格を借用する。
     */
    const gk::model::animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton;
    }
    /**
     * 有効なboneには検査用の名前を返す。
     */
    const char* BoneName(uint32_t bone) const override
    {
        return bone < 5u ? "substep_bone" : nullptr;
    }
    /**
     * 履歴配列の所有も検査するmorph名を返す。
     */
    const char* MorphName(uint32_t morph) const override
    {
        return morph == 0u ? "substep_morph" : nullptr;
    }
    /**
     * 等速移動clipの数を返す。
     */
    uint32_t ClipCount() const override
    {
        return 1u;
    }
    /**
     * 範囲内のclip名を借用する。
     */
    const char* ClipName(uint32_t clip) const override
    {
        return clip == 0u ? "linear_translation" : nullptr;
    }
    /**
     * 比較する短い時間区間を十分に含むclip長を返す。
     */
    double ClipDuration(uint32_t clip) const override
    {
        return clip == 0u ? 100.0 : -1.0;
    }
    /**
     * 身体と固定rootを別速度で平行移動し、必要なら固定rootだけを一定角速度で回す。
     */
    bool Sample(uint32_t clip, double seconds, gk::model::animation::FModelPose& output, gk::String& error) const override
    {
        if (clip != 0u || !std::isfinite(seconds) || !gk::model::animation::InitializeModelPose(skeleton, output, error))
        {
            return false;
        }
        output.localTransforms.At(0u).position[0] += static_cast<float>(0.6 * seconds);
        output.localTransforms.At(0u).position[2] += static_cast<float>(1.2 * seconds);
        output.localTransforms.At(1u).position[0] += static_cast<float>(1.2 * seconds);
        output.localTransforms.At(3u).position[0] += static_cast<float>(1.2 * seconds);
        output.morphWeights.At(0u) = static_cast<float>(0.25 + 0.1 * seconds);
        if (rotatingRoots || rotatingPivot)
        {
            // 1/60秒で120度を回し、中間行列の線形補間による骨長の縮みを顕在化させる。
            const double halfAngle = 20.0 * 3.14159265358979323846 * seconds * rotationRateScale;
            for (uint32_t root : { 0u, 1u, 3u })
            {
                if ((root == 0u && !rotatingPivot) || (root != 0u && !rotatingRoots))
                {
                    continue;
                }
                output.localTransforms.At(root).rotation[2] = static_cast<float>(sin(halfAngle));
                output.localTransforms.At(root).rotation[3] = static_cast<float>(cos(halfAngle));
            }
        }
        return true;
    }
    /**
     * この検査は状態配列だけを読み、頂点変形は要求しない。
     */
    bool Deform(const gk::model::animation::FModelPose&, gk::detail::ModelResource&, gk::String& error) const override
    {
        error.Clear();
        return true;
    }
};

/**
 * 同じsourceを読むinstance群を所有し、検査終了時にmodelも解放する。
 */
struct FSubstepTestFixture
{
    // sourceの寿命を共有する元model。
    gk::ModelHandle model{};
    // 比較する更新方法ごとのinstance。
    gk::ModelHandle instances[3]{};

    /**
     * instanceを先に解放してから共有sourceを解放する。
     */
    ~FSubstepTestFixture()
    {
        for (auto instance : instances)
        {
            if (instance.IsValid())
            {
                gk::DeleteModel(instance);
            }
        }
        if (model.IsValid())
        {
            gk::DeleteModel(model);
        }
    }
};

/**
 * instanceが所有する揺れ状態を借りる。未設定ではnullを返す。
 */
gk::model::FModelSecondaryMotionState* ReadState(gk::ModelHandle handle)
{
    auto* transform = gk::detail::FindModelTransform(handle);
    return transform && transform->playback ? transform->playback->secondaryMotion : nullptr;
}

/**
 * 小さな非対称接触が成立する二本鎖を、同じ初期履歴から作る。
 */
bool MakeFixture(FSubstepTestFixture& fixture, bool activeBody, bool rotatingRoots = false)
{
    auto* resource = gk::detail::CreateModelResource();
    if (!resource)
    {
        return false;
    }
    auto* source = new ASubstepTestSource;
    source->rotatingRoots = rotatingRoots;
    const int32_t parents[5] = { -1, -1, 1, -1, 3 };
    gk::model::animation::FModelBoneTransform rest[5]{};
    rest[1].position[0] = -0.5f;
    rest[2].position[1] = 1.0f;
    rest[3].position[0] = 0.5f;
    rest[4].position[1] = 1.0f;
    if (!source->skeleton.parents.AppendRange(parents, 5u) || !source->skeleton.restLocalTransforms.AppendRange(rest, 5u) || !source->skeleton.restMorphWeights.Append(0.25f))
    {
        delete source;
        gk::Release(&resource->reference);
        return false;
    }
    gk::String error;
    resource->animation = gk::model::CreateModelAnimationAsset(source, error);
    gk::detail::ModelVertex vertices[5]{};
    if (!resource->animation || !resource->vertices.AppendRange(vertices, 5u))
    {
        gk::Release(&resource->reference);
        return false;
    }
    fixture.model = gk::detail::RegisterModelResource(resource, error);
    gk::detail::ModelTransform transform{};
    transform.handle = fixture.model;
    transform.scale = { 1.0f, 1.0f, 1.0f };
    if (!fixture.model.IsValid())
    {
        gk::Release(&resource->reference);
        return false;
    }
    if (!gk::detail::GetContext().modelTransforms.Append(transform))
    {
        return false;
    }
    const uint32_t bones[2][2] = { { 1u, 2u }, { 3u, 4u } };
    gk::FModelSecondaryMotionSettings settings{};
    settings.frequencyHz = 4.0f;
    settings.dampingRatio = 0.4f;
    settings.gravity = { 0.0f, -0.5f, 0.0f };
    settings.windAcceleration = { 0.0f, 0.0f, 0.3f };
    settings.endOffset = { 0.0f, 0.25f, 0.0f };
    settings.maxAngleDegrees = 60.0f;
    settings.teleportDistance = 0.5f;
    settings.constraintIterations = 16u;
    gk::FModelSecondaryMotionCollider body{};
    body.bone = 0u;
    body.start = { activeBody ? 0.0f : 10.0f, 0.95f, -0.08f };
    body.end = body.start;
    body.radius = 0.14f;
    const gk::FModelSecondaryMotionContactAnchor anchors[2] = { { 2u, {}, 0.5f }, { 4u, {}, 0.5f } };
    const gk::FModelSecondaryMotionContactPoint contact = { 0u, 2u, 0.0f };
    for (auto& instance : fixture.instances)
    {
        instance = gk::CreateModelInstance(fixture.model);
        if (!instance.IsValid() || gk::PlayModelAnimation(instance, 0u, false) != 0 || gk::SetModelSecondaryMotionChain(instance, bones[0], 2u, settings) != 0 || gk::SetModelSecondaryMotionChain(instance, bones[1], 2u, settings) != 0 || gk::SetModelSecondaryMotionColliders(instance, &body, 1u, 0.0f) != 0 || gk::SetModelSecondaryMotionContactPoints(instance, &contact, 1u, anchors, 2u, 32u) != 0 || gk::ResetModelSecondaryMotion(instance) != 0 || gk::UpdateModelSecondaryMotion(instance, 0.0) != 0)
        {
            fprintf(stderr, "substep fixture setup failed: active=%d error=%s\n", activeBody ? 1 : 0, gk::GetLastErrorMessage());
            return false;
        }
    }
    return true;
}

/**
 * clip時刻を先に進め、その終端へ揺れ状態を更新する。
 */
bool Advance(gk::ModelHandle handle, double seconds)
{
    return gk::UpdateModelAnimation(handle, seconds) == 0 && gk::UpdateModelSecondaryMotion(handle, seconds) == 0;
}

/**
 * 二つの状態の点位置・速度・目標と、身体履歴の最大差を返す。
 */
double MaximumDifference(const gk::model::FModelSecondaryMotionState& first, const gk::model::FModelSecondaryMotionState& second)
{
    double maximum = 0.0;
    if (first.chains.Count() != second.chains.Count() || first.previousCollisionShapes.Count() != second.previousCollisionShapes.Count() || first.previousBaseMatrices.Count() != second.previousBaseMatrices.Count())
    {
        return std::numeric_limits<double>::infinity();
    }
    const auto IncludeVector = [&](const gk::Vec3& left, const gk::Vec3& right)
    {
        maximum = fmax(maximum, fabs(static_cast<double>(left.x) - right.x));
        maximum = fmax(maximum, fabs(static_cast<double>(left.y) - right.y));
        maximum = fmax(maximum, fabs(static_cast<double>(left.z) - right.z));
    };
    for (uint32_t chainIndex = 0u; chainIndex < first.chains.Count(); ++chainIndex)
    {
        const auto& left = first.chains.At(chainIndex)->simulation;
        const auto& right = second.chains.At(chainIndex)->simulation;
        if (left.points.Count() != right.points.Count() || left.previousTargets.Count() != right.previousTargets.Count())
        {
            return std::numeric_limits<double>::infinity();
        }
        for (uint32_t point = 0u; point < left.points.Count(); ++point)
        {
            IncludeVector(left.points.At(point).position, right.points.At(point).position);
            IncludeVector(left.points.At(point).velocity, right.points.At(point).velocity);
            IncludeVector(left.previousTargets.At(point), right.previousTargets.At(point));
        }
    }
    for (uint32_t index = 0u; index < first.previousCollisionShapes.Count(); ++index)
    {
        IncludeVector(first.previousCollisionShapes.At(index).start, second.previousCollisionShapes.At(index).start);
        IncludeVector(first.previousCollisionShapes.At(index).end, second.previousCollisionShapes.At(index).end);
        maximum = fmax(maximum, fabs(static_cast<double>(first.previousCollisionShapes.At(index).radius) - second.previousCollisionShapes.At(index).radius));
    }
    for (uint32_t index = 0u; index < first.previousBaseMatrices.Count(); ++index)
    {
        maximum = fmax(maximum, fabs(static_cast<double>(first.previousBaseMatrices.At(index)) - second.previousBaseMatrices.At(index)));
    }
    return maximum;
}

/**
 * 一回の更新と同じ時刻列を持つ二回の更新が、身体接触まで一致することを確かめる。
 */
bool TestCommonSubsteps(bool activeBody, bool rotatingRoots = false)
{
    FSubstepTestFixture fixture;
    if (!MakeFixture(fixture, activeBody, rotatingRoots))
    {
        return false;
    }
    const auto* initial = ReadState(fixture.instances[0]);
    const double initialWeightedZ = 0.5 * (initial->chains.At(0u)->simulation.points.At(1u).position.z + initial->chains.At(1u)->simulation.points.At(1u).position.z);
    if ((activeBody && initialWeightedZ < 0.01) || (!activeBody && fabs(initialWeightedZ) > 1.0e-6))
    {
        fprintf(stderr, "substep fixture failed to distinguish active and clear contact: active=%d z=%.9g\n", activeBody ? 1 : 0, initialWeightedZ);
        return false;
    }
    if (!Advance(fixture.instances[0], 1.0 / 60.0) || !Advance(fixture.instances[1], 1.0 / 120.0))
    {
        fprintf(stderr, "common substep update failed: active=%d rotating=%d error=%s\n", activeBody ? 1 : 0, rotatingRoots ? 1 : 0, gk::GetLastErrorMessage());
        return false;
    }
    if (rotatingRoots)
    {
        const auto* midpoint = ReadState(fixture.instances[1]);
        for (uint32_t chain = 0u; chain < 2u; ++chain)
        {
            const auto& targets = midpoint->chains.At(chain)->simulation.previousTargets;
            // 60度の中間姿勢は単位円上にあり、終端行列同士の中点とは異なる。
            if (fabs(static_cast<double>(targets.At(1u).x) - targets.At(0u).x + sqrt(0.75)) > 1.0e-6 || fabs(static_cast<double>(targets.At(1u).y) - targets.At(0u).y - 0.5) > 1.0e-6)
            {
                fprintf(stderr, "rotating substep fixture midpoint is not on the expected 60-degree arc\n");
                return false;
            }
        }
    }
    if (!Advance(fixture.instances[1], 1.0 / 120.0))
    {
        fprintf(stderr, "second half substep update failed: rotating=%d error=%s\n", rotatingRoots ? 1 : 0, gk::GetLastErrorMessage());
        return false;
    }
    const auto* full = ReadState(fixture.instances[0]);
    const auto* halves = ReadState(fixture.instances[1]);
    const double difference = full && halves ? MaximumDifference(*full, *halves) : std::numeric_limits<double>::infinity();
    if (!std::isfinite(difference) || difference > 2.0e-5)
    {
        fprintf(stderr, "common substep full/half mismatch: active=%d rotating=%d maximum=%.9g\n", activeBody ? 1 : 0, rotatingRoots ? 1 : 0, difference);
        return false;
    }
    if (rotatingRoots)
    {
        const uint32_t bones[4] = { 1u, 2u, 3u, 4u };
        gk::Vec3 rendered[4]{};
        if (gk::GetModelBonePositions(fixture.instances[0], bones, 4u, rendered) != 0)
        {
            return false;
        }
        for (uint32_t chain = 0u; chain < 2u; ++chain)
        {
            const auto& simulation = full->chains.At(chain)->simulation;
            const double expectedRootX = (chain == 0u ? -0.5 : 0.5) + 1.2 / 60.0;
            const auto& root = simulation.points.At(0u).position;
            if (fabs(root.x - expectedRootX) > 1.0e-6 || root.y != 0.0f || root.z != 0.0f || fabs(rendered[chain * 2u].x - expectedRootX) > 1.0e-6 || rendered[chain * 2u].y != 0.0f || rendered[chain * 2u].z != 0.0f)
            {
                fprintf(stderr, "rotating substeps moved a fixed root: chain=%u\n", chain);
                return false;
            }
            for (uint32_t segment = 0u; segment < 2u; ++segment)
            {
                const auto& first = simulation.points.At(segment).position;
                const auto& second = simulation.points.At(segment + 1u).position;
                const double x = static_cast<double>(second.x) - first.x;
                const double y = static_cast<double>(second.y) - first.y;
                const double z = static_cast<double>(second.z) - first.z;
                const double expectedLength = segment == 0u ? 1.0 : 0.25;
                if (fabs(sqrt(x * x + y * y + z * z) - expectedLength) > 1.0e-5)
                {
                    fprintf(stderr, "rotating substeps changed a bone or virtual-tip length: chain=%u segment=%u\n", chain, segment);
                    return false;
                }
            }
            const double x = static_cast<double>(rendered[chain * 2u + 1u].x) - rendered[chain * 2u].x;
            const double y = static_cast<double>(rendered[chain * 2u + 1u].y) - rendered[chain * 2u].y;
            const double z = static_cast<double>(rendered[chain * 2u + 1u].z) - rendered[chain * 2u].z;
            if (fabs(sqrt(x * x + y * y + z * z) - 1.0) > 1.0e-5)
            {
                fprintf(stderr, "rotating substeps changed rendered bone length: chain=%u\n", chain);
                return false;
            }
        }
    }
    return true;
}

/**
 * 状態の物理配列をbyte列へ写し、失敗前後で厳密に比較できるようにする。
 */
std::vector<unsigned char> Snapshot(const gk::model::FModelSecondaryMotionState& state)
{
    std::vector<unsigned char> result;
    const auto Append = [&](const auto& values)
    {
        const uint32_t count = values.Count();
        const auto* countBytes = reinterpret_cast<const unsigned char*>(&count);
        result.insert(result.end(), countBytes, countBytes + sizeof(count));
        if (values.Count() > 0u)
        {
            const auto* begin = reinterpret_cast<const unsigned char*>(values.Data());
            result.insert(result.end(), begin, begin + sizeof(*values.Data()) * values.Count());
        }
    };
    for (uint32_t chain = 0u; chain < state.chains.Count(); ++chain)
    {
        Append(state.chains.At(chain)->bones);
        Append(state.chains.At(chain)->simulation.points);
        Append(state.chains.At(chain)->simulation.previousTargets);
        Append(state.chains.At(chain)->transforms);
    }
    Append(state.colliders);
    Append(state.links);
    Append(state.distanceConstraints);
    Append(state.contactLinks);
    Append(state.contactPoints);
    Append(state.contactAnchors);
    Append(state.previousBaseMatrices);
    Append(state.previousCollisionShapes);
    Append(state.previousBasePose.localTransforms);
    Append(state.previousBasePose.morphWeights);
    Append(state.distanceLambdas);
    return result;
}

/**
 * 公開失敗を検査する一回の試行結果と、そこまでに実行した計算量を保持する。
 */
struct FAllocationAttempt
{
    // 更新が候補の確定まで成功したか。
    bool accepted = false;
    // 最終の描画用FK検査まで到達したかを成功試行と比較する値。
    uint64_t poseEvaluations = 0u;
    // 共有stepの予測と接触制約を完了したか比較する値。
    uint64_t referenceCalculations = 0u;
};

/**
 * 同じ初期履歴から指定回数の確保を許可し、失敗時に公開状態とcacheを厳密比較する。
 */
bool TryAllocationBudget(uint32_t budget, FAllocationAttempt& result, double delta = 1.0 / 60.0)
{
    FSubstepTestFixture fixture;
    if (!MakeFixture(fixture, true))
    {
        return false;
    }
    const auto instance = fixture.instances[0];
    if (gk::UpdateModelAnimation(instance, delta) != 0)
    {
        return false;
    }
    auto* playback = gk::detail::FindModelTransform(instance)->playback;
    const auto* state = playback->secondaryMotion;
    const auto* cache = playback->basePoseCache;
    if (!cache || !state->velocityHistoryValid)
    {
        return false;
    }
    const auto stateBytes = Snapshot(*state);
    const double savedDistanceStep = state->distanceStepSeconds;
    const auto* firstPoints = state->chains.At(0u)->simulation.points.Data();
    const auto* secondPoints = state->chains.At(1u)->simulation.points.Data();
    const auto* savedLocalTransforms = state->previousBasePose.localTransforms.Data();
    const auto* savedMorphWeights = state->previousBasePose.morphWeights.Data();
    const std::vector<gk::model::animation::FModelBoneTransform> cacheTransforms(cache->pose.localTransforms.Data(), cache->pose.localTransforms.Data() + cache->pose.localTransforms.Count());
    const std::vector<float> cacheMorphs(cache->pose.morphWeights.Data(), cache->pose.morphWeights.Data() + cache->pose.morphWeights.Count());
    const uint64_t cacheRevision = cache->revision;
    const uint32_t bones[5] = { 0u, 1u, 2u, 3u, 4u };
    gk::Vec3 before[5]{};
    if (gk::GetModelBonePositions(instance, bones, 5u, before) != 0 || playback->basePoseCache != cache)
    {
        return false;
    }
    gk::model::animation::ResetModelPoseWorkForTesting();
    gk::model::animation::ResetSecondaryMotionReferenceCalculationCount();
    gk::SetAllocationFailureAfterForTesting(budget);
    result.accepted = gk::UpdateModelSecondaryMotion(instance, delta) == 0;
    gk::ResetAllocationFailureForTesting();
    result.poseEvaluations = gk::model::animation::GetModelPoseEvaluationCountForTesting();
    result.referenceCalculations = gk::model::animation::GetSecondaryMotionReferenceCalculationCount();
    if (result.accepted)
    {
        return true;
    }
    // 更新拒否後にfalseとなる履歴flag以外は、所有先も保存値も保持する。
    const auto* preserved = ReadState(instance);
    if (preserved != state || preserved->velocityHistoryValid || Snapshot(*preserved) != stateBytes || preserved->distanceStepSeconds != savedDistanceStep || preserved->chains.At(0u)->simulation.points.Data() != firstPoints || preserved->chains.At(1u)->simulation.points.Data() != secondPoints || preserved->previousBasePose.localTransforms.Data() != savedLocalTransforms || preserved->previousBasePose.morphWeights.Data() != savedMorphWeights || playback->basePoseCache != cache || cache->revision != cacheRevision || cache->pose.localTransforms.Count() != cacheTransforms.size() || cache->pose.morphWeights.Count() != cacheMorphs.size() || memcmp(cache->pose.localTransforms.Data(), cacheTransforms.data(), cacheTransforms.size() * sizeof(cacheTransforms[0])) != 0 || memcmp(cache->pose.morphWeights.Data(), cacheMorphs.data(), cacheMorphs.size() * sizeof(float)) != 0)
    {
        fprintf(stderr, "allocation rejection changed published secondary state, owned pose, or cache: budget=%u\n", budget);
        return false;
    }
    gk::Vec3 after[5]{};
    if (gk::GetModelBonePositions(instance, bones, 5u, after) != 0 || memcmp(before, after, sizeof(before)) != 0 || playback->basePoseCache != cache)
    {
        fprintf(stderr, "allocation rejection changed public bone pose: budget=%u\n", budget);
        return false;
    }
    return true;
}

/**
 * 成功に必要な確保数を有界探索し、共有step完了後の最後の確保失敗でも確定前状態を保つことを確かめる。
 */
bool TestLateAllocationFailureAtomicity(double delta = 1.0 / 60.0)
{
    // 正常な小fixtureが際限なく確保を増やした場合も検査を終了する。
    constexpr uint32_t maximumBudget = 4096u;
    uint32_t rejectedBudget = 0u;
    uint32_t acceptedBudget = 1u;
    FAllocationAttempt attempt;
    while (acceptedBudget <= maximumBudget)
    {
        if (!TryAllocationBudget(acceptedBudget, attempt, delta))
        {
            return false;
        }
        if (attempt.accepted)
        {
            break;
        }
        rejectedBudget = acceptedBudget;
        acceptedBudget *= 2u;
    }
    if (acceptedBudget > maximumBudget)
    {
        fprintf(stderr, "shared substep allocation budget exceeded %u\n", maximumBudget);
        return false;
    }
    while (acceptedBudget - rejectedBudget > 1u)
    {
        const uint32_t middle = rejectedBudget + (acceptedBudget - rejectedBudget) / 2u;
        if (!TryAllocationBudget(middle, attempt, delta))
        {
            return false;
        }
        if (attempt.accepted)
        {
            acceptedBudget = middle;
        }
        else
        {
            rejectedBudget = middle;
        }
    }
    FAllocationAttempt completed;
    FAllocationAttempt lateFailure;
    if (!TryAllocationBudget(acceptedBudget, completed, delta) || !completed.accepted || !TryAllocationBudget(acceptedBudget - 1u, lateFailure, delta) || lateFailure.accepted || completed.poseEvaluations < 3u || completed.referenceCalculations == 0u || lateFailure.poseEvaluations != completed.poseEvaluations || lateFailure.referenceCalculations != completed.referenceCalculations)
    {
        fprintf(stderr, "late allocation failure did not reach completed shared steps and rendered FK: budget=%u successFk=%llu failedFk=%llu successReferences=%llu failedReferences=%llu\n", acceptedBudget, static_cast<unsigned long long>(completed.poseEvaluations), static_cast<unsigned long long>(lateFailure.poseEvaluations), static_cast<unsigned long long>(completed.referenceCalculations), static_cast<unsigned long long>(lateFailure.referenceCalculations));
        return false;
    }
    return true;
}

/**
 * 局所姿勢履歴の所有を検査し、設定変更直後と短い更新では共有分割を使わないことを確かめる。
 */
bool TestOwnedHistoryAndStepBoundaries()
{
    FSubstepTestFixture fixture;
    if (!MakeFixture(fixture, false))
    {
        return false;
    }
    const auto instance = fixture.instances[0];
    const auto* state = ReadState(instance);
    const auto* cache = gk::detail::FindModelTransform(instance)->playback->basePoseCache;
    if (!cache || state->previousBasePose.localTransforms.Count() != 5u || state->previousBasePose.morphWeights.Count() != 1u || state->previousBasePose.localTransforms.Data() == cache->pose.localTransforms.Data() || state->previousBasePose.morphWeights.Data() == cache->pose.morphWeights.Data())
    {
        fprintf(stderr, "secondary motion base pose history does not own its arrays\n");
        return false;
    }
    gk::model::FModelSecondaryMotionState copied;
    gk::String error;
    const auto original = Snapshot(*state);
    if (!copied.CloneFrom(*state, error) || Snapshot(copied) != original || copied.previousBasePose.localTransforms.Data() == state->previousBasePose.localTransforms.Data() || copied.previousBasePose.morphWeights.Data() == state->previousBasePose.morphWeights.Data())
    {
        return false;
    }
    copied.previousBasePose.localTransforms.At(0u).position[0] = 123.0f;
    copied.previousBasePose.morphWeights.At(0u) = 0.75f;
    if (Snapshot(*state) != original)
    {
        return false;
    }
    const double deltas[7] = { 0.0, 1.0 / 240.0, 1.0 / 120.0, 1.0 / 60.0, 1.0 / 30.0, std::nextafter(0.25, 0.0), 0.25 };
    const uint32_t expectedCounts[7] = { 1u, 1u, 1u, 2u, 2u, 2u, 1u };
    for (uint32_t index = 0u; index < 7u; ++index)
    {
        uint32_t count = 0u;
        if (!gk::model::GetSecondaryMotionWeightedStepCount(*state, state->previousBaseMatrices, deltas[index], count, error) || count != expectedCounts[index])
        {
            fprintf(stderr, "shared step boundary mismatch: delta=%.17g count=%u expected=%u\n", deltas[index], count, expectedCounts[index]);
            return false;
        }
    }
    gk::Array<float> shiftedMatrices;
    if (!shiftedMatrices.AppendRange(state->previousBaseMatrices.Data(), state->previousBaseMatrices.Count()))
    {
        return false;
    }
    // 正の位置へ動く二本目を使い、floatの次の値をdoubleでも区別できるようにする。
    const auto& movedChain = *state->chains.At(1u);
    const uint32_t rootOffset = movedChain.bones.At(0u) * 16u + 12u;
    const float exactBoundary = movedChain.simulation.previousTargets.At(0u).x + movedChain.settings.teleportDistance;
    shiftedMatrices.At(rootOffset) = exactBoundary;
    uint32_t boundaryCount = 0u;
    if (!gk::model::GetSecondaryMotionWeightedStepCount(*state, shiftedMatrices, 1.0 / 60.0, boundaryCount, error) || boundaryCount != 2u)
    {
        fprintf(stderr, "exact teleport-distance boundary unexpectedly reset the shared update\n");
        return false;
    }
    shiftedMatrices.At(rootOffset) = std::nextafter(exactBoundary, std::numeric_limits<float>::infinity());
    if (!gk::model::GetSecondaryMotionWeightedStepCount(*state, shiftedMatrices, 1.0 / 60.0, boundaryCount, error) || boundaryCount != 1u)
    {
        fprintf(stderr, "shared update missed the first float above teleport distance\n");
        return false;
    }
    if (gk::SetModelSecondaryMotionChainColliderMask(instance, 1u, 1u) != 0)
    {
        return false;
    }
    state = ReadState(instance);
    uint32_t firstCount = 0u;
    if (state->velocityHistoryValid || state->previousBasePose.localTransforms.Count() != 0u || state->previousBasePose.morphWeights.Count() != 0u || !gk::model::GetSecondaryMotionWeightedStepCount(*state, copied.previousBaseMatrices, 1.0 / 60.0, firstCount, error) || firstCount != 1u)
    {
        fprintf(stderr, "setting replacement retained a usable previous pose or subdivided cold history\n");
        return false;
    }
    return true;
}

/**
 * 身体接触がない円弧運動で、位置接触を二回に抑えても小刻みの予測と最後の速度時刻を保つことを確かめる。
 */
bool TestBoundedContactsKeepMicroPrediction()
{
    using namespace gk::model::animation;
    FSubstepTestFixture fixture;
    if (!MakeFixture(fixture, false))
    {
        return false;
    }
    auto* source = static_cast<ASubstepTestSource*>(gk::detail::FindModel(fixture.model)->animation->source);
    source->rotatingPivot = true;
    source->rotationRateScale = 0.5;
    source->skeleton.parents.At(1u) = 0;
    source->skeleton.parents.At(3u) = 0;
    const auto instance = fixture.instances[0];
    for (uint32_t chain = 0u; chain < 2u; ++chain)
    {
        // 回転する親の半径0.5の円弧を瞬間移動とは扱わない検査設定。
        ReadState(instance)->chains.At(chain)->settings.teleportDistance = 10.0f;
    }
    if (gk::ResetModelSecondaryMotion(instance) != 0 || gk::UpdateModelSecondaryMotion(instance, 0.0) != 0)
    {
        return false;
    }
    const auto* initial = ReadState(instance);
    FModelSecondaryMotionChainState prediction[2];
    for (uint32_t chain = 0u; chain < 2u; ++chain)
    {
        if (!prediction[chain].points.AppendRange(initial->chains.At(chain)->simulation.points.Data(), 3u) || !prediction[chain].previousTargets.AppendRange(initial->chains.At(chain)->simulation.previousTargets.Data(), 3u))
        {
            return false;
        }
    }
    const double delta = 1.0 / 30.0;
    const double microSeconds = delta / 4.0;
    gk::Vec3 lastMicroPreviousRoots[2]{};
    gk::Vec3 lastMacroPreviousRoots[2]{};
    gk::String error;
    for (uint32_t micro = 1u; micro <= 4u; ++micro)
    {
        FModelPose pose;
        gk::Array<float> matrices;
        if (!source->Sample(0u, microSeconds * micro, pose, error) || !EvaluateModelPose(source->skeleton, pose, matrices, error))
        {
            return false;
        }
        for (uint32_t chain = 0u; chain < 2u; ++chain)
        {
            const auto& definition = *initial->chains.At(chain);
            gk::Vec3 targets[3]{};
            for (uint32_t bone = 0u; bone < 2u; ++bone)
            {
                const uint32_t offset = definition.bones.At(bone) * 16u;
                targets[bone] = { matrices.At(offset + 12u), matrices.At(offset + 13u), matrices.At(offset + 14u) };
            }
            const uint32_t tipOffset = definition.bones.At(1u) * 16u;
            targets[2] = { targets[1].x + matrices.At(tipOffset + 4u) * 0.25f, targets[1].y + matrices.At(tipOffset + 5u) * 0.25f, targets[1].z + matrices.At(tipOffset + 6u) * 0.25f };
            if (micro == 2u)
            {
                lastMacroPreviousRoots[chain] = targets[0];
            }
            if (micro == 4u)
            {
                lastMicroPreviousRoots[chain] = prediction[chain].points.At(0u).position;
            }
            if (!StepSecondaryMotionChainSingleStep(targets, 3u, definition.settings, microSeconds, prediction[chain], error))
            {
                return false;
            }
        }
    }
    ResetSecondaryMotionReferenceCalculationCount();
    ResetModelPoseWorkForTesting();
    ResetSecondaryMotionAttachmentJacobianCountForTesting();
    if (!Advance(instance, delta))
    {
        fprintf(stderr, "bounded-contact curved-root update failed: %s\n", gk::GetLastErrorMessage());
        return false;
    }
    // 各鎖二線分を四回予測し、途中三時刻の局所FKを最終姿勢と別に評価する。
    const uint64_t referenceWork = GetSecondaryMotionReferenceCalculationCount();
    const uint64_t poseWork = GetModelPoseEvaluationCountForTesting();
    const uint64_t contactWork = GetSecondaryMotionAttachmentJacobianCountForTesting();
    if (referenceWork != 16u || poseWork < 5u || contactWork != 12u)
    {
        fprintf(stderr, "bounded contacts skipped micro predictions or exceeded two contact solves: references=%llu FK=%llu contactJacobians=%llu\n", static_cast<unsigned long long>(referenceWork), static_cast<unsigned long long>(poseWork), static_cast<unsigned long long>(contactWork));
        return false;
    }
    const auto* solved = ReadState(instance);
    for (uint32_t chain = 0u; chain < 2u; ++chain)
    {
        const auto& points = solved->chains.At(chain)->simulation.points;
        const auto& expected = prediction[chain].points;
        const auto& finalRoot = points.At(0u).position;
        const gk::Vec3 expectedRootVelocity = { static_cast<float>((static_cast<double>(finalRoot.x) - lastMicroPreviousRoots[chain].x) / microSeconds), static_cast<float>((static_cast<double>(finalRoot.y) - lastMicroPreviousRoots[chain].y) / microSeconds), static_cast<float>((static_cast<double>(finalRoot.z) - lastMicroPreviousRoots[chain].z) / microSeconds) };
        const double macroVelocityX = (static_cast<double>(finalRoot.x) - lastMacroPreviousRoots[chain].x) / (delta / 2.0);
        const double macroVelocityY = (static_cast<double>(finalRoot.y) - lastMacroPreviousRoots[chain].y) / (delta / 2.0);
        if (fabs(expectedRootVelocity.x - macroVelocityX) + fabs(expectedRootVelocity.y - macroVelocityY) < 0.1)
        {
            fprintf(stderr, "curved-root fixture cannot distinguish micro and macro secant velocity\n");
            return false;
        }
        for (uint32_t point = 0u; point < 3u; ++point)
        {
            const auto& current = points.At(point);
            const auto& predicted = expected.At(point);
            const double positionDifference = fabs(static_cast<double>(current.position.x) - predicted.position.x) + fabs(static_cast<double>(current.position.y) - predicted.position.y) + fabs(static_cast<double>(current.position.z) - predicted.position.z);
            const double relativeVelocityDifference = fabs((static_cast<double>(current.velocity.x) - points.At(0u).velocity.x) - (static_cast<double>(predicted.velocity.x) - expected.At(0u).velocity.x)) + fabs((static_cast<double>(current.velocity.y) - points.At(0u).velocity.y) - (static_cast<double>(predicted.velocity.y) - expected.At(0u).velocity.y)) + fabs((static_cast<double>(current.velocity.z) - points.At(0u).velocity.z) - (static_cast<double>(predicted.velocity.z) - expected.At(0u).velocity.z));
            if (!std::isfinite(positionDifference) || !std::isfinite(relativeVelocityDifference) || positionDifference > 2.0e-5 || relativeVelocityDifference > 5.0e-5)
            {
                fprintf(stderr, "bounded contact changed clear micro prediction: chain=%u point=%u position=%.9g relativeVelocity=%.9g\n", chain, point, positionDifference, relativeVelocityDifference);
                return false;
            }
            if (point > 0u)
            {
                const auto& parent = points.At(point - 1u).position;
                const double x = static_cast<double>(current.position.x) - parent.x;
                const double y = static_cast<double>(current.position.y) - parent.y;
                const double z = static_cast<double>(current.position.z) - parent.z;
                if (fabs(sqrt(x * x + y * y + z * z) - (point == 1u ? 1.0 : 0.25)) > 1.0e-5)
                {
                    return false;
                }
            }
        }
        const auto& actualRootVelocity = points.At(0u).velocity;
        if (fabs(actualRootVelocity.x - expectedRootVelocity.x) + fabs(actualRootVelocity.y - expectedRootVelocity.y) + fabs(actualRootVelocity.z - expectedRootVelocity.z) > 1.0e-5)
        {
            fprintf(stderr, "bounded contact kept macro rather than last-micro root velocity\n");
            return false;
        }
    }
    gk::model::FModelSecondaryMotionState zeroExpected;
    if (!zeroExpected.CloneFrom(*solved, error))
    {
        return false;
    }
    for (uint32_t chain = 0u; chain < zeroExpected.chains.Count(); ++chain)
    {
        // 時間0では固定rootの速度だけ0になる既存動作を保ち、自由点や位置へ積分を加えない。
        zeroExpected.chains.At(chain)->simulation.points.At(0u).velocity = {};
    }
    if (gk::UpdateModelSecondaryMotion(instance, 0.0) != 0 || Snapshot(*ReadState(instance)) != Snapshot(zeroExpected))
    {
        fprintf(stderr, "zero-time contact changed a bounded prediction snapshot\n");
        return false;
    }
    return true;
}

/**
 * reset直前の最大区間でも予測を三十回に分け、身体接触の処理回数と保存値を有限に保つことを確かめる。
 */
bool TestBoundedPredictionNearResetLimit()
{
    using namespace gk::model::animation;
    FSubstepTestFixture fixture;
    if (!MakeFixture(fixture, false))
    {
        return false;
    }
    ResetSecondaryMotionReferenceCalculationCount();
    ResetSecondaryMotionAttachmentJacobianCountForTesting();
    const double delta = std::nextafter(0.25, 0.0);
    if (!Advance(fixture.instances[0], delta))
    {
        fprintf(stderr, "bounded prediction failed just below long-reset threshold: %s\n", gk::GetLastErrorMessage());
        return false;
    }
    const uint64_t referenceWork = GetSecondaryMotionReferenceCalculationCount();
    const uint64_t contactWork = GetSecondaryMotionAttachmentJacobianCountForTesting();
    if (referenceWork != 120u || contactWork != 12u)
    {
        fprintf(stderr, "bounded prediction work grew contact processing with the interval: references=%llu contactJacobians=%llu\n", static_cast<unsigned long long>(referenceWork), static_cast<unsigned long long>(contactWork));
        return false;
    }
    const auto* state = ReadState(fixture.instances[0]);
    for (uint32_t chain = 0u; chain < state->chains.Count(); ++chain)
    {
        for (uint32_t point = 0u; point < state->chains.At(chain)->simulation.points.Count(); ++point)
        {
            const auto& current = state->chains.At(chain)->simulation.points.At(point);
            if (!std::isfinite(current.position.x) || !std::isfinite(current.position.y) || !std::isfinite(current.position.z) || !std::isfinite(current.velocity.x) || !std::isfinite(current.velocity.y) || !std::isfinite(current.velocity.z))
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * 明らかに離れた身体との接触検査で、位置だけの後検査や定義速度に微分計算を使わないことを確かめる。
 */
bool TestContactPositionChecksAvoidUnusedJacobians()
{
    using namespace gk::model::animation;
    FSubstepTestFixture fixture;
    if (!MakeFixture(fixture, false))
    {
        return false;
    }
    const auto instance = fixture.instances[0];
    const gk::FModelSecondaryMotionContactLink link = { 2u, { 0.05f, 0.0f, 0.0f }, 2u, { -0.05f, 0.1f, 0.0f }, 0.01f };
    if (gk::SetModelSecondaryMotionContactLinks(instance, &link, 1u) != 0 || gk::ResetModelSecondaryMotion(instance) != 0 || gk::UpdateModelSecondaryMotion(instance, 0.0) != 0)
    {
        return false;
    }
    const auto* previous = ReadState(instance);
    const auto* source = gk::detail::FindModel(instance)->animation->source;
    const auto original = Snapshot(*previous);
    // 位置solverは初回とfloat保存確認で勾配を評価し、正時間では速度solverにも各寄与と線分両端の微分が必要になる。
    const uint64_t requiredPerSolver = previous->contactAnchors.Count() + previous->contactLinks.Count() * 2u;
    uint64_t measured[2]{};
    for (uint32_t positive = 0u; positive < 2u; ++positive)
    {
        gk::model::FModelSecondaryMotionState candidate;
        gk::String error;
        if (!candidate.CloneFrom(*previous, error))
        {
            return false;
        }
        ResetSecondaryMotionAttachmentJacobianCountForTesting();
        const bool solved = gk::model::ConstrainSecondaryMotionContactPoints(candidate, *previous, source->Skeleton(), previous->previousBaseMatrices, previous->previousCollisionShapes.Data(), previous->previousCollisionShapes.Count(), positive == 0u ? 0.0 : 1.0 / 120.0, error, true);
        const uint64_t count = GetSecondaryMotionAttachmentJacobianCountForTesting();
        measured[positive] = count;
        const uint64_t expectedMaximum = requiredPerSolver * (positive == 0u ? 2u : 3u);
        if (!solved || count == 0u || count > expectedMaximum || Snapshot(candidate) != original || Snapshot(*previous) != original)
        {
            fprintf(stderr, "position-only contact validation or definition velocity evaluated unused Jacobians: positive=%u count=%llu maximum=%llu error=%s\n", positive, static_cast<unsigned long long>(count), static_cast<unsigned long long>(expectedMaximum), error.CStr());
            return false;
        }
    }
    fprintf(stdout, "contact position-only validation Jacobians: zero=%llu positive=%llu\n", static_cast<unsigned long long>(measured[0]), static_cast<unsigned long long>(measured[1]));
    return true;
}

/**
 * 三球の外側に狭い可行方向を持つ鎖と、その方向で成立済みの前回状態を作る。
 */
bool MakeHitchDirectionFixture(gk::model::FModelSecondaryMotionState& previous, gk::model::FModelSecondaryMotionState& candidate, gk::model::animation::FModelSkeleton& skeleton, gk::Array<float>& matrices, gk::model::animation::FModelSecondaryMotionCollisionShape (&shapes)[3], gk::String& error)
{
    using namespace gk::model::animation;
    const int32_t parents[3] = { -1, -1, 1 };
    FModelBoneTransform rest[3]{};
    rest[2].position[1] = 1.0f;
    FModelPose pose;
    if (!skeleton.parents.AppendRange(parents, 3u) || !skeleton.restLocalTransforms.AppendRange(rest, 3u) || !InitializeModelPose(skeleton, pose, error) || !EvaluateModelPose(skeleton, pose, matrices, error))
    {
        return false;
    }
    auto* chain = new gk::model::FModelSecondaryMotionChain;
    if (!previous.chains.Append(chain))
    {
        delete chain;
        return false;
    }
    const uint32_t bones[2] = { 1u, 2u };
    const gk::Vec3 targets[3] = { {}, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.25f, 0.0f } };
    chain->settings.gravity = {};
    chain->settings.windAcceleration = {};
    chain->settings.maxAngleDegrees = 180.0f;
    chain->settings.endOffset = { 0.0f, 0.25f, 0.0f };
    chain->settings.teleportDistance = 0.5f;
    previous.linkIterations = 1u;
    const gk::FModelSecondaryMotionContactPoint contact = { 0u, 1u, 0.0f };
    const gk::FModelSecondaryMotionContactAnchor anchor = { 2u, {}, 1.0f };
    if (!chain->bones.AppendRange(bones, 2u) || !chain->transforms.AppendRange(rest + 1u, 2u) || !ResetSecondaryMotionChain(targets, 3u, chain->simulation, error) || !previous.contactPoints.Append(contact) || !previous.contactAnchors.Append(anchor))
    {
        return false;
    }
    // 単位方向の各成分に近い下限を三球で与え、独立に既知の可行方向を用意する。
    const double witness[3] = { 0.3, 0.4, sqrt(0.75) };
    const double directionMargin = 0.003;
    for (uint32_t axis = 0u; axis < 3u; ++axis)
    {
        shapes[axis].start = { axis == 0u ? -1.0f : 0.0f, axis == 1u ? -1.0f : 0.0f, axis == 2u ? -1.0f : 0.0f };
        shapes[axis].end = shapes[axis].start;
        shapes[axis].radius = static_cast<float>(sqrt(2.0 + 2.0 * (witness[axis] - directionMargin)));
    }
    for (uint32_t point = 1u; point < 3u; ++point)
    {
        const double length = point == 1u ? 1.0 : 1.25;
        chain->simulation.points.At(point).position = { static_cast<float>(witness[0] * length), static_cast<float>(witness[1] * length), static_cast<float>(witness[2] * length) };
        // 前回の勢いは、長時間resetの幾何seedへ持ち込ませない。
        chain->simulation.points.At(point).velocity = { 2.0f, -3.0f, 4.0f };
    }
    for (uint32_t axis = 0u; axis < 3u; ++axis)
    {
        const auto& point = chain->simulation.points.At(1u).position;
        const double x = static_cast<double>(point.x) - shapes[axis].start.x;
        const double y = static_cast<double>(point.y) - shapes[axis].start.y;
        const double z = static_cast<double>(point.z) - shapes[axis].start.z;
        // 元の身体表面に対して既知方向の自由端が外側にあることを独立に確認する。
        if (sqrt(x * x + y * y + z * z) <= shapes[axis].radius)
        {
            return false;
        }
    }
    if (!candidate.CloneFrom(previous, error) || !ResetSecondaryMotionChain(targets, 3u, candidate.chains.At(0u)->simulation, error))
    {
        return false;
    }
    return true;
}

/**
 * 長い更新でrest候補が停滞した場合だけ、前回の成立方向を速度0のまま再利用できることを確かめる。
 */
bool TestHitchResetUsesPreviousGeometryAfterStall()
{
    using namespace gk::model::animation;
    gk::model::FModelSecondaryMotionState previous;
    gk::model::FModelSecondaryMotionState candidate;
    FModelSkeleton skeleton;
    gk::Array<float> matrices;
    FModelSecondaryMotionCollisionShape shapes[3]{};
    gk::String error;
    if (!MakeHitchDirectionFixture(previous, candidate, skeleton, matrices, shapes, error))
    {
        return false;
    }
    gk::model::FModelSecondaryMotionState noPrevious;
    gk::model::FModelSecondaryMotionState cold;
    gk::model::FModelSecondaryMotionState zeroDelta;
    if (!cold.CloneFrom(candidate, error) || !zeroDelta.CloneFrom(candidate, error))
    {
        return false;
    }
    const auto original = Snapshot(candidate);
    if (gk::model::ConstrainSecondaryMotionContactPoints(cold, noPrevious, skeleton, matrices, shapes, 3u, 0.3, error) || Snapshot(cold) != original)
    {
        fprintf(stderr, "hitch geometry fixture did not reproduce an atomic rest-seed stall\n");
        return false;
    }
    if (!gk::model::ConstrainSecondaryMotionContactPoints(zeroDelta, previous, skeleton, matrices, shapes, 3u, 0.0, error))
    {
        fprintf(stderr, "hitch geometry witness failed its independent zero-time direction retry: %s\n", error.CStr());
        return false;
    }
    const auto previousBytes = Snapshot(previous);
    // 閾値そのものと、実時間記録で連続した長い更新を同じ幾何契約へ通す。
    const double longDeltas[4] = { 0.25, 0.3, 0.7449875, 0.4312807 };
    for (double delta : longDeltas)
    {
        if (!candidate.CloneFrom(cold, error) || !gk::model::ConstrainSecondaryMotionContactPoints(candidate, previous, skeleton, matrices, shapes, 3u, delta, error))
        {
            fprintf(stderr, "long reset discarded a feasible previous direction after rest-seed stall: delta=%.17g error=%s\n", delta, error.CStr());
            return false;
        }
        if (Snapshot(candidate) != Snapshot(zeroDelta) || Snapshot(previous) != previousBytes)
        {
            fprintf(stderr, "long reset geometry retry changed the zero-time result, restored old velocity, or mutated history\n");
            return false;
        }
    }
    for (uint32_t index = 0u; index < candidate.chains.At(0u)->simulation.points.Count(); ++index)
    {
        const auto& point = candidate.chains.At(0u)->simulation.points.At(index);
        if (point.velocity.x != 0.0f || point.velocity.y != 0.0f || point.velocity.z != 0.0f)
        {
            return false;
        }
    }
    // 通常のrest解が成立する配置では、履歴を与えても従来結果を一切変更しない。
    FModelSecondaryMotionCollisionShape distant[3] = { shapes[0], shapes[1], shapes[2] };
    for (auto& shape : distant)
    {
        shape.start.x += 10.0f;
        shape.end.x += 10.0f;
    }
    if (!candidate.CloneFrom(cold, error) || !gk::model::ConstrainSecondaryMotionContactPoints(candidate, previous, skeleton, matrices, distant, 3u, 0.3, error) || Snapshot(candidate) != original)
    {
        fprintf(stderr, "successful long-reset rest candidate changed when previous geometry was available\n");
        return false;
    }
    for (uint32_t beyond = 0u; beyond < 2u; ++beyond)
    {
        gk::model::FModelSecondaryMotionState translated;
        if (!translated.CloneFrom(previous, error) || !candidate.CloneFrom(cold, error))
        {
            return false;
        }
        // 現在rootとの差が閾値ちょうどなら方向再試行し、float一個分でも超えたら除外する。
        const float oldRootX = beyond == 0u ? 0.5f : std::nextafter(0.5f, std::numeric_limits<float>::infinity());
        for (uint32_t point = 0u; point < 3u; ++point)
        {
            translated.chains.At(0u)->simulation.points.At(point).position.x += oldRootX;
            translated.chains.At(0u)->simulation.previousTargets.At(point).x += oldRootX;
        }
        const bool accepted = gk::model::ConstrainSecondaryMotionContactPoints(candidate, translated, skeleton, matrices, shapes, 3u, 0.3, error);
        if (accepted != (beyond == 0u) || (accepted ? Snapshot(candidate) != Snapshot(zeroDelta) : Snapshot(candidate) != original))
        {
            fprintf(stderr, "hitch geometry retry violated its teleport boundary or failure atomicity: beyond=%u\n", beyond);
            return false;
        }
    }
    gk::model::FModelSecondaryMotionState changedTopology;
    if (!changedTopology.CloneFrom(previous, error) || !candidate.CloneFrom(cold, error))
    {
        return false;
    }
    changedTopology.chains.At(0u)->bones.At(1u) = 0u;
    if (gk::model::ConstrainSecondaryMotionContactPoints(candidate, changedTopology, skeleton, matrices, shapes, 3u, 0.3, error) || error.Empty() || Snapshot(candidate) != original)
    {
        fprintf(stderr, "long-reset direction retry accepted changed bone ownership or changed failed candidate\n");
        return false;
    }
    return true;
}

/**
 * 長さの位置補正を速度へ変換せず、減衰後の予測速度の接線成分だけを残すことを確かめる。
 */
bool TestSingleStepPreservesPredictedTangentVelocity()
{
    using namespace gk::model::animation;
    const gk::Vec3 targets[2] = { {}, { 1.0f, 0.0f, 0.0f } };
    gk::FModelSecondaryMotionSettings settings{};
    settings.frequencyHz = 5.0f;
    settings.dampingRatio = 0.8f;
    settings.gravity = {};
    settings.windAcceleration = {};
    settings.maxAngleDegrees = 180.0f;
    const double step = 1.0 / 120.0;
    FModelSecondaryMotionChainState state;
    gk::String error;
    if (!ResetSecondaryMotionChain(targets, 2u, state, error))
    {
        return false;
    }
    state.points.At(1u).velocity = { 0.0f, 10.0f, 0.0f };
    // 初期位置は目標と一致するため、初回予測の加速度は0で減衰だけが作用する。
    const double omega = 2.0 * 3.14159265358979323846 * settings.frequencyHz;
    const double predictedY = 10.0 * exp(-2.0 * settings.dampingRatio * omega * step);
    const double predictedDisplacementY = predictedY * step;
    const double predictedLength = sqrt(1.0 + predictedDisplacementY * predictedDisplacementY);
    const double directionX = 1.0 / predictedLength;
    const double directionY = predictedDisplacementY / predictedLength;
    const double predictedNormal = directionY * predictedY;
    const double expectedVelocityX = -directionX * predictedNormal;
    const double expectedVelocityY = predictedY - directionY * predictedNormal;
    if (!StepSecondaryMotionChainSingleStep(targets, 2u, settings, step, state, error))
    {
        fprintf(stderr, "single-step tangent prediction failed: %s\n", error.CStr());
        return false;
    }
    const auto& root = state.points.At(0u);
    const auto& child = state.points.At(1u);
    const double length = sqrt(static_cast<double>(child.position.x) * child.position.x + static_cast<double>(child.position.y) * child.position.y + static_cast<double>(child.position.z) * child.position.z);
    const double radialVelocity = (static_cast<double>(child.position.x) * child.velocity.x + static_cast<double>(child.position.y) * child.velocity.y + static_cast<double>(child.position.z) * child.velocity.z) / length;
    const double tangentVelocity = -directionY * child.velocity.x + directionX * child.velocity.y;
    const double expectedTangentVelocity = directionX * predictedY;
    if (memcmp(&root.position, &targets[0], sizeof(gk::Vec3)) != 0 || root.velocity.x != 0.0f || root.velocity.y != 0.0f || root.velocity.z != 0.0f || fabs(length - 1.0) > 1.0e-6 || fabs(child.position.x - directionX) > 1.0e-6 || fabs(child.position.y - directionY) > 1.0e-6 || child.position.z != 0.0f)
    {
        fprintf(stderr, "single-step tangent fixture did not preserve its fixed root and normalized free prediction\n");
        return false;
    }
    if (fabs(radialVelocity) > 2.0e-6 || fabs(tangentVelocity - expectedTangentVelocity) > 2.0e-6 || fabs(child.velocity.x - expectedVelocityX) > 2.0e-6 || fabs(child.velocity.y - expectedVelocityY) > 2.0e-6 || child.velocity.z != 0.0f)
    {
        fprintf(stderr, "single-step length correction contaminated predicted velocity: radial=%.9g tangent=%.9g expectedTangent=%.9g velocity=(%.9g,%.9g) expected=(%.9g,%.9g)\n", radialVelocity, tangentVelocity, expectedTangentVelocity, child.velocity.x, child.velocity.y, expectedVelocityX, expectedVelocityY);
        return false;
    }
    return true;
}

/**
 * 分割済み区間の丸め境界で積分を再分割せず、長すぎる区間を状態変更前に拒否することを確かめる。
 */
bool TestSingleStepRoundingBoundary()
{
    using namespace gk::model::animation;
    const gk::Vec3 targets[3] = { {}, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.25f, 0.0f } };
    gk::FModelSecondaryMotionSettings settings{};
    settings.gravity = { 0.0f, -0.5f, 0.0f };
    settings.windAcceleration = { 0.0f, 0.0f, 0.3f };
    gk::String error;
    FModelSecondaryMotionChainState nominal;
    FModelSecondaryMotionChainState rounded;
    if (!ResetSecondaryMotionChain(targets, 3u, nominal, error) || !ResetSecondaryMotionChain(targets, 3u, rounded, error))
    {
        return false;
    }
    const double step = 1.0 / 120.0;
    const double roundedStep = std::nextafter(step, std::numeric_limits<double>::infinity());
    ResetSecondaryMotionReferenceCalculationCount();
    if (!StepSecondaryMotionChainSingleStep(targets, 3u, settings, step, nominal, error))
    {
        return false;
    }
    const uint64_t nominalWork = GetSecondaryMotionReferenceCalculationCount();
    ResetSecondaryMotionReferenceCalculationCount();
    if (!StepSecondaryMotionChainSingleStep(targets, 3u, settings, roundedStep, rounded, error) || nominalWork != 2u || GetSecondaryMotionReferenceCalculationCount() != nominalWork || memcmp(nominal.points.Data(), rounded.points.Data(), nominal.points.Count() * sizeof(FModelSecondaryMotionPoint)) != 0)
    {
        fprintf(stderr, "rounded single step was divided again or changed its prediction\n");
        return false;
    }
    const FModelSecondaryMotionPoint saved[3] = { rounded.points.At(0u), rounded.points.At(1u), rounded.points.At(2u) };
    const double invalidSteps[3] = { step * 1.01, -step, std::numeric_limits<double>::infinity() };
    for (double invalid : invalidSteps)
    {
        if (StepSecondaryMotionChainSingleStep(targets, 3u, settings, invalid, rounded, error) || memcmp(saved, rounded.points.Data(), sizeof(saved)) != 0 || memcmp(targets, rounded.previousTargets.Data(), sizeof(targets)) != 0)
        {
            fprintf(stderr, "invalid single-step interval changed its state\n");
            return false;
        }
    }
    return true;
}

/**
 * 入力拒否と確保失敗が直前の物理状態を保持し、resetとteleportが速度を消すことを確かめる。
 */
bool TestResetAndFailureAtomicity()
{
    FSubstepTestFixture fixture;
    if (!MakeFixture(fixture, false) || !Advance(fixture.instances[0], 1.0 / 60.0))
    {
        return false;
    }
    const auto instance = fixture.instances[0];
    const auto* previous = ReadState(instance);
    const auto snapshot = Snapshot(*previous);
    const double invalidDeltas[3] = { -0.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() };
    for (double delta : invalidDeltas)
    {
        if (gk::UpdateModelSecondaryMotion(instance, delta) != -1 || ReadState(instance) != previous || Snapshot(*ReadState(instance)) != snapshot)
        {
            fprintf(stderr, "invalid substep delta changed physical state\n");
            return false;
        }
    }
    gk::SetAllocationFailureAfterForTesting(0);
    const bool rejected = gk::UpdateModelSecondaryMotion(instance, 1.0 / 60.0) == -1;
    gk::ResetAllocationFailureForTesting();
    if (!rejected || ReadState(instance) != previous || Snapshot(*ReadState(instance)) != snapshot)
    {
        fprintf(stderr, "substep allocation failure changed physical state\n");
        return false;
    }
    // 明示reset、距離によるreset、長い時間によるresetを別々に検査する。
    const double targetTimes[3] = { 1.0, 2.0, 2.1 };
    const double updateSeconds[3] = { 0.0, 1.0 / 60.0, 0.3 };
    for (uint32_t mode = 0u; mode < 3u; ++mode)
    {
        if (gk::SetModelAnimationTime(instance, targetTimes[mode]) != 0 || (mode == 0u ? gk::ResetModelSecondaryMotion(instance) : gk::UpdateModelSecondaryMotion(instance, updateSeconds[mode])) != 0)
        {
            fprintf(stderr, "substep reset or teleport failed: mode=%u error=%s\n", mode, gk::GetLastErrorMessage());
            return false;
        }
        const auto* resetState = ReadState(instance);
        for (uint32_t chain = 0u; chain < resetState->chains.Count(); ++chain)
        {
            const auto& simulation = resetState->chains.At(chain)->simulation;
            for (uint32_t point = 0u; point < simulation.points.Count(); ++point)
            {
                const auto& position = simulation.points.At(point).position;
                const auto& velocity = simulation.points.At(point).velocity;
                const auto& target = simulation.previousTargets.At(point);
                if (memcmp(&position, &target, sizeof(position)) != 0 || velocity.x != 0.0f || velocity.y != 0.0f || velocity.z != 0.0f)
                {
                    fprintf(stderr, "outer reset or teleport was diluted into substeps: mode=%u chain=%u point=%u\n", mode, chain, point);
                    return false;
                }
            }
        }
        const auto resetSnapshot = Snapshot(*resetState);
        if (gk::UpdateModelSecondaryMotion(instance, 0.0) != 0 || Snapshot(*ReadState(instance)) != resetSnapshot)
        {
            fprintf(stderr, "zero-delta update changed the reset physical state: mode=%u\n", mode);
            return false;
        }
    }
    return true;
}

/**
 * 親基準の補間が単位回転と骨長を保ち、両端と失敗時の出力を保持することを確かめる。
 */
bool TestLocalPoseInterpolation()
{
    using namespace gk::model::animation;
    FModelSkeleton skeleton;
    const int32_t parents[2] = { -1, 0 };
    FModelBoneTransform rest[2]{};
    rest[1].position[0] = 1.0f;
    if (!skeleton.parents.AppendRange(parents, 2u) || !skeleton.restLocalTransforms.AppendRange(rest, 2u) || !skeleton.restMorphWeights.Append(0.2f))
    {
        return false;
    }
    gk::String error;
    FModelPose first;
    FModelPose second;
    FModelPose output;
    if (!InitializeModelPose(skeleton, first, error) || !InitializeModelPose(skeleton, second, error))
    {
        return false;
    }
    first.localTransforms.At(0u).position[0] = 2.0f;
    first.localTransforms.At(0u).rotation[3] = 3.0f;
    second.localTransforms.At(0u).position[0] = 6.0f;
    second.localTransforms.At(0u).rotation[2] = -4.0f;
    second.localTransforms.At(0u).rotation[3] = -4.0f;
    second.morphWeights.At(0u) = 0.8f;
    gk::Array<float> matrices;
    if (!BlendModelPoses(skeleton, first, second, 0.5f, output, error) || !EvaluateModelPose(skeleton, output, matrices, error))
    {
        return false;
    }
    const auto& rotation = output.localTransforms.At(0u).rotation;
    const double norm = static_cast<double>(rotation[0]) * rotation[0] + static_cast<double>(rotation[1]) * rotation[1] + static_cast<double>(rotation[2]) * rotation[2] + static_cast<double>(rotation[3]) * rotation[3];
    const double diagonal = sqrt(0.5);
    if (fabs(norm - 1.0) > 1.0e-6 || fabs(matrices.At(28u) - 4.0 - diagonal) > 1.0e-6 || fabs(matrices.At(29u) - diagonal) > 1.0e-6 || fabs(output.morphWeights.At(0u) - 0.5f) > 1.0e-6)
    {
        fprintf(stderr, "local pose interpolation shortened a rotating child or failed quaternion normalization\n");
        return false;
    }
    for (uint32_t endpoint = 0u; endpoint < 2u; ++endpoint)
    {
        const auto& expected = endpoint == 0u ? first : second;
        if (!BlendModelPoses(skeleton, first, second, static_cast<float>(endpoint), output, error) || !EvaluateModelPose(skeleton, output, matrices, error) || memcmp(output.localTransforms.At(0u).position, expected.localTransforms.At(0u).position, sizeof(float) * 3u) != 0 || output.morphWeights.At(0u) != expected.morphWeights.At(0u) || fabs(matrices.At(28u) - (endpoint == 0u ? 3.0 : 6.0)) > 1.0e-6 || fabs(matrices.At(29u) - static_cast<double>(endpoint)) > 1.0e-6)
        {
            fprintf(stderr, "local pose interpolation changed an exact endpoint\n");
            return false;
        }
    }
    const FModelBoneTransform saved[2] = { output.localTransforms.At(0u), output.localTransforms.At(1u) };
    const float savedMorph = output.morphWeights.At(0u);
    second.localTransforms.At(1u).rotation[3] = 0.0f;
    const bool rejected = !BlendModelPoses(skeleton, first, second, 0.5f, output, error);
    if (!rejected || error.Empty() || memcmp(saved, output.localTransforms.Data(), sizeof(saved)) != 0 || output.morphWeights.At(0u) != savedMorph)
    {
        fprintf(stderr, "invalid local rotation changed interpolation output\n");
        return false;
    }
    return true;
}
}

/**
 * 合成骨格だけで共通時間分割と履歴の契約を実行する。
 */
bool RunSecondaryMotionSubstepContracts()
{
    const bool interpolation = TestLocalPoseInterpolation();
    const bool clear = TestCommonSubsteps(false);
    const bool active = TestCommonSubsteps(true);
    const bool rotating = TestCommonSubsteps(false, true);
    const bool lifecycle = TestResetAndFailureAtomicity();
    const bool allocation = TestLateAllocationFailureAtomicity() && TestLateAllocationFailureAtomicity(1.0 / 30.0);
    const bool ownership = TestOwnedHistoryAndStepBoundaries();
    const bool rounding = TestSingleStepRoundingBoundary();
    const bool tangentVelocity = TestSingleStepPreservesPredictedTangentVelocity();
    const bool hitchGeometry = TestHitchResetUsesPreviousGeometryAfterStall();
    const bool positionChecks = TestContactPositionChecksAvoidUnusedJacobians();
    const bool boundedPrediction = TestBoundedContactsKeepMicroPrediction();
    const bool boundedLimit = TestBoundedPredictionNearResetLimit();
    return interpolation && clear && active && rotating && lifecycle && allocation && ownership && rounding && tangentVelocity && hitchGeometry && positionChecks && boundedPrediction && boundedLimit;
}
