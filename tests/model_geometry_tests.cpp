#include "../src/render/ModelGeometry.h"

#include "../src/foundation/Memory.h"

#include <limits>
#include <math.h>
#include <stdio.h>

namespace {

using namespace gk;
using namespace gk::render;

bool Check(bool value, const char* label) {
    if (value) return true;
    fprintf(stderr, "model geometry test failed: %s\n", label);
    return false;
}

detail::ModelVertex MakeVertex(float x, float y, float z, float nx, float ny, float nz,
                               float u, float v) {
    detail::ModelVertex result{};
    result.position[0] = x;
    result.position[1] = y;
    result.position[2] = z;
    result.normal[0] = nx;
    result.normal[1] = ny;
    result.normal[2] = nz;
    result.uv[0] = u;
    result.uv[1] = v;
    return result;
}

void MakeTriangle(detail::ModelResource& model, const Vec3 normals[3], const Vec3 points[3]) {
    for (uint32_t i = 0; i < 3; ++i)
        model.vertices.Append(MakeVertex(points[i].x, points[i].y, points[i].z,
                                         normals[i].x, normals[i].y, normals[i].z,
                                         static_cast<float>(i == 1), static_cast<float>(i == 2)));
    model.indices.Append(0);
    model.indices.Append(1);
    model.indices.Append(2);
}

void Frame(detail::FramePacket& frame) {
    frame.width = 640;
    frame.height = 480;
}

detail::DrawPacket Draw(detail::ModelResource& model) {
    detail::DrawPacket draw{};
    draw.kind = detail::DrawKind::Model;
    draw.model = &model;
    draw.cameraPosition = {0.0f, 0.0f, -5.0f};
    draw.cameraTarget = {0.0f, 0.0f, 0.0f};
    draw.modelScale = {1.0f, 1.0f, 1.0f};
    return draw;
}

ModelPartPlan Part(uint32_t first = 0, uint32_t count = 3) {
    ModelPartPlan part{};
    part.firstIndex = first;
    part.indexCount = count;
    part.materialIndex = -1;
    part.textureIndex = -1;
    part.baseColorFactor[0] = part.baseColorFactor[1] = part.baseColorFactor[2] = 1.0f;
    part.baseColorFactor[3] = 1.0f;
    part.metallicFactor = 0.35f;
    part.roughnessFactor = 0.65f;
    return part;
}

float Length(const float value[3]) {
    return sqrtf(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

bool Near(float a, float b, float tolerance = 0.0002f) {
    return fabsf(a - b) <= tolerance;
}

bool TestVertexContractAndBasicLightingPayload() {
    static_assert(sizeof(Vertex) == 40, "legacy vertex ABI remains unchanged");
    static_assert(sizeof(ModelRenderVertex) == 72, "lit model vertex ABI is 72 bytes");
    const Vec3 points[3] = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 0.0f}};
    const Vec3 normals[3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    ModelPartPlan part = Part();
    part.baseColorFactor[0] = 0.25f;
    part.baseColorFactor[1] = 0.5f;
    part.baseColorFactor[2] = 0.75f;
    part.baseColorFactor[3] = 0.4f;
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error),
               "valid lit model part is projected")) return false;
    if (!Check(vertices.Count() == 3, "one unclipped triangle emits three lit vertices")) return false;
    const ModelRenderVertex& vertex = vertices.At(0);
    if (!Check(Near(vertex.worldNormal[0], 0) && Near(vertex.worldNormal[1], 0) &&
               Near(vertex.worldNormal[2], 1), "source normal is normalized in world space")) return false;
    if (!Check(Near(vertex.viewDirection[0], 0.5f) && Near(vertex.viewDirection[1], 0.5f) &&
               Near(vertex.viewDirection[2], -5.0f),
               "view direction carries raw camera-minus-world coordinates")) return false;
    if (!Check(Near(vertex.metallicRoughness[0], 0.35f) &&
               Near(vertex.metallicRoughness[1], 0.65f), "metallic and roughness are carried to the vertex")) return false;
    const float expectedUv[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    for (uint32_t i = 0; i < 3; ++i) {
        if (!Check(vertices.At(i).surface.uv[0] == expectedUv[i][0] &&
                   vertices.At(i).surface.uv[1] == expectedUv[i][1],
                   "indexed UVs remain attached to their projected corners")) return false;
        if (!Check(vertices.At(i).surface.color[0] == 0.25f &&
                   vertices.At(i).surface.color[1] == 0.5f &&
                   vertices.At(i).surface.color[2] == 0.75f &&
                   vertices.At(i).surface.color[3] == 0.4f,
                   "linear base color remains in the legacy surface payload")) return false;
    }
    return true;
}

