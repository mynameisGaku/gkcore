// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODEL_SECONDARY_MOTION_DISTANCES_H
#define GKCORE_MODEL_ANIMATION_MODEL_SECONDARY_MOTION_DISTANCES_H

#include "foundation/FVector3d.h"
#include "foundation/String.h"

/**
 * 二つの自由節間に設定する距離制約の投影と検査。
 */
namespace gk::model::animation
{

/**
 * 基準距離・柔らかさ・秒数で候補点を補正する。失敗時は点と補正累計lambdaを保つ。
 */
bool ProjectSecondaryMotionDistance(const gk::FVector3d& referenceFirst, const gk::FVector3d& referenceSecond, double restDistance, double compliance, double stepSeconds, double& lambda, gk::FVector3d& first, gk::FVector3d& second, gk::String& error);

/**
 * 距離制約の残差を許容割合で検査する。範囲外または不成立ならfalseを返す。
 */
bool CheckSecondaryMotionDistance(double restDistance, double compliance, double stepSeconds, double lambda, const gk::FVector3d& first, const gk::FVector3d& second, gk::String& error, double toleranceFraction = 1.0);

}

#endif
