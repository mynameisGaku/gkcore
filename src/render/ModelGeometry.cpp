#include "ModelGeometry.h"

#include <math.h>

/**
 * Expands lit model primitives while preserving the caller's output on failure.
 */
namespace gk::render {
namespace {

/**
 * Validates one finite scalar material factor in the normalized range.
 */
bool IsFactor(float value) {
    return isfinite(value) != 0 && value >= 0.0f && value <= 1.0f;
}

} // namespace

/**
 * Projects one model primitive into lit vertices and commits the complete part atomically.
 */
bool AppendLitModelPart(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                        const ModelPartPlan& part, Array<ModelRenderVertex>& vertices,
                        uint32_t vertexLimit, String& error) {
    error.Clear();
    if (draw.kind != detail::DrawKind::Model || !draw.model || frame.width == 0 || frame.height == 0 ||
        part.indexCount == 0 || part.indexCount % 3 != 0 ||
        part.firstIndex > draw.model->indices.Count() ||
        part.indexCount > draw.model->indices.Count() - part.firstIndex ||
        vertices.Count() > vertexLimit || !IsFactor(part.metallicFactor) ||
        !IsFactor(part.roughnessFactor)) {
        error.Assign("The model part, material factors, or frame bounds are invalid");
        return false;
    }
    for (uint32_t component = 0; component < 4; ++component) {
        if (!IsFactor(part.baseColorFactor[component])) {
            error.Assign("The model material base-color factor is invalid");
            return false;
        }
    }

    Array<ModelRenderVertex> candidate;
    for (uint32_t offset = 0; offset < part.indexCount; offset += 3) {
        WorldVertex source[3]{};
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const uint32_t modelIndex = draw.model->indices.At(part.firstIndex + offset + corner);
            if (modelIndex >= draw.model->vertices.Count()) {
                error.Assign("The model part contains an invalid vertex index");
                return false;
            }
            const detail::ModelVertex& vertex = draw.model->vertices.At(modelIndex);
            source[corner].position = {vertex.position[0], vertex.position[1], vertex.position[2]};
            source[corner].normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]};
            source[corner].uv[0] = vertex.uv[0];
            source[corner].uv[1] = vertex.uv[1];
        }
        ProjectedWorldVertex projected[18]{};
        uint32_t projectedCount = 0;
        if (!ProjectWorldTriangle(frame, draw, source, true, true, part.baseColorFactor,
                                  projected, projectedCount, error)) return false;
        if (vertices.Count() > vertexLimit || candidate.Count() > vertexLimit - vertices.Count() ||
            projectedCount > vertexLimit - vertices.Count() - candidate.Count()) {
            error.Assign("The frame exceeds the dynamic vertex capacity");
            return false;
        }
        if (projectedCount > UINT32_MAX - candidate.Count() ||
            !candidate.Reserve(candidate.Count() + projectedCount)) {
            error.Assign("The model vertex allocation failed");
            return false;
        }
        for (uint32_t i = 0; i < projectedCount; ++i) {
            ModelRenderVertex vertex{};
            vertex.surface = projected[i].surface;
            for (uint32_t axis = 0; axis < 3; ++axis) {
                vertex.worldNormal[axis] = projected[i].worldNormal[axis];
                vertex.viewDirection[axis] = projected[i].viewDirection[axis];
            }
            vertex.metallicRoughness[0] = part.metallicFactor;
            vertex.metallicRoughness[1] = part.roughnessFactor;
            if (!candidate.Append(vertex)) {
                error.Assign("The model vertex allocation failed");
                return false;
            }
        }
    }

    if (candidate.Count() > UINT32_MAX - vertices.Count() ||
        !vertices.Reserve(vertices.Count() + candidate.Count())) {
        error.Assign("The frame vertex allocation failed");
        return false;
    }
    for (uint32_t i = 0; i < candidate.Count(); ++i) {
        if (!vertices.Append(candidate.At(i))) {
            error.Assign("The frame vertex allocation failed");
            return false;
        }
    }
    error.Clear();
    return true;
}

} // namespace gk::render
