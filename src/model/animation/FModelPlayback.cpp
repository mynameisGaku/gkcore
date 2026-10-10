// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FModelPlayback.h"
#include "model/animation/FModelSecondaryMotionState.h"
#include "model/animation/FModelPoseCache.h"

/**
 * model instanceの再生状態を解放する処理。
 */
namespace gk::model
{
FModelPlayback::~FModelPlayback()
{
    delete secondaryMotion;
    delete basePoseCache;
    for (uint32_t i = 0; i < 2; ++i)
    {
        if (clips[i].asset)
            Release(&clips[i].asset->reference);
    }
}
}
