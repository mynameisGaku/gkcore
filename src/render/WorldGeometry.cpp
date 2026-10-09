#include "render/WorldGeometry.h"
#include "model/animation/FModelDeferredPose.h"
#include "model/animation/FModelAnimationAsset.h"
#include "model/animation/AModelAnimationSource.h"
#include "render/PostProcess.h"
#include <float.h>
#include <math.h>

/**
 * world空間のsurfaceと照明属性を切り詰めて射影する。
 */
namespace gk::render
{
/**
 * world triangle用のcamera、変換、clipping、照明補助処理。
 */
namespace
{
/**
 * 射影前のcamera空間位置と基本色UV。
 */
struct ViewPoint
{
    // camera空間の横位置。
    double x;
    // camera空間の縦位置。
    double y;
    // cameraからの奥行き。
    double z;
    // 基本色画像の横座標。
    double u;
    // 基本色画像の縦座標。
    double v;
};

/**
 * clipping中に線形補間する照明属性付き頂点。
 */
struct ClipVertex
{
    // camera空間位置と基本色UV。
    ViewPoint view;
    // 照明計算に使う法線。
    float normal[3];
    // 頂点からcameraへ向かう未正規化方向。
    double viewDirection[3];
    // 基本色とは独立して切り詰める金属度・粗さの画像座標。
    double metallicRoughnessUv[2]{};
    // 法線画像の座標。
    double normalUv[2]{};
    // 自己発光画像の座標。
    double emissiveUv[2]{};
    // 環境遮蔽画像の座標。
    double occlusionUv[2]{};
    // clipping中に補間するワールド空間の接線とhandedness。
    double worldTangent[4]{};
};

/**
 * camera奥行きplaneと交差する辺の頂点属性を補間する。
 */
ClipVertex IntersectClipEdge(const ClipVertex& a, const ClipVertex& b, double planeZ)
{
    // 辺上で交点が占める補間比率。
    const double t = (planeZ - a.view.z) / (b.view.z - a.view.z);
    // 交点位置と補間後属性。
    ClipVertex vertex{};
    vertex.view = { a.view.x + (b.view.x - a.view.x) * t, a.view.y + (b.view.y - a.view.y) * t, planeZ, a.view.u + (b.view.u - a.view.u) * t, a.view.v + (b.view.v - a.view.v) * t };
    // 法線とview方向の各軸を補間するloop。
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        vertex.normal[axis] = static_cast<float>(a.normal[axis] + (b.normal[axis] - a.normal[axis]) * t);
        vertex.viewDirection[axis] = a.viewDirection[axis] + (b.viewDirection[axis] - a.viewDirection[axis]) * t;
    }
    // 位置の交点と同じ比率で第2の画像座標も保持する。
    // 金属度・粗さ画像の2座標を補間するloop。
    for (uint32_t coordinate = 0; coordinate < 2; ++coordinate)
    {
        vertex.metallicRoughnessUv[coordinate] = a.metallicRoughnessUv[coordinate] + (b.metallicRoughnessUv[coordinate] - a.metallicRoughnessUv[coordinate]) * t;
        vertex.normalUv[coordinate] = a.normalUv[coordinate] + (b.normalUv[coordinate] - a.normalUv[coordinate]) * t;
        vertex.emissiveUv[coordinate] = a.emissiveUv[coordinate] + (b.emissiveUv[coordinate] - a.emissiveUv[coordinate]) * t;
        vertex.occlusionUv[coordinate] = a.occlusionUv[coordinate] + (b.occlusionUv[coordinate] - a.occlusionUv[coordinate]) * t;
    }
    // 法線画像用の接線基底も同じ交点比率で補間する。
    for (uint32_t component = 0; component < 4; ++component)
        vertex.worldTangent[component] = a.worldTangent[component] + (b.worldTangent[component] - a.worldTangent[component]) * t;
    return vertex;
}

/**
 * 頂点属性を保ったままpolygonを奥行きplaneで切り詰める。
 */
bool ClipLightingPlane(const ClipVertex* input, uint32_t inputCount, double planeZ, bool keepGreater, ClipVertex* output, uint32_t& outputCount)
{
    // 出力polygonに現在追加済みの頂点数。
    outputCount = 0;
    // 入力polygonの頂点を順番に調べるloop。
    for (uint32_t i = 0; i < inputCount; ++i)
    {
        // polygon上で現在の頂点へ入る辺の始点。
        const ClipVertex& a = input[(i + inputCount - 1) % inputCount];
        // polygon上で処理中の頂点。
        const ClipVertex& b = input[i];
        // 始点と終点がplaneの残す側にあるか。
        const bool insideA = keepGreater ? a.view.z >= planeZ : a.view.z <= planeZ;
        // 辺の終点がplaneの残す側にあるか。
        const bool insideB = keepGreater ? b.view.z >= planeZ : b.view.z <= planeZ;
        if (insideA && insideB)
        {
            if (outputCount >= 8)
                return false;
            output[outputCount++] = b;
        }
        else if (insideA && !insideB)
        {
            if (outputCount >= 8)
                return false;
            output[outputCount++] = IntersectClipEdge(a, b, planeZ);
        }
        else if (!insideA && insideB)
        {
            if (outputCount > 6)
                return false;
            output[outputCount++] = IntersectClipEdge(a, b, planeZ);
            output[outputCount++] = b;
        }
    }
    return outputCount <= 8;
}
/**
 * packed sRGB色をlinear-light頂点色へ変換して保存する。
 */
void StoreColor(uint32_t packed, float* color)
{
    color[0] = SrgbToLinear(static_cast<float>((packed >> 16) & 255u) / 255.0f);
    color[1] = SrgbToLinear(static_cast<float>((packed >> 8) & 255u) / 255.0f);
    color[2] = SrgbToLinear(static_cast<float>(packed & 255u) / 255.0f);
    color[3] = 1.0f;
}

/**
 * GPU頂点形式で表現できる有限値だけfloatへ保存する。
 */
bool StoreFloat(double value, float& output, String& error)
{
    if (!isfinite(value) || fabs(value) > FLT_MAX)
    {
        error.Assign("The draw coordinates exceed the renderer's numeric range");
        return false;
    }
    output = static_cast<float>(value);
    return true;
}

/**
 * frameに記録したmodel変換を適用し、world位置をdouble精度で返す。
 */
bool TransformToWorld(Vec3 source, const FWorldGeometryContext& context, double output[3], String& error)
{
    output[0] = source.x;
    output[1] = source.y;
    output[2] = source.z;
    if (context.applyModelTransform)
    {
        // scale適用後のmodel位置。
        const double x = output[0] * context.modelScale[0], y = output[1] * context.modelScale[1], z = output[2] * context.modelScale[2];
        // Z回転適用後の横位置。
        const double x1 = x * context.rotationCos[2] - y * context.rotationSin[2];
        // Z回転適用後の縦位置。
        const double y1 = x * context.rotationSin[2] + y * context.rotationCos[2];
        // Y回転適用後の横位置。
        const double x2 = x1 * context.rotationCos[1] + z * context.rotationSin[1];
        // Y回転適用後の奥行き。
        const double z2 = -x1 * context.rotationSin[1] + z * context.rotationCos[1];
        output[0] = x2 + context.modelPosition[0];
        output[1] = y1 * context.rotationCos[0] - z2 * context.rotationSin[0] + context.modelPosition[1];
        output[2] = y1 * context.rotationSin[0] + z2 * context.rotationCos[0] + context.modelPosition[2];
    }
    if (!isfinite(output[0]) || !isfinite(output[1]) || !isfinite(output[2]))
    {
        error.Assign("The model world position is outside the renderer's numeric range");
        return false;
    }
    return true;
}

/**
 * 最大成分で縮尺を整えてoverflowを避けながらvectorを正規化する。
 */
bool NormalizeVector(const double source[3], float output[3])
{
    // 成分を安全に縮尺するための最大絶対値。
    const double largest = fmax(fmax(fabs(source[0]), fabs(source[1])), fabs(source[2]));
    if (!(largest > 0.0) || !isfinite(largest))
    {
        output[0] = output[1] = output[2] = 0.0f;
        return false;
    }
    // 最大成分で縮尺した各軸成分。
    const double x = source[0] / largest;
    // 縮尺後の残り2軸成分。
    const double y = source[1] / largest;
    // 縮尺後の奥行き成分。
    const double z = source[2] / largest;
    // 縮尺後vectorの長さ。
    const double length = sqrt(x * x + y * y + z * z);
    output[0] = static_cast<float>(x / length);
    output[1] = static_cast<float>(y / length);
    output[2] = static_cast<float>(z / length);
    return true;
}

/**
 * 法線へ逆scaleとZ/Y/X回転を適用して正規化する。
 */
bool TransformNormal(Vec3 source, const FWorldGeometryContext& context, float output[3], String& error)
{
    // model変換を適用する前の法線成分。
    double x = source.x;
    // 法線の残り2軸成分。
    double y = source.y;
    // 法線の奥行き成分。
    double z = source.z;
    if (context.applyModelTransform)
    {
        // 法線に適用するmodel scale。
        const double sx = context.modelScale[0], sy = context.modelScale[1], sz = context.modelScale[2];
        if (sx == 0.0 || sy == 0.0 || sz == 0.0 || !isfinite(sx) || !isfinite(sy) || !isfinite(sz))
        {
            error.Assign("The model scale cannot transform lighting normals");
            return false;
        }
        x /= sx;
        y /= sy;
        z /= sz;
        // Z回転後の法線成分。
        const double x1 = x * context.rotationCos[2] - y * context.rotationSin[2];
        // Z回転後の縦成分とY回転後の横・奥行き成分。
        const double y1 = x * context.rotationSin[2] + y * context.rotationCos[2];
        // Y回転後の横位置と奥行き。
        const double x2 = x1 * context.rotationCos[1] + z * context.rotationSin[1];
        // Y回転後の奥行き。
        const double z2 = -x1 * context.rotationSin[1] + z * context.rotationCos[1];
        x = x2;
        y = y1 * context.rotationCos[0] - z2 * context.rotationSin[0];
        z = y1 * context.rotationSin[0] + z2 * context.rotationCos[0];
    }
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
    {
        error.Assign("The transformed model normal is outside the renderer's numeric range");
        return false;
    }
    // model変換後の法線。
    const double transformed[3] = { x, y, z };
    NormalizeVector(transformed, output);
    return true;
}

/**
 * 変換済みworld位置から決定的な単位面法線を作る。
 */
void BuildFaceFallback(const double worldPositions[3][3], float output[3])
{
    // 三角形の始点から第2頂点へ向かう辺。
    const double ax = worldPositions[1][0] - worldPositions[0][0];
    // 第2頂点への辺の残り2軸と、第3頂点への辺。
    const double ay = worldPositions[1][1] - worldPositions[0][1];
    // 第2頂点への辺の奥行き成分。
    const double az = worldPositions[1][2] - worldPositions[0][2];
    // 三角形の始点から第3頂点へ向かう辺。
    const double bx = worldPositions[2][0] - worldPositions[0][0];
    // 第3頂点への辺の残り2軸成分。
    const double by = worldPositions[2][1] - worldPositions[0][1];
    // 第3頂点への辺の奥行き成分。
    const double bz = worldPositions[2][2] - worldPositions[0][2];
    // 2辺の外積で得る面法線。
    const double face[3] = { ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx };
    if (!NormalizeVector(face, output))
    {
        output[0] = 0.0f;
        output[1] = 1.0f;
        output[2] = 0.0f;
    }
}

/**
 * clippingに使うworld法線とcameraから頂点への方向を準備する。
 */
bool TransformTangent(const WorldVertex& source, const float normal[3], const FWorldGeometryContext& context, double output[4], String& error)
{
    // モデル空間の接線とbitangent向きを決める値。
    const double tangentX = source.tangent[0];
    const double tangentY = source.tangent[1];
    const double tangentZ = source.tangent[2];
    const double handedness = source.tangent[3];
    if (!isfinite(tangentX) || !isfinite(tangentY) || !isfinite(tangentZ) || (handedness != 1.0 && handedness != -1.0))
    {
        error.Assign("The model contains an invalid normal-map tangent");
        return false;
    }
    // モデル空間で法線と接線が平行でないことを確かめる。
    const double sourceNormal[3] = { source.normal.x, source.normal.y, source.normal.z };
    const double sourceTangent[3] = { tangentX, tangentY, tangentZ };
    float unitSourceNormal[3]{};
    float unitSourceTangent[3]{};
    if (!NormalizeVector(sourceNormal, unitSourceNormal) || !NormalizeVector(sourceTangent, unitSourceTangent))
    {
        error.Assign("The model normal-map basis contains a zero vector");
        return false;
    }
    // 単位化した2 vectorの外積で平行・反平行を検出する。
    const double cross[3] = { static_cast<double>(unitSourceNormal[1]) * unitSourceTangent[2] - static_cast<double>(unitSourceNormal[2]) * unitSourceTangent[1], static_cast<double>(unitSourceNormal[2]) * unitSourceTangent[0] - static_cast<double>(unitSourceNormal[0]) * unitSourceTangent[2], static_cast<double>(unitSourceNormal[0]) * unitSourceTangent[1] - static_cast<double>(unitSourceNormal[1]) * unitSourceTangent[0] };
    const double crossLengthSquared = cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2];
    if (!(crossLengthSquared > 1e-12))
    {
        error.Assign("The model tangent is parallel to its source normal");
        return false;
    }

