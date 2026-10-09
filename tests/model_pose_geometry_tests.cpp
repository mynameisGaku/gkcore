#include "render/ModelPoseGeometry.h"

#include <math.h>
#include <stdio.h>

namespace
{

using namespace gk;
using namespace gk::render;

bool Check(bool condition, const char* name)
{
    if (condition)
        return true;
    fprintf(stderr, "model pose geometry test failed: %s\n", name);
    return false;
}

bool Near(float left, float right, float tolerance = 0.00001f)
{
    return fabsf(left - right) <= tolerance;
}

detail::ModelVertex MakeVertex(float px = 1.0f, float nx = 0.0f, float ny = 3.0f, float nz = 4.0f)
{
    detail::ModelVertex vertex{};
    vertex.position[0] = px;
    vertex.position[1] = -2.0f;
    vertex.position[2] = 3.0f;
    vertex.normal[0] = nx;
    vertex.normal[1] = ny;
    vertex.normal[2] = nz;
    vertex.tangent[0] = 2.0f;
    vertex.tangent[1] = 0.0f;
    vertex.tangent[2] = 0.0f;
    vertex.tangent[3] = -1.0f;
    return vertex;
}

bool TestPackingAndSourceImmutability()
{
    detail::ModelResource model{};
    detail::ModelVertex first = MakeVertex();
    detail::ModelVertex second = MakeVertex(-4.0f, 0.0f, 0.0f, -2.0f);
    second.tangent[0] = 0.0f;
    second.tangent[3] = 0.25f;
    if (!Check(model.vertices.Append(first) && model.vertices.Append(second), "append source vertices"))
        return false;

    Array<FModelPoseVertex> output;
    String error;
    if (!Check(AppendModelPoseVertices(model, output, 4, error), "append normalized pose stream"))
        return false;
    if (!Check(output.Count() == 2, "both source vertices are packed"))
        return false;
    const FModelPoseVertex& packed = output.At(0);
    if (!Check(packed.position[0] == 1.0f && packed.position[1] == -2.0f && packed.position[2] == 3.0f && packed.position[3] == 0.0f, "position is copied with zero padding"))
        return false;
    if (!Check(Near(packed.normal[0], 0.0f) && Near(packed.normal[1], 0.6f) && Near(packed.normal[2], 0.8f) && packed.normal[3] == 0.0f, "normal is unit length with zero padding"))
        return false;
    if (!Check(packed.tangent[0] == 1.0f && packed.tangent[1] == 0.0f && packed.tangent[2] == 0.0f && packed.tangent[3] == -1.0f, "tangent is unit length and handedness is preserved"))
        return false;
    if (!Check(output.At(1).normal[0] == 0.0f && output.At(1).normal[1] == 0.0f && output.At(1).normal[2] == -1.0f && output.At(1).tangent[0] == 0.0f && output.At(1).tangent[1] == 0.0f && output.At(1).tangent[2] == 0.0f && output.At(1).tangent[3] == 0.25f, "zero tangent remains zero and keeps its fourth component"))
        return false;
    return Check(model.vertices.At(0).normal[1] == 3.0f && model.vertices.At(0).tangent[0] == 2.0f, "source model values are unchanged");
}

bool TestAppendFailurePreservesOutput()
{
    const float nan = static_cast<float>(NAN);
    detail::ModelVertex invalidVertices[5] = { MakeVertex(nan), MakeVertex(1.0f, 0.0f, 0.0f, 0.0f), MakeVertex(100000016.0f), MakeVertex(), MakeVertex() };
    invalidVertices[3].tangent[1] = nan;
    const uint32_t limits[5] = { 8, 8, 8, 8, 1 };
    for (uint32_t test = 0; test < 5; ++test)
    {
        detail::ModelResource model{};
        if (!Check(model.vertices.Append(invalidVertices[test]), "append invalid test source"))
            return false;
        Array<FModelPoseVertex> output;
        FModelPoseVertex sentinel{};
        sentinel.position[0] = 17.0f;
        if (!Check(output.Append(sentinel), "append output sentinel"))
            return false;
        String error;
        if (!Check(!AppendModelPoseVertices(model, output, limits[test], error), "invalid or oversized input is rejected"))
            return false;
        if (!Check(output.Count() == 1 && output.At(0).position[0] == 17.0f, "failed append leaves the existing output intact"))
            return false;
    }
    return true;
}

bool TestPositionSafetyBoundary()
{
    detail::ModelResource model{};
    detail::ModelVertex vertex = MakeVertex(100000000.0f);
    if (!Check(model.vertices.Append(vertex), "append boundary position"))
        return false;
    Array<FModelPoseVertex> output;
    String error;
    if (!Check(AppendModelPoseVertices(model, output, 1, error), "accept position at the safety boundary"))
        return false;
    return Check(output.Count() == 1 && output.At(0).position[0] == 100000000.0f, "boundary position is preserved");
}

bool TestMappedPartBasisValidation()
{
    detail::ModelResource model{};
    detail::ModelVertex vertex = MakeVertex();
    if (!Check(model.vertices.Append(vertex) && model.vertices.Append(vertex) && model.vertices.Append(vertex) && model.indices.Append(0) && model.indices.Append(1) && model.indices.Append(2), "append mapped test geometry"))
        return false;
    ModelPartPlan part{};
    part.firstIndex = 0;
    part.indexCount = 3;
    part.normalTextureIndex = -1;
    if (!Check(IsModelPosePartGpuSafe(model, part), "unmapped normal attributes do not require a tangent basis"))
        return false;
    part.firstIndex = 1;
    if (!Check(!IsModelPosePartGpuSafe(model, part), "invalid part index range is rejected"))
        return false;
    part.firstIndex = 0;
    part.normalTextureIndex = 0;
    if (!Check(IsModelPosePartGpuSafe(model, part), "orthogonal finite tangent basis is accepted"))
        return false;

    detail::ModelResource invalid = {};
    vertex.tangent[0] = 0.0f;
    vertex.tangent[1] = 3.0f;
    vertex.tangent[2] = 4.0f;
    if (!Check(invalid.vertices.Append(vertex) && invalid.vertices.Append(vertex) && invalid.vertices.Append(vertex) && invalid.indices.Append(0) && invalid.indices.Append(1) && invalid.indices.Append(2), "append parallel tangent geometry"))
        return false;
    if (!Check(!IsModelPosePartGpuSafe(invalid, part), "parallel tangent and normal are rejected"))
        return false;

    invalid.vertices.At(0).tangent[1] = 0.0f;
    invalid.vertices.At(0).tangent[2] = 0.0f;
    invalid.vertices.At(0).tangent[3] = 0.0f;
    if (!Check(!IsModelPosePartGpuSafe(invalid, part), "invalid tangent handedness is rejected"))
        return false;
    invalid.vertices.At(0).tangent[3] = 1.0f;
    invalid.vertices.At(0).tangent[0] = 0.0f;
    if (!Check(!IsModelPosePartGpuSafe(invalid, part), "zero mapped tangent is rejected"))
        return false;
    invalid.vertices.At(0).tangent[0] = NAN;
    if (!Check(!IsModelPosePartGpuSafe(invalid, part), "non-finite mapped tangent is rejected"))
        return false;
    return true;
}

}

int main()
{
    return TestPackingAndSourceImmutability() && TestAppendFailurePreservesOutput() && TestPositionSafetyBoundary() && TestMappedPartBasisValidation() ? 0 : 1;
}
