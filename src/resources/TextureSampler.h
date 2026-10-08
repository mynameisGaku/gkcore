// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RESOURCES_TEXTURESAMPLER_H
#define GKCORE_RESOURCES_TEXTURESAMPLER_H

#include "FTextureSampler.h"
#include <stdint.h>

/**
 * texture samplerの値検査と、固定descriptor表内の位置計算。
 */
namespace gk::detail
{

/**
 * 各軸の繰り返し方法と補間方法が対応範囲内か調べる。
 */
bool IsTextureSamplerValid(const FTextureSampler& sampler);
/**
 * 2つのtexture samplerがすべて同じ設定か調べる。
 */
bool AreTextureSamplersEqual(const FTextureSampler& left, const FTextureSampler& right);
/**
 * 有効なsamplerを108状態の固定表へ対応付ける。失敗時は出力indexを維持する。
 */
bool GetTextureSamplerIndex(const FTextureSampler& sampler, uint32_t& index);

// namespace gk::detail
}

#endif
