// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONATTACHMENTS_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONATTACHMENTS_H

#include <stdint.h>
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"
#include "model/animation/FSecondaryMotionAttachmentDefinition.h"
#include "model/animation/FSecondaryMotionAttachmentEvaluation.h"

/**
 * 骨に固定した接触点の評価と接触補正を提供する。
 */
namespace gk::model::animation
{

/**
 * 最小回転の実部が小さすぎて反対方向と区別できない境界。回転計算と到達診断で共有する。
 */
constexpr double kSecondaryMotionAttachmentAntiparallelThreshold = 1.0e-12;

/**
 * 親子骨位置から接触点と子位置に対する微分を求める。反対向きなど計算不能な場合はevaluationを保つ。
 */
bool EvaluateSecondaryMotionAttachment(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child, FSecondaryMotionAttachmentEvaluation& evaluation, gk::String& error);

/**
 * 接触点の位置だけを求め、探索候補で不要な子位置微分を省く。計算不能な場合はpositionを保つ。
 */
bool EvaluateSecondaryMotionAttachmentPosition(const FSecondaryMotionAttachmentDefinition& definition, const gk::FVector3d& parent, const gk::FVector3d& child, gk::FVector3d& position, gk::String& error);

#if defined(GKCORE_TESTING)
/**
 * 検査用の、骨に固定した接触点の微分計算回数を0へ戻す。
 */
void ResetSecondaryMotionAttachmentJacobianCountForTesting();
/**
 * 検査開始後に骨に固定した接触点の微分を計算した回数を返す。
 */
uint64_t GetSecondaryMotionAttachmentJacobianCountForTesting();
#endif

/**
 * 二つの骨に固定した接触点を準備済み身体形状から押し戻す。失敗時は4点の位置を保つ。
 */
bool ProjectSecondaryMotionAttachmentContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionAttachmentDefinition& firstDefinition, const FSecondaryMotionAttachmentDefinition& secondDefinition, gk::FVector3d* const points[4], gk::String& error);

}

#endif
