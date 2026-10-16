// SPDX-License-Identifier: NOASSERTION
#include "tests/support/ModelSkinSurfaceCapture.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

namespace gk::testing
{
namespace
{

using namespace gk::model::animation;

/**
 * 位置と補正をfloatへ保存しても有限か検査する。
 */
bool IsFloatPosition(const double value[3])
{
    for (uint32_t axis = 0u; axis < 3u; ++axis)
    {
        if (!isfinite(value[axis]) || fabs(value[axis]) > FLT_MAX)
        {
            return false;
        }
    }
    return true;
}

/**
 * 現在の身体形状がJSONへ有限値として保存できるか調べる。
 */
bool ValidateShapes(const gk::Array<FModelSecondaryMotionCollisionShape>& shapes)
{
    for (uint32_t index = 0u; index < shapes.Count(); ++index)
    {
        const auto& shape = shapes.At(index);
        if (!isfinite(shape.start.x) || !isfinite(shape.start.y) || !isfinite(shape.start.z) || !isfinite(shape.end.x) || !isfinite(shape.end.y) || !isfinite(shape.end.z) || !isfinite(shape.radius) || shape.radius < 0.0f)
        {
            return false;
        }
    }
    return true;
}

/**
 * ID順の補正を探し、実描画と同じfloatへの保存を行う。
 */
FModelSkinSurfaceCapture::FPoint ReadRenderedPoint(uint32_t positionId, const FModelSparsePoseGeometry& reference, const gk::Array<FModelSkinPointCorrection>& corrections)
{
    FModelSkinSurfaceCapture::FPoint point{};
    point.positionId = positionId;
    for (uint32_t axis = 0u; axis < 3u; ++axis)
    {
        point.position[axis] = reference.positions.At(positionId).value[axis];
    }
    uint32_t low = 0u;
    uint32_t high = corrections.Count();
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2u;
        if (corrections.At(middle).positionId < positionId)
        {
            low = middle + 1u;
        }
        else
        {
            high = middle;
        }
    }
    if (low < corrections.Count() && corrections.At(low).positionId == positionId)
    {
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            point.position[axis] = static_cast<float>(corrections.At(low).position[axis]);
        }
    }
    return point;
}

/**
 * 名前を引用符と制御文字も含めてJSON文字列へ変換する。
 */
bool AppendJsonString(const char* value, gk::String& json)
{
    if (!json.Append("\""))
    {
        return false;
    }
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor; ++cursor)
    {
        const unsigned char byte = *cursor;
        if (byte == '"' || byte == '\\')
        {
            const char escaped[3] = { '\\', static_cast<char>(byte), '\0' };
            if (!json.Append(escaped))
            {
                return false;
            }
        }
        else if (byte < 0x20u)
        {
            char escaped[7]{};
            snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(byte));
            if (!json.Append(escaped))
            {
                return false;
            }
        }
        else
        {
            const char character = static_cast<char>(byte);
            if (!json.Append(&character, 1u))
            {
                return false;
            }
        }
    }
    return json.Append("\"");
}

/**
 * 元IDとfloat保存済み位置を、精度を落とさずJSON配列へ書く。
 */
bool AppendPoints(const gk::Array<FModelSkinSurfaceCapture::FPoint>& points, gk::String& json)
{
    if (!json.Append("["))
    {
        return false;
    }
    for (uint32_t index = 0u; index < points.Count(); ++index)
    {
        const auto& point = points.At(index);
        char text[256]{};
        const int count = snprintf(text, sizeof(text), "%s{\"positionId\":%u,\"position\":[%.17g,%.17g,%.17g]}", index ? "," : "", point.positionId, static_cast<double>(point.position[0]), static_cast<double>(point.position[1]), static_cast<double>(point.position[2]));
        if (count < 0 || static_cast<size_t>(count) >= sizeof(text) || !json.Append(text, static_cast<uint32_t>(count)))
        {
            return false;
        }
    }
    return json.Append("]");
}

/**
 * ID列を順序を変えずJSON配列へ書く。
 */
