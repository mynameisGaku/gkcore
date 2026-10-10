// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODEL_IK_H
#define GKCORE_MODEL_ANIMATION_MODEL_IK_H

#include "model/animation/ModelPose.h"
#include <stdint.h>
#include <gkcore.h>

namespace gk::model::animation
{
#if defined(GKCORE_TESTING)
/**
 * 検査用のworld回転評価数を0へ戻す。製品buildには含めない。
 */
void ResetModelIkWorkForTesting();
/**
 * 検査中に計算したworld回転の骨数を返す。全骨格の入力検査とは別に数える。
 */
uint64_t GetModelIkWorldRotationCountForTesting();
#endif


/**
 * モデル空間の目標位置へ骨格姿勢を解くIK関数を提供する。
 */

/**
 * 連続したroot、middle、endボーンをpoleで曲げる2ボーンIKを解く。失敗時はoutputを保つ。
 */
bool SolveTwoBoneIk(const FModelSkeleton& skeleton, const FModelPose& source, uint32_t rootBone, uint32_t middleBone, uint32_t endBone, const float target[3], const float pole[3], float weight, FModelPose& output, gk::String& error);

/**
 * rootからendまでの連続chainをFABRIKで解く。失敗時はoutputを保つ。
 */
bool SolveFabrikIk(const FModelSkeleton& skeleton, const FModelPose& source, const uint32_t* chain, uint32_t chainCount, const float target[3], float weight, float tolerance, uint32_t maxIterations, FModelPose& output, gk::String& error);

/**
 * rootからendまでの連続chainをCCDで解く。失敗時はoutputを保つ。
 */
bool SolveCcdIk(const FModelSkeleton& skeleton, const FModelPose& source, const uint32_t* chain, uint32_t chainCount, const float target[3], float weight, float tolerance, uint32_t maxIterations, FModelPose& output, gk::String& error);

/**
 * 鎖と末端の先をmodel空間の方向へ回す。位置とscaleは保ち、失敗時はoutputを保つ。
 */
bool OrientModelPoseChain(const FModelSkeleton& skeleton, const FModelPose& source, const uint32_t* bones, uint32_t count, gk::Vec3 endOffset, const gk::Vec3* points, FModelPose& output, gk::String& error);

}

#endif
