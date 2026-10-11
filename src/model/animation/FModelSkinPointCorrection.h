// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSKINPOINTCORRECTION_H
#define GKCORE_MODEL_ANIMATION_FMODELSKINPOINTCORRECTION_H

#include "foundation/FVector3d.h"
#include <stdint.h>

/**
 * skin位置一つに適用するmodel空間の補正を表す。
 */
namespace gk::model::animation
{

/**
 * 指定位置IDのskin位置を置き換える補正値。
 */
struct FModelSkinPointCorrection
{
    // 補正するskin位置のID。
    uint32_t positionId = 0;
    // skin変形後に使うmodel空間の位置。
    double position[3]{};
};

}

#endif
