// SPDX-License-Identifier: NOASSERTION
#include "FModelPlayback.h"

/**
 * model instanceの再生状態を解放する処理。
 */
namespace gk::model
{
FModelPlayback::~FModelPlayback()
{
    for (uint32_t i = 0; i < 2; ++i)
    {
        if (clips[i].asset)
            Release(&clips[i].asset->reference);
    }
}
}
