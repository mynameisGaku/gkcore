// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FMODEL_SECONDARY_MOTION_DISTANCE_CONSTRAINT_H
#define GKCORE_FMODEL_SECONDARY_MOTION_DISTANCE_CONSTRAINT_H

#include <stdint.h>

/**
 * 別の揺れもの鎖の間隔を保つための設定。
 */
namespace gk
{
/**
 * 2つの自由boneの距離を、伸び縮みを許す量とともに指定する。
 */
struct FModelSecondaryMotionDistanceConstraint
{
    // 登録済み鎖の自由bone番号。
    uint32_t firstBone = 0;
    // 別の鎖にある自由bone番号。
    uint32_t secondBone = 0;
    // model空間の基準距離。0なら登録時のアニメーション姿勢から測る。
    float restDistance = 0.0f;
    // 柔らかさを表す有限な0以上の値。0は距離を保ち、正の値は伸び縮みを許す。
    float compliance = 0.0f;
};
}

#endif
