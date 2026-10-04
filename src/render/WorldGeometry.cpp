#include "WorldGeometry.h"
#include "PostProcess.h"
#include <float.h>
#include <math.h>

/**
 * Clips and projects world-space surface and lighting attributes.
 */
namespace gk::render {
/**
 * Private camera, transform, clipping, and lighting helpers for world triangles.
 */
namespace {
/**
 * Camera-space position and UV attributes before projection.
 */
struct ViewPoint {
    double x;
    double y;
    double z;
    double u;
    double v;
};

/**
 * One clipping vertex with linearly interpolated lighting payloads.
 */
struct ClipVertex {
    ViewPoint view;
    float normal[3];
    double viewDirection[3];
};

/**
 * Interpolates one clipped edge at a camera depth plane.
 */
ClipVertex IntersectClipEdge(const ClipVertex& a, const ClipVertex& b, double planeZ) {
    const double t = (planeZ - a.view.z) / (b.view.z - a.view.z);
    ClipVertex vertex{};
    vertex.view = {a.view.x + (b.view.x - a.view.x) * t,
                   a.view.y + (b.view.y - a.view.y) * t,
                   planeZ,
                   a.view.u + (b.view.u - a.view.u) * t,
                   a.view.v + (b.view.v - a.view.v) * t};
    for (uint32_t axis = 0; axis < 3; ++axis) {
        vertex.normal[axis] = static_cast<float>(a.normal[axis] + (b.normal[axis] - a.normal[axis]) * t);
        vertex.viewDirection[axis] = a.viewDirection[axis] +
                                     (b.viewDirection[axis] - a.viewDirection[axis]) * t;
    }
    return vertex;
}

/**
 * Clips a polygon against one depth plane while carrying each per-vertex attribute.
 */
bool ClipLightingPlane(const ClipVertex* input, uint32_t inputCount, double planeZ,
                       bool keepGreater, ClipVertex* output, uint32_t& outputCount) {
    outputCount = 0;
    for (uint32_t i = 0; i < inputCount; ++i) {
        const ClipVertex& a = input[(i + inputCount - 1) % inputCount];
        const ClipVertex& b = input[i];
        const bool insideA = keepGreater ? a.view.z >= planeZ : a.view.z <= planeZ;
        const bool insideB = keepGreater ? b.view.z >= planeZ : b.view.z <= planeZ;
        if (insideA && insideB) {
            if (outputCount >= 8) return false;
            output[outputCount++] = b;
        } else if (insideA && !insideB) {
            if (outputCount >= 8) return false;
            output[outputCount++] = IntersectClipEdge(a, b, planeZ);
        } else if (!insideA && insideB) {
            if (outputCount > 6) return false;
            output[outputCount++] = IntersectClipEdge(a, b, planeZ);
            output[outputCount++] = b;
        }
    }
    return outputCount <= 8;
}
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
 * Applies the captured model transform once and returns a double-precision world point.
 */
bool TransformToWorld(Vec3 source, const detail::DrawPacket& draw,
                      bool applyModelTransform, double output[3], String& error) {
    output[0] = source.x;
    output[1] = source.y;
    output[2] = source.z;
    if (applyModelTransform) {
        const double rx = draw.modelRotation.x, ry = draw.modelRotation.y, rz = draw.modelRotation.z;
        const double sx = draw.modelScale.x, sy = draw.modelScale.y, sz = draw.modelScale.z;
        if (!isfinite(rx) || !isfinite(ry) || !isfinite(rz) ||
            !isfinite(sx) || !isfinite(sy) || !isfinite(sz) ||
            !isfinite(draw.modelPosition.x) || !isfinite(draw.modelPosition.y) ||
            !isfinite(draw.modelPosition.z)) {
            error.Assign("The model transform is outside the renderer's numeric range");
            return false;
        }
        const double x = output[0] * sx, y = output[1] * sy, z = output[2] * sz;
        const double x1 = x * cos(rz) - y * sin(rz);
        const double y1 = x * sin(rz) + y * cos(rz);
        const double x2 = x1 * cos(ry) + z * sin(ry);
        const double z2 = -x1 * sin(ry) + z * cos(ry);
        output[0] = x2 + draw.modelPosition.x;
        output[1] = y1 * cos(rx) - z2 * sin(rx) + draw.modelPosition.y;
        output[2] = y1 * sin(rx) + z2 * cos(rx) + draw.modelPosition.z;
    }
    if (!isfinite(output[0]) || !isfinite(output[1]) || !isfinite(output[2])) {
        error.Assign("The model world position is outside the renderer's numeric range");
        return false;
    }
    return true;
}

/**
 * Normalizes a double vector without overflow or underflow from its original magnitude.
 */
bool NormalizeVector(const double source[3], float output[3]) {
    const double largest = fmax(fmax(fabs(source[0]), fabs(source[1])), fabs(source[2]));
    if (!(largest > 0.0) || !isfinite(largest)) {
        output[0] = output[1] = output[2] = 0.0f;
        return false;
    }
    const double x = source[0] / largest;
    const double y = source[1] / largest;
    const double z = source[2] / largest;
    const double length = sqrt(x * x + y * y + z * z);
    output[0] = static_cast<float>(x / length);
    output[1] = static_cast<float>(y / length);
    output[2] = static_cast<float>(z / length);
    return true;
}

/**
 * Applies inverse scale and Z/Y/X rotation to a normal, then normalizes it.
 */
bool TransformNormal(Vec3 source, const detail::DrawPacket& draw,
                     bool applyModelTransform, float output[3], String& error) {
    double x = source.x;
    double y = source.y;
    double z = source.z;
    if (applyModelTransform) {
        const double sx = draw.modelScale.x, sy = draw.modelScale.y, sz = draw.modelScale.z;
        if (sx == 0.0 || sy == 0.0 || sz == 0.0 ||
            !isfinite(sx) || !isfinite(sy) || !isfinite(sz)) {
            error.Assign("The model scale cannot transform lighting normals");
            return false;
        }
        x /= sx;
        y /= sy;
        z /= sz;
        const double rx = draw.modelRotation.x, ry = draw.modelRotation.y, rz = draw.modelRotation.z;
        if (!isfinite(rx) || !isfinite(ry) || !isfinite(rz)) {
            error.Assign("The model rotation is outside the renderer's numeric range");
            return false;
        }
        const double x1 = x * cos(rz) - y * sin(rz);
        const double y1 = x * sin(rz) + y * cos(rz);
        const double x2 = x1 * cos(ry) + z * sin(ry);
        const double z2 = -x1 * sin(ry) + z * cos(ry);
        x = x2;
        y = y1 * cos(rx) - z2 * sin(rx);
        z = y1 * sin(rx) + z2 * cos(rx);
    }
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) {
        error.Assign("The transformed model normal is outside the renderer's numeric range");
        return false;
    }
    const double transformed[3] = {x, y, z};
    NormalizeVector(transformed, output);
    return true;
}

/**
 * Produces a deterministic unit face normal, using world-space points only once transformed.
 */
void BuildFaceFallback(const double worldPositions[3][3], float output[3]) {
    const double ax = worldPositions[1][0] - worldPositions[0][0];
    const double ay = worldPositions[1][1] - worldPositions[0][1];
    const double az = worldPositions[1][2] - worldPositions[0][2];
    const double bx = worldPositions[2][0] - worldPositions[0][0];
    const double by = worldPositions[2][1] - worldPositions[0][1];
    const double bz = worldPositions[2][2] - worldPositions[0][2];
    const double face[3] = {ay * bz - az * by,
                            az * bx - ax * bz,
                            ax * by - ay * bx};
    if (!NormalizeVector(face, output)) {
        output[0] = 0.0f;
        output[1] = 1.0f;
        output[2] = 0.0f;
    }
}

/**
 * Prepares normalized world normals and raw camera-to-vertex directions for clipping.
 */
bool PrepareLighting(const WorldVertex points[3], const double worldPositions[3][3],
                     const detail::DrawPacket& draw, bool applyModelTransform,
                     ClipVertex output[3], String& error) {
    bool hasMissingNormal = false;
    const double cameraPosition[3] = {draw.cameraPosition.x,
                                      draw.cameraPosition.y,
                                      draw.cameraPosition.z};
    for (uint32_t i = 0; i < 3; ++i) {
        if (!TransformNormal(points[i].normal, draw, applyModelTransform,
                             output[i].normal, error)) return false;
        if (output[i].normal[0] == 0.0f && output[i].normal[1] == 0.0f &&
            output[i].normal[2] == 0.0f) hasMissingNormal = true;
        for (uint32_t axis = 0; axis < 3; ++axis) {
            output[i].viewDirection[axis] = cameraPosition[axis] - worldPositions[i][axis];
            if (!isfinite(output[i].viewDirection[axis])) {
                error.Assign("The model view direction is outside the renderer's numeric range");
                return false;
            }
        }
    }
    if (hasMissingNormal) {
        float fallback[3]{};
        BuildFaceFallback(worldPositions, fallback);
        for (uint32_t i = 0; i < 3; ++i) {
            if (output[i].normal[0] == 0.0f && output[i].normal[1] == 0.0f &&
                output[i].normal[2] == 0.0f) {
                for (uint32_t axis = 0; axis < 3; ++axis) output[i].normal[axis] = fallback[axis];
            }
        }
    }
    return true;
}

/**
 * Applies an optional model transform and camera transform without changing source UVs.
 */
bool TransformToView(const double world[3], const float sourceUv[2],
                     const detail::DrawPacket& draw, ViewPoint& output, String& error) {
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
    const double dx = world[0] - draw.cameraPosition.x;
    const double dy = world[1] - draw.cameraPosition.y;
    const double dz = world[2] - draw.cameraPosition.z;
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


} // namespace

/**
 * Clips and projects one triangle while interpolating UV and optional lighting payloads.
 */
bool ProjectWorldTriangle(const detail::FramePacket& frame, const detail::DrawPacket& draw,
                          const WorldVertex points[3], bool applyModelTransform,
                          bool includeLighting, const float* linearColor,
                          ProjectedWorldVertex output[18], uint32_t& outputCount,
                          String& error) {
    ClipVertex first[8]{};
    outputCount = 0;
    if (!output || !points || frame.width == 0 || frame.height == 0) {
        error.Assign("The world triangle or frame dimensions are invalid");
        return false;
    }
    double worldPositions[3][3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        const WorldVertex& source = points[i];
        const float uv[2] = {source.uv[0], source.uv[1]};
        if (!isfinite(source.position.x) || !isfinite(source.position.y) ||
            !isfinite(source.position.z) || !isfinite(uv[0]) || !isfinite(uv[1])) {
            error.Assign("The model contains non-finite position or texture coordinates");
            return false;
        }
        if (includeLighting && (!isfinite(source.normal.x) || !isfinite(source.normal.y) ||
                                !isfinite(source.normal.z))) {
            error.Assign("The model contains a non-finite normal");
            return false;
        }
        if (!TransformToWorld(source.position, draw, applyModelTransform, worldPositions[i], error)) return false;
        if (!TransformToView(worldPositions[i], uv, draw, first[i].view, error)) return false;
    }

    if (includeLighting && !PrepareLighting(points, worldPositions, draw,
                                            applyModelTransform, first, error)) return false;

    // Clip the complete attributes with the same polygon intersections as position and UV.
    ClipVertex polygonA[8] = {first[0], first[1], first[2]};
    ClipVertex polygonB[8]{};
    uint32_t polygonCount=3;
    uint32_t clippedCount=0;
    if (!ClipLightingPlane(polygonA, polygonCount, 0.1, true, polygonB, clippedCount) ||
        !ClipLightingPlane(polygonB, clippedCount, 1000.0, false, polygonA, polygonCount)) {
        error.Assign("The clipped triangle exceeds the polygon scratch capacity"); return false;
    }
    if (polygonCount < 3) return true;
    const uint32_t vertexCount = (polygonCount - 2) * 3;
    ProjectedWorldVertex projectedPolygon[8]{};
    for (uint32_t i = 0; i < polygonCount; ++i) {
        if (!ProjectView(polygonA[i].view, frame.width, frame.height, draw.color,
                         linearColor, projectedPolygon[i].surface, error)) return false;
        for (uint32_t axis = 0; axis < 3; ++axis) {
            projectedPolygon[i].worldNormal[axis] = polygonA[i].normal[axis];
            if (!StoreFloat(polygonA[i].viewDirection[axis],
                            projectedPolygon[i].viewDirection[axis], error)) return false;
        }
    }
    uint32_t outputIndex = 0;
    for (uint32_t i = 1; i + 1 < polygonCount; ++i) {
        output[outputIndex++] = projectedPolygon[0];
        output[outputIndex++] = projectedPolygon[i];
        output[outputIndex++] = projectedPolygon[i + 1];
    }
    outputCount = vertexCount;
    return true;
}


} // namespace gk::render
