// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelSecondaryMotionStep.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/ModelSecondaryMotionContactPoints.h"
#include "model/animation/ModelSecondaryMotionColliders.h"
#include "model/animation/ModelPose.h"
#include <math.h>

namespace gk::model
{
namespace
{
/**
 * 補間に使う局所姿勢の数と有限値を検査する。古い不完全履歴は補間しない。
 */
bool ValidPreviousPose(const animation::FModelPose& pose, uint32_t boneCount)
{
    if (pose.localTransforms.Count() != boneCount)
    {
        return false;
    }
    for (uint32_t bone = 0; bone < boneCount; ++bone)
    {
        // 回転は0でない有限なquaternionだけを補間する。
        const auto& transform = pose.localTransforms.At(bone);
        double rotationLengthSquared = 0.0;
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            if (!isfinite(transform.position[axis]) || !isfinite(transform.scale[axis]))
            {
                return false;
            }
        }
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if (!isfinite(transform.rotation[component]))
            {
                return false;
            }
            rotationLengthSquared += static_cast<double>(transform.rotation[component]) * transform.rotation[component];
        }
        if (!(rotationLengthSquared > 0.0) || !isfinite(rotationLengthSquared))
        {
            return false;
        }
    }
    for (uint32_t morph = 0; morph < pose.morphWeights.Count(); ++morph)
    {
        if (!isfinite(pose.morphWeights.At(morph)))
        {
            return false;
        }
    }
    return true;
}

/**
 * 全骨格の同時刻の行列から、鎖の骨原点とローカル末端を作る。範囲外では失敗する。
 */
bool BuildTargets(const FModelSecondaryMotionChain& chain, const Array<float>& matrices, Array<Vec3>& output, String& error)
{
    output.Clear();
    if (chain.bones.Count() == 0u || matrices.Count() % 16u != 0u || chain.bones.Count() == UINT32_MAX || !output.Reserve(chain.bones.Count() + 1u))
    {
        error.Assign("secondary motion shared step target allocation or chain is invalid");
        return false;
    }
    for (uint32_t item = 0; item < chain.bones.Count(); ++item)
    {
        // bone番号を先に検査し、行列の積算offsetを範囲内に保つ。
        const uint32_t bone = chain.bones.At(item);
        if (bone >= matrices.Count() / 16u)
        {
            error.Assign("secondary motion shared step bone is outside the skeleton");
            return false;
        }
        const uint32_t offset = bone * 16u;
        if (!output.Append(Vec3{ matrices.At(offset + 12u), matrices.At(offset + 13u), matrices.At(offset + 14u) }))
        {
            error.Assign("secondary motion shared step target append failed");
            return false;
        }
    }
    // 末端は最後のboneの回転・拡大率を含む行列で変換する。
    const uint32_t offset = chain.bones.At(chain.bones.Count() - 1u) * 16u;
    const auto tip = chain.settings.endOffset;
    const Vec3 endpoint{ matrices.At(offset + 12u) + matrices.At(offset) * tip.x + matrices.At(offset + 4u) * tip.y + matrices.At(offset + 8u) * tip.z, matrices.At(offset + 13u) + matrices.At(offset + 1u) * tip.x + matrices.At(offset + 5u) * tip.y + matrices.At(offset + 9u) * tip.z, matrices.At(offset + 14u) + matrices.At(offset + 2u) * tip.x + matrices.At(offset + 6u) * tip.y + matrices.At(offset + 10u) * tip.z };
    if (!output.Append(endpoint))
    {
        error.Assign("secondary motion shared step endpoint append failed");
        return false;
    }
    return true;
}

/**
 * 次の予測候補へ直前の鎖状態だけを複写する。設定配列は最初の複製を再利用する。
 */
bool CopySimulation(const FModelSecondaryMotionState& source, FModelSecondaryMotionState& output, String& error)
{
    if (source.chains.Count() != output.chains.Count())
    {
        error.Assign("secondary motion shared step chain count changed");
        return false;
    }
    for (uint32_t index = 0; index < source.chains.Count(); ++index)
    {
        // 両状態の鎖は初回のCloneFromで同じ順に所有済み。
        const auto& previous = source.chains.At(index)->simulation;
        auto& candidate = output.chains.At(index)->simulation;
        candidate.points.Clear();
        candidate.previousTargets.Clear();
        if (!candidate.points.AppendRange(previous.points.Data(), previous.points.Count()) || !candidate.previousTargets.AppendRange(previous.previousTargets.Data(), previous.previousTargets.Count()))
        {
            error.Assign("secondary motion shared step simulation copy failed");
            return false;
        }
    }
    return true;
}

/**
 * 接触補正済みの時刻に対応する基準姿勢と身体形状を保存する。公開状態は変更しない。
 */