    // 接線へ位置と同じscale・Z/Y/X回転を適用する成分。
    double x = tangentX;
    double y = tangentY;
    double z = tangentZ;
    double determinantSign = 1.0;
    if (context.applyModelTransform)
    {
        // 位置変換と共有するモデルscale。
        const double sx = context.modelScale[0], sy = context.modelScale[1], sz = context.modelScale[2];
        if (!isfinite(sx) || !isfinite(sy) || !isfinite(sz) || sx == 0.0 || sy == 0.0 || sz == 0.0)
        {
            error.Assign("The model transform cannot transform a normal-map tangent");
            return false;
        }
        x *= sx;
        y *= sy;
        z *= sz;
        determinantSign = sx * sy * sz < 0.0 ? -1.0 : 1.0;
        // Z回転後の接線成分。
        const double x1 = x * context.rotationCos[2] - y * context.rotationSin[2];
        // Z回転後の縦成分とY回転後の横・奥行き成分。
        const double y1 = x * context.rotationSin[2] + y * context.rotationCos[2];
        // Y回転後の横位置と奥行き。
        const double x2 = x1 * context.rotationCos[1] + z * context.rotationSin[1];
        // Y回転後の奥行き。
        const double z2 = -x1 * context.rotationSin[1] + z * context.rotationCos[1];
        x = x2;
        y = y1 * context.rotationCos[0] - z2 * context.rotationSin[0];
        z = y1 * context.rotationSin[0] + z2 * context.rotationCos[0];
    }
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
    {
        error.Assign("The transformed normal-map tangent is outside the renderer's numeric range");
        return false;
    }