bool TestInverseScaleRotationAndNegativeScale() {
    const Vec3 points[3] = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 0.0f}};
    const Vec3 normals[3] = {{1, 1, 0}, {1, 1, 0}, {1, 1, 0}};
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    draw.modelScale = {2, 1, 1};
    draw.modelRotation.z = 1.57079632679f;
    Array<ModelRenderVertex> vertices;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error),
               "nonuniform scale and rotation are supported")) return false;
    const float invSqrtFive = 0.4472135955f;
    if (!Check(Near(vertices.At(0).worldNormal[0], -2.0f * invSqrtFive) &&
               Near(vertices.At(0).worldNormal[1], invSqrtFive) &&
               Near(vertices.At(0).worldNormal[2], 0),
               "normal uses inverse scale before the model Euler rotation")) return false;
    draw.modelScale = {-1, 1, 1};
    draw.modelRotation = {0, 0, 0};
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error),
               "negative nonzero scale is supported")) return false;
    return Check(Near(vertices.At(0).worldNormal[0], -0.70710678f) &&
                 Near(vertices.At(0).worldNormal[1], 0.70710678f),
                 "negative scale changes the normal orientation through inverse scale");
}

bool TestZeroNormalFallbacks() {
    const Vec3 points[3] = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 0.0f}};
    const Vec3 zeros[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    detail::ModelResource model{};
    MakeTriangle(model, zeros, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error),
               "zero source normals use a face fallback")) return false;
    if (!Check(Near(vertices.At(0).worldNormal[2], 1), "face fallback follows triangle winding")) return false;
    model.vertices.At(1).position[0] = model.vertices.At(0).position[0];
    model.vertices.At(2).position[1] = model.vertices.At(0).position[1];
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error),
               "degenerate triangle has a finite fallback normal")) return false;
    return Check(Near(Length(vertices.At(0).worldNormal), 1) &&
                 isfinite(vertices.At(0).worldNormal[0]) &&
                 isfinite(vertices.At(0).worldNormal[1]) &&
                 isfinite(vertices.At(0).worldNormal[2]),
                 "degenerate fallback is finite and unit length");
}

bool TestZeroNormalFallbackIsAlreadyInWorldSpace() {
    const Vec3 points[3] = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 0.0f}};
    const Vec3 zeros[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    detail::ModelResource model{};
    MakeTriangle(model, zeros, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    draw.modelScale = {2.0f, 1.0f, 1.0f};
    draw.modelRotation.x = 1.57079632679f;
    Array<ModelRenderVertex> vertices;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error),
               "rotated and scaled zero-normal triangle uses world-face fallback")) return false;
    const ModelRenderVertex& vertex = vertices.At(0);
    return Check(Near(vertex.worldNormal[0], 0) && Near(vertex.worldNormal[1], -1) &&
                 Near(vertex.worldNormal[2], 0),
                 "world-space fallback face normal is not rotated a second time");
}

bool TestClippingInterpolatesLightingAndRejectsNonFiniteInputs() {
    const Vec3 points[3] = {{-1, 0, -4.95f}, {1, 0, 0}, {0, 1, 0}};
    const Vec3 normals[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error),
               "triangle crossing near plane is clipped")) return false;
    if (!Check(vertices.Count() == 6, "near clipping triangulates the clipped quad")) return false;
    bool foundInterpolatedNormal = false;
    bool foundNearViewDirection = false;
    for (uint32_t i = 0; i < vertices.Count(); ++i) {
        const ModelRenderVertex& vertex = vertices.At(i);
        if (Near(vertex.surface.position[3], 0.1f)) {
            foundInterpolatedNormal = foundInterpolatedNormal ||
                (vertex.worldNormal[0] > 0.98f && vertex.worldNormal[1] > 0.005f &&
                 vertex.worldNormal[1] < 0.02f);
            const bool clippedOnFirstEdge = fabsf(vertex.surface.uv[1]) < 0.0001f;
            if (clippedOnFirstEdge) {
                const float worldX = -1.0f + 2.0f * vertex.surface.uv[0];
                foundNearViewDirection = foundNearViewDirection ||
                    (Near(vertex.viewDirection[0], -worldX) &&
                     Near(vertex.viewDirection[1], 0.0f) &&
                     Near(vertex.viewDirection[2], -0.1f));
            }
        }
        if (!Check(isfinite(vertex.worldNormal[0]) && isfinite(vertex.worldNormal[1]) &&
                   isfinite(vertex.worldNormal[2]) && isfinite(vertex.viewDirection[0]) &&
                   isfinite(vertex.viewDirection[1]) && isfinite(vertex.viewDirection[2]),
                   "clipped lighting payload remains finite")) return false;
    }
    if (!Check(foundInterpolatedNormal, "near clipping linearly interpolates the normal payload")) return false;
    if (!Check(foundNearViewDirection, "near clipping linearly interpolates view direction")) return false;
    model.vertices.At(0).normal[0] = std::numeric_limits<float>::quiet_NaN();
    vertices.Clear();
    if (!Check(!AppendLitModelPart(frame, draw, Part(), vertices, 32, error),
               "non-finite model normal is rejected")) return false;
    return Check(vertices.Count() == 0, "invalid input does not append partial lit vertices");
}

