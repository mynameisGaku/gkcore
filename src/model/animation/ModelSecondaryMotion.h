// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTION_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTION_H

#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/ModelPose.h"

/**
 * 更新済みの揺れもの回転を姿勢へ重ねる処理。
 */
namespace gk::model
{
/**
 * 確定した回転だけを適用し、物理時刻は進めない。失敗時はposeを保つ。
 */
bool ApplyModelSecondaryMotion(const FModelSecondaryMotionState& state, animation::FModelPose& pose, String& error);
}

#endif
