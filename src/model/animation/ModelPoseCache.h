// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODEL_POSE_CACHE_H
#define GKCORE_MODEL_ANIMATION_MODEL_POSE_CACHE_H

#include "model/animation/FModelPoseCache.h"
#include "model/animation/FModelPlayback.h"
#include "resources/Resources.h"

/**
 * 現在の再生条件だけに一致する基準姿勢を再利用する処理。
 */
namespace gk::model
{
/**
 * 再生条件の変更後に世代を進める。番号が循環するときは旧cacheを解放する。
 */
void InvalidateModelPoseCache(FModelPlayback& playback);
/**
 * 評価済みの基準姿勢から候補cacheを作る。失敗はnullで、呼び出し側が所有する。
 */
FModelPoseCache* CreateModelPoseCache(const detail::ModelResource& source, const FModelPlayback& playback, const animation::FModelPose& pose, String& error);
/**
 * 現条件に一致するcacheだけを複製する。missはhit=false、失敗時はoutputを保つ。
 */
bool CopyCurrentModelPoseCache(const detail::ModelResource& source, const FModelPlayback& playback, animation::FModelPose& output, bool& hit, String& error);
}

#endif
