// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_WORLDGEOMETRY_H
#define GKCORE_RENDER_WORLDGEOMETRY_H

#include "render/Geometry.h"

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
    // 環境遮蔽画像に使う独立した座標。
    float occlusionUv[2]{};
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
    // clipping後の環境遮蔽画像座標。
    float occlusionUv[2]{};
};

/**
 * 一つのmodel描画で共有する変換とcamera基底。
 */
struct FWorldGeometryContext
{
    // model空間をworld空間へ変換するscale。
    double modelScale[3] = { 1.0, 1.0, 1.0 };
    // model空間をworld空間へ移す位置。
    double modelPosition[3]{};
    // Z/Y/X回転の正弦値。
    double rotationSin[3]{};
    // Z/Y/X回転の余弦値。
    double rotationCos[3] = { 1.0, 1.0, 1.0 };
    // cameraのworld位置。
    double cameraPosition[3]{};
    // cameraの右向き単位vector。
    double cameraRight[3]{};
    // cameraの上向き単位vector。
    double cameraUp[3]{};
    // cameraの前向き単位vector。
    double cameraForward[3]{};
    // model位置からview奥行きへ写す線形係数。
    double viewDepthCoefficients[3]{};
    // world位置とcameraから得るview奥行きの定数。
    double viewDepthOffset = 0.0;
    // model scale・回転・位置を適用するかを示す。
    bool applyModelTransform = false;
};

/**
 * draw packetからmodel変換とcamera基底を一度だけ計算する。
 */
bool BuildWorldGeometryContext(const detail::DrawPacket& draw, bool applyModelTransform, FWorldGeometryContext& output, String& error);

/**
 * world triangleを切り詰めて射影し、UVと照明属性を保つ。
 * 不正な入力または数値範囲外ではfalseを返す。
 */
bool ProjectWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw, const WorldVertex points[3], bool applyModelTransform, bool includeLighting, const float* linearColor, ProjectedWorldVertex output[18], uint32_t& outputCount, String& error, bool normalMapping = false);
/**
 * 共有済み変換contextでtriangleをclip・射影し、材質属性を保つ。
 */
bool ProjectWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw, const FWorldGeometryContext& context, const WorldVertex points[3], bool includeLighting, const float* linearColor, ProjectedWorldVertex output[18], uint32_t& outputCount, String& error, bool normalMapping = false);
/**
 * 保持したモデル姿勢とcameraから三角形重心のview奥行きを計算する。
 * 不正index・変換・cameraでは出力値を保ってfalseを返す。
 */
bool ModelTriangleViewDepth(const detail::DrawPacket& draw, uint32_t firstIndex, double& output, String& error);
/**
 * 共有済み変換contextを使いtriangle重心のview奥行きを計算する。
 */
bool ModelTriangleViewDepth(const detail::DrawPacket& draw, const FWorldGeometryContext& context, uint32_t firstIndex, double& output, String& error);

// namespace gk::render
}

#endif
