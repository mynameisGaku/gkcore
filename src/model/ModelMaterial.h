// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_MODELMATERIAL_H
#define GKCORE_MODEL_MODELMATERIAL_H

#include "resources/Resources.h"

/**
 * model材質変更用の内部snapshot操作。
 */
namespace gk::model
{
/**
 * sourceを複製し材質一つを差し替えたresourceを返す。失敗時はnull。
 */
detail::ModelResource* CreateMaterialSnapshot(const detail::ModelResource& source, uint32_t materialIndex, const float baseColorFactor[4], int32_t baseColorTextureIndex, bool alphaMask, bool alphaBlend, float alphaCutoff, String& error);
}

#endif
