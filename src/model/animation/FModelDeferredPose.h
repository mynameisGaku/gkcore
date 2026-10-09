// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODEL_DEFERRED_POSE_H
#define GKCORE_MODEL_ANIMATION_FMODEL_DEFERRED_POSE_H

#include "foundation/RefCount.h"
#include "model/animation/FModelPlayback.h"
#include "model/animation/FModelGpuSkinningGeometry.h"
#include "model/animation/FModelSparsePoseGeometry.h"
#include "model/animation/FModelPose.h"
#include "resources/Resources.h"

/**
 * 描画予約時のsparse姿勢と元モデルの寿命を保持する。
 */
namespace gk::model
{
/**
 * 元ModelResourceとsparse変形結果を共有所有する。
 */
struct FModelDeferredPose
{
    // 描画packetなどが共有する参照数。
    RefCounted reference;
    // source形式固有情報と未変形頂点を保持する。
    detail::ModelResource* source = nullptr;
    // unique位置とnormal groupだけの描画時姿勢。
    animation::FModelSparsePoseGeometry geometry;
    // GPU skinning shaderへ渡す、予約時poseのcluster行列。
    Array<animation::FModelGpuSkinningGeometry::FMatrix> gpuSkinningMatrices;
    // CPU fallbackを同じ予約姿勢で再評価するための値。
    animation::FModelPose frozenPose;
    // GPU評価時にCPUでpositions/normalsを全面計算していないことを示す。
    bool gpuEvaluationOnly = false;
};

/**
 * 対応sourceの姿勢を予約時に評価する。未対応ならerrorを空にしてnullを返す。
 */
FModelDeferredPose* EvaluateDeferredModelPose(const detail::ModelResource& source, const FModelPlayback* playback, String& error);
/**
 * GPU skinning用行列とBLENDに必要な位置だけを予約時に評価する。
 */
FModelDeferredPose* EvaluateGpuDeferredModelPose(const detail::ModelResource& source, const FModelPlayback* playback, String& error);
/**
 * sparse姿勢を通常の頂点配列へ展開する。返された参照は呼び出し側が解放する。
 */
detail::ModelResource* MaterializeDeferredModelPose(const FModelDeferredPose& pose, String& error);
}

#endif
