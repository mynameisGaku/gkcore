// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RESOURCES_FTEXTURESAMPLER_H
#define GKCORE_RESOURCES_FTEXTURESAMPLER_H

#include "ETextureAddressMode.h"
#include "ETextureFilter.h"

/**
 * 材質ごとの画像参照設定。
 */
namespace gk::detail
{
/**
 * 画像参照ごとの座標処理と画素補間設定。
 */
struct FTextureSampler
{
    // 横方向の範囲外座標を処理する方法。
    ETextureAddressMode addressU = ETextureAddressMode::ClampToEdge;
    // 縦方向の範囲外座標を処理する方法。
    ETextureAddressMode addressV = ETextureAddressMode::ClampToEdge;
    // 縮小時に使う補間方法。
    ETextureFilter minFilter = ETextureFilter::Linear;
    // 拡大時に使う補間方法。
    ETextureFilter magFilter = ETextureFilter::Linear;
};

// namespace gk::detail
}

#endif
