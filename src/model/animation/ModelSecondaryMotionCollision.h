// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCOLLISION_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCOLLISION_H

#include <stdint.h>
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FPreparedSecondaryMotionCollisionShape.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"

namespace gk
{
template <class T> class Array;
}

/**
 * 揺れものの節と線分を身体形状から離す計算。
 */
namespace gk::model::animation
{

/**
 * 接触形状の数、座標、半径がsolverの対応範囲か調べる。
 */
bool ValidateSecondaryMotionCollisionShapes(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, gk::String& error);

/**
 * 身体形状の半径へskin点半径を加えた候補を作る。失敗時はoutputを保つ。
 */
bool ExpandSecondaryMotionCollisionShapes(const FModelSecondaryMotionCollisionShape* input, uint32_t count, float pointRadius, gk::Array<FModelSecondaryMotionCollisionShape>& output, gk::String& error);

/**
 * 節位置の候補を形状の外へ投影する。失敗時はpositionsを保つ。
 */
bool ProjectSecondaryMotionContacts(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* referenceTargets, uint32_t pointCount, gk::Array<gk::FVector3d>& positions, gk::String& error);

/**
 * 節と隣接節の線分が接触形状へ侵入していないか調べる。許容幅の割合は0から1で、既定値は通常の許容幅を使う。
 */
bool CheckSecondaryMotionContacts(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* positions, uint32_t pointCount, gk::String& error, double toleranceFraction = 1.0);

/**
 * 候補線分が形状へ入る場合、最近位置を基準に固定長を保って方向を回す。親節が内部なら最初の節以外は補正を保留し、失敗時はdirectionを保つ。
 */
bool AvoidSecondaryMotionCollisionSegment(const FModelSecondaryMotionCollisionShape& shape, const gk::FVector3d& parent, double length, bool firstSegment, gk::FVector3d& direction, gk::String& error);

/**
 * float範囲内の有限な端点を持つ自由線分を身体形状から押し出す。入力が不正か収束しない場合は両端を保つ。
 */
bool ProjectSecondaryMotionFreeSegmentContacts(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, float radius, const gk::FVector3d& referenceFirst, const gk::FVector3d& referenceSecond, gk::FVector3d& first, gk::FVector3d& second, gk::String& error);

/**
 * float範囲内の有限な自由線分の両端と全区間が身体形状の外か調べる。不正入力や接触があれば失敗する。
 */
bool CheckSecondaryMotionFreeSegmentContacts(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, float radius, const gk::FVector3d& first, const gk::FVector3d& second, gk::String& error);

/**
 * 身体形状を線分半径込みで準備し、再利用できる候補配列へ原子的に保存する。
 */
bool PrepareSecondaryMotionFreeSegmentShapes(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, float radius, gk::Array<FPreparedSecondaryMotionCollisionShape>& output, gk::String& error);

/**
 * 準備済み身体形状から自由線分を投影する。失敗時は両端を保つ。
 */
bool ProjectPreparedSecondaryMotionFreeSegmentContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d& referenceFirst, const gk::FVector3d& referenceSecond, gk::FVector3d& first, gk::FVector3d& second, gk::String& error);

/**
 * 準備済み身体形状に対する自由線分の接触を検査する。
 */
bool CheckPreparedSecondaryMotionFreeSegmentContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d& first, const gk::FVector3d& second, gk::String& error);

/**
 * 準備済み形状で節位置を投影する。失敗時はpositionsを保つ。
 */
bool ProjectPreparedSecondaryMotionContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* referenceTargets, uint32_t pointCount, gk::Array<gk::FVector3d>& positions, gk::String& error);

/**
 * 準備済み形状で節と隣接節の接触を調べる。許容幅の割合は0から1。
 */
bool CheckPreparedSecondaryMotionContacts(const FPreparedSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, const gk::FVector3d* positions, uint32_t pointCount, gk::String& error, double toleranceFraction = 1.0);

/**
 * 半径を変えずに生形状を検証・準備し、失敗時はoutputを保つ。
 */
bool PrepareSecondaryMotionCollisionShapes(const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, gk::Array<FPreparedSecondaryMotionCollisionShape>& output, gk::String& error);

#if defined(GKCORE_TESTING)
/**
 * 接触形状の軸距離計算回数を0へ戻す。
 */
void ResetSecondaryMotionCollisionAxisQueryCount();

/**
 * 接触形状の軸距離計算回数を返す。
 */
uint64_t GetSecondaryMotionCollisionAxisQueryCount();

/**
 * 接触全体の検査呼び出し回数を0へ戻す。
 */
void ResetSecondaryMotionContactCheckCount();

/**
 * 接触全体の検査呼び出し回数を返す。
 */
uint64_t GetSecondaryMotionContactCheckCount();

/**
 * 接触形状の準備回数を0へ戻す。
 */
void ResetSecondaryMotionCollisionPrepareCount();

/**
 * 接触形状の準備回数を返す。
 */
uint64_t GetSecondaryMotionCollisionPrepareCount();

/**
 * 半径拡張処理の呼び出し回数を0へ戻す。
 */
void ResetSecondaryMotionCollisionExpansionCount();

/**
 * 半径拡張処理の呼び出し回数を返す。
 */
uint64_t GetSecondaryMotionCollisionExpansionCount();
#endif

}

#endif
