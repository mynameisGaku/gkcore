// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_MODEL_TANGENTS_H
#define GKCORE_MODEL_MODEL_TANGENTS_H

#include "../resources/Resources.h"

/**
 * model用の接線生成処理をまとめる。
 */
namespace gk::model
{

/**
 * 接線を持たないprimitiveから接線を生成し、異なる接線frameの頂点を分割する。
 * 入力は変更せず、入力不正・上限超過・割当失敗時は出力配列を維持する。
 */
bool GenerateModelTangents(const Array<detail::ModelVertex>& sourceVertices, const Array<uint32_t>& sourceIndices, uint32_t vertexLimit, Array<detail::ModelVertex>& outputVertices, Array<uint32_t>& outputIndices, String& error);

}

#endif
