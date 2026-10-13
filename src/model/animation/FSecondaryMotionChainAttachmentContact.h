// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONCHAINATTACHMENTCONTACT_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONCHAINATTACHMENTCONTACT_H

#include <stdint.h>
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"
#include "model/animation/FSecondaryMotionAttachmentDefinition.h"

/**
 * 鎖全体の姿勢探索に使う、boneローカルの接触形状を表す。
 */
namespace gk::model::animation
{

/**
 * 同じ鎖内の一本のboneへ付けた接触線分と、接触半径だけを加えて準備した全身形状範囲を借用する。
 */
struct FSecondaryMotionChainAttachmentContact
{
    // 接触線分を付けたboneの鎖内segment番号。
    uint32_t segmentIndex = 0;
    // 線分の始点に使うbone-local接触点。
    FSecondaryMotionAttachmentDefinition firstDefinition{};
    // 線分の終点に使うbone-local接触点。
    FSecondaryMotionAttachmentDefinition secondDefinition{};
    // contact linkの半径を加えて準備した借用形状群。
    const FPreparedSecondaryMotionCollisionShape* shapes = nullptr;
    // 借用する準備済み形状数。0以上64以下。
    uint32_t shapeCount = 0;
};

}

#endif
