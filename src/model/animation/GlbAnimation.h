// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_GLBANIMATION_H
#define GKCORE_MODEL_ANIMATION_GLBANIMATION_H

#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"
#include "resources/Resources.h"

struct cgltf_data;

namespace gk::model
{

/**
 * 読み込み済みGLB documentとbyte列の所有権を受け取り、共通animation sourceを作る。
 * 失敗時もdocumentとbyte列を解放する。
 */
AModelAnimationSource* CreateGlbAnimationSource(cgltf_data* ownedData, uint8_t* ownedBytes, uint32_t byteCount, const detail::ModelResource& geometry, String& error);

/**
 * GLB byte列を内部へ複製してanimation assetを作る。入力byte列は借用する。
 */
FModelAnimationAsset* LoadGlbAnimation(const uint8_t* bytes, uint32_t size, String& error);

}

#endif
