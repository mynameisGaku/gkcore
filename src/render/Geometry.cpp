#include "Geometry.h"
#include "WorldGeometry.h"
#include "RectangleGeometry.h"
#include "../resources/Resources.h"
#include "PostProcess.h"

#include <float.h>
#include <math.h>

/**
 * CPU vertex generation shared by the renderer and portable geometry tests.
 */
namespace gk::render {
namespace {

/**
 * Stores a finite double value when it is representable by the GPU vertex format.
 */
bool StoreFloat(double value, float& output, String& error) {
    if (!isfinite(value) || fabs(value) > FLT_MAX) {
        error.Assign("The draw coordinates exceed the renderer's numeric range");
        return false;
    }
    output = static_cast<float>(value);
    return true;
}

/**
 * Clips, projects, and appends a world-space triangle with perspective-correct UV payloads.
 */
bool AppendWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                         const Vec3* points, const float sourceUvs[3][2], bool applyModelTransform,
                         Array<Vertex>& vertices, uint32_t vertexLimit, String& error,
                         const float* linearColor = nullptr) {
    WorldVertex source[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        source[i].position = points[i];
        source[i].uv[0] = sourceUvs[i][0];
        source[i].uv[1] = sourceUvs[i][1];
    }
    ProjectedWorldVertex projected[18]{};
    uint32_t projectedCount = 0;
    if (!ProjectWorldTriangle(frame, draw, source, applyModelTransform, false, linearColor,
                              projected, projectedCount, error)) return false;
    if (vertices.Count() > vertexLimit || vertexLimit - vertices.Count() < projectedCount) {
        error.Assign("The frame exceeds the dynamic vertex capacity");
        return false;
    }
    if (projectedCount > UINT32_MAX - vertices.Count() ||
        !vertices.Reserve(vertices.Count() + projectedCount)) {
        error.Assign("The frame vertex allocation failed");
        return false;
    }
    for (uint32_t i = 0; i < projectedCount; ++i) {
        if (!vertices.Append(projected[i].surface)) {
            error.Assign("The frame vertex allocation failed");
            return false;
        }
    }
    return true;
}
} // namespace

/**
 * Dispatches each API draw kind to screen-space or world-space vertex generation.
 */
bool AppendDraw(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                Array<Vertex>& vertices, uint32_t vertexLimit, String& error) {
    if (draw.kind == detail::DrawKind::Rect) {
        return AppendRectangle(frame, draw, vertices, vertexLimit, error);
    }
    if (draw.kind == detail::DrawKind::Image) {
        return AppendSprite(frame, draw, vertices, vertexLimit, error);
    }
    if (draw.kind == detail::DrawKind::Triangle3D) {
        if ((draw.flags & 1u) == 0) {
            error.Assign("Unfilled 3D triangles are unavailable in the current renderer");
            return false;
        }
        const float noUvs[3][2] = {};
        return AppendWorldTriangle(frame, draw, draw.points, noUvs, false, vertices, vertexLimit, error);
    }
    if (draw.kind == detail::DrawKind::Model && draw.model) {
        const detail::ModelResource& model = *draw.model;
        if (model.indices.Count() % 3 != 0) {
            error.Assign("The model index list is not a triangle list");
            return false;
        }
        for (uint32_t i = 0; i < model.indices.Count(); i += 3) {
            Vec3 points[3]{};
            float uvs[3][2]{};
            for (uint32_t j = 0; j < 3; ++j) {
                const uint32_t index = model.indices.At(i + j);
                if (index >= model.vertices.Count()) {
                    error.Assign("The model contains an invalid vertex index");
                    return false;
                }
                const detail::ModelVertex& source = model.vertices.At(index);
                points[j] = {source.position[0], source.position[1], source.position[2]};
                uvs[j][0] = source.uv[0];
                uvs[j][1] = source.uv[1];
            }
            if (!AppendWorldTriangle(frame, draw, points, uvs, true, vertices, vertexLimit, error)) return false;
        }
        return true;
    }
    error.Assign("The requested draw kind is unavailable in the current renderer");
    return false;
}


/**
 * Expands one validated image into a clip-space quad with normalized source UVs.
 */
