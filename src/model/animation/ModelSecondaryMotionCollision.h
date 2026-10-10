// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCOLLISION_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCOLLISION_H

#include <stdint.h>
#include "foundation/FVector3d.h"
#include "foundation/String.h"
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

}

#endif
