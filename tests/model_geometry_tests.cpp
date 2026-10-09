#include "render/ModelGeometry.h"

#include "foundation/Memory.h"

#include <limits>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace
{

using namespace gk;
using namespace gk::render;

bool Check(bool value, const char* label)
{
    if (value)
        return true;
    fprintf(stderr, "model geometry test failed: %s\n", label);
    return false;
}

detail::ModelVertex MakeVertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
{
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

void MakeTriangle(detail::ModelResource& model, const Vec3 normals[3], const Vec3 points[3])
{
    for (uint32_t i = 0; i < 3; ++i)
        model.vertices.Append(MakeVertex(points[i].x, points[i].y, points[i].z, normals[i].x, normals[i].y, normals[i].z, static_cast<float>(i == 1), static_cast<float>(i == 2)));
    model.indices.Append(0);
    model.indices.Append(1);
    model.indices.Append(2);
}

void Frame(detail::FramePacket& frame)
{
    frame.width = 640;
    frame.height = 480;
}

detail::DrawPacket Draw(detail::ModelResource& model)
{
    detail::DrawPacket draw{};
    draw.kind = detail::DrawKind::Model;
    draw.model = &model;
    draw.cameraPosition = { 0.0f, 0.0f, -5.0f };
    draw.cameraTarget = { 0.0f, 0.0f, 0.0f };
    draw.modelScale = { 1.0f, 1.0f, 1.0f };
    return draw;
}

ModelPartPlan Part(uint32_t first = 0, uint32_t count = 3)
{
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

float Length(const float value[3])
{
    return sqrtf(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

bool Near(float a, float b, float tolerance = 0.0002f)
{
    return fabsf(a - b) <= tolerance;
}

bool TestVertexContractAndBasicLightingPayload()
{
    static_assert(sizeof(Vertex) == 40, "legacy vertex ABI remains unchanged");
    static_assert(sizeof(ModelRenderVertex) == 160, "lit model vertex ABI is 160 bytes");
    const Vec3 points[3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
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
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error), "valid lit model part is projected"))
        return false;
    if (!Check(vertices.Count() == 3, "one unclipped triangle emits three lit vertices"))
        return false;
    const ModelRenderVertex& vertex = vertices.At(0);
    if (!Check(Near(vertex.worldNormal[0], 0) && Near(vertex.worldNormal[1], 0) && Near(vertex.worldNormal[2], 1), "source normal is normalized in world space"))
        return false;
    if (!Check(Near(vertex.viewDirection[0], 0.5f) && Near(vertex.viewDirection[1], 0.5f) && Near(vertex.viewDirection[2], -5.0f), "view direction carries raw camera-minus-world coordinates"))
        return false;
    if (!Check(Near(vertex.metallicRoughness[0], 0.35f) && Near(vertex.metallicRoughness[1], 0.65f), "metallic and roughness are carried to the vertex"))
        return false;
    if (!Check(Near(vertex.emissiveFactorStrength[0], 0.0f) && Near(vertex.emissiveFactorStrength[1], 0.0f) && Near(vertex.emissiveFactorStrength[2], 0.0f) && Near(vertex.emissiveFactorStrength[3], 1.0f) && Near(vertex.emissiveUv[0], 0.0f) && Near(vertex.emissiveUv[1], 0.0f), "models without emission keep black zero-UV emission payload"))
        return false;
    if (!Check(Near(vertex.worldTangent[0], 0.0f) && Near(vertex.worldTangent[1], 0.0f) && Near(vertex.worldTangent[2], 0.0f) && Near(vertex.worldTangent[3], 0.0f) && Near(vertex.normalUv[0], 0.0f) && Near(vertex.normalUv[1], 0.0f) && Near(vertex.normalParameters[0], 0.0f) && Near(vertex.normalParameters[1], 0.0f), "models without a normal map keep the legacy lighting payload"))
        return false;
    const float expectedUv[3][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (!Check(vertices.At(i).surface.uv[0] == expectedUv[i][0] && vertices.At(i).surface.uv[1] == expectedUv[i][1], "indexed UVs remain attached to their projected corners"))
            return false;
        if (!Check(vertices.At(i).surface.color[0] == 0.25f && vertices.At(i).surface.color[1] == 0.5f && vertices.At(i).surface.color[2] == 0.75f && vertices.At(i).surface.color[3] == 0.4f, "linear base color remains in the legacy surface payload"))
            return false;
    }
    return true;
}

bool TestAlphaModePayloadValues()
{
    const Vec3 points[3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    ModelPartPlan part = Part();
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error) && Near(vertices.At(0).alphaMaskCutoff[0], 0.0f), "opaque mode uses zero alpha tag"))
        return false;
    part.alphaMask = true;
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error) && Near(vertices.At(0).alphaMaskCutoff[0], 1.0f), "mask mode uses one alpha tag"))
        return false;
    part.alphaMask = false;
    part.alphaBlend = true;
    vertices.Clear();
    return Check(AppendLitModelPart(frame, draw, part, vertices, 32, error) && Near(vertices.At(0).alphaMaskCutoff[0], 2.0f), "blend mode uses two alpha tag");
}

/**
 * 自己発光係数と独立UVを照明頂点へ保ち、失敗時は既存出力を残す。
 */