    // 逆転置で変換し正規化した法線へGram-Schmidt直交化する。
    const double dot = x * normal[0] + y * normal[1] + z * normal[2];
    const double orthogonal[3] = { x - dot * normal[0], y - dot * normal[1], z - dot * normal[2] };
    float unitTangent[3]{};
    if (!NormalizeVector(orthogonal, unitTangent))
    {
        error.Assign("The model tangent is zero or parallel to its normal");
        return false;
    }
    output[0] = unitTangent[0];
    output[1] = unitTangent[1];
    output[2] = unitTangent[2];
    output[3] = handedness * determinantSign;
    return true;
}

/**
 * clippingに使うworld法線とcameraから頂点への方向を準備する。
 */
bool PrepareLighting(const WorldVertex points[3], const double worldPositions[3][3], const FWorldGeometryContext& context, bool normalMapping, ClipVertex output[3], String& error)
{
    // 入力法線が欠けて面法線で補う必要があるか。
    bool hasMissingNormal = false;
    // view方向計算に使うcamera位置。
    // 入力triangleの各頂点を準備するloop。
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (!TransformNormal(points[i].normal, context, output[i].normal, error))
            return false;
        if (normalMapping && i > 0 && points[i].tangent[3] != points[0].tangent[3])
        {
            error.Assign("The model triangle has inconsistent normal-map tangent handedness");
            return false;
        }
        if (output[i].normal[0] == 0.0f && output[i].normal[1] == 0.0f && output[i].normal[2] == 0.0f)
        {
            if (normalMapping)
            {
                error.Assign("A normal map requires a nonzero vertex normal");
                return false;
            }
            hasMissingNormal = true;
        }
        if (normalMapping && !TransformTangent(points[i], output[i].normal, context, output[i].worldTangent, error))
            return false;
        // cameraから頂点への方向を各軸で求めるloop。
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            output[i].viewDirection[axis] = context.cameraPosition[axis] - worldPositions[i][axis];
            if (!isfinite(output[i].viewDirection[axis]))
            {
                error.Assign("The model view direction is outside the renderer's numeric range");
                return false;
            }
        }
    }
    if (hasMissingNormal)
    {
        // 欠損法線へ使う面法線。
        float fallback[3]{};
        BuildFaceFallback(worldPositions, fallback);
        // 欠損法線だけを面法線で置き換えるloop。
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (output[i].normal[0] == 0.0f && output[i].normal[1] == 0.0f && output[i].normal[2] == 0.0f)
            {
                // 面法線の各成分を頂点へ複写するloop。
                for (uint32_t axis = 0; axis < 3; ++axis)
                    output[i].normal[axis] = fallback[axis];
            }
        }
    }
    return true;
}

