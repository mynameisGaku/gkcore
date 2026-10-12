// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONFREESEGMENTANCHOR_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONFREESEGMENTANCHOR_H

#include "foundation/FVector3d.h"

/**
 * 自由節の位置補正で親位置と基準方向の角度制約を表す値。
 */
namespace gk::model::animation
{

/**
 * 親を固定したときに子が動ける方向を表す。角度余弦は-1から1。
 */
struct FSecondaryMotionFreeSegmentAnchor
{
    // 補正直前に固定する親節位置。
    gk::FVector3d parentPosition{};
    // 基準姿勢の親から子へ向かう単位方向。
    gk::FVector3d referenceDirection{};
    // 許容曲げ角の余弦。
    double cosineLimit = -1.0;
};

}

#endif