bool TestEmissivePayloadAndClipping()
{
    const Vec3 points[3] = { { -1, 0, -4.95f }, { 1, 0, 0 }, { 0, 1, 0 } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::ImageResource emissiveImage{};
    model.textures.Append(&emissiveImage);
    // 基本色UVとは異なる線形関係を持つ自己発光座標。
    for (uint32_t index = 0; index < model.vertices.Count(); ++index)
    {
        // 現在頂点に設定する独立UV。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.emissiveUv[0] = 2.0f + 3.0f * vertex.uv[0] - 2.0f * vertex.uv[1];
        vertex.emissiveUv[1] = -1.0f + vertex.uv[0] + 4.0f * vertex.uv[1];
    }
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    ModelPartPlan part = Part();
    part.emissiveTextureIndex = 0;
    part.emissiveFactorStrength[0] = 0.25f;
    part.emissiveFactorStrength[1] = 0.5f;
    part.emissiveFactorStrength[2] = 0.75f;
    part.emissiveFactorStrength[3] = 2.0f;
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error), "valid emissive payload survives near clipping"))
        return false;
    if (!Check(vertices.Count() == 6, "emissive near clipping emits the clipped quad"))
        return false;
    // 交点を含む全出力頂点で独立UVと未乗算係数を検査するloop。
    for (uint32_t index = 0; index < vertices.Count(); ++index)
    {
        // clipping後に対応するGPU頂点。
        const ModelRenderVertex& vertex = vertices.At(index);
        const float u = vertex.surface.uv[0];
        const float v = vertex.surface.uv[1];
        if (!Check(Near(vertex.emissiveUv[0], 2.0f + 3.0f * u - 2.0f * v) && Near(vertex.emissiveUv[1], -1.0f + u + 4.0f * v), "emissive UV uses the same clip-edge interpolation ratio"))
            return false;
        if (!Check(Near(vertex.emissiveFactorStrength[0], 0.25f) && Near(vertex.emissiveFactorStrength[1], 0.5f) && Near(vertex.emissiveFactorStrength[2], 0.75f) && Near(vertex.emissiveFactorStrength[3], 2.0f), "emissive factor and HDR strength remain separate raw values"))
            return false;
    }

    // 失敗時に維持する出力頂点sentinel。
    vertices.Clear();
    ModelRenderVertex sentinel{};
    sentinel.surface.position[0] = 91.0f;
    if (!vertices.Append(sentinel))
        return Check(false, "emissive geometry sentinel allocation failed");
    model.vertices.At(0).emissiveUv[0] = std::numeric_limits<float>::quiet_NaN();
    if (!Check(!AppendLitModelPart(frame, draw, part, vertices, 32, error), "non-finite emissive UV is rejected"))
        return false;
    return Check(vertices.Count() == 1 && Near(vertices.At(0).surface.position[0], 91.0f) && !error.Empty(), "invalid emissive input preserves prior output");
}

/**
 * 環境遮蔽UVと強度をclipping後も保持し、未使用時は無効値を渡す。
 */
bool TestOcclusionPayloadAndClipping()
{
    const Vec3 points[3] = { { -1, 0, -4.95f }, { 1, 0, 0 }, { 0, 1, 0 } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::ImageResource occlusionImage{};
    model.textures.Append(&occlusionImage);
    for (uint32_t index = 0; index < model.vertices.Count(); ++index)
    {
        // 現在の頂点へ設定する基本色と異なる環境遮蔽UV。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.occlusionUv[0] = -2.0f + 4.0f * vertex.uv[0] + vertex.uv[1];
        vertex.occlusionUv[1] = 3.0f - vertex.uv[0] + 2.0f * vertex.uv[1];
    }
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    ModelPartPlan part = Part();
    part.occlusionTextureIndex = 0;
    part.occlusionStrength = 0.35f;
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error), "valid occlusion payload survives near clipping"))
        return false;
    if (!Check(vertices.Count() == 6, "occlusion near clipping emits the clipped quad"))
        return false;
    for (uint32_t index = 0; index < vertices.Count(); ++index)
    {
        // clipping後の頂点と環境遮蔽UV・strength payload。
        const ModelRenderVertex& vertex = vertices.At(index);
        const float u = vertex.surface.uv[0];
        const float v = vertex.surface.uv[1];
        if (!Check(Near(vertex.occlusionUvStrength[0], -2.0f + 4.0f * u + v) && Near(vertex.occlusionUvStrength[1], 3.0f - u + 2.0f * v), "occlusion UV uses the same clip-edge interpolation ratio"))
            return false;
        if (!Check(Near(vertex.occlusionUvStrength[2], 0.35f) && Near(vertex.occlusionUvStrength[3], 0.0f), "occlusion strength occupies z and the reserved component stays zero"))
            return false;
    }

    part.occlusionTextureIndex = -1;
    part.occlusionStrength = 1.0f;
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error), "model without occlusion texture remains valid"))
        return false;
    if (!Check(Near(vertices.At(0).occlusionUvStrength[2], 0.0f) && Near(vertices.At(0).occlusionUvStrength[3], 0.0f), "missing occlusion texture disables the vertex strength"))
        return false;

    vertices.Clear();
    ModelRenderVertex sentinel{};
    sentinel.surface.position[0] = 91.0f;
    if (!vertices.Append(sentinel))
        return Check(false, "occlusion geometry sentinel allocation failed");
    model.vertices.At(0).occlusionUv[0] = std::numeric_limits<float>::quiet_NaN();
    if (!Check(!AppendLitModelPart(frame, draw, part, vertices, 32, error), "non-finite occlusion UV is rejected"))
        return false;
    return Check(vertices.Count() == 1 && Near(vertices.At(0).surface.position[0], 91.0f) && !error.Empty(), "invalid occlusion input preserves prior output");
}

/**
 * far planeで新しい交点を作る場合も遮蔽UVを位置と同じ比率で切る。
 */
