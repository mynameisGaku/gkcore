// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCONTACTPOINTS_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCONTACTPOINTS_H

#include <stdint.h>
#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/FModelSkeleton.h"

/**
 * Skin weightで混ぜた身体接触点を揺れものへ反映する。
 */
namespace gk::model
{
/**
 * 登録した接触点と骨の所属、weight合計を検査する。失敗時はstateを変更しない。
 */
bool ValidateSecondaryMotionContactPoints(const FModelSecondaryMotionState& state, const animation::FModelSkeleton& skeleton, String& error);

/**
 * 揺れ鎖を候補位置上で接触から退避させる。失敗時はcandidateの物理状態を変更しない。
 * usePredictedPositionsがtrueなら、共有更新で予測済みの点列をそのまま接触の初期値に使う。
 */
bool ConstrainSecondaryMotionContactPoints(FModelSecondaryMotionState& candidate, const FModelSecondaryMotionState& previous, const animation::FModelSkeleton& skeleton, const Array<float>& baseMatrices, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, double delta, String& error, bool usePredictedPositions = false);

/**
 * 描画FKで評価したskin weight接触点が身体形状へ侵入しないか調べる。
 */
bool CheckRenderedSecondaryMotionContactPoints(const FModelSecondaryMotionState& state, const Array<float>& renderedMatrices, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, String& error);

/**
 * 描画FKから1つのweighted接触点の中心を評価する。失敗時はoutputを保つ。
 */
bool EvaluateRenderedSecondaryMotionContactPoint(const FModelSecondaryMotionState& state, const Array<float>& renderedMatrices, uint32_t pointIndex, FVector3d& output, String& error);
}

#endif
