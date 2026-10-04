#include "Geometry.h"
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
 * Carries camera-space position and UVs until the polygon has passed depth clipping.
 */
struct ViewPoint {
    double x;
    double y;
    double z;
    double u;
    double v;
};

/**
 * Converts packed sRGB channels for the linear-light vertex pipeline.
 */
void StoreColor(uint32_t packed, float* color) {
    color[0] = SrgbToLinear(static_cast<float>((packed >> 16) & 255u) / 255.0f);
    color[1] = SrgbToLinear(static_cast<float>((packed >> 8) & 255u) / 255.0f);
    color[2] = SrgbToLinear(static_cast<float>(packed & 255u) / 255.0f);
    color[3] = 1.0f;
}

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
 * Applies an optional model transform and camera transform without changing source UVs.
 */
bool TransformToView(Vec3 source, const float sourceUv[2], const detail::DrawPacket& draw,
                     bool applyModelTransform, ViewPoint& output, String& error) {
    double px = source.x;
    double py = source.y;
    double pz = source.z;
    if (applyModelTransform) {
        const double sx = sin(static_cast<double>(draw.modelRotation.x));
        const double cx = cos(static_cast<double>(draw.modelRotation.x));
        const double sy = sin(static_cast<double>(draw.modelRotation.y));
        const double cy = cos(static_cast<double>(draw.modelRotation.y));
        const double sz = sin(static_cast<double>(draw.modelRotation.z));
        const double cz = cos(static_cast<double>(draw.modelRotation.z));
        const double x = px * draw.modelScale.x;
        const double y = py * draw.modelScale.y;
        const double z = pz * draw.modelScale.z;
        const double x1 = x * cz - y * sz;
        const double y1 = x * sz + y * cz;
        const double x2 = x1 * cy + z * sy;
        const double z2 = -x1 * sy + z * cy;
        px = x2 * 1.0 + draw.modelPosition.x;
        py = (y1 * cx - z2 * sx) + draw.modelPosition.y;
        pz = (y1 * sx + z2 * cx) + draw.modelPosition.z;
    }

    const double fx = static_cast<double>(draw.cameraTarget.x) - draw.cameraPosition.x;
    const double fy = static_cast<double>(draw.cameraTarget.y) - draw.cameraPosition.y;
    const double fz = static_cast<double>(draw.cameraTarget.z) - draw.cameraPosition.z;
    const double forwardLength = sqrt(fx * fx + fy * fy + fz * fz);
    if (!(forwardLength > 1e-12) || !isfinite(forwardLength)) {
        error.Assign("The camera direction is outside the renderer's numeric range");
        return false;
    }
    const double forwardX = fx / forwardLength;
    const double forwardY = fy / forwardLength;
    const double forwardZ = fz / forwardLength;
    double rightX = forwardZ;
    double rightY = 0.0;
    double rightZ = -forwardX;
    const double rightLength = sqrt(rightX * rightX + rightZ * rightZ);
    if (!(rightLength > 1e-12)) {
        rightX = 1.0;
        rightZ = 0.0;
    } else {
        rightX /= rightLength;
        rightZ /= rightLength;
    }
    const double upX = forwardY * rightZ - forwardZ * rightY;
    const double upY = forwardZ * rightX - forwardX * rightZ;
    const double upZ = forwardX * rightY - forwardY * rightX;
    const double dx = px - draw.cameraPosition.x;
    const double dy = py - draw.cameraPosition.y;
    const double dz = pz - draw.cameraPosition.z;
    output.x = dx * rightX + dy * rightY + dz * rightZ;
    output.y = dx * upX + dy * upY + dz * upZ;
    output.z = dx * forwardX + dy * forwardY + dz * forwardZ;
    output.u = sourceUv[0];
    output.v = sourceUv[1];
    if (!isfinite(output.x) || !isfinite(output.y) || !isfinite(output.z)) {
        error.Assign("The draw coordinates exceed the renderer's numeric range");
        return false;
    }
    return true;
}

