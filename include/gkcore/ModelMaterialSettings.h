// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODELMATERIALSETTINGS_H
#define GKCORE_MODELMATERIALSETTINGS_H

#include <gkcore/ModelAlphaModeTypes.h>
#include <gkcore/Handle.h>

/**
 * 公開モデル材質設定。
 */
namespace gk
{
/**
 * 1つのmodel材質へ適用する基本色とalpha設定。
 */
struct FModelMaterialSettings
{
    // 基本色へ掛けるRGBA係数。
    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    // 基本色画像。無効handleなら画像を使わない。
    ImageHandle baseColorImage{};
    // 基本色画像を縮小表示するときにmipmapを使う。
    bool generateMipmaps = true;
    // 基本色alphaの描画方法。
    EModelAlphaMode alphaMode = EModelAlphaMode::Opaque;
    // Maskで画素を残すalphaの下限。
    float alphaCutoff = 0.5f;
};
}

#endif
