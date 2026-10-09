// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODELMATERIAL_H
#define GKCORE_MODELMATERIAL_H

#include <gkcore.h>
#include <gkcore/FModelMaterialSettings.h>

/**
 * モデル材質を問い合わせ・変更する公開API。
 */
namespace gk
{
/**
 * modelが持つ材質数を返す。無効handleでは0。
 */
GKCORE_API uint32_t GetModelMaterialCount(ModelHandle model);
/**
 * modelの材質を複製して変更する。予約済み描画や他instanceの材質は保持する。
 * 係数が有限な[0,1]外、cutoffが負または非有限、handleやindexが無効なら-1。
 */
GKCORE_API int SetModelMaterial(ModelHandle model, uint32_t materialIndex, const FModelMaterialSettings& settings);
}

#endif