/**
 * modelとcameraの変換を適用し、入力UVを変更せずview位置へ保存する。
 */
bool TransformToView(const double world[3], const float sourceUv[2], const FWorldGeometryContext& context, ViewPoint& output, String& error)
{
    // camera位置からworld位置への差分。
    const double dx = world[0] - context.cameraPosition[0];
    // 差分の残り2軸成分。
    const double dy = world[1] - context.cameraPosition[1];
    // 差分の奥行き成分。
    const double dz = world[2] - context.cameraPosition[2];
    output.x = dx * context.cameraRight[0] + dy * context.cameraRight[1] + dz * context.cameraRight[2];
    output.y = dx * context.cameraUp[0] + dy * context.cameraUp[1] + dz * context.cameraUp[2];
    output.z = dx * context.cameraForward[0] + dy * context.cameraForward[1] + dz * context.cameraForward[2];
    output.u = sourceUv[0];
    output.v = sourceUv[1];
    if (!isfinite(output.x) || !isfinite(output.y) || !isfinite(output.z))
    {
        error.Assign("The draw coordinates exceed the renderer's numeric range");
        return false;
    }
    return true;
}

/**
 * view位置とUVをrendererの頂点形式へ射影する。
 */
bool ProjectView(const ViewPoint& point, uint32_t width, uint32_t height, uint32_t packedColor, const float* linearColor, Vertex& output, String& error)
{
    // frameの縦横比。
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    // 60度視野角に対応する焦点係数。
    const double focal = 1.7320508075688772;
    // 射影計算に使うnear/far plane。
    constexpr double nearPlane = 0.1;
    constexpr double farPlane = 1000.0;
    // 射影後のclip位置。
    const double clipX = point.x * focal / aspect;
    // clip位置の残り2成分。
    const double clipY = point.y * focal;
    // 奥行きのclip値。
    const double clipZ = (farPlane / (farPlane - nearPlane)) * point.z - (farPlane * nearPlane / (farPlane - nearPlane));
    if (!StoreFloat(clipX, output.position[0], error) || !StoreFloat(clipY, output.position[1], error) || !StoreFloat(clipZ, output.position[2], error) || !StoreFloat(point.z, output.position[3], error) || !StoreFloat(point.u, output.uv[0], error) || !StoreFloat(point.v, output.uv[1], error))
        return false;
    if (linearColor)
    {
        // 線形RGBAの各成分を頂点色へ複写するloop。
        for (uint32_t component = 0; component < 4; ++component)
            output.color[component] = linearColor[component];
    }
    else
    {
        StoreColor(packedColor, output.color);
    }
    return true;
}

