// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_ESECONDARYMOTIONCONTACTVELOCITYRESULT_H
#define GKCORE_MODEL_ANIMATION_ESECONDARYMOTIONCONTACTVELOCITYRESULT_H

#include <stdint.h>

/**
 * 接触速度の補正処理で使う結果を定義する。
 */
namespace gk::model::animation
{

/**
 * 接触速度投影の適用結果を区別する。
 */
enum class ESecondaryMotionContactVelocityResult : uint8_t
{
    // 全制約を満たす速度を適用した。
    Applied,
    // 有限な入力だが上限内に解けず、速度を変更しなかった。
    Unresolved,
    // 入力・数値・メモリ上の失敗で速度を変更しなかった。
    Failed
};

}

#endif
