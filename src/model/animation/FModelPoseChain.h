// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODEL_POSE_CHAIN_H
#define GKCORE_MODEL_ANIMATION_FMODEL_POSE_CHAIN_H

#include <stdint.h>
#include <gkcore.h>

/**
 * モデル姿勢の向きを合わせる計算用の型。
 */
namespace gk::model::animation
{
/**
 * 呼び出し中だけ借りる鎖の骨番号と目標位置。配列はcallerが保持する。
 */
struct FModelPoseChain
{
    // 親子順に並ぶ鎖のbone番号。
    const uint32_t* bones = nullptr;
    // 鎖に含めるbone数。
    uint32_t count = 0;
    // 最終boneから先へ伸ばすlocal空間の位置。
    gk::Vec3 endOffset{};
    // rootから仮想末端までのmodel空間目標位置。count+1個を保持する。
    const gk::Vec3* points = nullptr;
};

}

#endif
