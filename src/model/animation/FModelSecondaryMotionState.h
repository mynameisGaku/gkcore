// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTATE_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTATE_H

#include "model/animation/FModelSecondaryMotionChain.h"

/**
 * モデルinstanceの揺れもの所有状態。
 */
namespace gk::model
{
/**
 * 登録したすべての鎖を所有し、instance終了時に解放する。
 */
struct FModelSecondaryMotionState
{
    // 互いに祖先関係のない鎖。
    Array<FModelSecondaryMotionChain*> chains;
    /**
     * 各鎖と内部配列を解放する。
     */
    ~FModelSecondaryMotionState();
};
}

#endif
