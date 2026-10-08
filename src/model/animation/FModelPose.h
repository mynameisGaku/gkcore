// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODEL_POSE_H
#define GKCORE_MODEL_ANIMATION_FMODEL_POSE_H

#include "../../foundation/Array.h"
#include "FModelBoneTransform.h"

namespace gk::model::animation
{

/**
 * ひとつの評価時刻における親基準ボーン姿勢とmorph係数。
 */
struct FModelPose
{
    // skeletonと同じ順に並ぶ親基準ボーン変換。
    gk::Array<FModelBoneTransform> localTransforms;
    // skeletonのmorph target順に並ぶ係数。
    gk::Array<float> morphWeights;
};

}

#endif
