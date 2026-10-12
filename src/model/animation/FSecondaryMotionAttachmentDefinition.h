// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONATTACHMENTDEFINITION_H
#define GKCORE_MODEL_ANIMATION_FSECONDARYMOTIONATTACHMENTDEFINITION_H

#include "foundation/FVector3d.h"

/**
 * 骨に固定した接触点の基準情報を表す。
 */
namespace gk::model::animation
{

/**
 * 基準骨姿勢に対する接触点の方向、位置、許容角を保持する。
 */
struct FSecondaryMotionAttachmentDefinition
{
    // 基準姿勢で親から子へ向かう単位方向。
    gk::FVector3d direction{};
    // 基準骨の線形変換を適用した接触点の局所位置。
    gk::FVector3d offset{};
    // 基準方向から許される角度の余弦。
    double cosineLimit = -1.0;
};

}

#endif