/**
 * Appends a clip vertex without exceeding the fixed polygon scratch capacity.
 */
bool AppendClipPoint(ViewPoint* output, uint32_t& outputCount, const ViewPoint& point) {
    if (outputCount >= 8) return false;
    output[outputCount++] = point;
    return true;
}

/**
 * Clips positions and linear per-vertex attributes against one view-space depth plane.
 */
bool ClipPlane(const ViewPoint* input, uint32_t inputCount, double planeZ, bool keepGreater,
               ViewPoint* output, uint32_t& outputCount) {
    outputCount = 0;
    if (inputCount > 8) return false;
    if (inputCount == 0) return true;
    for (uint32_t i = 0; i < inputCount; ++i) {
        const ViewPoint& a = input[i];
        const ViewPoint& b = input[(i + 1) % inputCount];
        const bool insideA = keepGreater ? a.z >= planeZ : a.z <= planeZ;
        const bool insideB = keepGreater ? b.z >= planeZ : b.z <= planeZ;
        if (insideA && insideB) {
            if (!AppendClipPoint(output, outputCount, b)) return false;
        } else if (insideA && !insideB) {
            const double t = (planeZ - a.z) / (b.z - a.z);
            const ViewPoint intersection = {a.x + (b.x - a.x) * t,
                                            a.y + (b.y - a.y) * t,
                                            planeZ,
                                            a.u + (b.u - a.u) * t,
                                            a.v + (b.v - a.v) * t};
            if (!AppendClipPoint(output, outputCount, intersection)) return false;
        } else if (!insideA && insideB) {
            const double t = (planeZ - a.z) / (b.z - a.z);
            const ViewPoint intersection = {a.x + (b.x - a.x) * t,
                                            a.y + (b.y - a.y) * t,
                                            planeZ,
                                            a.u + (b.u - a.u) * t,
                                            a.v + (b.v - a.v) * t};
            if (!AppendClipPoint(output, outputCount, intersection) ||
                !AppendClipPoint(output, outputCount, b)) return false;
        }
    }
    return outputCount <= 8;
}

/**
 * Projects view-space position and its UV attributes into the renderer vertex format.
 */
bool ProjectView(const ViewPoint& point, uint32_t width, uint32_t height,
                 uint32_t packedColor, const float* linearColor,
                 Vertex& output, String& error) {
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double focal = 1.7320508075688772;
    constexpr double nearPlane = 0.1;
    constexpr double farPlane = 1000.0;
    const double clipX = point.x * focal / aspect;
    const double clipY = point.y * focal;
    const double clipZ = (farPlane / (farPlane - nearPlane)) * point.z -
                         (farPlane * nearPlane / (farPlane - nearPlane));
    if (!StoreFloat(clipX, output.position[0], error) ||
        !StoreFloat(clipY, output.position[1], error) ||
        !StoreFloat(clipZ, output.position[2], error) ||
        !StoreFloat(point.z, output.position[3], error) ||
        !StoreFloat(point.u, output.uv[0], error) ||
        !StoreFloat(point.v, output.uv[1], error)) return false;
    if (linearColor) {
        for (uint32_t component = 0; component < 4; ++component)
            output.color[component] = linearColor[component];
    } else {
        StoreColor(packedColor, output.color);
    }
    return true;
}

/**
 * Appends one projected triangle after checking remaining output capacity.
 */
bool AppendTriangle(Array<Vertex>& vertices, const Vertex* triangle,
                    uint32_t vertexLimit, String& error) {
    if (vertices.Count() > vertexLimit || vertexLimit - vertices.Count() < 3) {
        error.Assign("The frame exceeds the dynamic vertex capacity");
        return false;
    }
    if (!vertices.Append(triangle[0]) || !vertices.Append(triangle[1]) || !vertices.Append(triangle[2])) {
        error.Assign("The frame vertex allocation failed");
        return false;
    }
    return true;
}

