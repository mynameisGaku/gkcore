// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_EHUMANOIDBONE_H
#define GKCORE_EHUMANOIDBONE_H

#include <stdint.h>

/**
 * 公開APIで共有する型付き値。
 */
namespace gk
{
/**
 * モデル間でボーン名が違う場合にも使える、人型のボーンの役割。
 */
enum class EHumanoidBone : uint16_t
{
    None = 0,                // 役割を指定しない。
    Hips,                    // 腰。
    Spine,                   // 背骨の下側。
    Chest,                   // 胸。
    UpperChest,              // 胸の上側。
    Neck,                    // 首。
    Head,                    // 頭。
    LeftShoulder,            // 左肩。
    LeftUpperArm,            // 左上腕。
    LeftLowerArm,            // 左前腕。
    LeftHand,                // 左手。
    RightShoulder,           // 右肩。
    RightUpperArm,           // 右上腕。
    RightLowerArm,           // 右前腕。
    RightHand,               // 右手。
    LeftUpperLeg,            // 左大腿。
    LeftLowerLeg,            // 左すね。
    LeftFoot,                // 左足。
    LeftToes,                // 左つま先。
    RightUpperLeg,           // 右大腿。
    RightLowerLeg,           // 右すね。
    RightFoot,               // 右足。
    RightToes,               // 右つま先。
    LeftEye,                 // 左目。
    RightEye,                // 右目。
    Jaw,                     // あご。
    LeftThumbProximal,       // 左親指の付け根。
    LeftThumbIntermediate,   // 左親指の中間。
    LeftThumbDistal,         // 左親指の先端。
    LeftIndexProximal,       // 左人差し指の付け根。
    LeftIndexIntermediate,   // 左人差し指の中間。
    LeftIndexDistal,         // 左人差し指の先端。
    LeftMiddleProximal,      // 左中指の付け根。
    LeftMiddleIntermediate,  // 左中指の中間。
    LeftMiddleDistal,        // 左中指の先端。
    LeftRingProximal,        // 左薬指の付け根。
    LeftRingIntermediate,    // 左薬指の中間。
    LeftRingDistal,          // 左薬指の先端。
    LeftLittleProximal,      // 左小指の付け根。
    LeftLittleIntermediate,  // 左小指の中間。
    LeftLittleDistal,        // 左小指の先端。
    RightThumbProximal,      // 右親指の付け根。
    RightThumbIntermediate,  // 右親指の中間。
    RightThumbDistal,        // 右親指の先端。
    RightIndexProximal,      // 右人差し指の付け根。
    RightIndexIntermediate,  // 右人差し指の中間。
    RightIndexDistal,        // 右人差し指の先端。
    RightMiddleProximal,     // 右中指の付け根。
    RightMiddleIntermediate, // 右中指の中間。
    RightMiddleDistal,       // 右中指の先端。
    RightRingProximal,       // 右薬指の付け根。
    RightRingIntermediate,   // 右薬指の中間。
    RightRingDistal,         // 右薬指の先端。
    RightLittleProximal,     // 右小指の付け根。
    RightLittleIntermediate, // 右小指の中間。
    RightLittleDistal,       // 右小指の先端。
    Count                    // 役割値の検査用の終端。
};
}

#endif
