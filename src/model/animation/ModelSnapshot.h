// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSNAPSHOT_H
#define GKCORE_MODEL_ANIMATION_MODELSNAPSHOT_H

#include "model/animation/FModelPlayback.h"
#include "model/animation/ModelPose.h"
#include "resources/Resources.h"

/**
 * 描画時のモデル姿勢snapshotを作る内部機能。
 */
namespace gk::model
{
/**
 * 形状・材質をコピーし、画像参照を保持する。返された参照は呼び出し側が解放する。
 */
detail::ModelResource* CloneModelSnapshot(const detail::ModelResource& source, String& error);
/**
 * 現在のclip・ブレンド・IKを独立した描画用モデルへ評価する。
 * 静的モデルでも独立した参照を返す。失敗はnull。
 */
detail::ModelResource* EvaluateModelSnapshot(const detail::ModelResource& source, const FModelPlayback* playback, String& error);
/**
 * OBJ連番以外のclip・ブレンド・IKを共有姿勢へ評価する。失敗時はposeを変更しない。
 */
bool EvaluateModelPlaybackPose(const detail::ModelResource& source, const FModelPlayback& playback, animation::FModelPose& pose, String& error);
/**
 * 揺れものを除くclip・ブレンド・IK姿勢を評価する。物理更新の入力に使う。
 */
bool EvaluateModelPlaybackBasePose(const detail::ModelResource& source, const FModelPlayback& playback, animation::FModelPose& pose, String& error);

}

#endif
