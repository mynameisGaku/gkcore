#include "render/ModelGeometry.h"

#include <math.h>

/**
 * 呼び出し側の出力を失敗時に保ちながら、照明用model頂点を作る。
 */
namespace gk::render
{
namespace
{

/**
 * 材質係数が0から1の有限値か調べる。
 */
bool IsFactor(float value)
{
    return isfinite(value) != 0 && value >= 0.0f && value <= 1.0f;
}

// namespace
}

namespace
{
/**
 * 指定triangle群を一度に組み立て、完成後にだけ出力へ追加する。
 */
bool AppendLitModelTrianglesInternal(const detail::FramePacket& frame, const detail::DrawPacket& draw, const ModelPartPlan& part, uint32_t firstIndex, const uint32_t* firstIndices, uint32_t triangleCount, bool contiguous, Array<ModelRenderVertex>& vertices, uint32_t vertexLimit, String& error)
{
    error.Clear();
    const bool normalMapping = part.normalTextureIndex >= 0;
    if (draw.kind != detail::DrawKind::Model || !draw.model || frame.width == 0 || frame.height == 0 || part.indexCount == 0 || part.indexCount % 3 != 0 || triangleCount == 0 || triangleCount > part.indexCount / 3 || (!contiguous && !firstIndices) || part.firstIndex > draw.model->indices.Count() || part.indexCount > draw.model->indices.Count() - part.firstIndex || vertices.Count() > vertexLimit || !IsFactor(part.metallicFactor) || !IsFactor(part.roughnessFactor) || !isfinite(part.alphaCutoff) || part.alphaCutoff < 0.0f || (part.alphaMask && part.alphaBlend) || part.normalTextureIndex < -1 || part.emissiveTextureIndex < -1 || !isfinite(part.emissiveFactorStrength[3]) || part.emissiveFactorStrength[3] < 0.0f || !IsFactor(part.occlusionStrength) || part.occlusionTextureIndex < -1)
    {
        error.Assign("The model part, material factors, or frame bounds are invalid");
        return false;
    }
    if (normalMapping && (static_cast<uint32_t>(part.normalTextureIndex) >= draw.model->textures.Count() || !draw.model->textures.At(static_cast<uint32_t>(part.normalTextureIndex)) || !isfinite(part.normalScale)))
    {
        error.Assign("The model normal texture slot or scale is invalid");
        return false;
    }
    // 基本色RGBAが描画可能な範囲か調べるloop。
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (!IsFactor(part.baseColorFactor[component]))
        {
            error.Assign("The model material base-color factor is invalid");
            return false;
        }
    }
    for (uint32_t component = 0; component < 3; ++component)
    {
        if (!IsFactor(part.emissiveFactorStrength[component]))
        {
            error.Assign("The model material emissive factor is invalid");
            return false;
        }
    }
    if (part.emissiveTextureIndex >= 0 && (static_cast<uint32_t>(part.emissiveTextureIndex) >= draw.model->textures.Count() || !draw.model->textures.At(static_cast<uint32_t>(part.emissiveTextureIndex))))
    {
        error.Assign("The model emissive texture slot is invalid");
        return false;
    }
    if (part.occlusionTextureIndex >= 0 && (static_cast<uint32_t>(part.occlusionTextureIndex) >= draw.model->textures.Count() || !draw.model->textures.At(static_cast<uint32_t>(part.occlusionTextureIndex))))
    {
        error.Assign("The model occlusion texture slot is invalid");
        return false;
    }

    // 各triangleで再利用するcamera・model変換。
    FWorldGeometryContext geometryContext{};
    if (!BuildWorldGeometryContext(draw, true, geometryContext, error))
        return false;

