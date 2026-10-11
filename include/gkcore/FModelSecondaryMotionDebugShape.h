// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FMODEL_SECONDARY_MOTION_DEBUG_SHAPE_H
#define GKCORE_FMODEL_SECONDARY_MOTION_DEBUG_SHAPE_H

#include <gkcore.h>

/**
 * 揺れものの接触設定を描画や調整へ渡す値。
 */
namespace gk
{
/**
 * 現在の描画姿勢に追従した、model空間の球またはカプセルを表す。
 * startとendが同じなら球。身体のradiusは設定余白を含む。
 */
struct FModelSecondaryMotionDebugShape
{
    // 接触形状の始点または球の中心。
    Vec3 start{};
    // 接触形状の終点。球ではstartと同じ位置。
    Vec3 end{};
    // model空間での接触半径。0は大きさを持たない節。
    float radius = 0.0f;
    // 身体の取付bone、または揺れものの対象bone。
    uint32_t bone = 0;
    // 身体の接触形状ならtrue、揺れものの節ならfalse。
    bool body = false;
    // 揺れものの固定rootならtrue。身体形状ではfalse。
    bool fixed = false;
    // 最後のboneから延ばした仮想末端ならtrue。
    bool virtualTip = false;
};
}

#endif
