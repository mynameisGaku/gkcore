// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FMODELSECONDARYMOTIONCONTACTPOINT_H
#define GKCORE_FMODELSECONDARYMOTIONCONTACTPOINT_H

#include <gkcore.h>

/**
 * ゲームから使うモデル接触設定を提供する。
 */
namespace gk
{
/**
 * 複数boneの寄与をまとめた身体接触点を表す。
 */
struct FModelSecondaryMotionContactPoint
{
    // 寄与配列内で最初のanchor番号。
    uint32_t firstAnchor = 0;
    // この接触点に属するanchor数。
    uint32_t anchorCount = 0;
    // 接触点を押し出すmodel空間半径。
    float radius = 0.0f;
};
}

#endif
