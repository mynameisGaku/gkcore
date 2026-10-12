// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCHAINCONTACTS_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCHAINCONTACTS_H

#include <stdint.h>
#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"
#include "model/animation/FSecondaryMotionReferenceSegment.h"

/**
 * 鎖全体の骨長・曲げ角を保つ身体接触計算。
 */
namespace gk::model::animation
{

/**
 * 鎖の全節方向を同時に調整して身体接触を解く。有限探索で候補を見つけられない場合はpositionsを保ち、errorは空のまま返す。不正入力や計算失敗ではerrorを設定する。
 */
bool ResolvePreparedSecondaryMotionChainContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const FSecondaryMotionReferenceSegment* referenceSegments, uint32_t segmentCount, double cosineLimit, gk::Array<gk::FVector3d>& positions, gk::String& error, double toleranceFraction = 1.0);

}

#endif
