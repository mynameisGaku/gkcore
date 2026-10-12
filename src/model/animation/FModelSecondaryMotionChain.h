// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONCHAIN_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONCHAIN_H

#include <stdint.h>
#include <gkcore/FModelSecondaryMotionSettings.h>
#include "model/animation/FModelSecondaryMotionChainState.h"
#include "model/animation/FModelBoneTransform.h"

/**
 * モデルinstanceが所有する揺れものの内部型。
 */
namespace gk::model
{
/**
 * 1本の独立した鎖の設定・物理状態・確定回転を所有する。
 */
struct FModelSecondaryMotionChain
{
    // 書き換える連続したbone列。
    Array<uint32_t> bones;
    // この鎖のばね・減衰・外力。
    FModelSecondaryMotionSettings settings;
    // 各節の球と隣接節間の太さ。model空間で指定し、0は従来の節接触。
    float collisionRadius = 0.0f;
    // 節球・隣接線分が調べるcollider index。全形状を既定とする。
    uint64_t colliderMask = UINT64_MAX;
    // 前回更新の節位置と速度。
    animation::FModelSecondaryMotionChainState simulation;
    // bonesと同じ順の確定回転。位置とscaleは適用しない。
    Array<animation::FModelBoneTransform> transforms;
};
}

#endif
