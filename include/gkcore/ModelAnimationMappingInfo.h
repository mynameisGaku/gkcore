// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODELANIMATIONMAPPINGINFO_H
#define GKCORE_MODELANIMATIONMAPPINGINFO_H

#include <stdint.h>

/**
 * モデルへ適用したアニメーションの対応状況を表す公開型。
 */
namespace gk
{
/**
 * 登録時点の骨対応を数えた値。対応件数は、その骨に動きがあることを保証しない。
 */
struct FModelAnimationMappingInfo
{
    // 適用先の全ボーン数。補助骨も含む。
    uint32_t targetBoneCount = 0;
    // 名前または役割でsourceに結び付いた適用先ボーン数。
    uint32_t mappedBoneCount = 0;
    // 登録時に人型役割を持っていた適用先ボーン数。
    uint32_t humanoidBoneCount = 0;
    // 同じ人型役割でsourceへ結び付いた適用先ボーン数。名前だけの対応は除く。
    uint32_t mappedHumanoidBoneCount = 0;
};
}

#endif
