// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTEPSHAPES_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTEPSHAPES_H

#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"
#include <stdint.h>

/**
 * 1回のstep内で借りる、半径拡張済み接触形状群のview。
 */
namespace gk::model::animation
{

/**
 * cacheのGetが検証・生成した配列を、delta0 solverの呼び出し中だけ参照する。
 */
struct FModelSecondaryMotionStepShapes
{
    // 節の球半径を加えた身体形状。
    const FModelSecondaryMotionCollisionShape* expanded = nullptr;
    // 接触許容幅を相殺する余白を加えた形状。
    const FModelSecondaryMotionCollisionShape* guarded = nullptr;
    // guard済み形状から準備した接触計算用形状。
    const FPreparedSecondaryMotionCollisionShape* prepared = nullptr;
    // 3配列で共有する形状数。
    uint32_t count = 0;
};

}

#endif