bool AppendSprite(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                  Array<Vertex>& vertices, uint32_t vertexLimit, String& error) {
    if (!draw.image || draw.image->width == 0 || draw.image->height == 0 ||
        frame.width == 0 || frame.height == 0 ||
        !isfinite(draw.scaleX) || !isfinite(draw.scaleY) ||
        draw.scaleX <= 0.0f || draw.scaleY <= 0.0f || !isfinite(draw.rotation)) {
        error.Assign("The image resource or sprite transform is invalid");
        return false;
    }
    const uint64_t pixelCount = static_cast<uint64_t>(draw.image->width) * draw.image->height;
    if (draw.image->width > 16384 || draw.image->height > 16384 ||
        pixelCount > 64u * 1024u * 1024u) {
        error.Assign("The image resource pixel buffer is incomplete");
        return false;
    }
    const uint64_t requiredBytes = pixelCount * 4u;
    if (requiredBytes > draw.image->rgba.Count()) {
        error.Assign("The image resource pixel buffer is incomplete");
        return false;
    }
    if (vertices.Count() > vertexLimit || vertexLimit - vertices.Count() < 6) {
        error.Assign("The frame exceeds the dynamic vertex capacity");
        return false;
    }

    const bool centered = (draw.flags & detail::DrawImageCentered) != 0;
    const double scaledWidth = static_cast<double>(draw.image->width) * draw.scaleX;
    const double scaledHeight = static_cast<double>(draw.image->height) * draw.scaleY;
    const double localLeft = centered ? -scaledWidth * 0.5 : 0.0;
    const double localTop = centered ? -scaledHeight * 0.5 : 0.0;
    const double localRight = localLeft + scaledWidth;
    const double localBottom = localTop + scaledHeight;
    const double cosine = cos(static_cast<double>(draw.rotation));
    const double sine = sin(static_cast<double>(draw.rotation));
    const double anchorX = draw.rect[0];
    const double anchorY = draw.rect[1];
    const double localPoints[4][2] = {
        {localLeft, localTop}, {localRight, localTop},
        {localRight, localBottom}, {localLeft, localBottom}
    };
    const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    const uint32_t cornerIndices[6] = {0, 1, 2, 0, 2, 3};
    Vertex sprite[6]{};
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t corner = cornerIndices[i];
        const double screenX = anchorX + localPoints[corner][0] * cosine - localPoints[corner][1] * sine;
        const double screenY = anchorY + localPoints[corner][0] * sine + localPoints[corner][1] * cosine;
        if (!StoreFloat(screenX * 2.0 / frame.width - 1.0, sprite[i].position[0], error) ||
            !StoreFloat(1.0 - screenY * 2.0 / frame.height, sprite[i].position[1], error)) return false;
        sprite[i].position[2] = 0.0f;
        sprite[i].position[3] = 1.0f;
        sprite[i].color[0] = sprite[i].color[1] = sprite[i].color[2] = sprite[i].color[3] = 1.0f;
        sprite[i].uv[0] = uvs[corner][0];
        sprite[i].uv[1] = uvs[corner][1];
    }
    if (!vertices.Reserve(vertices.Count() + 6)) {
        error.Assign("The frame vertex allocation failed");
        return false;
    }
    for (uint32_t i = 0; i < 6; ++i) {
        if (!vertices.Append(sprite[i])) {
            error.Assign("The frame vertex allocation failed");
            return false;
        }
    }
    return true;
}

/**
 * Expands only the index range selected for one material primitive.
 */
bool AppendModelPart(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                     const ModelPartPlan& part, Array<Vertex>& vertices,
                     uint32_t vertexLimit, String& error) {
    if (draw.kind != detail::DrawKind::Model || !draw.model || frame.width == 0 ||
        frame.height == 0 || part.indexCount == 0 || part.indexCount % 3 != 0 ||
        part.firstIndex > draw.model->indices.Count() ||
        part.indexCount > draw.model->indices.Count() - part.firstIndex) {
        error.Assign("The model part or frame bounds are invalid");
        return false;
    }
    for (uint32_t component = 0; component < 4; ++component) {
        const float factor = part.baseColorFactor[component];
        if (!(factor >= 0.0f && factor <= 1.0f) || !isfinite(factor)) {
            error.Assign("The model material base-color factor is invalid");
            return false;
        }
    }
    const detail::ModelResource& model = *draw.model;
    for (uint32_t indexOffset = 0; indexOffset < part.indexCount; indexOffset += 3) {
        Vec3 points[3]{};
        float uvs[3][2]{};
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const uint32_t index = model.indices.At(part.firstIndex + indexOffset + corner);
            if (index >= model.vertices.Count()) {
                error.Assign("The model part contains an invalid vertex index");
                return false;
            }
            const detail::ModelVertex& source = model.vertices.At(index);
            points[corner] = {source.position[0], source.position[1], source.position[2]};
            uvs[corner][0] = source.uv[0];
            uvs[corner][1] = source.uv[1];
        }
        if (!AppendWorldTriangle(frame, draw, points, uvs, true, vertices, vertexLimit, error,
                                 part.baseColorFactor)) return false;
    }
    return true;
}

}