// namespace
}

bool BuildWorldGeometryContext(const detail::DrawPacket& draw, bool applyModelTransform, FWorldGeometryContext& output, String& error)
{
    FWorldGeometryContext candidate{};
    candidate.applyModelTransform = applyModelTransform;
    candidate.cameraPosition[0] = draw.cameraPosition.x;
    candidate.cameraPosition[1] = draw.cameraPosition.y;
    candidate.cameraPosition[2] = draw.cameraPosition.z;
    // cameraが向く方向と長さを一度だけ求める。
    const double fx = static_cast<double>(draw.cameraTarget.x) - draw.cameraPosition.x;
    const double fy = static_cast<double>(draw.cameraTarget.y) - draw.cameraPosition.y;
    const double fz = static_cast<double>(draw.cameraTarget.z) - draw.cameraPosition.z;
    const double forwardLength = sqrt(fx * fx + fy * fy + fz * fz);
    if (!(forwardLength > 1e-12) || !isfinite(forwardLength) || !isfinite(candidate.cameraPosition[0]) || !isfinite(candidate.cameraPosition[1]) || !isfinite(candidate.cameraPosition[2]))
    {
        error.Assign("The camera direction is outside the renderer's numeric range");
        return false;
    }
    candidate.cameraForward[0] = fx / forwardLength;
    candidate.cameraForward[1] = fy / forwardLength;
    candidate.cameraForward[2] = fz / forwardLength;
    candidate.cameraRight[0] = candidate.cameraForward[2];
    candidate.cameraRight[1] = 0.0;
    candidate.cameraRight[2] = -candidate.cameraForward[0];
    const double rightLength = sqrt(candidate.cameraRight[0] * candidate.cameraRight[0] + candidate.cameraRight[2] * candidate.cameraRight[2]);
    if (!(rightLength > 1e-12))
    {
        candidate.cameraRight[0] = 1.0;
        candidate.cameraRight[2] = 0.0;
    }
    else
    {
        candidate.cameraRight[0] /= rightLength;
        candidate.cameraRight[2] /= rightLength;
    }
    candidate.cameraUp[0] = candidate.cameraForward[1] * candidate.cameraRight[2] - candidate.cameraForward[2] * candidate.cameraRight[1];
    candidate.cameraUp[1] = candidate.cameraForward[2] * candidate.cameraRight[0] - candidate.cameraForward[0] * candidate.cameraRight[2];
    candidate.cameraUp[2] = candidate.cameraForward[0] * candidate.cameraRight[1] - candidate.cameraForward[1] * candidate.cameraRight[0];
    if (applyModelTransform)
    {
        // modelのscale、位置、回転を描画間で共有する。
        const double rotation[3] = { draw.modelRotation.x, draw.modelRotation.y, draw.modelRotation.z };
        candidate.modelScale[0] = draw.modelScale.x;
        candidate.modelScale[1] = draw.modelScale.y;
        candidate.modelScale[2] = draw.modelScale.z;
        candidate.modelPosition[0] = draw.modelPosition.x;
        candidate.modelPosition[1] = draw.modelPosition.y;
        candidate.modelPosition[2] = draw.modelPosition.z;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(rotation[axis]) || !isfinite(candidate.modelScale[axis]) || !isfinite(candidate.modelPosition[axis]))
            {
                error.Assign("The model transform is outside the renderer's numeric range");
                return false;
            }
            candidate.rotationSin[axis] = sin(rotation[axis]);
            candidate.rotationCos[axis] = cos(rotation[axis]);
        }
    }
    double worldOrigin[3]{};
    if (!TransformToWorld(Vec3{}, candidate, worldOrigin, error))
        return false;
    candidate.viewDepthOffset = 0.0;
    for (uint32_t axis = 0; axis < 3; ++axis)
        candidate.viewDepthOffset += (worldOrigin[axis] - candidate.cameraPosition[axis]) * candidate.cameraForward[axis];
    const Vec3 basis[3] = { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 1.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f } };
    for (uint32_t inputAxis = 0; inputAxis < 3; ++inputAxis)
    {
        double worldBasis[3]{};
        if (!TransformToWorld(basis[inputAxis], candidate, worldBasis, error))
            return false;
        for (uint32_t worldAxis = 0; worldAxis < 3; ++worldAxis)
            candidate.viewDepthCoefficients[inputAxis] += (worldBasis[worldAxis] - worldOrigin[worldAxis]) * candidate.cameraForward[worldAxis];
    }
    output = candidate;
    error.Clear();
    return true;
}

