// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RESOURCES_ETEXTUREADDRESSMODE_H
#define GKCORE_RESOURCES_ETEXTUREADDRESSMODE_H

#include <stdint.h>

/**
 * 画像の範囲外座標を扱う資源設定。
 */
namespace gk::detail
{
/**
 * 画像座標が範囲外へ出たときの繰り返し方。
 */
enum class ETextureAddressMode : uint32_t
{
    // 端の画素を使う。
    ClampToEdge = 0,
    // 画像を繰り返す。
    Repeat = 1,
    // 反転を交互に加えて繰り返す。
    MirroredRepeat = 2
};

// namespace gk::detail
}

#endif
