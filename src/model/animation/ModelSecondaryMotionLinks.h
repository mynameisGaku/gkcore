// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODEL_SECONDARY_MOTION_LINKS_H
#define GKCORE_MODEL_ANIMATION_MODEL_SECONDARY_MOTION_LINKS_H

#include "model/animation/FModelSecondaryMotionState.h"
#include "foundation/String.h"

/**
 * 揺れものの鎖を結ぶ線分と、既存の骨制約を交互に解く処理。
 */
namespace gk::model
{
/**
 * 接続の数、端点の所属、半径、重複を検査する。stateは変更しない。
 */
bool ValidateSecondaryMotionLinks(const FModelSecondaryMotionState& state, String& error);
/**
 * 距離制約の数、自由節、lambda配列、係数、重複を検査する。stateは変更しない。
 */
bool ValidateSecondaryMotionDistanceConstraints(const FModelSecondaryMotionState& state, String& error);
/**
 * candidate内の接続接触と骨制約を解く。失敗した候補は呼出側が破棄する。
 * deltaは元の更新間隔で、接触を加えた位置差だけを速度へ反映する。
 */
bool ConstrainSecondaryMotionLinks(FModelSecondaryMotionState& candidate, const FModelSecondaryMotionState& previous, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, double delta, String& error);
/**
 * 描画FKから解決したbone原点の接続線分を身体形状へ検査する。
 */
bool CheckRenderedSecondaryMotionLinks(const FModelSecondaryMotionState& state, const Array<float>& matrices, const animation::FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, String& error);
#if defined(GKCORE_TESTING)
/**
 * 接続の端点解決・形状準備・鎖復元の回数を0へ戻す。
 */
void ResetSecondaryMotionLinkPreparationCounts();
/**
 * 接続solver内で確定した端点の組数を返す。
 */
uint64_t GetSecondaryMotionLinkResolutionCount();
/**
 * 接続solver内で準備した身体形状の数を返す。
 */
uint64_t GetSecondaryMotionLinkShapePreparationCount();
/**
 * 接続投影後に鎖の制約を復元した呼び出し数を返す。
 */
uint64_t GetSecondaryMotionLinkChainConstraintCount();
#endif
}

#endif