bool TestOcclusionFarPlaneClipping()
{
    const Vec3 points[3] = { { -1, 0, 0 }, { 1, 0, 2000.0f }, { 0, 1, 0 } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::ImageResource occlusionImage{};
    model.textures.Append(&occlusionImage);
    for (uint32_t index = 0; index < model.vertices.Count(); ++index)
    {
        // far clippingで線形関係を保つ遮蔽UV。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.occlusionUv[0] = 1.0f + 2.0f * vertex.uv[0] - vertex.uv[1];
        vertex.occlusionUv[1] = -3.0f + vertex.uv[0] + 5.0f * vertex.uv[1];
    }
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    ModelPartPlan part = Part();
    part.occlusionTextureIndex = 0;
    part.occlusionStrength = 0.6f;
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error), "valid occlusion payload survives far clipping"))
        return false;
    if (!Check(vertices.Count() == 6, "occlusion far clipping emits the clipped quad"))
        return false;
    for (uint32_t index = 0; index < vertices.Count(); ++index)
    {
        // far plane交点を含む環境遮蔽頂点。
        const ModelRenderVertex& vertex = vertices.At(index);
        const float u = vertex.surface.uv[0];
        const float v = vertex.surface.uv[1];
        if (!Check(Near(vertex.occlusionUvStrength[0], 1.0f + 2.0f * u - v) && Near(vertex.occlusionUvStrength[1], -3.0f + u + 5.0f * v) && Near(vertex.occlusionUvStrength[2], 0.6f), "far-clipped occlusion UV uses the same edge interpolation ratio"))
            return false;
    }
    return true;
}

bool TestInverseScaleRotationAndNegativeScale()
{
    const Vec3 points[3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
    const Vec3 normals[3] = { { 1, 1, 0 }, { 1, 1, 0 }, { 1, 1, 0 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    draw.modelScale = { 2, 1, 1 };
    draw.modelRotation.z = 1.57079632679f;
    Array<ModelRenderVertex> vertices;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error), "nonuniform scale and rotation are supported"))
        return false;
    const float invSqrtFive = 0.4472135955f;
    if (!Check(Near(vertices.At(0).worldNormal[0], -2.0f * invSqrtFive) && Near(vertices.At(0).worldNormal[1], invSqrtFive) && Near(vertices.At(0).worldNormal[2], 0), "normal uses inverse scale before the model Euler rotation"))
        return false;
    draw.modelScale = { -1, 1, 1 };
    draw.modelRotation = { 0, 0, 0 };
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error), "negative nonzero scale is supported"))
        return false;
    return Check(Near(vertices.At(0).worldNormal[0], -0.70710678f) && Near(vertices.At(0).worldNormal[1], 0.70710678f), "negative scale changes the normal orientation through inverse scale");
}

bool TestZeroNormalFallbacks()
{
    const Vec3 points[3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
    const Vec3 zeros[3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
    detail::ModelResource model{};
    MakeTriangle(model, zeros, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error), "zero source normals use a face fallback"))
        return false;
    if (!Check(Near(vertices.At(0).worldNormal[2], 1), "face fallback follows triangle winding"))
        return false;
    model.vertices.At(1).position[0] = model.vertices.At(0).position[0];
    model.vertices.At(2).position[1] = model.vertices.At(0).position[1];
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error), "degenerate triangle has a finite fallback normal"))
        return false;
    return Check(Near(Length(vertices.At(0).worldNormal), 1) && isfinite(vertices.At(0).worldNormal[0]) && isfinite(vertices.At(0).worldNormal[1]) && isfinite(vertices.At(0).worldNormal[2]), "degenerate fallback is finite and unit length");
}

bool TestZeroNormalFallbackIsAlreadyInWorldSpace()
{
    const Vec3 points[3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
    const Vec3 zeros[3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
    detail::ModelResource model{};
    MakeTriangle(model, zeros, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    draw.modelScale = { 2.0f, 1.0f, 1.0f };
    draw.modelRotation.x = 1.57079632679f;
    Array<ModelRenderVertex> vertices;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error), "rotated and scaled zero-normal triangle uses world-face fallback"))
        return false;
    const ModelRenderVertex& vertex = vertices.At(0);
    return Check(Near(vertex.worldNormal[0], 0) && Near(vertex.worldNormal[1], -1) && Near(vertex.worldNormal[2], 0), "world-space fallback face normal is not rotated a second time");
}