/**
 * Clips, projects, and appends a world-space triangle with perspective-correct UV payloads.
 */
bool AppendWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                         const Vec3* points, const float sourceUvs[3][2], bool applyModelTransform,
                         Array<Vertex>& vertices, uint32_t vertexLimit, String& error,
                         const float* linearColor = nullptr) {
    ViewPoint first[8]{};
    ViewPoint second[8]{};
    for (uint32_t i = 0; i < 3; ++i) {
        if (!isfinite(sourceUvs[i][0]) || !isfinite(sourceUvs[i][1])) {
            error.Assign("The model contains non-finite texture coordinates");
            return false;
        }
        if (!TransformToView(points[i], sourceUvs[i], draw, applyModelTransform, first[i], error)) return false;
    }
    uint32_t count = 0;
    if (!ClipPlane(first, 3, 0.1, true, second, count) ||
        !ClipPlane(second, count, 1000.0, false, first, count)) {
        error.Assign("The clipped triangle exceeds the polygon scratch capacity");
        return false;
    }
    if (count < 3) return true;
    const uint32_t requiredVertices = (count - 2) * 3;
    if (vertices.Count() > vertexLimit || vertexLimit - vertices.Count() < requiredVertices) {
        error.Assign("The frame exceeds the dynamic vertex capacity");
        return false;
    }
    if (!vertices.Reserve(vertices.Count() + requiredVertices)) {
        error.Assign("The frame vertex allocation failed");
        return false;
    }
    Vertex projected[8]{};
    for (uint32_t i = 0; i < count; ++i) {
        if (!ProjectView(first[i], frame.width, frame.height, draw.color, linearColor,
                         projected[i], error)) return false;
    }
    for (uint32_t i = 1; i + 1 < count; ++i) {
        const Vertex triangle[3] = {projected[0], projected[i], projected[i + 1]};
        if (!AppendTriangle(vertices, triangle, vertexLimit, error)) return false;
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
        if ((draw.flags & 1u) == 0) {
            error.Assign("Unfilled rectangles are unavailable in the current renderer");
            return false;
        }
        if (frame.width == 0 || frame.height == 0) {
            error.Assign("The frame size must be positive");
            return false;
        }
        if (vertices.Count() > vertexLimit || vertexLimit - vertices.Count() < 6) {
            error.Assign("The frame exceeds the dynamic vertex capacity");
            return false;
        }
        float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
        if (!StoreFloat(static_cast<double>(draw.rect[0]) * 2.0 / frame.width - 1.0, left, error) ||
            !StoreFloat((static_cast<double>(draw.rect[0]) + draw.rect[2]) * 2.0 / frame.width - 1.0, right, error) ||
            !StoreFloat(1.0 - static_cast<double>(draw.rect[1]) * 2.0 / frame.height, top, error) ||
            !StoreFloat(1.0 - (static_cast<double>(draw.rect[1]) + draw.rect[3]) * 2.0 / frame.height, bottom, error))
            return false;
        Vertex rectangle[6]{};
        const float points[6][2] = {{left,top},{right,top},{right,bottom},{left,top},{right,bottom},{left,bottom}};
        const float uvs[6][2] = {{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
        for (uint32_t i = 0; i < 6; ++i) {
            rectangle[i].position[0] = points[i][0];
            rectangle[i].position[1] = points[i][1];
            rectangle[i].position[2] = 0.0f;
            rectangle[i].position[3] = 1.0f;
            StoreColor(draw.color, rectangle[i].color);
            rectangle[i].uv[0] = uvs[i][0];
            rectangle[i].uv[1] = uvs[i][1];
            if (!vertices.Append(rectangle[i])) {
                error.Assign("The frame vertex allocation failed");
                return false;
            }
        }
        return true;
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
