// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODELSECONDARYMOTION_H
#define GKCORE_MODELSECONDARYMOTION_H

#include <gkcore.h>

namespace gk
{
struct FModelSecondaryMotionSettings;
}
#include <gkcore/FModelSecondaryMotionSettings.h>

/**
 * アニメーションの後に、髪や衣服のボーンへ揺れを加えるAPI。
 */
namespace gk
{
/**
 * 親子が連続したbone列をコピーして登録する。同じ先頭boneの設定は置き換える。
 * 各鎖は独立した枝に置く。正のほぼ一様な祖先scaleと、書き換え可能な骨格が必要。
 * endOffsetは最後のboneから延ばす有限でゼロ以外のローカル位置。失敗時は既存の設定を保つ。
 */
GKCORE_API int SetModelSecondaryMotionChain(ModelHandle model, const uint32_t* bones, uint32_t count, const FModelSecondaryMotionSettings& settings);
/**
 * clip・ブレンド・IK後の姿勢へ揺れを進める。deltaSecondsは有限な0以上の秒数。
 * アニメーション更新とIK設定の後、描画の前に1回呼ぶ。照会や描画は揺れを進めない。
 * 重力・風・移動距離はmodel空間で扱う。長い停止や大きな移動では姿勢へ戻す。
 */
GKCORE_API int UpdateModelSecondaryMotion(ModelHandle model, double deltaSeconds);
/**
 * 揺れの速度を0にし、現在のclip・ブレンド・IK姿勢へ戻す。設定は保つ。
 */
GKCORE_API int ResetModelSecondaryMotion(ModelHandle model);
/**
 * このinstanceの揺れもの設定と状態を解放する。未設定でも成功する。
 */
GKCORE_API int ClearModelSecondaryMotion(ModelHandle model);
}

#endif
