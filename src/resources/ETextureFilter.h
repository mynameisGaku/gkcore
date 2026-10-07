// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RESOURCES_ETEXTUREFILTER_H
#define GKCORE_RESOURCES_ETEXTUREFILTER_H

#include <stdint.h>

/**
 * 画像の拡大・縮小で使う補間設定。
 */
namespace gk::detail
{
/**
 * 画像の画素間を補間するときの方法。
 */
enum class ETextureFilter : uint32_t
{
    // 周囲4画素を補間する。
    Linear = 0,
    // 近い1画素をそのまま使う。
    Nearest = 1
};

// namespace gk::detail
}

#endif
