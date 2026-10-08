// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_MODEL_NORMALS_H
#define GKCORE_MODEL_MODEL_NORMALS_H

#include "../resources/Resources.h"

/**
 * model primitiveの平面法線生成処理をまとめる。
 */
namespace gk::model
{

/**
 * NORMALがない三角形meshから面法線を作り、鋭い辺で頂点を分ける。
 * 入力は変更せず、入力不正・上限超過・割当失敗時は出力配列を維持する。
 */
bool GenerateModelNormals(const Array<detail::ModelVertex>& sourceVertices, const Array<uint32_t>& sourceIndices, uint32_t vertexLimit, Array<detail::ModelVertex>& outVertices, Array<uint32_t>& outIndices, String& error);

}

#endif
