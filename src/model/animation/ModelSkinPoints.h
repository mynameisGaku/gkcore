// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODEL_SKIN_POINTS_H
#define GKCORE_MODEL_ANIMATION_MODEL_SKIN_POINTS_H

#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FModelGpuSkinningGeometry.h"
#include <stdint.h>

/**
 * model animationのskin位置計算を提供する。
 */
namespace gk::model::animation
{

/**
 * 指定したskin位置を全influenceの行列でmodel空間へ評価する。失敗時はoutputを保つ。
 */
bool EvaluateModelSkinPoints(const FModelGpuSkinningGeometry& geometry, const gk::Array<FModelGpuSkinningGeometry::FMatrix>& matrices, const uint32_t* ids, uint32_t count, gk::Array<gk::FVector3d>& output, gk::String& error);

}

#endif
