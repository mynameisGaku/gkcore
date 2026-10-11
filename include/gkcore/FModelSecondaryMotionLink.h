// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FMODEL_SECONDARY_MOTION_LINK_H
#define GKCORE_FMODEL_SECONDARY_MOTION_LINK_H

#include <stdint.h>

/**
 * 揺れものの鎖の間に置く接触補助を表す値。
 */
namespace gk
{
/**
 * 別の鎖にある自由bone原点を、身体との接触を調べる線分で結ぶ。
 * 辺の長さを固定する機能ではない。両端に鎖の固定rootは指定できない。
 */
struct FModelSecondaryMotionLink
{
    // 線分の始点になる登録済みの自由bone番号。
    uint32_t firstBone = 0;
    // 別の鎖にある、線分の終点になる自由bone番号。
    uint32_t secondBone = 0;
    // 線分を包むmodel空間の半径。0は厚みを持たない線分。
    float radius = 0.0f;
};
}

#endif
