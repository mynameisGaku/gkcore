// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONATTACHMENTEVALUATION_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONATTACHMENTEVALUATION_H

#include "foundation/FVector3d.h"

/**
 * 骨に固定した接触点の位置と微分を表す。
 */
namespace gk::model::animation
{

/**
 * 子骨位置の変化に対する接触点位置の変化率を保持する。
 */
struct FSecondaryMotionAttachmentEvaluation
{
    // 現在の親子方向へ回転した接触点の位置。
    gk::FVector3d position{};
    // 子位置のXYZ変化から接触点XYZ変化への行優先3x3行列。
    double childJacobian[9]{};
};

}

#endif
