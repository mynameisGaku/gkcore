// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_MODELGEOMETRY_H
#define GKCORE_RENDER_MODELGEOMETRY_H

#include "WorldGeometry.h"

/**
 * 内蔵PBR pipelineへ渡す静的model頂点の展開。
 */
namespace gk::render
{

/**
 * surface属性、world空間の照明入力、材質係数を持つ描画頂点。
 */
struct ModelRenderVertex
{
    // 位置、頂点色、画像座標。
    Vertex surface;
    // 変換後のworld法線。
    float worldNormal[3];
    // cameraから頂点へ向かう方向量。
    float viewDirection[3];
    // PBR金属度と粗さ。
    float metallicRoughness[2];
    // アルファ抜きの有効値（0または1）と境界値。
    float alphaMaskCutoff[2]{};
    // 金属度・粗さの画像に使う座標。
    float metallicRoughnessUv[2]{};
    // ワールド空間の接線と、縦方向を決める符号。
    float worldTangent[4]{};
    // 法線マップの画像座標。
    float normalUv[2]{};
    // 法線マップの有効値（0または1）と倍率。
    float normalParameters[2]{};
};
static_assert(sizeof(ModelRenderVertex) == 120, "lit model vertex ABI must remain 120 bytes");

/**
 * 検証済み材質範囲を照明用頂点へ展開する。失敗時は出力配列を保つ。
 */
bool AppendLitModelPart(const detail::FramePacket& frame, const detail::DrawPacket& draw, const ModelPartPlan& part, Array<ModelRenderVertex>& vertices, uint32_t vertexLimit, String& error);

// namespace gk::render
}

#endif
