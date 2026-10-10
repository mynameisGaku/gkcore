// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_EXAMPLES_FMODELARMIKPREVIEW_H
#define GKCORE_EXAMPLES_FMODELARMIKPREVIEW_H

#include <gkcore.h>
#include <gkcore/ModelAnimation.h>

/**
 * 腕のIK目標と肘の向きを表示する補助機能。
 */
namespace gk::examples
{

/**
 * 右腕の現在位置から作った、確認用の二骨IK値。
 */
struct FModelArmIkPreview
{
    // IK対象の上腕、前腕、手のボーン番号。
    uint32_t bones[3]{};
    // 上腕、肘、手首の現在位置。
    gk::Vec3 joints[3]{};
    // 腕の長さの範囲内に置く手先目標。
    gk::Vec3 target{};
    // 肘を胴体の前へ向けるための補助点。
    gk::Vec3 pole{};
    // 上腕と前腕の長さの合計。
    float armLength = 0.0f;
};

/**
 * 胴体の外側へ届く右腕IK目標と肘の補助点を計算する。
 * 入力が有限でない、腕の長さが正でない、目標が2ボーンで届かない、または肩が胴体中心と同じXZ位置ならfalseを返し、outputを保つ。
 */
bool MakeModelArmIkPreview(gk::Vec3 torso, const gk::Vec3 joints[3], FModelArmIkPreview& output);

/**
 * モデルの右腕と胴体位置を取得し、確認用IK値を作る。
 * 必要な人型役割またはボーン位置を取得できない場合はfalseを返し、outputを保つ。
 */
bool BuildModelArmIkPreview(gk::ModelHandle model, FModelArmIkPreview& output);

/**
 * モデル座標の手先目標を表示座標へ変換し、UIレイヤー上にオレンジ色の中抜き菱形を描く。
 * 呼び出し側は事前にUIレイヤーを選ぶ。scaleが正の有限値でない、または描画APIが失敗した場合はfalseを返す。
 */
bool DrawModelArmIkTarget(const FModelArmIkPreview& preview, float scale, gk::Vec3 center, float rotationY);

}

#endif