bool AppendIndices(const gk::Array<uint32_t>& indices, gk::String& json)
{
    if (!json.Append("["))
    {
        return false;
    }
    for (uint32_t index = 0u; index < indices.Count(); ++index)
    {
        if ((index > 0u && !json.Append(",")) || !json.AppendUnsigned(indices.At(index)))
        {
            return false;
        }
    }
    return json.Append("]");
}

/**
 * 既に保存したsnapshotの位置をJSONの有限値契約で確認する。
 */
bool ArePointsFinite(const gk::Array<FModelSkinSurfaceCapture::FPoint>& points)
{
    for (uint32_t index = 0u; index < points.Count(); ++index)
    {
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            if (!isfinite(points.At(index).position[axis]))
            {
                return false;
            }
        }
    }
    return true;
}

}

void FModelSkinSurfaceCapture::MoveFrom(FModelSkinSurfaceCapture& source)
{
    if (this == &source)
    {
        return;
    }
    bodyMeshName.MoveFrom(source.bodyMeshName);
    bodyNode = source.bodyNode;
    bodySegmentIndices.MoveFrom(source.bodySegmentIndices);
    bodyPoints.MoveFrom(source.bodyPoints);
    trianglePositionIds.MoveFrom(source.trianglePositionIds);
    clothPoints.MoveFrom(source.clothPoints);
    collisionShapes.MoveFrom(source.collisionShapes);
    source.bodyNode = 0u;
}

bool BuildModelSkinSurfaceCapture(const FModelGpuSkinningGeometry& geometry, const gk::Array<FModelSparseVertexMap>& vertexMap, const gk::Array<uint32_t>& drawIndices, uint32_t bodyNode, const char* bodyMeshName, const FModelSparsePoseGeometry& reference, const gk::Array<FModelSkinPointCorrection>& corrections, const gk::Array<uint32_t>& selectedClothIds, const gk::Array<FModelSecondaryMotionCollisionShape>& collisionShapes, FModelSkinSurfaceCapture& output, gk::String& error)
{
    if (&error == &output.bodyMeshName)
    {
        return false;
    }
    error.Clear();
    if (!bodyMeshName || !bodyMeshName[0] || reference.positions.Count() == 0u || reference.positions.Count() != geometry.positions.Count() || vertexMap.Count() == 0u || drawIndices.Count() == 0u || drawIndices.Count() % 3u != 0u || selectedClothIds.Count() == 0u || !ValidateShapes(collisionShapes))
    {
        error.Assign("surface capture input is empty or invalid");
        return false;
    }
    for (uint32_t id = 0u; id < reference.positions.Count(); ++id)
    {
        for (uint32_t axis = 0u; axis < 4u; ++axis)
        {
            if (!isfinite(reference.positions.At(id).value[axis]))
            {
                error.Assign("surface capture reference position is not finite");
                return false;
            }
        }
    }
    for (uint32_t index = 0u; index < corrections.Count(); ++index)
    {
        const auto& correction = corrections.At(index);
        if (correction.positionId >= reference.positions.Count() || (index > 0u && corrections.At(index - 1u).positionId >= correction.positionId) || !IsFloatPosition(correction.position))
        {
            error.Assign("surface capture corrections have invalid IDs, order, or positions");
            return false;
        }
    }
    FModelSkinSurfaceCapture candidate;
    gk::Array<uint32_t> owners;
    if (!candidate.bodyMeshName.Assign(bodyMeshName) || !owners.Reserve(reference.positions.Count()))
    {
        error.Assign("surface capture workspace allocation failed");
        return false;
    }
    candidate.bodyNode = bodyNode;
    for (uint32_t id = 0u; id < reference.positions.Count(); ++id)
    {
        if (!owners.Append(UINT32_MAX))
        {
            error.Assign("surface capture ownership allocation failed");
            return false;
        }
    }
    for (uint32_t segmentIndex = 0u; segmentIndex < geometry.segments.Count(); ++segmentIndex)
    {
        const auto& segment = geometry.segments.At(segmentIndex);
        if (segment.firstPosition > owners.Count() || segment.positionCount > owners.Count() - segment.firstPosition)
        {
            error.Assign("surface capture segment range is invalid");
            return false;
        }
        for (uint32_t local = 0u; local < segment.positionCount; ++local)
        {
            const uint32_t id = segment.firstPosition + local;
            if (owners.At(id) != UINT32_MAX)
            {
                error.Assign("surface capture segment ranges overlap");
                return false;
            }
            owners.At(id) = segmentIndex;
            if (segment.nodeIndex == bodyNode && !candidate.bodyPoints.Append(ReadRenderedPoint(id, reference, corrections)))
            {
                error.Assign("surface capture body point allocation failed");
                return false;
            }
        }
        if (segment.nodeIndex == bodyNode && !candidate.bodySegmentIndices.Append(segmentIndex))
        {
            error.Assign("surface capture body segment allocation failed");
            return false;
        }
    }
    for (uint32_t first = 0u; first < drawIndices.Count(); first += 3u)
    {
        uint32_t ids[3]{};
        uint32_t bodyCorners = 0u;
        for (uint32_t corner = 0u; corner < 3u; ++corner)
        {
            const uint32_t vertex = drawIndices.At(first + corner);
            if (vertex >= vertexMap.Count() || vertexMap.At(vertex).positionIndex >= owners.Count())
            {
                error.Assign("surface capture draw index or sparse vertex map is out of range");
                return false;
            }
            ids[corner] = vertexMap.At(vertex).positionIndex;
            const uint32_t owner = owners.At(ids[corner]);
            if (owner == UINT32_MAX)
            {
                error.Assign("surface capture draw position has no mesh segment");
                return false;
            }
            bodyCorners += geometry.segments.At(owner).nodeIndex == bodyNode ? 1u : 0u;
        }
        if (bodyCorners != 0u && bodyCorners != 3u)
        {
            error.Assign("surface capture triangle mixes body and another mesh");
            return false;
        }
        if (bodyCorners == 3u && !candidate.trianglePositionIds.AppendRange(ids, 3u))
        {
            error.Assign("surface capture triangle allocation failed");
            return false;
        }
    }
    for (uint32_t index = 0u; index < selectedClothIds.Count(); ++index)
    {
        const uint32_t id = selectedClothIds.At(index);
        if (id >= reference.positions.Count())
        {
            error.Assign("surface capture selected cloth position is out of range");
            return false;
        }
        if (!candidate.clothPoints.Append(ReadRenderedPoint(id, reference, corrections)))
        {
            error.Assign("surface capture cloth point allocation failed");
            return false;
        }
    }
    if (candidate.bodyPoints.Count() == 0u || candidate.trianglePositionIds.Count() == 0u)
    {
        error.Assign("surface capture selected body mesh has no points or draw triangles");
        return false;
    }
    if (!candidate.collisionShapes.AppendRange(collisionShapes.Data(), collisionShapes.Count()))
    {
        error.Assign("surface capture collision shape allocation failed");
        return false;
    }
    output.MoveFrom(candidate);
    return true;
}

