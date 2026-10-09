#include "render/ModelDrawConstants.h"
#include "render/WorldGeometry.h"

#include <float.h>
#include <math.h>

/**
 * model描画定数を構築する。
 */
namespace gk::render
{
namespace
{

/**
 * double値を有限なGPU floatへ変換する。
 */
bool StoreFloat(double value, float& output, String& error)
{
    if (!isfinite(value) || fabs(value) > FLT_MAX)
    {
        error.Assign("The model draw constants exceed the GPU float range");
        return false;
    }
    output = static_cast<float>(value);
    if (!isfinite(output))
    {
        error.Assign("The model draw constants exceed the GPU float range");
        return false;
    }
    return true;
}

/**
 * 失敗理由を設定する。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

/**
 * CPUの描画入力をGPU変換定数へまとめる。
 * 変換不能な値では出力を変更せずfalseを返す。
 */
bool PackModelDrawConstants(const detail::FramePacket& frame, const detail::DrawPacket& draw, FModelDrawConstants& output, String& error)
{
    error.Clear();
    if (draw.kind != detail::DrawKind::Model || !draw.model || frame.width == 0 || frame.height == 0)
        return Fail(error, "The model draw constants input is invalid");

    FWorldGeometryContext context{};
    if (!BuildWorldGeometryContext(draw, true, context, error))
        return false;

    FModelDrawConstants candidate{};
    const double scales[3] = { context.modelScale[0], context.modelScale[1], context.modelScale[2] };
    const double translations[3] = { context.modelPosition[0], context.modelPosition[1], context.modelPosition[2] };
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (scales[axis] == 0.0 || !isfinite(scales[axis]))
            return Fail(error, "The model scale cannot be represented by inverse-scale constants");
        if (!StoreFloat(scales[axis], candidate.scale[axis], error) || !StoreFloat(1.0 / scales[axis], candidate.inverseScale[axis], error) || !StoreFloat(translations[axis], candidate.translation[axis], error))
            return false;
    }

    // 位置変換と同じZ→Y→X順で、行列Rx*Ry*Rzを組み立てる。
    const double sx = context.rotationSin[0];
    const double cx = context.rotationCos[0];
    const double sy = context.rotationSin[1];
    const double cy = context.rotationCos[1];
    const double sz = context.rotationSin[2];
    const double cz = context.rotationCos[2];
    const double rotation[3][3] = { { cy * cz, -cy * sz, sy }, { cx * sz + sx * sy * cz, cx * cz - sx * sy * sz, -sx * cy }, { sx * sz - cx * sy * cz, sx * cz + cx * sy * sz, cx * cy } };
    for (uint32_t row = 0; row < 3; ++row)
    {
        for (uint32_t column = 0; column < 3; ++column)
        {
            if (!StoreFloat(rotation[row][column], candidate.rotationRows[row][column], error))
                return false;
        }
    }

    // camera基底と位置を定数bufferへ複写するloop。
    const double* cameraRows[4] = { context.cameraForward, context.cameraRight, context.cameraUp, context.cameraPosition };
    float* outputRows[4] = { candidate.cameraForward, candidate.cameraRight, candidate.cameraUp, candidate.cameraPosition };
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!StoreFloat(cameraRows[row][axis], outputRows[row][axis], error))
                return false;
        }
    }

    constexpr double focal = 1.7320508075688772;
    constexpr double nearPlane = 0.1;
    constexpr double farPlane = 1000.0;
    const double aspect = static_cast<double>(frame.width) / static_cast<double>(frame.height);
    const double projection[4] = { focal / aspect, focal, farPlane / (farPlane - nearPlane), -farPlane * nearPlane / (farPlane - nearPlane) };
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (!StoreFloat(projection[component], candidate.projection[component], error))
            return false;
    }

    // tintは個別draw定数へ分け、材質係数は頂点属性のまま保つ。
    candidate.tint[0] = 1.0f;
    candidate.tint[1] = 1.0f;
    candidate.tint[2] = 1.0f;
    candidate.tint[3] = 1.0f;
    candidate.flags[0] = 1.0f;
    output = candidate;
    error.Clear();
    return true;
}

}
