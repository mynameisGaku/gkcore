// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FTEXTUREMIPLEVEL_H
#define GKCORE_RENDER_FTEXTUREMIPLEVEL_H

#include <stdint.h>

/**
 * 縮小段の配置を扱う描画資源の値。
 */
namespace gk::render
{

/**
 * 連続したRGBA配列内で一段分の寸法とbyte範囲を示す。
 */
struct FTextureMipLevel
{
    // この段の画像横幅。
    uint32_t width = 0;
    // この段の画像縦幅。
    uint32_t height = 0;
    // mip chain先頭からのbyte位置。
    uint32_t offset = 0;
    // この段が占めるRGBA byte数。
    uint32_t byteSize = 0;
};

}

#endif
