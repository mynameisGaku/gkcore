// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONSOLVER_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONSOLVER_H

#include <gkcore/FModelSecondaryMotionSettings.h>
#include "foundation/String.h"
#include "model/animation/FModelSecondaryMotionChainState.h"

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

}

#endif
