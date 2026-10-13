// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_FMODELSECONDARYMOTIONCONTACTANCHOR_H
#define GKCORE_FMODELSECONDARYMOTIONCONTACTANCHOR_H

#include <gkcore.h>

/**
 * 身体接触点へ寄与するboneとローカル位置、影響量を表す。
 */
namespace gk
{
/**
 * 変形後の接触点を組み立てる1つのbone寄与。
 */
struct FModelSecondaryMotionContactAnchor
{
    // 寄与するskeleton bone番号。
    uint32_t bone = 0;
    // 寄与boneのローカル位置。
    Vec3 offset{};
    // 接触点に対する正の影響量。
    float weight = 1.0f;
};
}

#endif
