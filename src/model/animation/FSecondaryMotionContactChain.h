// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONCONTACTCHAIN_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONCONTACTCHAIN_H

#include <stdint.h>
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"
#include "model/animation/FSecondaryMotionChainAttachmentContact.h"

/**
 * 全身の重み付き接触計算へ渡す、連続した鎖の読み取り範囲を定義する。
 */
namespace gk::model::animation
{

/**
 * 全鎖配列内の位置・基準線分範囲と、その鎖に属する身体接触を借用する。
 */
struct FSecondaryMotionContactChain
{
    // 全位置配列内の固定root位置。
    uint32_t firstPoint = 0;
    // 全基準線分配列内の最初のsegment。
    uint32_t firstSegment = 0;
    // この鎖が持つ長さ・角度制約segment数。
    uint32_t segmentCount = 0;
    // 基準方向に対して許される最大曲げ角の余弦。
    double cosineLimit = 1.0;
    // この鎖の身体接触判定に使う準備済み形状。
    const FPreparedSecondaryMotionCollisionShape* shapes = nullptr;
    // 借用する身体形状数。0以上64以下。
    uint32_t shapeCount = 0;
    // この鎖のbone-local接触線分。
    const FSecondaryMotionChainAttachmentContact* attachments = nullptr;
    // 借用するbone-local接触線分数。
    uint32_t attachmentCount = 0;
};

}

#endif
