// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODEL_POSE_H
#define GKCORE_MODEL_ANIMATION_MODEL_POSE_H

#include "FModelPose.h"
#include "FModelSkeleton.h"
#include "../../foundation/String.h"
#include <stdint.h>

namespace gk::model::animation
{

/**
 * 骨格姿勢の初期化、補間、親子変換評価を提供する。
 */

/**
 * skeletonのrest姿勢とdefault morph係数からposeを作る。失敗時はoutputを保つ。
 */
bool InitializeModelPose(const FModelSkeleton& skeleton, FModelPose& output, gk::String& error);

/**
 * 位置・拡大率を線形補間し、quaternionを最短経路でslerpする。失敗時はoutputを保つ。
 */
bool BlendModelPoses(const FModelSkeleton& skeleton, const FModelPose& first, const FModelPose& second, float weight, FModelPose& output, gk::String& error);

/**
 * 親基準poseをcolumn-major 4x4モデル空間行列へ展開する。失敗時はoutputを保つ。
 */
bool EvaluateModelPose(const FModelSkeleton& skeleton, const FModelPose& pose, gk::Array<float>& worldMatrices, gk::String& error);

/**
 * 一つの骨変換をcolumn-major行列へ変換する。有限値でない入力ではoutputを保つ。
 */
bool BuildModelBoneMatrix(const FModelBoneTransform& transform, float output[16], gk::String& error);

/**
 * 親とlocalのcolumn-major行列を乗算する。範囲外結果ではoutputを保つ。
 */
bool MultiplyModelBoneMatrices(const float parent[16], const float local[16], float output[16], gk::String& error);

}

#endif