    // 出力を変更せず三角形ごとに組み立てる一時頂点配列。
    Array<ModelRenderVertex> candidate;
    // 指定順にtriangleを組み立てるloop。
    for (uint32_t triangle = 0; triangle < triangleCount; ++triangle)
    {
        // part内の三頂点indexの先頭。
        const uint32_t triangleFirstIndex = contiguous ? firstIndex + triangle * 3 : firstIndices[triangle];
        if (triangleFirstIndex < part.firstIndex || triangleFirstIndex - part.firstIndex > part.indexCount - 3 || (triangleFirstIndex - part.firstIndex) % 3 != 0)
        {
            error.Assign("The model triangle index is outside its aligned part range");
            return false;
        }
        // 投影前の三角形頂点と属性。
        WorldVertex source[3]{};
        // 3頂点のsource属性を読み取るloop。
        for (uint32_t corner = 0; corner < 3; ++corner)
        {
            // model頂点配列内の参照先。
            const uint32_t modelIndex = draw.model->indices.At(triangleFirstIndex + corner);
            if (modelIndex >= draw.model->vertices.Count())
            {
                error.Assign("The model part contains an invalid vertex index");
                return false;
            }
            // model payloadから借用する位置・法線・画像座標。
            const detail::ModelVertex& vertex = draw.model->vertices.At(modelIndex);
            source[corner].position = { vertex.position[0], vertex.position[1], vertex.position[2] };
            source[corner].normal = { vertex.normal[0], vertex.normal[1], vertex.normal[2] };
            source[corner].uv[0] = vertex.uv[0];
            source[corner].uv[1] = vertex.uv[1];
            source[corner].metallicRoughnessUv[0] = vertex.metallicRoughnessUv[0];
            source[corner].metallicRoughnessUv[1] = vertex.metallicRoughnessUv[1];
            source[corner].emissiveUv[0] = vertex.emissiveUv[0];
            source[corner].emissiveUv[1] = vertex.emissiveUv[1];
            source[corner].occlusionUv[0] = vertex.occlusionUv[0];
            source[corner].occlusionUv[1] = vertex.occlusionUv[1];
            if (normalMapping)
            {
                // 法線画像用の座標と、モデル空間の接線基底。
                source[corner].normalUv[0] = vertex.normalUv[0];
                source[corner].normalUv[1] = vertex.normalUv[1];
                for (uint32_t component = 0; component < 4; ++component)
                    source[corner].tangent[component] = vertex.tangent[component];
            }
        }
        // camera clipping後の頂点を受け取る固定配列。
        ProjectedWorldVertex projected[18];
        // clipping後に使うprojected要素数。
        uint32_t projectedCount = 0;
        if (!ProjectWorldTriangle(frame, draw, geometryContext, source, true, part.baseColorFactor, projected, projectedCount, error, normalMapping))
            return false;
        if (vertices.Count() > vertexLimit || candidate.Count() > vertexLimit - vertices.Count() || projectedCount > vertexLimit - vertices.Count() - candidate.Count())
        {
            error.Assign("The frame exceeds the dynamic vertex capacity");
            return false;
        }
        // 投影属性と材質値を描画頂点へまとめるloop。
        for (uint32_t i = 0; i < projectedCount; ++i)
        {
            // vertex bufferへ渡す統合済みmodel頂点。
            ModelRenderVertex vertex{};
            vertex.surface = projected[i].surface;
            // 法線と視線方向のxyz成分を複写するloop。
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                vertex.worldNormal[axis] = projected[i].worldNormal[axis];
                vertex.viewDirection[axis] = projected[i].viewDirection[axis];
            }
            vertex.metallicRoughness[0] = part.metallicFactor;
            vertex.metallicRoughness[1] = part.roughnessFactor;
            vertex.alphaMaskCutoff[0] = part.alphaBlend ? 2.0f : (part.alphaMask ? 1.0f : 0.0f);
            vertex.alphaMaskCutoff[1] = part.alphaCutoff;
            vertex.metallicRoughnessUv[0] = projected[i].metallicRoughnessUv[0];
            vertex.metallicRoughnessUv[1] = projected[i].metallicRoughnessUv[1];
            vertex.emissiveUv[0] = projected[i].emissiveUv[0];
            vertex.emissiveUv[1] = projected[i].emissiveUv[1];
            vertex.occlusionUvStrength[0] = projected[i].occlusionUv[0];
            vertex.occlusionUvStrength[1] = projected[i].occlusionUv[1];
            vertex.occlusionUvStrength[2] = part.occlusionTextureIndex >= 0 ? part.occlusionStrength : 0.0f;
            vertex.occlusionUvStrength[3] = 0.0f;
            for (uint32_t component = 0; component < 4; ++component)
                vertex.emissiveFactorStrength[component] = part.emissiveFactorStrength[component];
            if (normalMapping)
            {
                // 接線基底、法線UV、有効値と倍率をまとめるloop。
                for (uint32_t component = 0; component < 4; ++component)
                    vertex.worldTangent[component] = projected[i].worldTangent[component];
                vertex.normalUv[0] = projected[i].normalUv[0];
                vertex.normalUv[1] = projected[i].normalUv[1];
                vertex.normalParameters[0] = 1.0f;
                vertex.normalParameters[1] = part.normalScale;
            }
            if (!candidate.Append(vertex))
            {
                error.Assign("The model vertex allocation failed");
                return false;
            }
        }
    }

    if (candidate.Count() > UINT32_MAX - vertices.Count() || !vertices.AppendRange(candidate.Data(), candidate.Count()))
    {
        error.Assign("The frame vertex allocation failed");
        return false;
    }
    error.Clear();
    return true;
}
}

/**
 * model primitive全体を照明用頂点へ投影し、完成後に出力へ追加する。
 */
bool AppendLitModelPart(const detail::FramePacket& frame, const detail::DrawPacket& draw, const ModelPartPlan& part, Array<ModelRenderVertex>& vertices, uint32_t vertexLimit, String& error)
{
    return AppendLitModelTrianglesInternal(frame, draw, part, part.firstIndex, nullptr, part.indexCount / 3, true, vertices, vertexLimit, error);
}

/**
 * part内の指定順triangle群を照明用頂点へ投影し、完成後に出力へ追加する。
 */
bool AppendLitModelTriangles(const detail::FramePacket& frame, const detail::DrawPacket& draw, const ModelPartPlan& part, const uint32_t* firstIndices, uint32_t triangleCount, Array<ModelRenderVertex>& vertices, uint32_t vertexLimit, String& error)
{
    return AppendLitModelTrianglesInternal(frame, draw, part, 0, firstIndices, triangleCount, false, vertices, vertexLimit, error);
}

// namespace gk::render
}
