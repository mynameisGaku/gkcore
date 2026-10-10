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
 * 右腕の現在位置から作った、確認用の2ボーンIK値。
 */
struct FModelArmIkPreview
{
    // IK対象の上腕、前腕、手のボーン番号。
    uint32_t bones[3]{};
    // 上腕、肘、手首の現在位置。
    gk::Vec3 joints[3]{};
    // 胴体から右肩へ向かう単位方向。
    gk::Vec3 outward{};
    // 胴体の下側から上側へ向かう単位方向。
    gk::Vec3 up{};
    // 腕の長さの範囲内に置く手先目標。
    gk::Vec3 target{};
    // 肘の曲げる方向を決める補助点。
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
 * 元motionの手先と曲げ面を保ち、体の前方へ小さな確認用補正を加える。
 * offsetRatioは腕長に対する0から1の比率。届く範囲まで補正を縮める。
 * 無効入力はfalseでoutputを保つ。0なら手先の目標位置は変えない。
 */
bool MakeMotionFollowingArmIkPreview(gk::Vec3 hips, gk::Vec3 torso, gk::Vec3 leftShoulder, const gk::Vec3 joints[3], FModelArmIkPreview& output, float offsetRatio = 0.05f);

/**
 * 現在のアニメーションから胴体と腕の位置をまとめて取得し、動く確認用目標を作る。
 * 右腕・左上腕・腰・胸の役割が必要。失敗時はoutputを保つ。
 */
bool BuildAnimatedModelArmIkPreview(gk::ModelHandle model, FModelArmIkPreview& output);

/**
 * 右上腕・前腕・手・腰・胸・左上腕の順に6骨を解決する。失敗時はoutputを保つ。
 * outputは6要素必要。同じモデルの役割を変更したら呼び直す。
 */
bool ResolveAnimatedModelArmIkBones(gk::ModelHandle model, uint32_t output[6]);

/**
 * 事前解決した6骨の現在位置から目標を作る。役割表は再検索しない。
 * bonesは同じモデルで解決した6要素。無効番号・姿勢評価の失敗時はoutputを保つ。
 * 前frameのIKを目標へ持ち越さない場合は、照会前にそのIKを解除する。
 */
bool BuildAnimatedModelArmIkPreview(gk::ModelHandle model, const uint32_t bones[6], FModelArmIkPreview& output);

/**
 * モデル座標の手先目標を表示座標へ変換し、UIレイヤー上にオレンジ色の中抜き菱形を描く。
 * 呼び出し側は事前にUIレイヤーを選ぶ。scaleが正の有限値でない、または描画APIが失敗した場合はfalseを返す。
 */
bool DrawModelArmIkTarget(const FModelArmIkPreview& preview, float scale, gk::Vec3 center, float rotationY);

}

#endif
