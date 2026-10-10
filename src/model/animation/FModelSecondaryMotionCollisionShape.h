// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONCOLLISIONSHAPE_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONCOLLISIONSHAPE_H

#include <gkcore.h>

/**
 * 揺れものの計算で使うモデル空間の接触形状。
 */
namespace gk::model::animation
{

/**
 * モデル空間に配置する球またはカプセル。
 */
struct FModelSecondaryMotionCollisionShape
{
    // カプセル軸の始点。球ではendと同じ位置。
    gk::Vec3 start{};
    // カプセル軸の終点。球ではstartと同じ位置。
    gk::Vec3 end{};
    // 節の余白を含む、正の接触半径。
    float radius = 0.0f;
};

}

#endif
