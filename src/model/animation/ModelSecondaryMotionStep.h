// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONSTEP_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONSTEP_H

#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/FModelSkeleton.h"

/**
 * 全鎖の小刻みな予測と、更新内で回数を制限した共通接触計算を提供する。
 */
namespace gk::model
{
/**
 * 前回の局所姿勢と全更新間の移動量から接触段階数を最大2回に決める。履歴切れ・長時間・瞬間移動は1回とし、入力不正ではcountを保つ。
 */
bool GetSecondaryMotionWeightedStepCount(const FModelSecondaryMotionState& previous, const Array<float>& currentMatrices, double delta, uint32_t& count, String& error);

/**
 * 全鎖を1/120秒以下で予測し、接触は中間と終端で解く。速度は直前の予測時刻から計算し、全更新成功時だけoutputを置き換える。
 */
bool AdvanceSecondaryMotionWeightedSteps(const FModelSecondaryMotionState& previous, const animation::FModelSkeleton& skeleton, const animation::FModelPose& currentBase, const Array<float>& currentMatrices, const Array<animation::FModelSecondaryMotionCollisionShape>& currentShapes, double delta, uint32_t stepCount, FModelSecondaryMotionState& output, String& error);
}

#endif
