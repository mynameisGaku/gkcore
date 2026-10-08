// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_WORLDGEOMETRY_H
#define GKCORE_RENDER_WORLDGEOMETRY_H

#include "Geometry.h"

/**
 * model変換とcamera clippingの間で保持するworld属性。
 */
namespace gk::render
{

/**
 * modelとcameraで変換する前のtriangle頂点属性。
 */
struct WorldVertex
{
    // world変換前の位置。
    Vec3 position;
    // 照明に使う変換前の法線。
    Vec3 normal;
    // 基本色画像に使うUV。
    float uv[2];
    // 金属度・粗さの画像座標。
    float metallicRoughnessUv[2]{};
    // 法線マップの画像座標。
    float normalUv[2]{};
    // モデル空間の接線と縦方向の符号。
    float tangent[4]{};
    // 自己発光画像を読む独立した座標。
    float emissiveUv[2]{};
};

/**
 * clipping後に射影したsurfaceと照明の頂点属性。
 */
struct ProjectedWorldVertex
{
    // GPUへ渡す射影済みsurface属性。
    Vertex surface;
    // world空間の単位法線。
    float worldNormal[3];
    // cameraから頂点へ向かう方向。
    float viewDirection[3];
    // クリッピング後の金属度・粗さの画像座標。
    float metallicRoughnessUv[2]{};
    // 切り詰めた法線マップの座標。
    float normalUv[2]{};
    // ワールド空間の接線と縦方向の符号。
    float worldTangent[4]{};
    // clipping後の自己発光画像座標。
    float emissiveUv[2]{};
};

/**
 * world triangleを切り詰めて射影し、UVと照明属性を保つ。
 * 不正な入力または数値範囲外ではfalseを返す。
 */
bool ProjectWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw, const WorldVertex points[3], bool applyModelTransform, bool includeLighting, const float* linearColor, ProjectedWorldVertex output[18], uint32_t& outputCount, String& error, bool normalMapping = false);

// namespace gk::render
}

#endif
