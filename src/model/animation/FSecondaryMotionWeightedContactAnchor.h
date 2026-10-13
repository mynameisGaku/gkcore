// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONWEIGHTEDCONTACTANCHOR_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONWEIGHTEDCONTACTANCHOR_H

#include <stdint.h>
#include "foundation/FVector3d.h"
#include "model/animation/FSecondaryMotionAttachmentDefinition.h"

/**
 * 重み付き接触点を構成する、一つの骨寄与を定義する。
 */
namespace gk::model::animation
{

/**
 * 動く鎖上のoffset、または固定されたmodel-space寄与とそのskin weightを借用する。
 */
struct FSecondaryMotionWeightedContactAnchor
{
    // 固定寄与の場合は0xFFFFFFFF、動く寄与では鎖配列の番号。
    uint32_t chainIndex = 0xFFFFFFFFu;
    // 動く寄与が属する鎖内segment番号。
    uint32_t segmentIndex = 0;
    // 動く骨に付けたbase方向・local offsetの定義。
    FSecondaryMotionAttachmentDefinition definition{};
    // 固定寄与で使うmodel-space位置。
    gk::FVector3d constantPosition{};
    // skinning入力にある変更してはならない寄与weight。
    double weight = 0.0;
};

}

#endif
