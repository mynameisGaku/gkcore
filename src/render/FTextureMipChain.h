// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FTEXTUREMIPCHAIN_H
#define GKCORE_RENDER_FTEXTUREMIPCHAIN_H

#include "render/FTextureMipLevel.h"
#include "foundation/Array.h"
#include <stdint.h>

/**
 * 転送用の縮小画像列を保持する描画資源。
 */
namespace gk::render
{

/**
 * GPU uploadへ渡す全mip段の配置と連続RGBA byte列を所有する。
 */
struct FTextureMipChain
{
    // byte列内の各mip段配置。
    Array<FTextureMipLevel> levels;
    // level順に詰めたRGBA画素byte列。
    Array<uint8_t> rgba;
};

}

#endif
