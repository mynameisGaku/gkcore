#include "render/ModelLocalGeometry.h"

#include <float.h>
#include <math.h>
#include "resources/TextureSampler.h"

/**
 * cameraやinstance transformを含まないmodel-local頂点展開。
 */
namespace gk::render
{
namespace
{

/**
 * 理由を設定し、失敗を返す。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * 値が有限で指定範囲に入るか確認する。
 */
bool IsFinite(float value)
{
    return isfinite(value) != 0 && value >= -FLT_MAX && value <= FLT_MAX;
}

/**
 * 最大成分で縮尺を整えて、3成分を安全に単位化する。
 */
bool Normalize(const float source[3], float output[3])
{
    const double x = source[0];
    const double y = source[1];
    const double z = source[2];
    const double largest = fmax(fmax(fabs(x), fabs(y)), fabs(z));
    if (!(largest > 0.0) || !isfinite(largest))
        return false;
    const double scaledX = x / largest;
    const double scaledY = y / largest;
    const double scaledZ = z / largest;
    const double length = sqrt(scaledX * scaledX + scaledY * scaledY + scaledZ * scaledZ);
    if (!(length > 0.0) || !isfinite(length))
        return false;
    output[0] = static_cast<float>(scaledX / length);
    output[1] = static_cast<float>(scaledY / length);
    output[2] = static_cast<float>(scaledZ / length);
    return IsFinite(output[0]) && IsFinite(output[1]) && IsFinite(output[2]);
}

/**
 * 材質値を通常の内蔵model shaderで扱える範囲か確認する。
 */
bool IsFactor(float value)
{
    return IsFinite(value) && value >= 0.0f && value <= 1.0f;
}

/**
 * 有効なmodel-local頂点payloadを作る。
 */
bool MakeLocalVertex(const detail::ModelVertex& source, uint32_t modelIndex, const ModelPartPlan& part, bool normalMapping, ModelRenderVertex& output, String& error)
{
    for (uint32_t component = 0; component < 3; ++component)
    {
        if (!IsFinite(source.position[component]) || !IsFinite(source.normal[component]))
            return Fail(error, "The model contains a non-finite local position or normal");
    }
    for (uint32_t coordinate = 0; coordinate < 2; ++coordinate)
    {
        if (!IsFinite(source.uv[coordinate]) || !IsFinite(source.metallicRoughnessUv[coordinate]) || !IsFinite(source.normalUv[coordinate]) || !IsFinite(source.emissiveUv[coordinate]) || !IsFinite(source.occlusionUv[coordinate]))
            return Fail(error, "The model contains a non-finite local texture coordinate");
    }

    float normal[3]{};
    if (!Normalize(source.normal, normal))
        return Fail(error, "The model contains a zero local normal that requires CPU geometry fallback");

    float tangent[3]{};
    float tangentW = 0.0f;
    if (normalMapping)
    {
        if (!IsFinite(source.tangent[0]) || !IsFinite(source.tangent[1]) || !IsFinite(source.tangent[2]) || (source.tangent[3] != 1.0f && source.tangent[3] != -1.0f))
            return Fail(error, "The model contains an invalid local normal-map tangent");
        if (!Normalize(source.tangent, tangent))
            return Fail(error, "The model contains a zero local normal-map tangent");
        const double cross[3] = { static_cast<double>(normal[1]) * tangent[2] - static_cast<double>(normal[2]) * tangent[1], static_cast<double>(normal[2]) * tangent[0] - static_cast<double>(normal[0]) * tangent[2], static_cast<double>(normal[0]) * tangent[1] - static_cast<double>(normal[1]) * tangent[0] };
        const double crossLengthSquared = cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2];
        if (!(crossLengthSquared > 1e-12))
            return Fail(error, "The model tangent is parallel to its local normal");
        tangentW = source.tangent[3];
    }

