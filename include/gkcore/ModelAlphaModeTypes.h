// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODELALPHAMODETYPES_H
#define GKCORE_MODELALPHAMODETYPES_H

/**
 * 基本色のalphaをモデル描画で扱う方法。
 */
namespace gk
{
enum class EModelAlphaMode : unsigned char
{
    // alphaを使わず不透明に描く。
    Opaque = 0,
    // alphaCutoff未満の画素を捨てる。
    Mask = 1,
    // alphaで背後の色と混ぜる。
    Blend = 2
};
}

#endif
