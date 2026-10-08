// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FBXANIMATION_H
#define GKCORE_MODEL_ANIMATION_FBXANIMATION_H

#include "AModelAnimationSource.h"
#include "FModelAnimationAsset.h"
#include "FbxGeometrySegment.h"
#include "../../foundation/Array.h"
#include "../../resources/Resources.h"
#include <stdint.h>

struct ufbx_scene;

/**
 * 共通モデルanimationへFBX形式を接続する。
 */
namespace gk::model
{
/**
 * 読み込み済みsceneとflatten対応表を消費してFBX animation sourceを作る。
 * 失敗時もsceneを解放する。
 */
AModelAnimationSource* CreateFbxAnimationSource(ufbx_scene* ownedScene, const detail::ModelResource& geometry, const Array<detail::FFbxGeometrySegment>& segments, String& error);

/**
 * FBX byte列からgeometryを使わないanimation assetを読み込む。
 */
FModelAnimationAsset* LoadFbxAnimation(const uint8_t* bytes, uint32_t size, const char* utf8Path, String& error);
}

#endif
