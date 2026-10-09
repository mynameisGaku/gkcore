#include "render/ModelDrawConstants.h"
#include "render/WorldGeometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

using namespace gk;
using namespace gk::render;

bool Check(bool condition, const char* name)
{
    if (condition)
        return true;
    fprintf(stderr, "model draw constants test failed: %s\n", name);
    return false;
}

bool Near(float left, float right, float tolerance = 0.0001f)
{
    return fabsf(left - right) <= tolerance;
}

struct Matrix3
{
    double value[3][3];
};

Matrix3 Multiply(const Matrix3& left, const Matrix3& right)
{
    Matrix3 result{};
    for (uint32_t row = 0; row < 3; ++row)
    {
        for (uint32_t column = 0; column < 3; ++column)
        {
            for (uint32_t inner = 0; inner < 3; ++inner)
                result.value[row][column] += left.value[row][inner] * right.value[inner][column];
        }
    }
    return result;
}

Matrix3 RotationX(double angle)
{
    const double cosine = cos(angle);
    const double sine = sin(angle);
    return { { { 1.0, 0.0, 0.0 }, { 0.0, cosine, -sine }, { 0.0, sine, cosine } } };
}

Matrix3 RotationY(double angle)
{
    const double cosine = cos(angle);
    const double sine = sin(angle);
    return { { { cosine, 0.0, sine }, { 0.0, 1.0, 0.0 }, { -sine, 0.0, cosine } } };
}

Matrix3 RotationZ(double angle)
{
    const double cosine = cos(angle);
    const double sine = sin(angle);
    return { { { cosine, -sine, 0.0 }, { sine, cosine, 0.0 }, { 0.0, 0.0, 1.0 } } };
}

void Normalize(double vector[3])
{
    const double length = sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
    for (uint32_t axis = 0; axis < 3; ++axis)
        vector[axis] /= length;
}

detail::DrawPacket MakeDraw(detail::ModelResource& model)
{
    detail::DrawPacket draw{};
    draw.kind = detail::DrawKind::Model;
    draw.model = &model;
    draw.cameraPosition = { 3.0f, -2.0f, -5.0f };
    draw.cameraTarget = { -1.0f, 1.0f, 2.0f };
    draw.modelPosition = { 1.25f, -3.5f, 7.0f };
    draw.modelRotation = { 0.37f, -0.61f, 1.08f };
    draw.modelScale = { -2.0f, 0.75f, 3.25f };
    draw.color = 0xff00ff00u;
    return draw;
}

