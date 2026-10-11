// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONSOLVER_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONSOLVER_H

#include <stdint.h>
#include <gkcore/FModelSecondaryMotionSettings.h>
#include "foundation/String.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/FModelSecondaryMotionChainState.h"
#include "model/animation/FModelSecondaryMotionStepShapes.h"

namespace gk::model::animation
{

/**
 * 指定位置から揺れもの鎖を初期化する。失敗時はstateを保つ。
 */
bool ResetSecondaryMotionChain(const gk::Vec3* targets, uint32_t count, FModelSecondaryMotionChainState& output, gk::String& error);

/**
 * アニメーション目標へ鎖を進め、長さと曲げ角を保つ。失敗時はstateを保つ。
 */
bool StepSecondaryMotionChain(const gk::Vec3* targets, uint32_t count, const gk::FModelSecondaryMotionSettings& settings, double delta, FModelSecondaryMotionChainState& state, gk::String& error);

/**
 * 各小刻み更新で身体の接触と鎖の長さ・角度を解く。pointRadiusは節中心を包む半径で、失敗時はstateを保つ。
 * 形状は前回と今回で同数。高速な更新間移動の連続接触は保証しない。
 */
bool StepSecondaryMotionChainWithCollisions(const gk::Vec3* targets, uint32_t count, const gk::FModelSecondaryMotionSettings& settings, double delta, const FModelSecondaryMotionCollisionShape* previousShapes, const FModelSecondaryMotionCollisionShape* currentShapes, uint32_t shapeCount, FModelSecondaryMotionChainState& state, gk::String& error, float pointRadius = 0.0f);

/**
 * cacheのGetが返したshape viewを用いて時間0の接触・鎖制約を解く。viewの数と各配列を検証し、失敗時はstateを保つ。
 */
bool StepSecondaryMotionChainWithPreparedCollisions(const gk::Vec3* targets, uint32_t count, const gk::FModelSecondaryMotionSettings& settings, const FModelSecondaryMotionStepShapes& shapes, FModelSecondaryMotionChainState& state, gk::String& error);

#if defined(GKCORE_TESTING) || (defined(_WIN32) && defined(GKCORE_TEST_FRAME_CAPTURE))
/**
 * 鎖制約の参照方向・長さ計算回数を0へ戻す。検証用の直列Updateで使う。
 */
void ResetSecondaryMotionReferenceCalculationCount();

/**
 * 鎖制約の参照方向・長さ計算回数を返す。
 */
uint64_t GetSecondaryMotionReferenceCalculationCount();
#endif

}

#endif
