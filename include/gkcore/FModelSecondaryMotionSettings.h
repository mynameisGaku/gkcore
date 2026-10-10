// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FMODELSECONDARYMOTIONSETTINGS_H
#define GKCORE_FMODELSECONDARYMOTIONSETTINGS_H

#include <gkcore.h>
#include <stdint.h>

namespace gk
{

/**
 * 1本のモデル空間揺れもの鎖に使う物理設定。
 */
struct FModelSecondaryMotionSettings
{
    // 目標姿勢へ戻すばねの振動数。
    float frequencyHz = 3.0f;
    // 速度を抑える比率。
    float dampingRatio = 0.7f;
    // モデル空間で各点へ加える重力。
    Vec3 gravity{ 0.0f, -9.81f, 0.0f };
    // モデル空間で各点へ加える風の加速度。
    Vec3 windAcceleration{};
    // アニメーション鎖から許す最大曲げ角。
    float maxAngleDegrees = 60.0f;
    // 最後のboneから延ばす、有限でゼロ以外のローカル位置。
    Vec3 endOffset{ 0.0f, 0.05f, 0.0f };
    // anchorが一度に移動したときsimulationを再配置する距離。
    float teleportDistance = 0.5f;
    // 鎖の長さと曲げ角を戻す反復回数。
    uint32_t constraintIterations = 8;
};

}

#endif