bool TestConstantLayoutAndTransformRows()
{
    static_assert(sizeof(FModelDrawConstants) == 208, "constant ABI must remain thirteen float4 rows");
    detail::ModelResource model{};
    detail::FramePacket frame{};
    frame.width = 800;
    frame.height = 400;
    detail::DrawPacket draw = MakeDraw(model);
    FModelDrawConstants constants{};
    String error;
    if (!Check(PackModelDrawConstants(frame, draw, constants, error), "pack mixed-axis reflected transform"))
        return false;

    const Matrix3 expectedRotation = Multiply(Multiply(RotationX(draw.modelRotation.x), RotationY(draw.modelRotation.y)), RotationZ(draw.modelRotation.z));
    for (uint32_t row = 0; row < 3; ++row)
    {
        for (uint32_t column = 0; column < 3; ++column)
        {
            if (!Check(Near(constants.rotationRows[row][column], static_cast<float>(expectedRotation.value[row][column])), "rotation rows match Rx*Ry*Rz"))
                return false;
        }
        if (!Check(constants.rotationRows[row][3] == 0.0f, "rotation row padding is zero"))
            return false;
    }

    const float scales[3] = { draw.modelScale.x, draw.modelScale.y, draw.modelScale.z };
    const float positions[3] = { draw.modelPosition.x, draw.modelPosition.y, draw.modelPosition.z };
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!Check(Near(constants.scale[axis], scales[axis]) && Near(constants.inverseScale[axis], 1.0f / scales[axis]), "signed scale and inverse scale are preserved"))
            return false;
        if (!Check(Near(constants.translation[axis], positions[axis]), "model translation is preserved"))
            return false;
    }
    if (!Check(constants.scale[3] == 0.0f && constants.inverseScale[3] == 0.0f && constants.translation[3] == 0.0f, "model vector padding is zero"))
        return false;
    if (!Check(constants.flags[0] == 1.0f && constants.flags[1] == 0.0f && constants.flags[2] == 0.0f && constants.flags[3] == 0.0f, "local-position mode flag is set"))
        return false;
    if (!Check(constants.tint[0] == 1.0f && constants.tint[1] == 1.0f && constants.tint[2] == 1.0f && constants.tint[3] == 1.0f, "instance tint is identity"))
        return false;

    const double forward[3] = { draw.cameraTarget.x - draw.cameraPosition.x, draw.cameraTarget.y - draw.cameraPosition.y, draw.cameraTarget.z - draw.cameraPosition.z };
    double unitForward[3] = { forward[0], forward[1], forward[2] };
    Normalize(unitForward);
    double right[3] = { unitForward[2], 0.0, -unitForward[0] };
    if (right[0] * right[0] + right[2] * right[2] < 1e-24)
    {
        right[0] = 1.0;
        right[2] = 0.0;
    }
    Normalize(right);
    double up[3] = { unitForward[1] * right[2], unitForward[2] * right[0] - unitForward[0] * right[2], -unitForward[1] * right[0] };
    Normalize(up);
    const float cameraPosition[3] = { draw.cameraPosition.x, draw.cameraPosition.y, draw.cameraPosition.z };
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!Check(Near(constants.cameraForward[axis], static_cast<float>(unitForward[axis])) && Near(constants.cameraRight[axis], static_cast<float>(right[axis])) && Near(constants.cameraUp[axis], static_cast<float>(up[axis])), "camera basis rows match independent basis"))
            return false;
        if (!Check(Near(constants.cameraPosition[axis], cameraPosition[axis]), "camera position is preserved"))
            return false;
    }
    if (!Check(constants.cameraForward[3] == 0.0f && constants.cameraRight[3] == 0.0f && constants.cameraUp[3] == 0.0f && constants.cameraPosition[3] == 0.0f, "camera row padding is zero"))
        return false;

    const float aspect = static_cast<float>(frame.width) / static_cast<float>(frame.height);
    const float focal = 1.7320508075688772f;
    if (!Check(Near(constants.projection[0], focal / aspect) && Near(constants.projection[1], focal), "horizontal and vertical projection scales match"))
        return false;
    if (!Check(Near(constants.projection[2], 1000.0f / 999.9f) && Near(constants.projection[3], -100.0f / 999.9f), "near and far depth projection matches"))
        return false;
    return true;
}

bool TestShaderTransformMatchesCpuProjection()
{
    detail::ModelResource model{};
    detail::FramePacket frame{};
    frame.width = 640;
    frame.height = 480;
    detail::DrawPacket draw = MakeDraw(model);
    draw.cameraPosition = { 1.5f, 0.25f, -6.0f };
    draw.cameraTarget = { 0.5f, -0.25f, 0.0f };
    draw.modelPosition = { 0.1f, 0.2f, 0.0f };
    draw.modelScale = { 0.8f, 1.4f, -0.65f };
    draw.color = 0xff0000ffu;
    FModelDrawConstants constants{};
    String error;
    if (!PackModelDrawConstants(frame, draw, constants, error))
        return Check(false, "pack transform for projected comparison");

    const Vec3 sourcePositions[3] = { { -0.08f, 0.12f, 0.03f }, { 0.14f, 0.09f, 0.0f }, { -0.02f, -0.1f, 0.08f } };
    WorldVertex source[3]{};
    for (uint32_t i = 0; i < 3; ++i)
        source[i].position = sourcePositions[i];
    ProjectedWorldVertex projected[18]{};
    uint32_t projectedCount = 0;
    if (!ProjectWorldTriangle(frame, draw, source, true, false, nullptr, projected, projectedCount, error))
        return Check(false, "CPU projection accepts comparison triangle");
    if (!Check(projectedCount == 3, "comparison triangle is not clipped"))
        return false;

    const double scaled[3] = { sourcePositions[0].x * constants.scale[0], sourcePositions[0].y * constants.scale[1], sourcePositions[0].z * constants.scale[2] };
    double world[3]{};
    for (uint32_t row = 0; row < 3; ++row)
    {
        world[row] = constants.translation[row];
        for (uint32_t column = 0; column < 3; ++column)
            world[row] += static_cast<double>(constants.rotationRows[row][column]) * scaled[column];
    }
    const double difference[3] = { world[0] - constants.cameraPosition[0], world[1] - constants.cameraPosition[1], world[2] - constants.cameraPosition[2] };
    const double viewX = difference[0] * constants.cameraRight[0] + difference[1] * constants.cameraRight[1] + difference[2] * constants.cameraRight[2];
    const double viewY = difference[0] * constants.cameraUp[0] + difference[1] * constants.cameraUp[1] + difference[2] * constants.cameraUp[2];
    const double viewZ = difference[0] * constants.cameraForward[0] + difference[1] * constants.cameraForward[1] + difference[2] * constants.cameraForward[2];
    const float expectedClip[4] = { static_cast<float>(viewX * constants.projection[0]), static_cast<float>(viewY * constants.projection[1]), static_cast<float>(viewZ * constants.projection[2] + constants.projection[3]), static_cast<float>(viewZ) };
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (!Check(Near(expectedClip[component], projected[0].surface.position[component], 0.0003f), "packed shader transform agrees with CPU clip projection"))
            return false;
    }
    return true;
}