/**
 * UVと任意の照明属性を補間しながらtriangleを切り詰めて射影する。
 */
bool ModelTriangleViewDepth(const detail::DrawPacket& draw, uint32_t firstIndex, double& output, String& error)
{
    FWorldGeometryContext context{};
    if (!BuildWorldGeometryContext(draw, true, context, error))
        return false;
    return ModelTriangleViewDepth(draw, context, firstIndex, output, error);
}

bool ModelTriangleViewDepth(const detail::DrawPacket& draw, const FWorldGeometryContext& context, uint32_t firstIndex, double& output, String& error)
{
    if (draw.kind != detail::DrawKind::Model || !draw.model || firstIndex > draw.model->indices.Count() || draw.model->indices.Count() - firstIndex < 3)
    {
        error.Assign("transparent triangle index range is invalid");
        return false;
    }
    // 線形なcamera奥行きを各triangle頂点へ適用する。
    double depth = 0.0;
    for (uint32_t corner = 0; corner < 3; ++corner)
    {
        const uint32_t index = draw.model->indices.At(firstIndex + corner);
        if (index >= draw.model->vertices.Count())
        {
            error.Assign("transparent triangle vertex index is invalid");
            return false;
        }
        const auto& vertex = draw.model->vertices.At(index);
        Vec3 position{ vertex.position[0], vertex.position[1], vertex.position[2] };
        if (draw.deferredPose)
        {
            const auto* source = draw.deferredPose->source;
            const auto* map = source && source->animation ? source->animation->source->SparseVertexMap() : nullptr;
            if (!map || index >= map->Count() || map->At(index).positionIndex >= draw.deferredPose->geometry.positions.Count())
            {
                error.Assign("deferred transparent triangle position is invalid");
                return false;
            }
            const auto& posed = draw.deferredPose->geometry.positions.At(map->At(index).positionIndex);
            position = { posed.value[0], posed.value[1], posed.value[2] };
        }
        const double vertexDepth = static_cast<double>(position.x) * context.viewDepthCoefficients[0] + static_cast<double>(position.y) * context.viewDepthCoefficients[1] + static_cast<double>(position.z) * context.viewDepthCoefficients[2] + context.viewDepthOffset;
        if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z) || !isfinite(vertexDepth))
        {
            error.Assign("transparent triangle position is outside the renderer's numeric range");
            return false;
        }
        depth += vertexDepth / 3.0;
    }
    if (!isfinite(depth))
        return false;
    output = depth;
    error.Clear();
    return true;
}

