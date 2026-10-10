// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONCHAINSTATE_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONCHAINSTATE_H

#include "foundation/Array.h"
#include "model/animation/FModelSecondaryMotionPoint.h"

namespace gk::model::animation
{

/**
 * 1本の揺れもの鎖を次の更新へ渡す状態。
 */
struct FModelSecondaryMotionChainState
{
    // 各節の現在位置と速度。
    gk::Array<FModelSecondaryMotionPoint> points;
    // 前回更新で使ったアニメーション目標位置。
    gk::Array<gk::Vec3> previousTargets;
};

}

#endif
