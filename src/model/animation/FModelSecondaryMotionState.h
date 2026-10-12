// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTATE_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSTATE_H

#include "model/animation/FModelSecondaryMotionChain.h"
#include <gkcore/FModelSecondaryMotionCollider.h>
#include <gkcore/FModelSecondaryMotionLink.h>
#include <gkcore/FModelSecondaryMotionDistanceConstraint.h>
#include <gkcore/FModelSecondaryMotionContactLink.h>
#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/FModelGpuSkinningGeometry.h"

/**
 * モデルinstanceの揺れもの所有状態。
 */
namespace gk::model
{
/**
 * 登録したすべての鎖を所有し、instance終了時に解放する。
 */
struct FModelSecondaryMotionState
{
    // 互いに祖先関係のない鎖。
    Array<FModelSecondaryMotionChain*> chains;
    // 身体に取り付ける接触形状。端点と半径はboneのローカル空間。
    Array<FModelSecondaryMotionCollider> colliders;
    // 異なる鎖の自由boneを結ぶ身体接触の補助線分。
    Array<FModelSecondaryMotionLink> links;
    // 別鎖の自由bone間の基準距離と柔らかさ。基準距離は登録時に正の値へ解決する。
    Array<FModelSecondaryMotionDistanceConstraint> distanceConstraints;
    // boneローカル位置に置く衣服側の補助接触線分。
    Array<FModelSecondaryMotionContactLink> contactLinks;
    // 最後の距離補正で蓄積した力相当の値。描画姿勢の残差検査にも使う。
    Array<double> distanceLambdas;
    // 最後の距離補正に使った秒数。時間0では基準の1/120秒を使う。
    double distanceStepSeconds = 1.0 / 120.0;
    // 線分接触・距離・既存の骨制約を交互に解く共通上限。
    uint32_t linkIterations = 32;
    // 前回更新した身体のmodel空間形状。
    Array<animation::FModelSecondaryMotionCollisionShape> previousCollisionShapes;
    // 節の接触に足すmodel空間の余白。
    float colliderMargin = 0.005f;
    // 対応するFBXの変形後頂点へ、描画時点の身体接触を加える。
    bool skinPointContactsEnabled = false;
    // 登録鎖の枝にweightを持つ位置番号。設定時に選別し、描画では変更しない。
    Array<uint32_t> skinContactPositionIds;
    // 選別した不変geometryの識別子。元モデルが寿命を所有する。
    const animation::FModelGpuSkinningGeometry* skinContactGeometry = nullptr;
    /**
     * 各鎖と内部配列を解放する。
     */
    ~FModelSecondaryMotionState();
};
}

#endif