bool SaveHistory(FModelSecondaryMotionState& state, const animation::FModelPose& base, const Array<float>& matrices, const Array<animation::FModelSecondaryMotionCollisionShape>& shapes, String& error)
{
    state.previousBasePose.localTransforms.Clear();
    state.previousBasePose.morphWeights.Clear();
    state.previousBaseMatrices.Clear();
    state.previousCollisionShapes.Clear();
    if (!state.previousBasePose.localTransforms.AppendRange(base.localTransforms.Data(), base.localTransforms.Count()) || !state.previousBasePose.morphWeights.AppendRange(base.morphWeights.Data(), base.morphWeights.Count()) || !state.previousBaseMatrices.AppendRange(matrices.Data(), matrices.Count()) || !state.previousCollisionShapes.AppendRange(shapes.Data(), shapes.Count()))
    {
        error.Assign("secondary motion shared step history copy failed");
        return false;
    }
    state.velocityHistoryValid = true;
    return true;
}
}

bool GetSecondaryMotionWeightedStepCount(const FModelSecondaryMotionState& previous, const Array<float>& currentMatrices, double delta, uint32_t& count, String& error)
{
    error.Clear();
    if (!isfinite(delta) || delta < 0.0 || currentMatrices.Count() % 16u != 0u)
    {
        error.Assign("secondary motion shared step interval or matrices are invalid");
        return false;
    }
    for (uint32_t index = 0; index < currentMatrices.Count(); ++index)
    {
        if (!isfinite(currentMatrices.At(index)))
        {
            error.Assign("secondary motion shared step matrix is not finite");
            return false;
        }
    }
    if (delta == 0.0 || delta >= 0.25 || !previous.velocityHistoryValid || previous.previousBaseMatrices.Count() != currentMatrices.Count() || !ValidPreviousPose(previous.previousBasePose, currentMatrices.Count() / 16u) || previous.previousCollisionShapes.Count() != previous.colliders.Count())
    {
        count = 1u;
        return true;
    }
    for (uint32_t index = 0; index < previous.chains.Count(); ++index)
    {
        // フレーム全体のroot移動を先に調べ、小分けによるteleport判定の消失を防ぐ。
        const auto* chain = previous.chains.At(index);
        if (!chain || chain->bones.Count() == 0u || chain->simulation.points.Count() != static_cast<uint64_t>(chain->bones.Count()) + 1u || chain->simulation.previousTargets.Count() != chain->simulation.points.Count() || chain->bones.At(0u) >= currentMatrices.Count() / 16u || !isfinite(chain->settings.teleportDistance) || !(chain->settings.teleportDistance > 0.0f))
        {
            error.Assign("secondary motion shared step previous chain is invalid");
            return false;
        }
        const auto& oldRoot = chain->simulation.previousTargets.At(0u);
        const uint32_t offset = chain->bones.At(0u) * 16u;
        const double x = static_cast<double>(currentMatrices.At(offset + 12u)) - oldRoot.x;
        const double y = static_cast<double>(currentMatrices.At(offset + 13u)) - oldRoot.y;
        const double z = static_cast<double>(currentMatrices.At(offset + 14u)) - oldRoot.z;
        const double distance = sqrt(x * x + y * y + z * z);
        if (!isfinite(distance))
        {
            error.Assign("secondary motion shared step root displacement is not finite");
            return false;
        }
        if (distance > chain->settings.teleportDistance)
        {
            count = 1u;
            return true;
        }
    }
    // 重い共通接触計算は最大2回とし、その間のばね予測は別途小刻みに進める。
    const uint32_t calculated = static_cast<uint32_t>(ceil(delta * 120.0));
    count = calculated > 1u ? 2u : 1u;
    return true;
}

