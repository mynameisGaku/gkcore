// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODELANIMATION_H
#define GKCORE_MODELANIMATION_H

#include <gkcore.h>
#include <gkcore/EHumanoidBone.h>

/**
 * 公開model animation API。
 */
namespace gk
{
/**
 * 同じ形状を共有し、独立した変換・再生状態を持つモデルを作る。
 */
GKCORE_API ModelHandle CreateModelInstance(ModelHandle model);
/**
 * 同じ頂点・面の順序を持つ連番OBJを読み込む。fpsは有限で正の値。
 * 最終frameまでの長さは(frameCount-1)/fps。失敗は無効handle。
 */
GKCORE_API ModelHandle LoadModelSequence(const char* const* utf8Paths, uint32_t frameCount, float fps);
/**
 * GLB/FBXからclipと骨格だけを読み込む。meshなしのデータも使用できる。
 */
GKCORE_API ModelAnimationHandle LoadModelAnimation(const char* utf8Path);
/**
 * 連番OBJを独立したアニメーションとして読み込む。失敗は無効handle。
 */
GKCORE_API ModelAnimationHandle LoadModelSequenceAnimation(const char* const* utf8Paths, uint32_t frameCount, float fps);
/**
 * アニメーションhandleを解放する。適用済みモデルはデータを保持する。
 */
GKCORE_API int DeleteModelAnimation(ModelAnimationHandle animation);
/**
 * 外部データにあるclip数を返す。無効handleは0。
 */
GKCORE_API uint32_t GetAnimationClipCount(ModelAnimationHandle animation);
/**
 * 外部clip名を借用参照で返す。無効指定はnull。
 */
GKCORE_API const char* GetAnimationClipName(ModelAnimationHandle animation, uint32_t clip);
/**
 * 外部clip長を秒で返す。無効指定は-1。
 */
GKCORE_API double GetAnimationClipDuration(ModelAnimationHandle animation, uint32_t clip);
/**
 * 埋め込みclipの数を返す。静的モデル・無効handleは0。
 */
GKCORE_API uint32_t GetModelAnimationCount(ModelHandle model);
/**
 * 埋め込みclip名を借用参照で返す。無効指定はnull。
 */
GKCORE_API const char* GetModelAnimationName(ModelHandle model, uint32_t clip);
/**
 * 埋め込みclipの長さを秒で返す。無効指定は-1。
 */
GKCORE_API double GetModelAnimationDuration(ModelHandle model, uint32_t clip);
/**
 * 埋め込みclipを時刻0から再生する。loop=falseなら両端で止まる。
 */
GKCORE_API int PlayModelAnimation(ModelHandle model, uint32_t clip = 0, bool loop = true);
/**
 * 外部clipを時刻0から再生する。骨格は名前または設定した役割で対応付ける。
 * 連番OBJは同じ頂点・面順序が必要。失敗時は現在の再生状態を保つ。
 */
GKCORE_API int ApplyModelAnimation(ModelHandle model, ModelAnimationHandle animation, uint32_t clip = 0, bool loop = true);
/**
 * 埋め込みclipを2番目の再生枠へ設定し、0から1のweightでブレンドする。
 */
GKCORE_API int SetModelAnimationBlend(ModelHandle model, uint32_t clip, float weight);
/**
 * 外部clipを2番目の再生枠へ設定する。weight=0は主clip、1は外部clip。
 */
GKCORE_API int SetModelAnimationBlend(ModelHandle model, ModelAnimationHandle animation, uint32_t clip, float weight);
/**
 * clipや時刻を変えずにブレンド係数を設定する。両再生枠の設定が必要。
 */
GKCORE_API int SetModelAnimationBlendWeight(ModelHandle model, float weight);
/**
 * 両再生枠を解除して初期姿勢に戻す。IKの設定は保持する。
 */
GKCORE_API int StopModelAnimation(ModelHandle model);
/**
 * 再生枠0または1の有限時刻を秒で設定する。loopに応じ折り返しまたは両端へ制限する。
 */
GKCORE_API int SetModelAnimationTime(ModelHandle model, double seconds, uint32_t slot = 0);
/**
 * 再生枠の現在時刻を秒で返す。未設定・無効指定は-1。
 */
GKCORE_API double GetModelAnimationTime(ModelHandle model, uint32_t slot = 0);
/**
 * 有限の再生倍率を設定する。0で停止、負値で逆再生する。
 */
GKCORE_API int SetModelAnimationSpeed(ModelHandle model, double speed, uint32_t slot = 0);
/**
 * 指定枠のloopを設定する。再生時刻も新しい範囲に合わせる。
 */
GKCORE_API int SetModelAnimationLoop(ModelHandle model, bool loop, uint32_t slot = 0);
/**
 * 0以上の有限経過秒を、両再生枠へ各速度を掛けて加算する。
 * アプリが毎frame呼ぶ。BeginFrameでは時刻を自動更新しない。
 */
GKCORE_API int UpdateModelAnimation(ModelHandle model, double deltaSeconds);
/**
 * モデルのボーン数を返す。静的OBJ・無効handleは0。
 */
GKCORE_API uint32_t GetModelBoneCount(ModelHandle model);
/**
 * 指定ボーン名を借用参照で返す。無効指定はnull。
 */
GKCORE_API const char* GetModelBoneName(ModelHandle model, uint32_t bone);
/**
 * 一意なボーン名を検索する。見つからない・同名が複数ある場合は-1。
 */
GKCORE_API int32_t FindModelBone(ModelHandle model, const char* name);
/**
 * 外部データのボーン数を返す。無効handleは0。
 */
GKCORE_API uint32_t GetAnimationBoneCount(ModelAnimationHandle animation);
/**
 * 外部データのボーン名を返す。無効指定はnull。
 */
GKCORE_API const char* GetAnimationBoneName(ModelAnimationHandle animation, uint32_t bone);
/**
 * モデルのボーンへ人型の役割を割り当てる。適用前に設定し、同じ役割を重複させない。
 */
GKCORE_API int SetModelBoneRole(ModelHandle model, uint32_t bone, EHumanoidBone role);
/**
 * 外部データのボーンへ人型の役割を割り当てる。適用済みの対応表は変更しない。
 */
GKCORE_API int SetAnimationBoneRole(ModelAnimationHandle animation, uint32_t bone, EHumanoidBone role);
/**
 * 一般的な人型ボーン名から役割を推定する。手動設定は保ち、曖昧な候補は拒否する。
 * 認識できるボーンがない場合は-1、成功は0。適用前に呼ぶ。
 */
GKCORE_API int AutoMapModelHumanoidBones(ModelHandle model);
/**
 * 外部motionの人型ボーン名から役割を推定する。適用済みの対応表は変えない。
 */
GKCORE_API int AutoMapAnimationHumanoidBones(ModelAnimationHandle animation);
/**
 * 設定したモデルのボーン役割を返す。無効指定・未設定はNone。
 */
GKCORE_API EHumanoidBone GetModelBoneRole(ModelHandle model, uint32_t bone);
/**
 * 設定した外部motionのボーン役割を返す。無効指定・未設定はNone。
 */
GKCORE_API EHumanoidBone GetAnimationBoneRole(ModelAnimationHandle animation, uint32_t bone);
/**
 * 適用先ボーンに対応するsource番号を返す。未対応・未再生・無効指定は-1。
 */
GKCORE_API int32_t GetModelAnimationSourceBone(ModelHandle model, uint32_t targetBone, uint32_t slot = 0);
/**
 * 連続した3ボーンのIKを設定する。targetとpoleはモデル空間、weightは0から1。
 * 同じrootへの指定は上書きする。ブレンド後の姿勢へ適用する。
 */
GKCORE_API int SetModelTwoBoneIk(ModelHandle model, uint32_t root, uint32_t middle, uint32_t end, Vec3 target, Vec3 pole, float weight = 1.0f);
/**
 * 親子が連続した2本以上のボーン列へFABRIKのIKを設定する。
 * targetはモデル空間、weightは0から1。配列は呼び出し中にコピーする。
 */
GKCORE_API int SetModelIkChain(ModelHandle model, const uint32_t* bones, uint32_t count, Vec3 target, float weight = 1.0f);
/**
 * このモデルのIK設定をすべて解除する。
 */
GKCORE_API int ClearModelIk(ModelHandle model);
}

#endif
