// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSNAPSHOT_H
#define GKCORE_MODEL_ANIMATION_MODELSNAPSHOT_H

#include "FModelPlayback.h"
#include "../../resources/Resources.h"

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
}

#endif
