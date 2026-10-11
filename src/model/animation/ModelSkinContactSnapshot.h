// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODEL_SKIN_CONTACT_SNAPSHOT_H
#define GKCORE_MODEL_ANIMATION_MODEL_SKIN_CONTACT_SNAPSHOT_H

#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/FModelSkinPointCorrection.h"

/**
 * 描画時点の姿勢と身体形状から、所有される頂点補正を作る処理。
 */
namespace gk::model
{
/**
 * 登録した鎖の枝に正weightを持つ位置番号を設定時に選別する。失敗時はoutputを保つ。
 */
bool SelectModelSkinContactPositions(const AModelAnimationSource& source, const FModelSecondaryMotionState& state, Array<uint32_t>& output, String& error);
/**
 * 登録した揺れboneの枝にweightを持つ点を補正する。失敗時はoutputを保つ。
 * 対応するFBX skin経路に限り、補正OFFでは空配列を返す。
 */
bool BuildModelSkinContactSnapshot(const AModelAnimationSource& source, const FModelSecondaryMotionState* state, const animation::FModelPose& pose, const Array<animation::FModelGpuSkinningGeometry::FMatrix>& matrices, Array<animation::FModelSkinPointCorrection>& output, String& error);
}
#endif