bool TestClippingInterpolatesLightingAndRejectsNonFiniteInputs()
{
    const Vec3 points[3] = { { -1, 0, -4.95f }, { 1, 0, 0 }, { 0, 1, 0 } };
    const Vec3 normals[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    // 2つのUVが別の値でも、同じ交点で正しく補間されるかを確認する。
    for (uint32_t index = 0; index < model.vertices.Count(); ++index)
    {
        // 基本色のUVから独立した2次元の線形関係を持つ材質座標。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.metallicRoughnessUv[0] = 2.0f + 3.0f * vertex.uv[0] - 2.0f * vertex.uv[1];
        vertex.metallicRoughnessUv[1] = -1.0f + vertex.uv[0] + 4.0f * vertex.uv[1];
    }
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), vertices, 32, error), "triangle crossing near plane is clipped"))
        return false;
    if (!Check(vertices.Count() == 6, "near clipping triangulates the clipped quad"))
        return false;
    bool foundInterpolatedNormal = false;
    bool foundNearViewDirection = false;
    for (uint32_t i = 0; i < vertices.Count(); ++i)
    {
        const ModelRenderVertex& vertex = vertices.At(i);
        // 線形な関係は、切り詰めて作った交点にも成り立つ。
        const float expectedU = 2.0f + 3.0f * vertex.surface.uv[0] - 2.0f * vertex.surface.uv[1];
        const float expectedV = -1.0f + vertex.surface.uv[0] + 4.0f * vertex.surface.uv[1];
        if (!Check(Near(vertex.metallicRoughnessUv[0], expectedU) && Near(vertex.metallicRoughnessUv[1], expectedV), "near clipping preserves independent metallic-roughness UV"))
            return false;
        if (Near(vertex.surface.position[3], 0.1f))
        {
            foundInterpolatedNormal = foundInterpolatedNormal || (vertex.worldNormal[0] > 0.98f && vertex.worldNormal[1] > 0.005f && vertex.worldNormal[1] < 0.02f);
            const bool clippedOnFirstEdge = fabsf(vertex.surface.uv[1]) < 0.0001f;
            if (clippedOnFirstEdge)
            {
                const float worldX = -1.0f + 2.0f * vertex.surface.uv[0];
                foundNearViewDirection = foundNearViewDirection || (Near(vertex.viewDirection[0], -worldX) && Near(vertex.viewDirection[1], 0.0f) && Near(vertex.viewDirection[2], -0.1f));
            }
        }
        if (!Check(isfinite(vertex.worldNormal[0]) && isfinite(vertex.worldNormal[1]) && isfinite(vertex.worldNormal[2]) && isfinite(vertex.viewDirection[0]) && isfinite(vertex.viewDirection[1]) && isfinite(vertex.viewDirection[2]), "clipped lighting payload remains finite"))
            return false;
    }
    if (!Check(foundInterpolatedNormal, "near clipping linearly interpolates the normal payload"))
        return false;
    if (!Check(foundNearViewDirection, "near clipping linearly interpolates view direction"))
        return false;
    model.vertices.At(0).normal[0] = std::numeric_limits<float>::quiet_NaN();
    vertices.Clear();
    if (!Check(!AppendLitModelPart(frame, draw, Part(), vertices, 32, error), "non-finite model normal is rejected"))
        return false;
    if (!Check(vertices.Count() == 0, "invalid input does not append partial lit vertices"))
        return false;
    model.vertices.At(0).normal[0] = 1.0f;
    model.vertices.At(0).metallicRoughnessUv[1] = std::numeric_limits<float>::quiet_NaN();
    if (!Check(!AppendLitModelPart(frame, draw, Part(), vertices, 32, error), "non-finite metallic-roughness UV is rejected"))
        return false;
    return Check(vertices.Count() == 0, "invalid metallic-roughness UV leaves output unchanged");
}

/**
 * normal mapのUV、倍率、変換後の接線基底を出力へ保持する。
 */
bool TestNormalMapPayloadAndTransformedTangentFrame()
{
    const Vec3 points[3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::ImageResource normalImage{};
    model.textures.Append(&normalImage);
    // 各頂点へ基本色とは異なる法線UVと同一の接線基底を設定する。
    for (uint32_t index = 0; index < model.vertices.Count(); ++index)
    {
        // 画像座標とモデル空間接線を持つ頂点。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.normalUv[0] = 2.0f + 3.0f * vertex.uv[0] - 2.0f * vertex.uv[1];
        vertex.normalUv[1] = -1.0f + vertex.uv[0] + 4.0f * vertex.uv[1];
        vertex.tangent[0] = 1.0f;
        vertex.tangent[3] = 1.0f;
    }
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    ModelPartPlan part = Part();
    part.normalTextureIndex = 0;
    part.normalScale = 0.75f;
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error), "valid normal map payload is projected"))
        return false;
    if (!Check(vertices.Count() == 3, "one normal-mapped triangle emits three vertices"))
        return false;
    for (uint32_t index = 0; index < vertices.Count(); ++index)
    {
        // 出力頂点のnormal画像座標と変換後接線。
        const ModelRenderVertex& vertex = vertices.At(index);
        const detail::ModelVertex& source = model.vertices.At(index);
        if (!Check(Near(vertex.normalUv[0], source.normalUv[0]) && Near(vertex.normalUv[1], source.normalUv[1]), "normal map keeps its independent UV"))
            return false;
        if (!Check(Near(vertex.normalParameters[0], 1.0f) && Near(vertex.normalParameters[1], 0.75f), "normal map enabled flag and scale reach the vertex"))
            return false;
        if (!Check(Near(vertex.worldTangent[0], 1.0f) && Near(vertex.worldTangent[1], 0.0f) && Near(vertex.worldTangent[2], 0.0f) && Near(vertex.worldTangent[3], 1.0f), "identity transform keeps the tangent frame"))
            return false;
    }
    part.normalScale = 0.0f;
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error) && Near(vertices.At(0).normalParameters[1], 0.0f), "zero normal scale remains valid"))
        return false;
    part.normalScale = -1.0f;
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error) && Near(vertices.At(0).normalParameters[1], -1.0f), "negative normal scale remains valid"))
        return false;
    part.normalScale = 0.75f;

    // 非一様scale後も接線は位置と同じ直接変換で求める。
    for (uint32_t index = 0; index < 3; ++index)
    {
        // N=(1,1,0)と、その法線へ直交するモデル空間接線。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.normal[0] = 1.0f;
        vertex.normal[1] = 1.0f;
        vertex.normal[2] = 0.0f;
        vertex.tangent[0] = 1.0f;
        vertex.tangent[1] = -1.0f;
        vertex.tangent[2] = 0.0f;
        vertex.tangent[3] = 1.0f;
    }
    draw.modelScale = { 2.0f, 1.0f, 1.0f };
    draw.modelRotation.z = 1.57079632679f;
    vertices.Clear();
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error), "nonuniform scale and Euler rotation transform a normal map basis"))
        return false;
    const float inverseSqrtFive = 0.4472135955f;
    const ModelRenderVertex& rotated = vertices.At(0);
    if (!Check(Near(rotated.worldNormal[0], -2.0f * inverseSqrtFive) && Near(rotated.worldNormal[1], inverseSqrtFive) && Near(rotated.worldNormal[2], 0.0f), "normal uses inverse scale before model rotation"))
        return false;
    if (!Check(Near(rotated.worldTangent[0], inverseSqrtFive) && Near(rotated.worldTangent[1], 2.0f * inverseSqrtFive) && Near(rotated.worldTangent[2], 0.0f), "tangent uses direct scale, rotation, and Gram-Schmidt"))
        return false;
    if (!Check(Near(rotated.worldNormal[0] * rotated.worldTangent[0] + rotated.worldNormal[1] * rotated.worldTangent[1] + rotated.worldNormal[2] * rotated.worldTangent[2], 0.0f), "transformed tangent is perpendicular to transformed normal"))
        return false;

    // 鏡映scaleはTを反転し、bitangent handednessにも符号を反映する。
    draw.modelScale = { -1.0f, 1.0f, 1.0f };
    draw.modelRotation = { 0.0f, 0.0f, 0.0f };
    for (uint32_t index = 0; index < 3; ++index)
    {
        // 鏡映を調べるため法線をZ軸、接線をX軸へ戻す。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.normal[0] = 0.0f;
        vertex.normal[1] = 0.0f;
        vertex.normal[2] = 1.0f;
        vertex.tangent[0] = 1.0f;
        vertex.tangent[1] = 0.0f;
        vertex.tangent[2] = 0.0f;
        vertex.tangent[3] = 1.0f;
    }
    vertices.Clear();
    ModelPartPlan mirroredPart = Part(0, 3);
    mirroredPart.normalTextureIndex = 0;
    if (!Check(AppendLitModelPart(frame, draw, mirroredPart, vertices, 32, error), "negative runtime scale transforms a normal map basis"))
        return false;
    const ModelRenderVertex& mirrored = vertices.At(0);
    return Check(Near(mirrored.worldTangent[0], -1.0f) && Near(mirrored.worldTangent[1], 0.0f) && Near(mirrored.worldTangent[2], 0.0f) && Near(mirrored.worldTangent[3], -1.0f), "runtime reflection changes tangent handedness");
}