bool EncodeModelSkinSurfaceCapture(uint32_t frame, const FModelSkinSurfaceCapture& snapshot, gk::String& output, gk::String& error)
{
    if (&output == &error || &output == &snapshot.bodyMeshName || &error == &snapshot.bodyMeshName)
    {
        return false;
    }
    error.Clear();
    if (snapshot.bodyMeshName.Empty() || snapshot.bodySegmentIndices.Count() == 0u || snapshot.bodyPoints.Count() == 0u || snapshot.trianglePositionIds.Count() == 0u || snapshot.trianglePositionIds.Count() % 3u != 0u || snapshot.clothPoints.Count() == 0u || !ArePointsFinite(snapshot.bodyPoints) || !ArePointsFinite(snapshot.clothPoints) || !ValidateShapes(snapshot.collisionShapes))
    {
        error.Assign("surface capture snapshot is empty or non-finite");
        return false;
    }
    gk::String json;
    bool encoded = json.Assign("{\"type\":\"gkcore_model_skin_surface_capture\",\"frame\":") && json.AppendUnsigned(frame) && json.Append(",\"bodyMeshName\":") && AppendJsonString(snapshot.bodyMeshName.CStr(), json) && json.Append(",\"bodyNode\":") && json.AppendUnsigned(snapshot.bodyNode) && json.Append(",\"positionSource\":\"rendered skin CPU reference; float positions; model space before instance SRT\",\"triangleIndexSource\":\"draw indices mapped through SparseVertexMap; global unique position IDs; original winding\",\"clothPointRadius\":0,\"collisionShapeRadiusSource\":\"current model-space shapes; no cloth radius expansion\",\"bodySegmentIndices\":") && AppendIndices(snapshot.bodySegmentIndices, json) && json.Append(",\"bodyPoints\":") && AppendPoints(snapshot.bodyPoints, json) && json.Append(",\"trianglePositionIds\":") && AppendIndices(snapshot.trianglePositionIds, json) && json.Append(",\"clothPoints\":") && AppendPoints(snapshot.clothPoints, json) && json.Append(",\"collisionShapes\":[");
    for (uint32_t index = 0u; encoded && index < snapshot.collisionShapes.Count(); ++index)
    {
        const auto& shape = snapshot.collisionShapes.At(index);
        char text[512]{};
        const int count = snprintf(text, sizeof(text), "%s{\"start\":[%.17g,%.17g,%.17g],\"end\":[%.17g,%.17g,%.17g],\"radius\":%.17g}", index ? "," : "", static_cast<double>(shape.start.x), static_cast<double>(shape.start.y), static_cast<double>(shape.start.z), static_cast<double>(shape.end.x), static_cast<double>(shape.end.y), static_cast<double>(shape.end.z), static_cast<double>(shape.radius));
        encoded = count >= 0 && static_cast<size_t>(count) < sizeof(text) && json.Append(text, static_cast<uint32_t>(count));
    }
    if (!encoded || !json.Append("]}\n"))
    {
        error.Assign("surface capture JSON allocation or formatting failed");
        return false;
    }
    output.MoveFrom(json);
    return true;
}

