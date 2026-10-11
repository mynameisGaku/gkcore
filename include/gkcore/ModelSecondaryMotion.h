// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODELSECONDARYMOTION_H
#define GKCORE_MODELSECONDARYMOTION_H

#include <gkcore.h>

namespace gk
{
struct FModelSecondaryMotionSettings;
struct FModelSecondaryMotionCollider;
}
#include <gkcore/FModelSecondaryMotionSettings.h>
#include <gkcore/FModelSecondaryMotionCollider.h>
#include <gkcore/FModelSecondaryMotionDebugShape.h>
#include <gkcore/FModelSecondaryMotionLink.h>
#include <gkcore/FModelSecondaryMotionDistanceConstraint.h>

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
/**
 * 身体の接触形状をコピーして全置換する。先に揺れものの鎖を登録する。
 * 最大64個、marginはmodel空間の有限な0以上の余白。count=0で接触だけを解除する。
 * 形状はclip・blend・IK後の身体に追従し、揺れるboneの子孫へは取り付けない。
 * rootは動かさず、rootが形状内にある場合は最初の線分だけ接触から除外する。
 * 各更新時の節と線分を検査する。更新間の高速なすり抜けやmesh面の接触は扱わない。
 */
GKCORE_API int SetModelSecondaryMotionColliders(ModelHandle model, const FModelSecondaryMotionCollider* colliders, uint32_t count, float margin = 0.005f);

/**
 * 揺れboneの枝にweightを持つ、変形後の頂点を身体形状の外へ押し出す。
 * 鎖と身体形状の登録後に切り替える。既定はOFFで、対応するzero-morph FBXに限る。
 * 骨・IKは変更せず、描画予約ごとに補正を保存する。三角形内部と自己衝突は扱わない。
 * 現在の姿勢が未対応または設定が未登録なら失敗し、以前の設定を保つ。
 * 有効化後にmorph等の未対応姿勢へ進む場合は描画時に診断して失敗する。
 */
GKCORE_API int SetModelSecondaryMotionVertexContactsEnabled(ModelHandle model, bool enabled);

/**
 * 登録済み鎖の各boneと仮想末端を球として扱う半径をmodel空間で指定する。
 * firstBoneは鎖の先頭。有限な0以上で、0は従来の節・線分接触を保つ。
 * 次の揺れ更新で適用し、同じ先頭boneの再登録とResetでは設定を保つ。
 * 失敗時は以前の設定を保つ。固定rootの意図した身体との重なりは解かない。
 */
GKCORE_API int SetModelSecondaryMotionChainCollisionRadius(ModelHandle model, uint32_t firstBone, float radius);
/**
 * 別の鎖の自由boneを結ぶ接触補助をコピーして全置換する。最大1024本。
 * 同じbone対や固定rootは指定できない。iterationsは1から32、count=0で解除する。
 * 反復上限は距離制約と共通で、最後に呼んだsetterの値を使う。
 * 次のUpdateで骨長・曲げ角と交互に解き、未収束なら前回の揺れ状態を保って失敗する。
 * 辺長保持、自己衝突、布面全体や更新間の連続接触を扱う機能ではない。
 */
GKCORE_API int SetModelSecondaryMotionLinks(ModelHandle model, const FModelSecondaryMotionLink* links, uint32_t count, uint32_t iterations = 32);
/**
 * 別の鎖の自由bone間の距離制約をコピーして全置換する。最大1024組、count=0で解除する。
 * 基準距離0は登録時のclip・blend・IK姿勢から測り、正の値はmodel空間の距離を使う。
 * 柔らかさは有限な0以上。iterationsは接触線分と距離制約に共通の反復上限で1から32。
 * 最後のsetterで上限を決め、次のUpdateで適用する。身体形状がなくても使える。
 * setter失敗時は以前の設定、更新時の未収束では以前の姿勢を保つ。
 * 固定rootや同じ鎖のbone対は指定できない。Reset後は次のUpdateで距離を解く。
 */
GKCORE_API int SetModelSecondaryMotionDistanceConstraints(ModelHandle model, const FModelSecondaryMotionDistanceConstraint* constraints, uint32_t count, uint32_t iterations = 32);
/**
 * 身体と揺れものの接触形状を、現在の描画姿勢のmodel空間値へ複製する。
 * nullptr,0で必要な個数を返す。十分なcapacityなら書き込んだ個数を返す。
 * 無効なモデルや容量不足は-1でoutputを保ち、時間やsimulationを進めない。
 */
GKCORE_API int GetModelSecondaryMotionDebugShapes(ModelHandle model, FModelSecondaryMotionDebugShape* output, uint32_t capacity);

}

#endif
