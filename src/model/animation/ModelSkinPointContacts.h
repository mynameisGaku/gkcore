// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODEL_SKIN_POINT_CONTACTS_H
#define GKCORE_MODEL_ANIMATION_MODEL_SKIN_POINT_CONTACTS_H

#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/FModelGpuSkinningGeometry.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/FModelSkinPointCorrection.h"
#include "model/animation/FModelSparsePoseGeometry.h"
#include <stdint.h>

/**
 * skin位置と身体接触形状の検査・補正を提供する。
 */
namespace gk::model::animation
{

/**
 * skin位置群を球またはカプセルの外へ投影する。複数回の投影後に全形状を再検査し、失敗時はoutputを保つ。
 */
bool ProjectModelSkinPoints(const gk::FVector3d* points, uint32_t count, const FModelSecondaryMotionCollisionShape* shapes, uint32_t shapeCount, uint32_t passes, gk::Array<gk::FVector3d>& output, gk::String& error);

/**
 * 全skin位置を評価し、指定位置だけ補正してGPU参照と同じ面法線を作る。失敗時はoutputを保つ。
 */
bool EvaluateModelSkinContactGeometry(const FModelGpuSkinningGeometry& geometry, const gk::Array<FModelGpuSkinningGeometry::FMatrix>& matrices, const FModelSkinPointCorrection* corrections, uint32_t correctionCount, FModelSparsePoseGeometry& output, gk::String& error);

}

#endif
