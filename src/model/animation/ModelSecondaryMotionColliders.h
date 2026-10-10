// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCOLLIDERS_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCOLLIDERS_H

#include <gkcore/FModelSecondaryMotionCollider.h>
#include "model/animation/ModelPose.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"

/**
 * 身体に取り付けた接触形状の位置を求める処理。
 */
namespace gk::model
{
/**
 * 検証済みのclip・blend・IK姿勢から、身体のmodel空間形状を作る。
 * 端点・半径・祖先scaleが不正なら失敗し、outputを保つ。
 */
bool EvaluateSecondaryMotionColliders(const animation::FModelSkeleton& skeleton, const animation::FModelPose& pose, const Array<float>& matrices, const FModelSecondaryMotionCollider* colliders, uint32_t count, float margin, Array<animation::FModelSecondaryMotionCollisionShape>& output, String& error);
}

#endif