bool AdvanceSecondaryMotionWeightedSteps(const FModelSecondaryMotionState& previous, const animation::FModelSkeleton& skeleton, const animation::FModelPose& currentBase, const Array<float>& currentMatrices, const Array<animation::FModelSecondaryMotionCollisionShape>& currentShapes, double delta, uint32_t stepCount, FModelSecondaryMotionState& output, String& error)
{
    // 呼出側で確定した分割数と、同じ不変履歴から求めた数を照合する。
    uint32_t expectedSteps = 0u;
    if (!GetSecondaryMotionWeightedStepCount(previous, currentMatrices, delta, expectedSteps, error))
    {
        return false;
    }
    if (stepCount != 2u || stepCount != expectedSteps || previous.contactPoints.Count() == 0u || previous.previousBasePose.morphWeights.Count() != currentBase.morphWeights.Count() || currentBase.localTransforms.Count() != skeleton.parents.Count() || currentMatrices.Count() / 16u != skeleton.parents.Count() || currentShapes.Count() != previous.previousCollisionShapes.Count())
    {
        error.Assign("secondary motion shared weighted step input does not match its history");
        return false;
    }
    // 接触段階の前後は最初に一度だけ複製し、速度用の追加履歴は必要な場合だけ作る。
    FModelSecondaryMotionState running;
    FModelSecondaryMotionState next;
    FModelSecondaryMotionState velocityPrevious;
    if (!running.CloneFrom(previous, error) || !next.CloneFrom(previous, error))
    {
        return false;
    }
    // 接触段階の間隔とは別に、予測の刻み幅を既存SingleStepの範囲内へ保つ。
    const double stageSeconds = delta / stepCount;
    const uint32_t predictionCount = stageSeconds <= (1.0 / 120.0) * (1.0 + 1.0e-12) ? 1u : static_cast<uint32_t>(ceil(stageSeconds * 120.0));
    const double predictionSeconds = stageSeconds / predictionCount;
    if (predictionCount > 1u && !velocityPrevious.CloneFrom(previous, error))
    {
        return false;
    }
    Array<Vec3> targets;
    for (uint32_t step = 0; step < stepCount; ++step)
    {
        // 各予測時刻も局所姿勢を補間し、world座標の直線補間で骨長を縮めない。
        animation::FModelPose interpolatedBase;
        Array<float> interpolatedMatrices;
        Array<animation::FModelSecondaryMotionCollisionShape> interpolatedShapes;
        const bool finalStep = step + 1u == stepCount;
        const animation::FModelPose* base = &currentBase;
        const Array<float>* matrices = &currentMatrices;
        const Array<animation::FModelSecondaryMotionCollisionShape>* shapes = &currentShapes;
        if (!CopySimulation(running, next, error))
        {
            return false;
        }
        for (uint32_t prediction = 0u; prediction < predictionCount; ++prediction)
        {
            const bool lastPrediction = prediction + 1u == predictionCount;
            if (predictionCount > 1u && lastPrediction)
            {
                // 幾何接触の間隔とは分け、速度に使う骨・身体・目標を直前の予測時刻へ揃える。
                Array<animation::FModelSecondaryMotionCollisionShape> velocityShapes;
                if (!CopySimulation(next, velocityPrevious, error) || !EvaluateSecondaryMotionColliders(skeleton, *base, *matrices, previous.colliders.Data(), previous.colliders.Count(), previous.colliderMargin, velocityShapes, error) || !SaveHistory(velocityPrevious, *base, *matrices, velocityShapes, error))
                {
                    return false;
                }
            }
            if (finalStep && lastPrediction)
            {
                // 終端は入力の姿勢と行列を直接使い、再補間による保存丸めを持ち込まない。
                base = &currentBase;
                matrices = &currentMatrices;
            }
            else
            {
                // 前回成功フレームからの割合を使い、予測ごとの補間を累積しない。
                const float weight = static_cast<float>((static_cast<double>(step) + static_cast<double>(prediction + 1u) / predictionCount) / stepCount);
                if (!animation::BlendModelPoses(skeleton, previous.previousBasePose, currentBase, weight, interpolatedBase, error) || !animation::EvaluateModelPose(skeleton, interpolatedBase, interpolatedMatrices, error))
                {
                    return false;
                }
                base = &interpolatedBase;
                matrices = &interpolatedMatrices;
            }
            if (lastPrediction && !finalStep)
            {
                if (!EvaluateSecondaryMotionColliders(skeleton, *base, *matrices, previous.colliders.Data(), previous.colliders.Count(), previous.colliderMargin, interpolatedShapes, error))
                {
                    return false;
                }
                shapes = &interpolatedShapes;
            }
            for (uint32_t chainIndex = 0; chainIndex < next.chains.Count(); ++chainIndex)
            {
                auto& chain = *next.chains.At(chainIndex);
                if (!BuildTargets(chain, *matrices, targets, error) || !animation::StepSecondaryMotionChainSingleStep(targets.Data(), targets.Count(), chain.settings, predictionSeconds, chain.simulation, error))
                {
                    error.Append(" (shared step chain root: ");
                    error.AppendUnsigned(chain.bones.At(0u));
                    error.Append(")");
                    return false;
                }
            }
        }
        // 予測が1回なら従来の状態と保存丸めをそのまま使い、固定60Hzの結果を保つ。
        const FModelSecondaryMotionState& velocityHistory = predictionCount == 1u ? running : velocityPrevious;
        // 予測をseedとして直接渡し、前回状態からの重複した予測を省く。
        if (!ConstrainSecondaryMotionContactPoints(next, velocityHistory, skeleton, *matrices, shapes->Data(), shapes->Count(), predictionSeconds, error, true) || !SaveHistory(next, *base, *matrices, *shapes, error))
        {
            return false;
        }
        // 空の移動先を順に使い、所有鎖を削除せず前後を交換する。
        FModelSecondaryMotionState exchanged;
        exchanged.MoveFrom(running);
        running.MoveFrom(next);
        next.MoveFrom(exchanged);
    }
    output.MoveFrom(running);
    error.Clear();
    return true;
}
}
