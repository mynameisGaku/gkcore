// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_OBJSEQUENCE_H
#define GKCORE_MODEL_ANIMATION_OBJSEQUENCE_H

#include "model/animation/FModelAnimationAsset.h"
#include "resources/Resources.h"

/**
 * OBJ連番animationを読み込む内部機能。
 */
namespace gk::model
{
/**
 * 連番OBJを読み込み、初期モデルとclipを返す。失敗時は両方null。
 * 返されたbaseとassetの参照は、呼び出し側がそれぞれ解放する。
 */
FModelAnimationAsset* LoadObjSequence(const char* const* paths, uint32_t count, float fps, detail::ModelResource*& base, String& error);
}

#endif
