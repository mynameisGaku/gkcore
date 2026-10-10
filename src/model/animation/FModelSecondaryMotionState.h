// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTATE_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTATE_H

#include "model/animation/FModelSecondaryMotionChain.h"
#include <gkcore/FModelSecondaryMotionCollider.h>
#include "model/animation/FModelSecondaryMotionCollisionShape.h"

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
    // 身体に取り付ける接触形状。端点と半径はboneのローカル空間。
    Array<FModelSecondaryMotionCollider> colliders;
    // 前回更新した身体のmodel空間形状。
    Array<animation::FModelSecondaryMotionCollisionShape> previousCollisionShapes;
    // 節の接触に足すmodel空間の余白。
    float colliderMargin = 0.005f;
    /**
     * 各鎖と内部配列を解放する。
     */
    ~FModelSecondaryMotionState();
};
}

#endif
