// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_ETEXTURECOLORSPACE_H
#define GKCORE_RENDER_ETEXTURECOLORSPACE_H

#include <stdint.h>

namespace gk::render
{

/**
 * GPU textureで画像RGBを解釈する色空間。
 */
enum class ETextureColorSpace : uint8_t
{
    // sRGBから線形へ変換する基本色画像。
    Srgb,
    // 値を変換せず読む金属度・粗さ画像。
    Linear
};

}

#endif
