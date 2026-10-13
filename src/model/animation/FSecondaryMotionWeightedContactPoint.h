// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONWEIGHTEDCONTACTPOINT_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONWEIGHTEDCONTACTPOINT_H

#include <stdint.h>
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"

/**
 * 複数骨の寄与をskin weightどおりに混ぜた接触点を定義する。
 */
namespace gk::model::animation
{

/**
 * 平坦なanchor配列の範囲と、その点に適用する準備済み身体形状を借用する。
 */
struct FSecondaryMotionWeightedContactPoint
{
    // 共有anchor配列内の最初の寄与番号。
    uint32_t firstAnchor = 0;
    // この点を構成する寄与数。1以上64以下。
    uint32_t anchorCount = 0;
    // この点の接触判定に使う準備済み身体形状。
    const FPreparedSecondaryMotionCollisionShape* shapes = nullptr;
    // 借用する身体形状数。0以上64以下。
    uint32_t shapeCount = 0;
};

}

#endif