    output = {};
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        output.surface.position[axis] = source.position[axis];
        output.worldNormal[axis] = normal[axis];
        output.viewDirection[axis] = 0.0f;
    }
    // 次の姿勢評価で元頂点を参照できるよう、floatで正確なindexを保持する。
    output.surface.position[3] = static_cast<float>(modelIndex);
    for (uint32_t component = 0; component < 4; ++component)
        output.surface.color[component] = part.baseColorFactor[component];
    output.surface.uv[0] = source.uv[0];
    output.surface.uv[1] = source.uv[1];
    output.metallicRoughness[0] = part.metallicFactor;
    output.metallicRoughness[1] = part.roughnessFactor;
    output.alphaMaskCutoff[0] = part.alphaBlend ? 2.0f : (part.alphaMask ? 1.0f : 0.0f);
    output.alphaMaskCutoff[1] = part.alphaCutoff;
    output.metallicRoughnessUv[0] = source.metallicRoughnessUv[0];
    output.metallicRoughnessUv[1] = source.metallicRoughnessUv[1];
    output.emissiveUv[0] = source.emissiveUv[0];
    output.emissiveUv[1] = source.emissiveUv[1];
    for (uint32_t component = 0; component < 3; ++component)
        output.worldTangent[component] = tangent[component];
    output.worldTangent[3] = tangentW;
    output.normalUv[0] = source.normalUv[0];
    output.normalUv[1] = source.normalUv[1];
    output.normalParameters[0] = normalMapping ? 1.0f : 0.0f;
    output.normalParameters[1] = part.normalScale;
    for (uint32_t component = 0; component < 4; ++component)
        output.emissiveFactorStrength[component] = part.emissiveFactorStrength[component];
    output.occlusionUvStrength[0] = source.occlusionUv[0];
    output.occlusionUvStrength[1] = source.occlusionUv[1];
    output.occlusionUvStrength[2] = part.occlusionTextureIndex >= 0 ? part.occlusionStrength : 0.0f;
    output.occlusionUvStrength[3] = 0.0f;
    return true;
}

}

/**
 * primitive全体をlocal空間の照明頂点へ変換し、完成後にだけ追加する。
 */
bool AppendLocalModelPart(const detail::ModelResource& model, const ModelPartPlan& part, Array<ModelRenderVertex>& vertices, uint32_t vertexLimit, String& error)
{
    error.Clear();
    if (part.firstIndex > model.indices.Count() || part.indexCount == 0 || part.indexCount % 3 != 0 || part.indexCount > model.indices.Count() - part.firstIndex || vertices.Count() > vertexLimit || part.indexCount > vertexLimit - vertices.Count() || (part.alphaMask && part.alphaBlend) || !IsFactor(part.metallicFactor) || !IsFactor(part.roughnessFactor) || !IsFinite(part.alphaCutoff) || part.alphaCutoff < 0.0f || !IsFinite(part.normalScale) || !IsFinite(part.emissiveFactorStrength[3]) || part.emissiveFactorStrength[3] < 0.0f || !IsFactor(part.occlusionStrength) || !detail::IsTextureSamplerValid(part.baseColorSampler) || !detail::IsTextureSamplerValid(part.metallicRoughnessSampler) || !detail::IsTextureSamplerValid(part.normalSampler) || !detail::IsTextureSamplerValid(part.emissiveSampler) || !detail::IsTextureSamplerValid(part.occlusionSampler))
        return Fail(error, "The model part or material values are invalid for local geometry");
    if (model.vertices.Count() > (1u << 20))
        return Fail(error, "The model vertex indices cannot be represented exactly in local geometry");
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (!IsFactor(part.baseColorFactor[component]))
            return Fail(error, "The model base-color factor is invalid for local geometry");
        if (component < 3 && !IsFactor(part.emissiveFactorStrength[component]))
            return Fail(error, "The model emissive factor is invalid for local geometry");
    }
    const bool normalMapping = part.normalTextureIndex >= 0;
    if (part.textureIndex < -1 || part.metallicRoughnessTextureIndex < -1 || part.normalTextureIndex < -1 || part.emissiveTextureIndex < -1 || part.occlusionTextureIndex < -1)
        return Fail(error, "The model material texture slot is invalid for local geometry");
    const int32_t textureIndices[5] = { part.textureIndex, part.metallicRoughnessTextureIndex, part.normalTextureIndex, part.emissiveTextureIndex, part.occlusionTextureIndex };
    for (uint32_t i = 0; i < 5; ++i)
    {
        if (textureIndices[i] >= 0 && (static_cast<uint32_t>(textureIndices[i]) >= model.textures.Count() || !model.textures.At(static_cast<uint32_t>(textureIndices[i]))))
            return Fail(error, "The model material texture slot is invalid for local geometry");
    }
    Array<ModelRenderVertex> candidate;
    if (!candidate.Reserve(part.indexCount))
        return Fail(error, "Not enough memory to prepare local model geometry");
    for (uint32_t offset = 0; offset < part.indexCount; ++offset)
    {
        const uint32_t modelIndex = model.indices.At(part.firstIndex + offset);
        if (modelIndex >= model.vertices.Count())
            return Fail(error, "The model part contains an invalid vertex index");
        ModelRenderVertex vertex{};
        if (!MakeLocalVertex(model.vertices.At(modelIndex), modelIndex, part, normalMapping, vertex, error) || !candidate.Append(vertex))
        {
            if (error.Empty())
                error.Assign("Not enough memory to expand local model geometry");
            return false;
        }
    }
    if (candidate.Count() > UINT32_MAX - vertices.Count() || !vertices.AppendRange(candidate.Data(), candidate.Count()))
        return Fail(error, "Not enough memory to append local model geometry");
    error.Clear();
    return true;
}

// namespace gk::render
}
