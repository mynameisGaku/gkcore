// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_FBXGEOMETRY_H
#define GKCORE_MODEL_FBXGEOMETRY_H

#include "Model.h"
#include "FbxMaterial.h"
#include "animation/FbxGeometrySegment.h"
#include "../../third_party/ufbx/ufbx.h"
#include "../foundation/String.h"
#include <stdint.h>

/**
 * FBXメッシュ1件を読み込み順にモデル形状へ変換する。
 */
namespace gk::detail
{

/**
 * nodeの変換を適用し、メッシュをモデルへ追加する。
 *
 * 面の順序を保ち、同じ材質が続く範囲をまとめる。
 * UV座標を画像resourceの左上原点へ変換する。
 * 任意の出力値へ退化三角形を含む面数を記録する。
 */
bool AppendFbxNodeGeometry(const ufbx_node* node, const char* utf8ModelPath, ModelResource& model, FbxMaterialContext& materialContext, uint32_t* degenerateFaceCount, FFbxGeometrySegment* outputSegment, String& error);

/**
 * animation対応表を作らず、テスト用にFBXメッシュを追加する。
 */
bool AppendFbxNodeGeometry(const ufbx_node* node, const char* utf8ModelPath, ModelResource& model, FbxMaterialContext& materialContext, uint32_t* degenerateFaceCount, String& error);

} // namespace gk::detail

#endif
