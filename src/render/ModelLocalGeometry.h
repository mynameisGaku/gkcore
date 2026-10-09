// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_MODELLOCALGEOMETRY_H
#define GKCORE_RENDER_MODELLOCALGEOMETRY_H

#include "render/ModelGeometry.h"

/**
 * cameraやinstance transformを含まないmodel-local頂点展開。
 */
namespace gk::render
{

/**
 * primitive全体をlocal空間の照明頂点へ変換し、成功時だけ出力へ追加する。
 * 法線や接線が安全に扱えない場合はfalseと理由を返し、出力を保つ。
 */
bool AppendLocalModelPart(const detail::ModelResource& model, const ModelPartPlan& part, Array<ModelRenderVertex>& vertices, uint32_t vertexLimit, String& error);

// namespace gk::render
}

#endif
