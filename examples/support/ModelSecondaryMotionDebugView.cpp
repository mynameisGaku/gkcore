// SPDX-License-Identifier: NOASSERTION
#include "examples/support/ModelSecondaryMotionDebugView.h"

#include <gkcore/FModelSecondaryMotionDebugShape.h>
#include <gkcore/ModelSecondaryMotion.h>

#include <float.h>
#include <math.h>
#include <string.h>

namespace gk::examples
{
namespace
{

// 一時表示で扱う形状数の上限。
const uint32_t DebugShapeLimit = 4096;
// 円やカプセル輪郭を折れ線で近似する分割数。
const uint32_t CircleSegments = 12;
// 画面上で見える輪郭線の表示座標幅。
const float RibbonHalfWidth = 0.0015f;
// 半径0の接触点を示す交差印の表示座標半幅。
const float MarkerHalfSize = 0.006f;

/**
 * float値が有限か調べる。
 */
bool IsFinite(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

/**
 * 3成分が有限か調べる。
 */
bool IsFinite(gk::Vec3 value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

/**
 * 二点間の差を倍精度で求める。
 */
void Difference(gk::Vec3 first, gk::Vec3 second, double output[3])
{
    output[0] = static_cast<double>(first.x) - second.x;
    output[1] = static_cast<double>(first.y) - second.y;
    output[2] = static_cast<double>(first.z) - second.z;
}

/**
 * ベクトルを有限な単位方向へ変換する。
 */
bool Normalize(const double input[3], double output[3])
{
    const double length = sqrt(input[0] * input[0] + input[1] * input[1] + input[2] * input[2]);
    if (!(length > 1e-12) || !isfinite(length))
    {
        return false;
    }
    output[0] = input[0] / length;
    output[1] = input[1] / length;
    output[2] = input[2] / length;
    return true;
}

/**
 * 表示変換した倍精度座標を有限なVec3へ変換する。
 */
bool TransformPoint(gk::Vec3 point, float scale, gk::Vec3 center, float cosine, float sine, gk::Vec3& output)
{
    const double localX = (static_cast<double>(point.x) - center.x) * scale;
    const double localY = (static_cast<double>(point.y) - center.y) * scale;
    const double localZ = (static_cast<double>(point.z) - center.z) * scale;
    const double worldX = localX * cosine + localZ * sine;
    const double worldZ = -localX * sine + localZ * cosine;
    if (!isfinite(worldX) || !isfinite(localY) || !isfinite(worldZ) || fabs(worldX) > FLT_MAX || fabs(localY) > FLT_MAX || fabs(worldZ) > FLT_MAX)
    {
        return false;
    }
    output = { static_cast<float>(worldX), static_cast<float>(localY), static_cast<float>(worldZ) };
    return IsFinite(output);
}

/**
 * 輪郭線を細い塗り三角形の帯として追加する。
 */
bool DrawRibbon(gk::Vec3 first, gk::Vec3 second, uint32_t color)
{
    double direction[3]{};
    Difference(second, first, direction);
    double side[3] = { -direction[1], direction[0], 0.0 };
    if (!Normalize(side, side))
    {
        side[0] = 1.0;
        side[1] = 0.0;
        side[2] = 0.0;
    }
    const gk::Vec3 offset = { static_cast<float>(side[0] * RibbonHalfWidth), static_cast<float>(side[1] * RibbonHalfWidth), static_cast<float>(side[2] * RibbonHalfWidth) };
    const gk::Vec3 a = { first.x + offset.x, first.y + offset.y, first.z + offset.z };
    const gk::Vec3 b = { first.x - offset.x, first.y - offset.y, first.z - offset.z };
    const gk::Vec3 c = { second.x + offset.x, second.y + offset.y, second.z + offset.z };
    const gk::Vec3 d = { second.x - offset.x, second.y - offset.y, second.z - offset.z };
    return gk::DrawTriangle3D(a, b, c, color, true) == 0 && gk::DrawTriangle3D(b, d, c, color, true) == 0;
}

/**
 * 半径0の位置を、衝突半径を誇張しない交差印で示す。
 */
bool DrawPointMarker(gk::Vec3 center, uint32_t color, float scale)
{
    const float extent = MarkerHalfSize * scale;
    const gk::Vec3 xA = { center.x - extent, center.y, center.z };
    const gk::Vec3 xB = { center.x + extent, center.y, center.z };
    const gk::Vec3 yA = { center.x, center.y - extent, center.z };
    const gk::Vec3 yB = { center.x, center.y + extent, center.z };
    const gk::Vec3 zA = { center.x, center.y, center.z - extent };
    const gk::Vec3 zB = { center.x, center.y, center.z + extent };
    return DrawRibbon(xA, xB, color) && DrawRibbon(yA, yB, color) && DrawRibbon(zA, zB, color);
}

/**
 * 球の大円を三方向に描く。
 */
bool DrawSphere(gk::Vec3 center, float radius, uint32_t color, float scale)
{
    if (!(radius > 0.0f))
    {
        return DrawPointMarker(center, color, scale);
    }
    const float scaledRadius = radius * scale;
    for (uint32_t plane = 0; plane < 3; ++plane)
    {
        gk::Vec3 previous{};
        for (uint32_t index = 0; index <= CircleSegments; ++index)
        {
            const double angle = 6.28318530717958647692 * index / CircleSegments;
            const float cosine = static_cast<float>(cos(angle) * scaledRadius);
            const float sine = static_cast<float>(sin(angle) * scaledRadius);
            gk::Vec3 current = center;
            if (plane == 0)
            {
                current.x += cosine;
                current.y += sine;
            }
            else if (plane == 1)
            {
                current.x += cosine;
                current.z += sine;
            }
            else
            {
                current.y += cosine;
                current.z += sine;
            }
            if (index > 0 && !DrawRibbon(previous, current, color))
            {
                return false;
            }
            previous = current;
        }
    }
    return true;
}

/**
 * 端点の周囲へ軸直交リングを描く。
 */
bool DrawEndRing(gk::Vec3 center, const double axis[3], double radius, uint32_t color)
{
    double reference[3] = { 0.0, 0.0, 1.0 };
    if (fabs(axis[2]) > 0.9)
    {
        reference[0] = 1.0;
        reference[2] = 0.0;
    }
    double firstBasis[3] = { axis[1] * reference[2] - axis[2] * reference[1], axis[2] * reference[0] - axis[0] * reference[2], axis[0] * reference[1] - axis[1] * reference[0] };
    if (!Normalize(firstBasis, firstBasis))
    {
        return false;
    }
    const double secondBasis[3] = { axis[1] * firstBasis[2] - axis[2] * firstBasis[1], axis[2] * firstBasis[0] - axis[0] * firstBasis[2], axis[0] * firstBasis[1] - axis[1] * firstBasis[0] };
    gk::Vec3 previous{};
    for (uint32_t index = 0; index <= CircleSegments; ++index)
    {
        const double angle = 6.28318530717958647692 * index / CircleSegments;
        const double cosine = cos(angle) * radius;
        const double sine = sin(angle) * radius;
        const double point[3] = { center.x + firstBasis[0] * cosine + secondBasis[0] * sine, center.y + firstBasis[1] * cosine + secondBasis[1] * sine, center.z + firstBasis[2] * cosine + secondBasis[2] * sine };
        if (fabs(point[0]) > FLT_MAX || fabs(point[1]) > FLT_MAX || fabs(point[2]) > FLT_MAX)
        {
            return false;
        }
        const gk::Vec3 current = { static_cast<float>(point[0]), static_cast<float>(point[1]), static_cast<float>(point[2]) };
        if (index > 0 && !DrawRibbon(previous, current, color))
        {
            return false;
        }
        previous = current;
    }
    return true;
}

/**
 * 線分を軸とするカプセルの端リングと四方向の輪郭を描く。
 */
bool DrawCapsule(gk::Vec3 start, gk::Vec3 end, float radius, uint32_t color, float scale)
{
    const double delta[3] = { static_cast<double>(end.x) - start.x, static_cast<double>(end.y) - start.y, static_cast<double>(end.z) - start.z };
    double axis[3]{};
    if (!Normalize(delta, axis))
    {
        return DrawSphere(start, radius, color, scale);
    }
    const double scaledRadius = static_cast<double>(radius) * scale;
    if (!DrawEndRing(start, axis, scaledRadius, color) || !DrawEndRing(end, axis, scaledRadius, color))
    {
        return false;
    }
    double reference[3] = { 0.0, 0.0, 1.0 };
    if (fabs(axis[2]) > 0.9)
    {
        reference[0] = 1.0;
        reference[2] = 0.0;
    }
    double firstBasis[3] = { axis[1] * reference[2] - axis[2] * reference[1], axis[2] * reference[0] - axis[0] * reference[2], axis[0] * reference[1] - axis[1] * reference[0] };
    if (!Normalize(firstBasis, firstBasis))
    {
        return false;
    }
    const double secondBasis[3] = { axis[1] * firstBasis[2] - axis[2] * firstBasis[1], axis[2] * firstBasis[0] - axis[0] * firstBasis[2], axis[0] * firstBasis[1] - axis[1] * firstBasis[0] };
    for (uint32_t meridian = 0; meridian < 4; ++meridian)
    {
        const double angle = 1.57079632679489661923 * meridian;
        const double radial[3] = { firstBasis[0] * cos(angle) + secondBasis[0] * sin(angle), firstBasis[1] * cos(angle) + secondBasis[1] * sin(angle), firstBasis[2] * cos(angle) + secondBasis[2] * sin(angle) };
        gk::Vec3 endPrevious{};
        gk::Vec3 startPrevious{};
        if (!DrawRibbon({ static_cast<float>(start.x + radial[0] * scaledRadius), static_cast<float>(start.y + radial[1] * scaledRadius), static_cast<float>(start.z + radial[2] * scaledRadius) }, { static_cast<float>(end.x + radial[0] * scaledRadius), static_cast<float>(end.y + radial[1] * scaledRadius), static_cast<float>(end.z + radial[2] * scaledRadius) }, color) || !DrawRibbon({ static_cast<float>(end.x - radial[0] * scaledRadius), static_cast<float>(end.y - radial[1] * scaledRadius), static_cast<float>(end.z - radial[2] * scaledRadius) }, { static_cast<float>(start.x - radial[0] * scaledRadius), static_cast<float>(start.y - radial[1] * scaledRadius), static_cast<float>(start.z - radial[2] * scaledRadius) }, color))
        {
            return false;
        }
        for (uint32_t index = 0; index <= CircleSegments; ++index)
        {
            const double capAngle = 3.14159265358979323846 * index / CircleSegments;
            const double along = sin(capAngle) * scaledRadius;
            const double side = cos(capAngle) * scaledRadius;
            const double endPoint[3] = { static_cast<double>(end.x) + axis[0] * along + radial[0] * side, static_cast<double>(end.y) + axis[1] * along + radial[1] * side, static_cast<double>(end.z) + axis[2] * along + radial[2] * side };
            const double startPoint[3] = { static_cast<double>(start.x) - axis[0] * along - radial[0] * side, static_cast<double>(start.y) - axis[1] * along - radial[1] * side, static_cast<double>(start.z) - axis[2] * along - radial[2] * side };
            if (fabs(endPoint[0]) > FLT_MAX || fabs(endPoint[1]) > FLT_MAX || fabs(endPoint[2]) > FLT_MAX || fabs(startPoint[0]) > FLT_MAX || fabs(startPoint[1]) > FLT_MAX || fabs(startPoint[2]) > FLT_MAX)
            {
                return false;
            }
            const gk::Vec3 endCurrent = { static_cast<float>(endPoint[0]), static_cast<float>(endPoint[1]), static_cast<float>(endPoint[2]) };
            const gk::Vec3 startCurrent = { static_cast<float>(startPoint[0]), static_cast<float>(startPoint[1]), static_cast<float>(startPoint[2]) };
            if (index > 0 && (!DrawRibbon(endPrevious, endCurrent, color) || !DrawRibbon(startPrevious, startCurrent, color)))
            {
                return false;
            }
            endPrevious = endCurrent;
            startPrevious = startCurrent;
        }
    }
    return true;
}

/**
 * 表示上の優先順位に従って形状色を選ぶ。
 */
uint32_t ShapeColor(const gk::FModelSecondaryMotionDebugShape& shape)
{
    if (shape.fixed)
    {
        return gk::ColorRGB(255, 255, 255);
    }
    if (shape.virtualTip)
    {
        return gk::ColorRGB(185, 185, 205);
    }
    if (shape.body)
    {
        return gk::ColorRGB(65, 155, 255);
    }
    return gk::ColorRGB(255, 145, 45);
}

}

bool ParseSecondaryMotionDebugOptions(int& argc, char** argv, bool& enabled)
{
    int selected = -1;
    for (int index = 6; index < argc; ++index)
    {
        if (!argv[index])
        {
            return false;
        }
        if (strcmp(argv[index], "--materials") == 0 && index + 1 < argc)
        {
            ++index;
            continue;
        }
        if (strcmp(argv[index], "--collision-debug") == 0)
        {
            if (selected >= 0)
            {
                return false;
            }
            selected = index;
        }
    }
    enabled = selected >= 0;
    if (selected >= 0)
    {
        for (int index = selected; index + 1 < argc; ++index)
        {
            argv[index] = argv[index + 1];
        }
        --argc;
        argv[argc] = nullptr;
    }
    return true;
}

bool DrawModelSecondaryMotionDebug(gk::ModelHandle model, float scale, gk::Vec3 displayCenter, float rotationY)
{
    if (!IsFinite(scale) || !(scale > 0.0f) || !IsFinite(displayCenter) || !IsFinite(rotationY))
    {
        return false;
    }
    const int required = gk::GetModelSecondaryMotionDebugShapes(model, nullptr, 0);
    if (required < 0 || required > static_cast<int>(DebugShapeLimit))
    {
        return false;
    }
    gk::FModelSecondaryMotionDebugShape shapes[DebugShapeLimit]{};
    if (required > 0 && gk::GetModelSecondaryMotionDebugShapes(model, shapes, static_cast<uint32_t>(required)) != required)
    {
        return false;
    }
    const float cosine = cosf(rotationY);
    const float sine = sinf(rotationY);
    for (int index = 0; index < required; ++index)
    {
        gk::FModelSecondaryMotionDebugShape& shape = shapes[index];
        if (!IsFinite(shape.start) || !IsFinite(shape.end) || !IsFinite(shape.radius) || shape.radius < 0.0f || !IsFinite(shape.radius * scale))
        {
            return false;
        }
        gk::Vec3 transformedStart{};
        gk::Vec3 transformedEnd{};
        if (!TransformPoint(shape.start, scale, displayCenter, cosine, sine, transformedStart) || !TransformPoint(shape.end, scale, displayCenter, cosine, sine, transformedEnd))
        {
            return false;
        }
        shape.start = transformedStart;
        shape.end = transformedEnd;
        shape.radius *= scale;
        if (!IsFinite(shape.radius))
        {
            return false;
        }
        const double extent = static_cast<double>(shape.radius > 0.0f ? shape.radius : MarkerHalfSize) + RibbonHalfWidth;
        const double coordinates[6] = { shape.start.x, shape.start.y, shape.start.z, shape.end.x, shape.end.y, shape.end.z };
        for (uint32_t component = 0; component < 6; ++component)
        {
            if (fabs(coordinates[component]) + extent > FLT_MAX)
            {
                return false;
            }
        }
    }
    for (int index = 0; index < required; ++index)
    {
        const gk::FModelSecondaryMotionDebugShape& shape = shapes[index];
        const uint32_t color = ShapeColor(shape);
        if (!DrawCapsule(shape.start, shape.end, shape.radius, color, 1.0f))
        {
            return false;
        }
    }
    return true;
}

}