bool TestAxisRotationsAndFailureAtomicity()
{
    detail::ModelResource model{};
    detail::FramePacket frame{};
    frame.width = 320;
    frame.height = 240;
    detail::DrawPacket draw = MakeDraw(model);
    String error;
    const Vec3 rotations[3] = { { 0.5f, 0.0f, 0.0f }, { 0.0f, -0.7f, 0.0f }, { 0.0f, 0.0f, 1.2f } };
    const Matrix3 expected[3] = { RotationX(rotations[0].x), RotationY(rotations[1].y), RotationZ(rotations[2].z) };
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        draw.modelRotation = rotations[axis];
        FModelDrawConstants constants{};
        if (!PackModelDrawConstants(frame, draw, constants, error))
            return Check(false, "pack single-axis rotation");
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
            {
                if (!Check(Near(constants.rotationRows[row][column], static_cast<float>(expected[axis].value[row][column])), "single-axis rotation row"))
                    return false;
            }
        }
    }

    FModelDrawConstants sentinel{};
    memset(&sentinel, 0x5a, sizeof(sentinel));
    FModelDrawConstants output{};
    memcpy(&output, &sentinel, sizeof(output));
    frame.width = 0;
    if (!Check(!PackModelDrawConstants(frame, draw, output, error), "zero frame width is rejected") || !Check(memcmp(&output, &sentinel, sizeof(output)) == 0, "failed pack preserves output bytes"))
        return false;

    frame.width = 320;
    draw.modelScale.x = 0.0f;
    if (!Check(!PackModelDrawConstants(frame, draw, output, error), "zero model scale cannot produce inverse scale") || !Check(memcmp(&output, &sentinel, sizeof(output)) == 0, "zero scale failure preserves output"))
        return false;

    draw.modelScale.x = 1e-39f;
    if (!Check(!PackModelDrawConstants(frame, draw, output, error), "inverse scale overflow is rejected") || !Check(memcmp(&output, &sentinel, sizeof(output)) == 0, "overflow failure preserves output"))
        return false;

    draw.modelScale.x = 1.0f;
    draw.cameraTarget = draw.cameraPosition;
    if (!Check(!PackModelDrawConstants(frame, draw, output, error), "zero camera direction is rejected") || !Check(memcmp(&output, &sentinel, sizeof(output)) == 0, "camera failure preserves output"))
        return false;
    return true;
}

bool TestVerticalCameraUsesStableRightAxis()
{
    detail::ModelResource model{};
    detail::FramePacket frame{};
    frame.width = 320;
    frame.height = 240;
    detail::DrawPacket draw = MakeDraw(model);
    draw.cameraPosition = { 0.0f, 0.0f, 0.0f };
    draw.cameraTarget = { 0.0f, 1.0f, 0.0f };
    FModelDrawConstants constants{};
    String error;
    if (!PackModelDrawConstants(frame, draw, constants, error))
        return Check(false, "pack vertical camera direction");
    return Check(Near(constants.cameraForward[0], 0.0f) && Near(constants.cameraForward[1], 1.0f) && Near(constants.cameraForward[2], 0.0f) && Near(constants.cameraRight[0], 1.0f) && Near(constants.cameraRight[1], 0.0f) && Near(constants.cameraRight[2], 0.0f) && Near(constants.cameraUp[0], 0.0f) && Near(constants.cameraUp[1], 0.0f) && Near(constants.cameraUp[2], -1.0f), "vertical camera uses finite fallback right axis");
}

}

int main()
{
    return TestConstantLayoutAndTransformRows() && TestShaderTransformMatchesCpuProjection() && TestAxisRotationsAndFailureAtomicity() && TestVerticalCameraUsesStableRightAxis() ? 0 : 1;
}