/**
 * near clippingでnormal UVと接線を他属性と同じ交点比率で補間する。
 */
bool TestNormalMapAttributesInterpolateThroughClipping()
{
    const Vec3 points[3] = { { -1, 0, -4.95f }, { 1, 0, 0 }, { 0, 1, 0 } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::ImageResource normalImage{};
    model.textures.Append(&normalImage);
    // UVと接線成分の異なる線形関係を切り詰め前の3頂点へ設定する。
    for (uint32_t index = 0; index < model.vertices.Count(); ++index)
    {
        // 基本色UVから独立した法線UVと、Z法線に直交する単位接線。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.normalUv[0] = 2.0f + 3.0f * vertex.uv[0] - 2.0f * vertex.uv[1];
        vertex.normalUv[1] = -1.0f + vertex.uv[0] + 4.0f * vertex.uv[1];
        vertex.tangent[0] = 1.0f - vertex.uv[0] - vertex.uv[1];
        vertex.tangent[1] = vertex.uv[0] - vertex.uv[1];
        vertex.tangent[3] = 1.0f;
    }
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> vertices;
    String error;
    ModelPartPlan part = Part();
    part.normalTextureIndex = 0;
    if (!Check(AppendLitModelPart(frame, draw, part, vertices, 32, error), "normal map attributes survive near clipping"))
        return false;
    if (!Check(vertices.Count() == 6, "near clipping emits a quad as six vertices"))
        return false;
    for (uint32_t index = 0; index < vertices.Count(); ++index)
    {
        // clipping後の独立UVと補間後の接線。
        const ModelRenderVertex& vertex = vertices.At(index);
        const float u = vertex.surface.uv[0];
        const float v = vertex.surface.uv[1];
        if (!Check(Near(vertex.normalUv[0], 2.0f + 3.0f * u - 2.0f * v) && Near(vertex.normalUv[1], -1.0f + u + 4.0f * v), "clipped normal UV uses the position edge intersection"))
            return false;
        if (!Check(Near(vertex.worldTangent[0], 1.0f - u - v) && Near(vertex.worldTangent[1], u - v) && Near(vertex.worldTangent[3], 1.0f), "clipped tangent uses the same edge intersection"))
            return false;
    }
    return true;
}

/**
 * 不正なnormal map基底や倍率では出力配列を変更しない。
 */
bool TestInvalidNormalMapBasisPreservesOutput()
{
    const Vec3 points[3] = { { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::ImageResource normalImage{};
    model.textures.Append(&normalImage);
    // 検証対象全頂点に正しい基底を与えるloop。
    for (uint32_t index = 0; index < model.vertices.Count(); ++index)
    {
        // 既定の有効なnormal map頂点。
        detail::ModelVertex& vertex = model.vertices.At(index);
        vertex.tangent[0] = 1.0f;
        vertex.tangent[3] = 1.0f;
    }
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    ModelPartPlan part = Part();
    part.normalTextureIndex = 0;
    Array<ModelRenderVertex> vertices;
    ModelRenderVertex sentinel{};
    sentinel.surface.position[0] = 91.0f;
    vertices.Append(sentinel);
    String error;

    // 不正な入力でも呼び出し側の既存頂点を保持する。
    const auto expectRejected = [&](const char* label) -> bool
    {
        const bool rejected = !AppendLitModelPart(frame, draw, part, vertices, 32, error);
        return Check(rejected && vertices.Count() == 1 && Near(vertices.At(0).surface.position[0], 91.0f) && !error.Empty(), label);
    };
    model.vertices.At(0).tangent[3] = 0.999f;
    if (!expectRejected("tangent handedness must be exactly plus or minus one"))
        return false;
    model.vertices.At(0).tangent[3] = 1.0f;
    model.vertices.At(1).tangent[3] = -1.0f;
    if (!expectRejected("a triangle requires consistent tangent handedness"))
        return false;
    model.vertices.At(1).tangent[3] = 1.0f;
    model.vertices.At(0).tangent[0] = 0.0f;
    if (!expectRejected("zero tangent is rejected"))
        return false;
    model.vertices.At(0).tangent[0] = 0.0f;
    model.vertices.At(0).tangent[2] = 1.0f;
    if (!expectRejected("tangent parallel to the normal is rejected"))
        return false;
    model.vertices.At(0).normal[0] = 1.0f;
    model.vertices.At(0).normal[1] = 1.0f;
    model.vertices.At(0).normal[2] = 0.0f;
    model.vertices.At(0).tangent[0] = 1.0f;
    model.vertices.At(0).tangent[1] = 1.0f;
    model.vertices.At(0).tangent[2] = 0.0f;
    draw.modelScale = { 2.0f, 1.0f, 1.0f };
    if (!expectRejected("source-parallel tangent remains invalid under nonuniform scale"))
        return false;
    model.vertices.At(0).normal[0] = 0.0f;
    model.vertices.At(0).normal[1] = 0.0f;
    model.vertices.At(0).normal[2] = 1.0f;
    model.vertices.At(0).tangent[0] = 1.0f;
    model.vertices.At(0).tangent[1] = 0.0f;
    model.vertices.At(0).tangent[2] = 0.0f;
    draw.modelScale = { 1.0f, 1.0f, 1.0f };
    model.vertices.At(0).normalUv[0] = std::numeric_limits<float>::quiet_NaN();
    if (!expectRejected("non-finite normal UV is rejected"))
        return false;
    model.vertices.At(0).normalUv[0] = 0.0f;
    part.normalScale = std::numeric_limits<float>::infinity();
    if (!expectRejected("non-finite normal scale is rejected"))
        return false;
    part.normalScale = 1.0f;
    part.normalTextureIndex = 1;
    if (!expectRejected("normal texture slot must exist"))
        return false;
    part.normalTextureIndex = 0;
    model.vertices.At(0).tangent[1] = std::numeric_limits<float>::quiet_NaN();
    if (!expectRejected("non-finite tangent is rejected"))
        return false;
    return true;
}

bool TestExtremeFiniteViewDirectionsDoNotOverflowDuringClipping()
{
    const float large = 0.75f * 3.402823466e+38f;
    const Vec3 points[3] = { { -large, 0.0f, -large }, { large, 0.0f, large }, { 0.0f, 1.0f, 2.0f } };
    const Vec3 normals[3] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    frame.width = 16384;
    frame.height = 1;
    detail::DrawPacket draw = Draw(model);
    draw.cameraPosition = { 0, 0, 0 };
    draw.cameraTarget = { 0, 0, 1 };
    Array<ModelRenderVertex> vertices;
    String error;
    const bool appended = AppendLitModelPart(frame, draw, Part(), vertices, 32, error);
    if (!appended)
    {
        return Check(vertices.Count() == 0, "extreme finite clipping failure leaves output unchanged");
    }
    if (!Check(vertices.Count() > 0, "extreme finite triangle produces clipped vertices"))
        return false;
    for (uint32_t i = 0; i < vertices.Count(); ++i)
    {
        const ModelRenderVertex& vertex = vertices.At(i);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!Check(isfinite(vertex.viewDirection[axis]), "extreme finite view-direction interpolation stays finite"))
                return false;
            if (!Check(isfinite(vertex.worldNormal[axis]), "extreme finite clipping preserves finite normals"))
                return false;
        }
        for (uint32_t component = 0; component < 4; ++component)
            if (!Check(isfinite(vertex.surface.position[component]), "extreme finite clipping preserves finite projected positions"))
                return false;
    }
    return true;
}

bool TestCapacityAndAllocationFailuresPreserveOutput()
{
    const Vec3 points[3] = { { -0.5f, -0.5f, 0 }, { 0.5f, -0.5f, 0 }, { 0, 0.5f, 0 } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
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
    if (!Check(!AppendLitModelPart(frame, draw, Part(), vertices, 3, error), "vertex limit is checked before appending a triangle"))
        return false;
    if (!Check(vertices.Count() == 1 && vertices.At(0).surface.position[0] == 91.0f, "capacity failure preserves existing output"))
        return false;
    vertices.Clear();
    vertices.Append(sentinel);
    while (vertices.Count() < vertices.Capacity())
        vertices.Append(sentinel);
    const uint32_t oldCount = vertices.Count();
    SetAllocationFailureAfterForTesting(0);
    const bool appended = AppendLitModelPart(frame, draw, Part(), vertices, 32, error);
    ResetAllocationFailureForTesting();
    if (!Check(!appended, "output allocation failure is reported"))
        return false;
    return Check(vertices.Count() == oldCount && vertices.At(0).surface.position[0] == 91.0f, "allocation failure preserves existing output");
}

/**
 * 三角形を複数回追加しても頂点順と既存出力を保ち、配列を段階的に拡張する。
 */
bool TestRepeatedModelPartAppendsRetainGeometryAndGrowCapacity()
{
    const Vec3 points[3] = { { -0.5f, -0.5f, 0 }, { 0.5f, -0.5f, 0 }, { 0, 0.5f, 0 } };
    const Vec3 normals[3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    Array<ModelRenderVertex> reference;
    Array<ModelRenderVertex> streamed;
    String error;
    if (!Check(AppendLitModelPart(frame, draw, Part(), reference, 128, error), "reference triangle is generated"))
        return false;
    ModelRenderVertex sentinel{};
    sentinel.surface.position[0] = 91.0f;
    if (!Check(streamed.Append(sentinel), "streamed output accepts its existing prefix"))
        return false;
    const uint32_t prefixCapacity = streamed.Capacity();
    constexpr uint32_t appendCount = 16;
    for (uint32_t append = 0; append < appendCount; ++append)
        if (!Check(AppendLitModelPart(frame, draw, Part(), streamed, 128, error), "repeated triangle append succeeds"))
            return false;
    if (!Check(streamed.Count() == 1 + appendCount * reference.Count(), "repeated appends retain every projected vertex"))
        return false;
    if (!Check(streamed.Capacity() > streamed.Count() && streamed.Capacity() > prefixCapacity, "streamed output grows capacity geometrically"))
        return false;
    if (!Check(streamed.At(0).surface.position[0] == 91.0f, "repeated appends preserve the existing prefix"))
        return false;
    for (uint32_t append = 0; append < appendCount; ++append)
    {
        for (uint32_t vertex = 0; vertex < reference.Count(); ++vertex)
        {
            const ModelRenderVertex& actual = streamed.At(1 + append * reference.Count() + vertex);
            const ModelRenderVertex& expected = reference.At(vertex);
            if (!Check(memcmp(&actual, &expected, sizeof(ModelRenderVertex)) == 0, "repeated appends preserve exact vertex payload and order"))
                return false;
        }
    }
    return true;
}

/**
 * 指定順のtriangle batchが単体描画と一致し、範囲・容量エラー時に出力を保つ。
 */
bool TestModelTriangleBatchOrderAndAtomicFailure()
{
    const Vec3 points[6] = { { -0.8f, -0.5f, 0 }, { -0.2f, -0.5f, 0 }, { -0.5f, 0.5f, 0 }, { 0.2f, -0.5f, 0 }, { 0.8f, -0.5f, 0 }, { 0.5f, 0.5f, 0 } };
    const Vec3 normals[6] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    detail::ModelResource model{};
    for (uint32_t i = 0; i < 6; ++i)
        model.vertices.Append(MakeVertex(points[i].x, points[i].y, points[i].z, normals[i].x, normals[i].y, normals[i].z, static_cast<float>(i % 3 == 1), static_cast<float>(i % 3 == 2)));
    for (uint32_t i = 0; i < 6; ++i)
        model.indices.Append(i);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    const ModelPartPlan wholePart = Part(0, 6);
    const uint32_t shuffledFirstIndices[2] = { 3, 0 };
    Array<ModelRenderVertex> batch;
    String error;
    if (!Check(AppendLitModelTriangles(frame, draw, wholePart, shuffledFirstIndices, 2, batch, 64, error), "shuffled triangle batch succeeds"))
        return false;
    Array<ModelRenderVertex> firstTriangle;
    Array<ModelRenderVertex> secondTriangle;
    if (!Check(AppendLitModelPart(frame, draw, Part(3, 3), firstTriangle, 64, error), "first reference triangle succeeds") || !Check(AppendLitModelPart(frame, draw, Part(0, 3), secondTriangle, 64, error), "second reference triangle succeeds"))
        return false;
    if (!Check(batch.Count() == firstTriangle.Count() + secondTriangle.Count(), "batch emits all selected triangle vertices"))
        return false;
    for (uint32_t i = 0; i < firstTriangle.Count(); ++i)
        if (!Check(memcmp(&batch.At(i), &firstTriangle.At(i), sizeof(ModelRenderVertex)) == 0, "batch follows caller triangle order"))
            return false;
    for (uint32_t i = 0; i < secondTriangle.Count(); ++i)
        if (!Check(memcmp(&batch.At(firstTriangle.Count() + i), &secondTriangle.At(i), sizeof(ModelRenderVertex)) == 0, "batch preserves later triangle payload"))
            return false;

    ModelRenderVertex sentinel{};
    sentinel.surface.position[0] = 91.0f;
    Array<ModelRenderVertex> unchanged;
    if (!Check(unchanged.Append(sentinel), "atomic batch output starts with a sentinel"))
        return false;
    const uint32_t invalidFirstIndices[2] = { 0, 1 };
    if (!Check(!AppendLitModelTriangles(frame, draw, wholePart, invalidFirstIndices, 2, unchanged, 64, error), "misaligned triangle index is rejected"))
        return false;
    if (!Check(unchanged.Count() == 1 && unchanged.At(0).surface.position[0] == 91.0f, "invalid batch preserves prior output"))
        return false;
    if (!Check(!AppendLitModelTriangles(frame, draw, wholePart, shuffledFirstIndices, 2, unchanged, 4, error), "batch vertex capacity is enforced"))
        return false;
    return Check(unchanged.Count() == 1 && unchanged.At(0).surface.position[0] == 91.0f, "capacity failure preserves prior output");
}

/**
 * 共有contextでも従来のprojection結果を保ち、triangle群でのCPU時間を記録する。
 */
bool TestReusableTransformContextAndManyTriangles()
{
    const Vec3 points[3] = { { -0.7f, -0.4f, 0.2f }, { 0.8f, -0.5f, 0.4f }, { 0.1f, 0.9f, -0.3f } };
    const Vec3 normals[3] = { { 0.2f, 0.1f, 1.0f }, { 0.0f, 0.3f, 1.0f }, { -0.1f, 0.2f, 1.0f } };
    detail::ModelResource model{};
    MakeTriangle(model, normals, points);
    detail::FramePacket frame{};
    Frame(frame);
    detail::DrawPacket draw = Draw(model);
    draw.cameraPosition = { 1.2f, -0.8f, -5.0f };
    draw.cameraTarget = { 0.2f, 0.4f, 0.0f };
    draw.modelScale = { 1.7f, 0.8f, 1.2f };
    draw.modelRotation = { 0.13f, -0.21f, 0.17f };
    draw.modelPosition = { -0.3f, 0.2f, 0.1f };
    WorldVertex source[3]{};
    for (uint32_t corner = 0; corner < 3; ++corner)
    {
        const detail::ModelVertex& vertex = model.vertices.At(corner);
        source[corner].position = { vertex.position[0], vertex.position[1], vertex.position[2] };
        source[corner].normal = { vertex.normal[0], vertex.normal[1], vertex.normal[2] };
        source[corner].uv[0] = vertex.uv[0];
        source[corner].uv[1] = vertex.uv[1];
    }
    String error;
    FWorldGeometryContext context{};
    if (!Check(BuildWorldGeometryContext(draw, true, context, error), "model/camera context builds"))
        return false;
    ProjectedWorldVertex independent[18]{};
    ProjectedWorldVertex reused[18]{};
    uint32_t independentCount = 0;
    uint32_t reusedCount = 0;
    if (!Check(ProjectWorldTriangle(frame, draw, source, true, true, nullptr, independent, independentCount, error), "per-call projection is valid") || !Check(ProjectWorldTriangle(frame, draw, context, source, true, nullptr, reused, reusedCount, error), "shared-context projection is valid"))
        return false;
    bool sameProjection = independentCount == reusedCount;
    for (uint32_t vertex = 0; sameProjection && vertex < independentCount; ++vertex)
    {
        sameProjection = memcmp(independent[vertex].surface.position, reused[vertex].surface.position, sizeof(independent[vertex].surface.position)) == 0 && memcmp(independent[vertex].surface.color, reused[vertex].surface.color, sizeof(independent[vertex].surface.color)) == 0 && memcmp(independent[vertex].surface.uv, reused[vertex].surface.uv, sizeof(independent[vertex].surface.uv)) == 0 && memcmp(independent[vertex].worldNormal, reused[vertex].worldNormal, sizeof(independent[vertex].worldNormal)) == 0 && memcmp(independent[vertex].viewDirection, reused[vertex].viewDirection, sizeof(independent[vertex].viewDirection)) == 0 && memcmp(independent[vertex].metallicRoughnessUv, reused[vertex].metallicRoughnessUv, sizeof(independent[vertex].metallicRoughnessUv)) == 0 && memcmp(independent[vertex].normalUv, reused[vertex].normalUv, sizeof(independent[vertex].normalUv)) == 0 && memcmp(independent[vertex].worldTangent, reused[vertex].worldTangent, sizeof(independent[vertex].worldTangent)) == 0 && memcmp(independent[vertex].emissiveUv, reused[vertex].emissiveUv, sizeof(independent[vertex].emissiveUv)) == 0 && memcmp(independent[vertex].occlusionUv, reused[vertex].occlusionUv, sizeof(independent[vertex].occlusionUv)) == 0;
    }
    if (!Check(sameProjection, "shared context preserves exact projected attributes"))
        return false;

    // 実モデルで多いtriangle数に合わせ、両経路の処理時間を測る。
    constexpr uint32_t triangleCount = 84000;
    const clock_t independentStart = clock();
    for (uint32_t triangle = 0; triangle < triangleCount; ++triangle)
    {
        ProjectedWorldVertex projected[18];
        uint32_t projectedCount = 0;
        if (!ProjectWorldTriangle(frame, draw, source, true, true, nullptr, projected, projectedCount, error))
            return Check(false, "per-call projection benchmark remains valid");
    }
    const clock_t independentEnd = clock();
    const clock_t reusedStart = clock();
    for (uint32_t triangle = 0; triangle < triangleCount; ++triangle)
    {
        ProjectedWorldVertex projected[18];
        uint32_t projectedCount = 0;
        if (!ProjectWorldTriangle(frame, draw, context, source, true, nullptr, projected, projectedCount, error))
            return Check(false, "shared-context projection benchmark remains valid");
    }
    const clock_t reusedEnd = clock();
    const double independentMilliseconds = 1000.0 * static_cast<double>(independentEnd - independentStart) / CLOCKS_PER_SEC;
    const double reusedMilliseconds = 1000.0 * static_cast<double>(reusedEnd - reusedStart) / CLOCKS_PER_SEC;
    fprintf(stdout, "model geometry 84000 triangles: per-call %.3f ms, reused context %.3f ms\n", independentMilliseconds, reusedMilliseconds);
    return true;
}

// namespace
}

int main()
{
    return TestVertexContractAndBasicLightingPayload() && TestAlphaModePayloadValues() && TestEmissivePayloadAndClipping() && TestOcclusionPayloadAndClipping() && TestOcclusionFarPlaneClipping() && TestInverseScaleRotationAndNegativeScale() && TestZeroNormalFallbacks() && TestExtremeFiniteViewDirectionsDoNotOverflowDuringClipping() && TestZeroNormalFallbackIsAlreadyInWorldSpace() && TestClippingInterpolatesLightingAndRejectsNonFiniteInputs() && TestNormalMapPayloadAndTransformedTangentFrame() && TestNormalMapAttributesInterpolateThroughClipping() && TestInvalidNormalMapBasisPreservesOutput() && TestCapacityAndAllocationFailuresPreserveOutput() && TestRepeatedModelPartAppendsRetainGeometryAndGrowCapacity() && TestModelTriangleBatchOrderAndAtomicFailure() && TestReusableTransformContextAndManyTriangles() ? 0 : 1;
}
