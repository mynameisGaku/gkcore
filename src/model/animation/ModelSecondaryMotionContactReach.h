// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCONTACTREACH_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCONTACTREACH_H

#include "model/animation/ModelSecondaryMotionWeightedContacts.h"

/**
 * 重み付き接触点が動ける範囲を保守的に調べる。
 */
namespace gk::model::animation
{

/**
 * 検証済みsolver入力から、全回転を許しても退避できない最初の点と身体形状番号を返す。
 * falseは可解性不明を表し、計算が不確かな場合も含む。falseの場合は出力番号を保つ。
 */
bool FindUnreachableSecondaryMotionWeightedContact(const FSecondaryMotionContactChain* chains, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& positions, uint32_t& contactIndex, uint32_t& shapeIndex);

}

#endif
