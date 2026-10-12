// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONDIRECTION_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONDIRECTION_H

#include <stdint.h>
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"

/**
 * 骨長と曲げ角を保った節方向の接触計算。
 */
namespace gk::model::animation
{

/**
 * 全身体形状に対して固定長の骨方向を探す。toleranceFractionは接触余裕の割合で、0から1を指定する。有限探索で候補を見つけられない場合はdirectionを保ち、errorは空のまま返す。不正入力や計算失敗ではerrorを設定する。
 */
bool FindPreparedSecondaryMotionCollisionDirection(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d& parent, double length, bool firstSegment, const gk::FVector3d& referenceDirection, double cosineLimit, gk::FVector3d& direction, gk::String& error, double toleranceFraction = 0.0);

/**
 * 節方向を正規化して基準方向の角度制限内へ戻す。
 */
bool NormalizeAndLimitSecondaryMotionDirection(const gk::FVector3d& referenceDirection, double cosineLimit, gk::FVector3d& direction);

}

#endif
