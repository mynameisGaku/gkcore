// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODEL_SKELETON_H
#define GKCORE_MODEL_ANIMATION_FMODEL_SKELETON_H

#include "../../foundation/Array.h"
#include "FModelBoneTransform.h"
#include <stdint.h>

namespace gk::model::animation
{

/**
 * 形式ごとの骨格情報を共通親階層とrest姿勢で保持する。
 */
struct FModelSkeleton
{
    // 各ボーンの親index。根は-1で、親indexは自身より小さい。
    gk::Array<int32_t> parents;
    // rest姿勢における親基準の各ボーン変換。有限なゼロscaleを許可する。
    gk::Array<FModelBoneTransform> restLocalTransforms;
    // rest姿勢におけるmorph target係数。
    gk::Array<float> restMorphWeights;
};

}

#endif
