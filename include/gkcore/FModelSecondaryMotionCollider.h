// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FMODELSECONDARYMOTIONCOLLIDER_H
#define GKCORE_FMODELSECONDARYMOTIONCOLLIDER_H

#include <gkcore.h>

/**
 * 揺れものと身体の接触形状を表す公開型。
 */
namespace gk
{
/**
 * 指定boneに追従するカプセル。両端が同じなら球として扱う。
 * 端点と半径はboneのローカル空間で指定し、正のほぼ一様な祖先scaleが必要。
 */
struct FModelSecondaryMotionCollider
{
    // 形状を取り付ける身体のbone番号。揺れる鎖の子孫には取り付けない。
    uint32_t bone = 0;
    // boneを基準にした軸の始点。
    Vec3 start{};
    // boneを基準にした軸の終点。
    Vec3 end{};
    // 身体の正の半径。節の余白はAPIで別に指定する。
    float radius = 0.05f;
};
}

#endif
