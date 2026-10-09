// SPDX-License-Identifier: NOASSERTION
#include "render/ModelPoseGeometry.h"

#include <float.h>
#include <math.h>

/**
 * 描画時点のmodel姿勢をGPU用頂点payloadへ変換する処理。
 */
namespace gk::render
{
namespace
{

// pose streamで扱う位置の上限。shader内の相対座標計算で精度を保つ。
constexpr double kMaximumPosition = 100000000.0;

/**
 * float値が有限であることを確認する。
 */
bool IsFiniteFloat(float value)
{
    return isfinite(value) != 0 && value >= -FLT_MAX && value <= FLT_MAX;
}

/**
 * 3成分をdouble精度で安全に単位化する。
 */
bool NormalizeVector(const float source[3], float output[3])
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
    return IsFiniteFloat(output[0]) && IsFiniteFloat(output[1]) && IsFiniteFloat(output[2]);
}

/**
 * 法線マップbasisの法線・接線が有限で直交可能か調べる。
 */
bool HasValidTangentBasis(const detail::ModelVertex& vertex)
{
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (!IsFiniteFloat(vertex.normal[i]) || !IsFiniteFloat(vertex.tangent[i]))
            return false;
    }
    if (!IsFiniteFloat(vertex.tangent[3]) || (vertex.tangent[3] != 1.0f && vertex.tangent[3] != -1.0f))
        return false;
    float normal[3]{};
    float tangent[3]{};
    if (!NormalizeVector(vertex.normal, normal) || !NormalizeVector(vertex.tangent, tangent))
        return false;
    const double crossX = static_cast<double>(normal[1]) * tangent[2] - static_cast<double>(normal[2]) * tangent[1];
    const double crossY = static_cast<double>(normal[2]) * tangent[0] - static_cast<double>(normal[0]) * tangent[2];
    const double crossZ = static_cast<double>(normal[0]) * tangent[1] - static_cast<double>(normal[1]) * tangent[0];
    return crossX * crossX + crossY * crossY + crossZ * crossZ > 1.0e-12;
}

/**
 * pose頂点を検証して各属性を正規化する。
 */
bool PackVertex(const detail::ModelVertex& source, FModelPoseVertex& output)
{
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (!IsFiniteFloat(source.position[i]) || fabs(static_cast<double>(source.position[i])) > kMaximumPosition || !IsFiniteFloat(source.normal[i]) || !IsFiniteFloat(source.tangent[i]))
            return false;
    }
    if (!IsFiniteFloat(source.tangent[3]))
        return false;
    FModelPoseVertex candidate{};
    for (uint32_t i = 0; i < 3; ++i)
        candidate.position[i] = source.position[i];
    if (!NormalizeVector(source.normal, candidate.normal))
        return false;
    const double largestTangent = fmax(fmax(fabs(static_cast<double>(source.tangent[0])), fabs(static_cast<double>(source.tangent[1]))), fabs(static_cast<double>(source.tangent[2])));
    if (largestTangent > 0.0 && !NormalizeVector(source.tangent, candidate.tangent))
        return false;
    candidate.tangent[3] = source.tangent[3];
    output = candidate;
    return true;
}

}

bool AppendModelPoseVertices(const detail::ModelResource& model, Array<FModelPoseVertex>& output, uint32_t vertexLimit, String& error)
{
    error.Clear();
    if (output.Count() > vertexLimit || model.vertices.Count() > vertexLimit - output.Count())
    {
        error.Assign("model pose vertex limit exceeded");
        return false;
    }
    Array<FModelPoseVertex> candidate;
    if (!candidate.Reserve(model.vertices.Count()))
    {
        error.Assign("model pose vertex allocation failed");
        return false;
    }
    for (uint32_t i = 0; i < model.vertices.Count(); ++i)
    {
        FModelPoseVertex vertex{};
        if (!PackVertex(model.vertices.At(i), vertex))
        {
            error.Assign("model pose vertex contains invalid or out-of-range attributes");
            return false;
        }
        if (!candidate.Append(vertex))
        {
            error.Assign("model pose vertex allocation failed");
            return false;
        }
    }
    if (!output.AppendRange(candidate.Data(), candidate.Count()))
    {
        error.Assign("model pose output allocation failed");
        return false;
    }
    error.Clear();
    return true;
}

bool IsModelPosePartGpuSafe(const detail::ModelResource& model, const ModelPartPlan& part)
{
    if (part.normalTextureIndex < -1 || part.indexCount == 0 || part.indexCount % 3 != 0 || part.firstIndex > model.indices.Count() || part.indexCount > model.indices.Count() - part.firstIndex)
        return false;
    const bool normalMapping = part.normalTextureIndex >= 0;
    for (uint32_t i = 0; i < part.indexCount; ++i)
    {
        const uint32_t vertexIndex = model.indices.At(part.firstIndex + i);
        if (vertexIndex >= model.vertices.Count())
            return false;
        if (normalMapping && !HasValidTangentBasis(model.vertices.At(vertexIndex)))
            return false;
    }
    return true;
}

// namespace gk::render
}
