// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_TEXTUREMIPCHAIN_H
#define GKCORE_RENDER_TEXTUREMIPCHAIN_H

#include "ETextureColorSpace.h"
#include "FTextureMipChain.h"
#include "../resources/Resources.h"

/**
 * 画像の縮小段を計画・生成する描画補助処理。
 */
namespace gk::render
{

/**
 * 画像寸法から全mip段のbyte配置を計画する。範囲超過や確保失敗では出力を保つ。
 */
bool PlanTextureMipChain(uint32_t width, uint32_t height, bool fullChain, Array<FTextureMipLevel>& levels, uint64_t& byteCount, String& error);

/**
 * RGBA画像から色空間を保つmip画像列を作る。入力と出力は失敗時に保たれる。
 */
bool BuildTextureMipChain(const detail::ImageResource& image, ETextureColorSpace colorSpace, bool fullChain, FTextureMipChain& output, String& error);

}

#endif
