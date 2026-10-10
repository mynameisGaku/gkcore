// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODEL_POSE_CACHE_H
#define GKCORE_MODEL_ANIMATION_FMODEL_POSE_CACHE_H

#include "model/animation/FModelPose.h"
#include "model/animation/FModelIkCommand.h"
#include <stdint.h>

/**
 * instanceごとに基準姿勢を再利用する内部型。
 */
namespace gk::model
{
class AModelAnimationSource;
struct FModelAnimationAsset;
/**
 * 揺れを重ねる前の姿勢と、その評価条件を所有する。元データの参照は識別用に借りる。
 */
struct FModelPoseCache
{
    // 基準姿勢を作った元モデルのsource。
    const AModelAnimationSource* source = nullptr;
    // 保存時の再生状態の世代番号。
    uint64_t revision = 0;
    // 主・副clipのasset識別子。所有参照はPlayback側にある。
    const FModelAnimationAsset* assets[2]{};
    // 同じasset内でsourceが差し替わっていないか確認する識別子。
    const AModelAnimationSource* clipSources[2]{};
    // 保存時のclip番号。
    uint32_t clips[2]{};
    // 保存時の再生時刻。
    double seconds[2]{};
    // 保存時の再生速度。
    double speeds[2]{};
    // 保存時のloop設定。
    bool loops[2]{};
    // 保存時のブレンド率。
    float blendWeight = 0.0f;
    // 保存時のIK命令。現在の命令と値で照合する。
    Array<FModelIkCommand> ik;
    // 保存時のIKの骨番号。
    Array<uint32_t> ikBones;
    // clip・blend・IKだけを評価した基準姿勢。
    animation::FModelPose pose;
};
}

#endif
