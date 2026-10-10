// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONPOINT_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONPOINT_H

#include <gkcore.h>

namespace gk::model::animation
{

/**
 * 揺れもの鎖の位置と速度を保持する値。
 */
struct FModelSecondaryMotionPoint
{
    // model空間での位置。
    gk::Vec3 position;
    // model空間での速度。
    gk::Vec3 velocity;
};

}

#endif
