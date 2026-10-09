// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_MODELPOSEGEOMETRY_H
#define GKCORE_RENDER_MODELPOSEGEOMETRY_H

#include "render/FModelPoseVertex.h"
#include "render/ModelDrawPlan.h"

/**
 * 描画時点のmodel姿勢をGPU用頂点payloadへ変換する処理。
 */
namespace gk::render
{

/**
 * model頂点を有限値・安全範囲で検証してpose streamへ追加する。失敗時は出力を保つ。
 */
bool AppendModelPoseVertices(const detail::ModelResource& model, Array<FModelPoseVertex>& output, uint32_t vertexLimit, String& error);
/**
 * 法線マップを使うpartの法線・接線basisがGPU経路で有効か調べる。
 */
bool IsModelPosePartGpuSafe(const detail::ModelResource& model, const ModelPartPlan& part);

// namespace gk::render
}

#endif
