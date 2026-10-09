// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELANIMATIONRESOURCES_H
#define GKCORE_MODEL_ANIMATION_MODELANIMATIONRESOURCES_H

#include "model/animation/FModelAnimationAsset.h"
#include <gkcore/Handle.h>

/**
 * animation handle registryの内部機能。
 */
namespace gk::model
{
/**
 * assetの参照を消費してhandleを登録する。失敗時も参照を解放する。
 */
ModelAnimationHandle RegisterAnimation(FModelAnimationAsset* asset, String& error);
/**
 * 登録中のanimationを借用参照で返す。無効handleはnull。
 */
FModelAnimationAsset* FindAnimation(ModelAnimationHandle handle);
/**
 * 指定handleの登録参照を解放する。無効handleはfalse。
 */
bool DeleteAnimation(ModelAnimationHandle handle, String& error);
/**
 * 登録中のanimation handleをすべて無効化し、参照を解放する。
 */
void ClearAnimations();
}

#endif