/**
 * world triangleを切り詰めて投影し、材質属性を保持する。
 */
bool ProjectWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw, const WorldVertex points[3], bool applyModelTransform, bool includeLighting, const float* linearColor, ProjectedWorldVertex output[18], uint32_t& outputCount, String& error, bool normalMapping)
{
    FWorldGeometryContext context{};
    if (!BuildWorldGeometryContext(draw, applyModelTransform, context, error))
    {
        outputCount = 0;
        return false;
    }
    return ProjectWorldTriangle(frame, draw, context, points, includeLighting, linearColor, output, outputCount, error, normalMapping);
}

bool ProjectWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw, const FWorldGeometryContext& context, const WorldVertex points[3], bool includeLighting, const float* linearColor, ProjectedWorldVertex output[18], uint32_t& outputCount, String& error, bool normalMapping)
{
    // camera空間へ変換した入力頂点を保持する領域。
    ClipVertex first[3]{};
    outputCount = 0;
    if (!output || !points || frame.width == 0 || frame.height == 0)
    {
        error.Assign("The world triangle or frame dimensions are invalid");
        return false;
    }
    // cameraと照明計算で共用する変換済みworld位置。
    double worldPositions[3][3];
    // 入力triangleの各頂点を検査して変換するloop。
    for (uint32_t i = 0; i < 3; ++i)
    {
        // 処理中の入力頂点属性。
        const WorldVertex& source = points[i];
        // 基本色画像用UVの一時参照。
        const float uv[2] = { source.uv[0], source.uv[1] };
        if (!isfinite(source.position.x) || !isfinite(source.position.y) || !isfinite(source.position.z) || !isfinite(uv[0]) || !isfinite(uv[1]))
        {
            error.Assign("The model contains non-finite position or texture coordinates");
            return false;
        }
        if (includeLighting && (!isfinite(source.normal.x) || !isfinite(source.normal.y) || !isfinite(source.normal.z)))
        {
            error.Assign("The model contains a non-finite normal");
            return false;
        }
        if (!TransformToWorld(source.position, context, worldPositions[i], error))
            return false;
        if (!TransformToView(worldPositions[i], uv, context, first[i].view, error))
            return false;
        // 材質画像のUVも有限値に限り、交点計算まで同じ頂点へ保持する。
        // 追加UVを有限性検査してclipping属性へ複写するloop。
        for (uint32_t coordinate = 0; coordinate < 2; ++coordinate)
        {
            if (!isfinite(source.metallicRoughnessUv[coordinate]))
            {
                error.Assign("The model contains non-finite metallic-roughness texture coordinates");
                return false;
            }
            first[i].metallicRoughnessUv[coordinate] = source.metallicRoughnessUv[coordinate];
            if (normalMapping && !isfinite(source.normalUv[coordinate]))
            {
                error.Assign("The model contains non-finite normal texture coordinates");
                return false;
            }
            if (normalMapping)
                first[i].normalUv[coordinate] = source.normalUv[coordinate];
            if (!isfinite(source.emissiveUv[coordinate]))
            {
                error.Assign("The model contains non-finite emissive texture coordinates");
                return false;
            }
            first[i].emissiveUv[coordinate] = source.emissiveUv[coordinate];
            if (!isfinite(source.occlusionUv[coordinate]))
            {
                error.Assign("The model contains non-finite occlusion texture coordinates");
                return false;
            }
            first[i].occlusionUv[coordinate] = source.occlusionUv[coordinate];
        }
    }

    if (includeLighting && !PrepareLighting(points, worldPositions, context, normalMapping, first, error))
        return false;

    // 位置と基本色UVに使う交点で全属性を同じpolygon clippingへ通す。
    // 1回目と2回目のclipping結果を交互に保持する領域。
    ClipVertex polygonA[8];
    polygonA[0] = first[0];
    polygonA[1] = first[1];
    polygonA[2] = first[2];
    // 1段目clipping結果を一時保持する領域。
    ClipVertex polygonB[8];
    // 現在polygonに含まれる頂点数。
    uint32_t polygonCount = 3;
    // 1段目clippingの出力頂点数。
    uint32_t clippedCount = 0;
    if (!ClipLightingPlane(polygonA, polygonCount, 0.1, true, polygonB, clippedCount) || !ClipLightingPlane(polygonB, clippedCount, 1000.0, false, polygonA, polygonCount))
    {
        error.Assign("The clipped triangle exceeds the polygon scratch capacity");
        return false;
    }
    if (polygonCount < 3)
        return true;
    // fan分割後に出力する三角形頂点数。
    const uint32_t vertexCount = (polygonCount - 2) * 3;
    // 射影したpolygon頂点を一時保持する領域。
    ProjectedWorldVertex projectedPolygon[8];
    // clipping後polygonを射影済み頂点へ変換するloop。
    for (uint32_t i = 0; i < polygonCount; ++i)
    {
        if (!ProjectView(polygonA[i].view, frame.width, frame.height, draw.color, linearColor, projectedPolygon[i].surface, error))
            return false;
        // 切り詰めた第2のUVを、位置と同じ射影に使う頂点へ渡す。
        // 追加UVをGPU頂点形式へ変換するloop。
        for (uint32_t coordinate = 0; coordinate < 2; ++coordinate)
        {
            if (!StoreFloat(polygonA[i].metallicRoughnessUv[coordinate], projectedPolygon[i].metallicRoughnessUv[coordinate], error))
            {
                return false;
            }
            if (normalMapping && !StoreFloat(polygonA[i].normalUv[coordinate], projectedPolygon[i].normalUv[coordinate], error))
                return false;
            if (!StoreFloat(polygonA[i].emissiveUv[coordinate], projectedPolygon[i].emissiveUv[coordinate], error))
                return false;
            if (!StoreFloat(polygonA[i].occlusionUv[coordinate], projectedPolygon[i].occlusionUv[coordinate], error))
                return false;
        }
        if (normalMapping)
        {
            // 接線のxyzとhandednessをGPU頂点へ渡すloop。
            for (uint32_t component = 0; component < 4; ++component)
            {
                if (!StoreFloat(polygonA[i].worldTangent[component], projectedPolygon[i].worldTangent[component], error))
                    return false;
            }
        }
        // 法線とview方向を出力属性へ複写するloop。
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            projectedPolygon[i].worldNormal[axis] = polygonA[i].normal[axis];
            if (!StoreFloat(polygonA[i].viewDirection[axis], projectedPolygon[i].viewDirection[axis], error))
                return false;
        }
    }
    // 三角形化した頂点の出力位置。
    uint32_t outputIndex = 0;
    // polygonを先頭頂点からtriangle fanへ分割するloop。
    for (uint32_t i = 1; i + 1 < polygonCount; ++i)
    {
        output[outputIndex++] = projectedPolygon[0];
        output[outputIndex++] = projectedPolygon[i];
        output[outputIndex++] = projectedPolygon[i + 1];
    }
    outputCount = vertexCount;
    return true;
}

// namespace gk::render
}
