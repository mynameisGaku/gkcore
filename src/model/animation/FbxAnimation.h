// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FBXANIMATION_H
#define GKCORE_MODEL_ANIMATION_FBXANIMATION_H

#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/FbxGeometrySegment.h"
#include "foundation/Array.h"
#include "resources/Resources.h"
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

#ifdef GKCORE_TESTING
/**
 * ufbx従来評価を使い、skin高速経路の比較用geometryを作る。
 */
bool DeformFbxAnimationWithUfbxForTesting(AModelAnimationSource& source, const animation::FModelPose& pose, detail::ModelResource& output, String& error);
/**
 * 直前のDeform()呼び出しがskin高速経路を完了したかを返す。
 */
bool FbxLastDeformUsedFastPathForTesting();
/**
 * 直前の高速変形について段階時間(ms)と規模を返す。時間配列はscratch,node,cluster,position,normal,scatter,commit順。
 */
void GetLastFbxDeformProfileForTesting(double milliseconds[7], uint32_t counts[6]);
#endif

/**
 * FBX byte列からgeometryを使わないanimation assetを読み込む。
 */
FModelAnimationAsset* LoadFbxAnimation(const uint8_t* bytes, uint32_t size, const char* utf8Path, String& error);
}

#endif
