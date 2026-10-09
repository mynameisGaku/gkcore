#include "render/RectangleGeometry.h"
#include "render/PostProcess.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

/**
 * Bounded screen-space rectangle mesh generation.
 */
namespace gk::render
{
/**
 * Small conversion helpers shared by filled and stroked rectangles.
 */
namespace
{
/**
 * Stores one screen coordinate in clip space when it fits the vertex format.
 */
bool StoreClipCoordinate(double value, float& output)
{
    if (!isfinite(value) || fabs(value) > FLT_MAX)
        return false;
    output = static_cast<float>(value);
    return isfinite(output) != 0;
}

/**
 * Maps packed sRGB bytes to the renderer's linear-light vertex color.
 */
void StoreLinearColor(uint32_t packed, float output[4])
{
    output[0] = SrgbToLinear(static_cast<float>((packed >> 16) & 255u) / 255.0f);
    output[1] = SrgbToLinear(static_cast<float>((packed >> 8) & 255u) / 255.0f);
    output[2] = SrgbToLinear(static_cast<float>(packed & 255u) / 255.0f);
    output[3] = 1.0f;
}

/**
 * Writes one quad as two triangles with UVs normalized to the complete rectangle.
 */
bool WriteQuad(const detail::FramePacket& frame, const detail::DrawPacket& draw, double x0, double y0, double x1, double y1, const float color[4], Vertex* output, uint32_t& outputCount)
{
    const double rectangleX = draw.rect[0];
    const double rectangleY = draw.rect[1];
    const double rectangleWidth = draw.rect[2];
    const double rectangleHeight = draw.rect[3];
    const double screenX[4] = { x0, x1, x1, x0 };
    const double screenY[4] = { y0, y0, y1, y1 };
    const double uvX[4] = { (x0 - rectangleX) / rectangleWidth, (x1 - rectangleX) / rectangleWidth, (x1 - rectangleX) / rectangleWidth, (x0 - rectangleX) / rectangleWidth };
    const double uvY[4] = { (y0 - rectangleY) / rectangleHeight, (y0 - rectangleY) / rectangleHeight, (y1 - rectangleY) / rectangleHeight, (y1 - rectangleY) / rectangleHeight };
    Vertex corners[4]{};
    for (uint32_t corner = 0; corner < 4; ++corner)
    {
        Vertex& vertex = corners[corner];
        if (!StoreClipCoordinate(screenX[corner] * 2.0 / frame.width - 1.0, vertex.position[0]) || !StoreClipCoordinate(1.0 - screenY[corner] * 2.0 / frame.height, vertex.position[1]) || !StoreClipCoordinate(uvX[corner], vertex.uv[0]) || !StoreClipCoordinate(uvY[corner], vertex.uv[1]))
            return false;
        vertex.position[2] = 0.0f;
        vertex.position[3] = 1.0f;
        for (uint32_t channel = 0; channel < 4; ++channel)
            vertex.color[channel] = color[channel];
    }
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    for (uint32_t index = 0; index < 6; ++index)
    {
        output[outputCount++] = corners[indices[index]];
    }
    return true;
}

/**
 * Builds the local geometry for one filled or four-strip outline rectangle.
 */
bool BuildRectangle(const detail::FramePacket& frame, const detail::DrawPacket& draw, const float color[4], Vertex* output, uint32_t& outputCount, uint32_t expectedCount)
{
    const double x = draw.rect[0];
    const double y = draw.rect[1];
    const double width = draw.rect[2];
    const double height = draw.rect[3];
    if ((draw.flags & detail::DrawFilled) != 0 || expectedCount == 6)
        return WriteQuad(frame, draw, x, y, x + width, y + height, color, output, outputCount);

    const double thickness = draw.rectOutlineThickness;
    const double innerTop = y + thickness;
    const double innerBottom = y + height - thickness;
    return WriteQuad(frame, draw, x, y, x + width, innerTop, color, output, outputCount) && WriteQuad(frame, draw, x, innerBottom, x + width, y + height, color, output, outputCount) && WriteQuad(frame, draw, x, innerTop, x + thickness, innerBottom, color, output, outputCount) && WriteQuad(frame, draw, x + width - thickness, innerTop, x + width, innerBottom, color, output, outputCount);
}
}

/**
 * Appends one screen-space rectangle while keeping existing output unchanged on failure.
 */
bool AppendRectangle(const detail::FramePacket& frame, const detail::DrawPacket& draw, Array<Vertex>& vertices, uint32_t vertexLimit, String& error)
{
    if (draw.kind != detail::DrawKind::Rect)
    {
        error.Assign("The rectangle geometry packet kind is invalid");
        return false;
    }
    if (!frame.width || !frame.height)
    {
        error.Assign("The frame size must be positive");
        return false;
    }
    const double x = draw.rect[0];
    const double y = draw.rect[1];
    const double width = draw.rect[2];
    const double height = draw.rect[3];
    if (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height) || width <= 0.0 || height <= 0.0 || !isfinite(x + width) || !isfinite(y + height))
    {
        error.Assign("Rectangle coordinates must be finite with positive dimensions");
        return false;
    }
    const bool filled = (draw.flags & detail::DrawFilled) != 0;
    uint32_t neededVertices = 6;
    if (!filled)
    {
        const double thickness = draw.rectOutlineThickness;
        if (!isfinite(thickness) || thickness <= 0.0)
        {
            error.Assign("Rectangle outline thickness must be finite and positive");
            return false;
        }
        if (thickness * 2.0 < (width < height ? width : height))
            neededVertices = 24;
    }
    const uint32_t currentCount = vertices.Count();
    if (currentCount > vertexLimit || neededVertices > vertexLimit - currentCount || neededVertices > UINT32_MAX - currentCount)
    {
        error.Assign("The frame exceeds the dynamic vertex capacity");
        return false;
    }

    float color[4];
    StoreLinearColor(draw.color, color);
    Vertex rectangle[24]{};
    uint32_t outputCount = 0;
    if (!BuildRectangle(frame, draw, color, rectangle, outputCount, neededVertices) || outputCount != neededVertices)
    {
        error.Assign("Rectangle coordinates exceed the renderer's numeric range");
        return false;
    }
    if (!vertices.AppendRange(rectangle, outputCount))
    {
        error.Assign("The frame vertex allocation failed");
        return false;
    }
    error.Clear();
    return true;
}
} // namespace gk::render