bool EncodeModelSkinSurfaceBaseMatrices(uint32_t frame, const gk::Array<const char*>& boneNames, const gk::Array<float>& matrices, gk::String& output, gk::String& error)
{
    if (&output == &error)
    {
        return false;
    }
    error.Clear();
    const uint32_t boneCount = boneNames.Count();
    if (boneCount == 0u || boneCount > UINT32_MAX / 16u || matrices.Count() != boneCount * 16u)
    {
        error.Assign("surface base matrix names or count is invalid");
        return false;
    }
    // 最後のboneに不正値があっても、JSON候補の作成前に検出する。
    for (uint32_t bone = 0u; bone < boneCount; ++bone)
    {
        if (!boneNames.At(bone))
        {
            error.Assign("surface base matrix bone name is null");
            return false;
        }
        for (uint32_t element = 0u; element < 16u; ++element)
        {
            if (!isfinite(matrices.At(bone * 16u + element)))
            {
                error.Assign("surface base matrix is not finite");
                return false;
            }
        }
    }
    // 出力全体を別所有し、途中の確保失敗では公開文字列を変更しない。
    gk::String json;
    for (uint32_t bone = 0u; bone < boneCount; ++bone)
    {
        bool encoded = json.Append("{\"type\":\"gkcore_model_skin_surface_base_matrix\",\"frame\":") && json.AppendUnsigned(frame) && json.Append(",\"bone\":") && json.AppendUnsigned(bone) && json.Append(",\"boneCount\":") && json.AppendUnsigned(boneCount) && json.Append(",\"boneName\":") && AppendJsonString(boneNames.At(bone), json) && json.Append(",\"matrixLayout\":\"column-major\",\"matrixSource\":\"previousBaseMatrices from the current completed update; pre-secondary base; model space before instance SRT\",\"previousBaseMatrix\":[");
        for (uint32_t element = 0u; encoded && element < 16u; ++element)
        {
            // floatをdoubleへ正確に拡大し、再読込時のbitを保つ17桁で記録する。
            char text[64]{};
            const int count = snprintf(text, sizeof(text), "%s%.17g", element ? "," : "", static_cast<double>(matrices.At(bone * 16u + element)));
            encoded = count >= 0 && static_cast<size_t>(count) < sizeof(text) && json.Append(text, static_cast<uint32_t>(count));
        }
        if (!encoded || !json.Append("]}\n"))
        {
            error.Assign("surface base matrix JSON allocation or formatting failed");
            return false;
        }
    }
    output.MoveFrom(json);
    return true;
}

}
