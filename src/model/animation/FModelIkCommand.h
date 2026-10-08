// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELIKCOMMAND_H
#define GKCORE_MODEL_ANIMATION_FMODELIKCOMMAND_H

#include <stdint.h>

/**
 * model animation用の内部IK型。
 */
namespace gk::model
{
/**
 * ブレンド済み姿勢へ適用する、モデル空間のIK設定。
 */
struct FModelIkCommand
{
    // 保存したボーン列の開始位置。
    uint32_t offset = 0;
    // ボーン列の長さ。
    uint32_t count = 0;
    // endボーンの目標位置。
    float target[3]{};
    // 2ボーンの曲げ方向を決める目標位置。
    float pole[3]{};
    // IK姿勢の寄与率。
    float weight = 1.0f;
    // trueは2ボーン、falseは汎用FABRIK。
    bool twoBone = false;
};
}

#endif
