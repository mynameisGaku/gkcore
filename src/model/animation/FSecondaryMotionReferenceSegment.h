// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONREFERENCESEGMENT_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONREFERENCESEGMENT_H

#include "foundation/FVector3d.h"

/**
 * 鎖の長さ・角度制約で使うアニメーション基準線分。
 */
namespace gk::model::animation
{

/**
 * 正規化した目標方向と目標距離を保持する。
 */
struct FSecondaryMotionReferenceSegment
{
    // 正規化済みの親から子への目標方向。
    gk::FVector3d direction{};
    // アニメーション基準姿勢の線分長。
    double length = 0.0;
};

}

#endif