bool TestExtremeFiniteViewDirectionsDoNotOverflowDuringClipping() {
    const float large = 0.75f * 3.402823466e+38f;
    const Vec3 points[3] = {{-large, 0.0f, -large}, {large, 0.0f, large}, {0.0f, 1.0f, 2.0f}};
    const Vec3 normals[3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    frame.width = 16384;
    frame.height = 1;
    detail::DrawPacket draw = Draw(model);
    draw.cameraPosition = {0, 0, 0};
    draw.cameraTarget = {0, 0, 1};
    Array<ModelRenderVertex> vertices;
    String error;
    const bool appended = AppendLitModelPart(frame, draw, Part(), vertices, 32, error);
    if (!appended) {
        return Check(vertices.Count() == 0,
                     "extreme finite clipping failure leaves output unchanged");
    }
    if (!Check(vertices.Count() > 0, "extreme finite triangle produces clipped vertices")) return false;
    for (uint32_t i = 0; i < vertices.Count(); ++i) {
        const ModelRenderVertex& vertex = vertices.At(i);
        for (uint32_t axis = 0; axis < 3; ++axis) {
            if (!Check(isfinite(vertex.viewDirection[axis]),
                       "extreme finite view-direction interpolation stays finite")) return false;
            if (!Check(isfinite(vertex.worldNormal[axis]),
                       "extreme finite clipping preserves finite normals")) return false;
        }
        for (uint32_t component = 0; component < 4; ++component)
            if (!Check(isfinite(vertex.surface.position[component]),
                       "extreme finite clipping preserves finite projected positions")) return false;
    }
    return true;
}

bool TestCapacityAndAllocationFailuresPreserveOutput() {
    const Vec3 points[3] = {{-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0, 0.5f, 0}};
    const Vec3 normals[3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    ModelRenderVertex sentinel{};
    sentinel.surface.position[0] = 91.0f;
    vertices.Append(sentinel);
    String error;
    if (!Check(!AppendLitModelPart(frame, draw, Part(), vertices, 3, error),
               "vertex limit is checked before appending a triangle")) return false;
    if (!Check(vertices.Count() == 1 && vertices.At(0).surface.position[0] == 91.0f,
               "capacity failure preserves existing output")) return false;
    vertices.Clear();
    vertices.Append(sentinel);
    while (vertices.Count() < vertices.Capacity()) vertices.Append(sentinel);
    const uint32_t oldCount = vertices.Count();
    SetAllocationFailureAfterForTesting(0);
    const bool appended = AppendLitModelPart(frame, draw, Part(), vertices, 32, error);
    ResetAllocationFailureForTesting();
    if (!Check(!appended, "output allocation failure is reported")) return false;
    return Check(vertices.Count() == oldCount && vertices.At(0).surface.position[0] == 91.0f,
                 "allocation failure preserves existing output");
}

} // namespace

int main() {
    return TestVertexContractAndBasicLightingPayload() &&
           TestInverseScaleRotationAndNegativeScale() &&
           TestZeroNormalFallbacks() &&
           TestExtremeFiniteViewDirectionsDoNotOverflowDuringClipping() &&
           TestZeroNormalFallbackIsAlreadyInWorldSpace() &&
           TestClippingInterpolatesLightingAndRejectsNonFiniteInputs() &&
           TestCapacityAndAllocationFailuresPreserveOutput() ? 0 : 1;
}
