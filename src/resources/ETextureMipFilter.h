// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RESOURCES_ETEXTUREMIPFILTER_H
#define GKCORE_RESOURCES_ETEXTUREMIPFILTER_H

#include <stdint.h>

/**
 * mip段階を選ぶ補間設定。
 */
namespace gk::detail
{
/**
 * mip段階間の画像補間方法。
 */
enum class ETextureMipFilter : uint32_t
{
    // mip段階を使わない。
    None = 0,
    // 最も近いmip段階を選ぶ。
    Nearest = 1,
    // mip段階間を補間する。
    Linear = 2
};

// namespace gk::detail
}

#endif
