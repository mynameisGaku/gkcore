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
 * 失敗時はpositionsを保つ。有限探索で候補が見つからない場合はerrorを空にする。
 * 可動範囲の全体が身体内にあると確認した場合は、接触点と適用する身体形状の0始まり番号をerrorへ記す。
 * 不正入力・数値範囲外・確保失敗でもerrorを設定する。
 */
bool ResolvePreparedSecondaryMotionWeightedContacts(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, uint32_t referenceCount, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, gk::Array<gk::FVector3d>& positions, gk::String& error, uint32_t maximumIterations = 32);

}

#endif
