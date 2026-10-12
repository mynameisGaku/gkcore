// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FMODELSECONDARYMOTIONCONTACTLINK_H
#define GKCORE_FMODELSECONDARYMOTIONCONTACTLINK_H

#include <gkcore.h>

/**
 * ゲームから使うモデル接触設定を提供する。
 */
namespace gk
{
/**
 * 骨ローカル位置を結ぶ、揺れもの用の補助接触線分。
 */
struct FModelSecondaryMotionContactLink
{
    // 1つ目の自由bone番号。
    uint32_t firstBone = 0;
    // 1つ目のboneローカル端点。
    Vec3 firstOffset{};
    // 2つ目の自由bone番号。
    uint32_t secondBone = 0;
    // 2つ目のboneローカル端点。
    Vec3 secondOffset{};
    // 線分のmodel空間半径。
    float radius = 0.0f;
};
}

#endif
