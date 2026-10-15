// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCONTACTMINIMAX_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCONTACTMINIMAX_H

#include "model/animation/ModelSecondaryMotionWeightedContacts.h"

/**
 * 全接触を片側制約として扱う、有限回の補助探索を定義する。
 */
namespace gk::model::animation
{

/**
 * 重み付き点、鎖の節と線分、骨に固定した線分の順で符号付き侵入量と許容幅を求める。
 * 鎖の行順はshape、segment、節、線分とし、除外する線分にも値-1と許容幅0の行を残す。
 * 原本solverの身体判定を共有し、入力不正、数値範囲外、確保失敗では出力を保つ。
 */
bool EvaluateSecondaryMotionContactSignedRows(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::FVector3d* positions, uint32_t positionCount, gk::Array<double>& signedValues, gk::Array<double>& tolerances, gk::String& error);

/**
 * floatに保存した候補が骨長、角度、全身体接触と重み付き接触を保つか確認する。
 * rootの固定は呼出し側で同じ始点から位置を再構成して保証する。
 * 成立しない候補はclear=false、入力や計算の失敗はfalseとerrorで知らせる。
 */
bool CheckSecondaryMotionWeightedStoredCandidate(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& positions, bool& clear, gk::String& error);

/**
 * 基準候補の近接行から自由度を選び、最大侵入を減らす片側制約の探索を行う。
 * 入力は呼出し中だけ借用し、最大32反復、32segment、4096行を超える問題は未解決として戻す。
 * 全保存後検査が成立した場合だけcandidatePositionsを置き換え、foundClear=trueにする。
 * 有限探索の未解決はtrueとfoundClear=false、入力、数値、確保の失敗はfalseで知らせる。
 */
bool RefineSecondaryMotionContactMinimax(const FSecondaryMotionContactChain* chains, uint32_t chainCount, const FSecondaryMotionReferenceSegment* references, uint32_t referenceCount, const FSecondaryMotionWeightedContactAnchor* anchors, uint32_t anchorCount, const FSecondaryMotionWeightedContactPoint* contacts, uint32_t contactCount, const gk::Array<gk::FVector3d>& roots, const gk::Array<gk::FVector3d>& baseDirections, gk::Array<gk::FVector3d>& candidatePositions, bool& foundClear, gk::String& error, uint32_t maximumIterations);

}

#endif
