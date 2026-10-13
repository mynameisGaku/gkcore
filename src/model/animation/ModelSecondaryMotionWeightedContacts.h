// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONWEIGHTEDCONTACTS_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONWEIGHTEDCONTACTS_H

#include <stdint.h>
#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FSecondaryMotionContactChain.h"
#include "model/animation/FSecondaryMotionReferenceSegment.h"
#include "model/animation/FSecondaryMotionWeightedContactAnchor.h"
#include "model/animation/FSecondaryMotionWeightedContactPoint.h"

/**
 * 複数のsecondary chainが共有するweighted skin contact solverを定義する。
 */
namespace gk::model::animation
{

/**
 * 全鎖のroot・骨長・角度を保ち、skin weightで混ぜた接触点を同時に解く。
 * 入力配列は呼出し中だけ借用する。最大反復数は1から32で、既定値は32。
 * 解けない場合はpositionsを保ちerrorを空で返し、不正入力・数値範囲外・確保失敗ではerrorを設定する。
 */
bool ResolvePreparedSecondaryMotionWeightedContacts(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, uint32_t referenceCount, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, gk::Array<gk::FVector3d>& positions, gk::String& error, uint32_t maximumIterations = 32);

}

#endif
