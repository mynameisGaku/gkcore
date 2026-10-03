#include "../src/render/Geometry.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace {

void Require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

void FillImagePixels(gk::detail::ImageResource& image) {
    const uint32_t count = image.width * image.height * 4u;
    for (uint32_t i = 0; i < count; ++i) image.rgba.Append(static_cast<uint8_t>(255));
}

void TestRectClipVertices() {
    gk::detail::FramePacket frame{};
    frame.width = 200;
    frame.height = 100;
    gk::detail::DrawPacket draw{};
    draw.kind = gk::detail::DrawKind::Rect;
    draw.flags = 1;
    draw.rect[0] = 20;
    draw.rect[1] = 10;
    draw.rect[2] = 40;
    draw.rect[3] = 30;
    draw.color = 0xE65028;
    gk::Array<gk::render::Vertex> vertices;
    gk::String error;
    Require(gk::render::AppendDraw(frame, draw, vertices, 32, error), "filled rectangle vertex conversion");
    Require(vertices.Count() == 6, "rectangle expands to two triangles");
    Require(std::fabs(vertices.At(0).position[0] + 0.8f) < 0.0001f, "rectangle left maps to clip space");
    Require(std::fabs(vertices.At(0).position[1] - 0.8f) < 0.0001f, "rectangle top maps to clip space");
    Require(std::fabs(vertices.At(2).position[0] + 0.4f) < 0.0001f, "rectangle right maps to clip space");
    Require(std::fabs(vertices.At(2).color[0] - 0.79129794f) < 0.0001f,
            "packed sRGB red channel is converted to linear light");
}

void TestTriangleProjection() {
    gk::detail::FramePacket frame{};
    frame.width = 800;
    frame.height = 600;
    gk::detail::DrawPacket draw{};
    draw.kind = gk::detail::DrawKind::Triangle3D;
    draw.flags = 1;
    draw.cameraPosition = {0, 0, -5};
    draw.cameraTarget = {0, 0, 0};
    draw.modelScale = {1, 1, 1};
    draw.points[0] = {-1, 0, 0};
    draw.points[1] = {1, 0, 0};
    draw.points[2] = {0, 1, 0};
    draw.color = 0x28B4F0;
    gk::Array<gk::render::Vertex> vertices;
    gk::String error;
    Require(gk::render::AppendDraw(frame, draw, vertices, 32, error), "visible triangle vertex conversion");
    Require(vertices.Count() == 3, "3D triangle remains one triangle");
    bool leftPoint = false, rightPoint = false, upperPoint = false;
    for (uint32_t i = 0; i < vertices.Count(); ++i) {
        leftPoint = leftPoint || vertices.At(i).position[0] < 0.0f;
        rightPoint = rightPoint || vertices.At(i).position[0] > 0.0f;
        upperPoint = upperPoint || vertices.At(i).position[1] > 0.0f;
    }
    Require(leftPoint && rightPoint, "camera projection preserves horizontal orientation");
    Require(upperPoint, "camera projection preserves Y-up orientation");
    Require(std::fabs(vertices.At(0).position[3] - 5.0f) < 0.0001f, "perspective clip W contains view depth");
}

void TestNearPlaneClippingAndDepth() {
    gk::detail::FramePacket frame{};
    frame.width = 800;
    frame.height = 600;
    gk::detail::DrawPacket draw{};
    draw.kind = gk::detail::DrawKind::Triangle3D;
    draw.flags = 1;
    draw.cameraPosition = {0, 0, -5};
    draw.cameraTarget = {0, 0, 0};
    draw.points[0] = {-1, 0, -4.95f};
    draw.points[1] = {1, 0, 0};
    draw.points[2] = {0, 1, 0};
    gk::Array<gk::render::Vertex> nearVertices;
    gk::String error;
    Require(gk::render::AppendDraw(frame, draw, nearVertices, 32, error), "near-plane crossing triangle conversion");
    Require(nearVertices.Count() == 6, "near-plane crossing triangle clips into two triangles");
    float minimumDepth = 1.0f;
    for (uint32_t i = 0; i < nearVertices.Count(); ++i) {
        const auto& vertex = nearVertices.At(i);
        Require(vertex.position[3] >= 0.1f, "clipped vertex remains in front of near plane");
        const float depth = vertex.position[2] / vertex.position[3];
        Require(depth >= -0.0001f && depth <= 1.0001f, "clip-space depth remains in the Direct3D range");
        if (depth < minimumDepth) minimumDepth = depth;
    }
    Require(std::fabs(minimumDepth) < 0.0001f, "near-plane intersection maps to depth zero");

    draw.points[0] = {-1, 0, 0};
    draw.points[1] = {1, 0, 0};
    draw.points[2] = {0, 1, 0};
    gk::Array<gk::render::Vertex> nearDepth;
    Require(gk::render::AppendDraw(frame, draw, nearDepth, 32, error), "near depth triangle conversion");
    draw.points[0].z = draw.points[1].z = draw.points[2].z = 10;
    gk::Array<gk::render::Vertex> farDepth;
    Require(gk::render::AppendDraw(frame, draw, farDepth, 32, error), "far depth triangle conversion");
    Require(nearDepth.Count() == 3 && farDepth.Count() == 3, "depth comparison triangles remain visible");
    Require(nearDepth.At(0).position[2] / nearDepth.At(0).position[3] <
            farDepth.At(0).position[2] / farDepth.At(0).position[3], "depth increases with view distance");
}

void TestOutOfRangeCoordinatesReportError() {
    gk::detail::FramePacket frame{};
    frame.width = 640;
    frame.height = 480;
    gk::detail::DrawPacket draw{};
    draw.kind = gk::detail::DrawKind::Triangle3D;
    draw.flags = 1;
    draw.cameraPosition = {0, 0, -5};
    draw.cameraTarget = {0, 0, 0};
    draw.points[0] = {3.4e38f, 0, 0};
    draw.points[1] = {3.4e38f, 1, 0};
    draw.points[2] = {3.4e38f, 0, 1};
    gk::Array<gk::render::Vertex> vertices;
    gk::String error;
    Require(!gk::render::AppendDraw(frame, draw, vertices, 32, error), "unrepresentable projected coordinates are rejected");
    Require(error.Length() != 0, "coordinate range failure reports a diagnostic");
}

void TestSpritePixelsAndTopLeftUv() {
    gk::detail::ImageResource image{};
    image.width = 40;
    image.height = 20;
    FillImagePixels(image);
    gk::detail::FramePacket frame{};
    frame.width = 200;
    frame.height = 100;
    gk::detail::DrawPacket draw{};
    draw.kind = gk::detail::DrawKind::Image;
    draw.image = &image;
    draw.rect[0] = 20;
    draw.rect[1] = 10;
    draw.scaleX = 1.0f;
    draw.scaleY = 1.0f;
    gk::Array<gk::render::Vertex> vertices;
    gk::String error;
    Require(gk::render::AppendSprite(frame, draw, vertices, 32, error), "image expands to a textured quad");
    Require(vertices.Count() == 6, "sprite expands to two triangles");
    Require(std::fabs(vertices.At(0).position[0] + 0.8f) < 0.0001f, "sprite starts at its top-left X");
    Require(std::fabs(vertices.At(0).position[1] - 0.8f) < 0.0001f, "sprite starts at its top-left Y");
    Require(vertices.At(0).uv[0] == 0.0f && vertices.At(0).uv[1] == 0.0f, "sprite UV origin matches top-left RGBA rows");
    Require(vertices.At(2).uv[0] == 1.0f && vertices.At(2).uv[1] == 1.0f, "sprite UV end matches bottom-right RGBA pixel");
}

void TestCenteredRotatedSpriteGeometry() {
    gk::detail::ImageResource image{};
    image.width = 20;
    image.height = 10;
    FillImagePixels(image);
    gk::detail::FramePacket frame{};
    frame.width = 100;
    frame.height = 100;
    gk::detail::DrawPacket draw{};
    draw.kind = gk::detail::DrawKind::Image;
    draw.flags = gk::detail::DrawImageCentered;
    draw.image = &image;
    draw.rect[0] = 50;
    draw.rect[1] = 50;
    draw.scaleX = 2.0f;
    draw.scaleY = 2.0f;
    draw.rotation = 1.57079632679f;
    gk::Array<gk::render::Vertex> vertices;
    gk::String error;
    Require(gk::render::AppendSprite(frame, draw, vertices, 32, error), "centered rotated sprite conversion");
    Require(vertices.Count() == 6, "centered sprite remains two triangles");
    float minX = vertices.At(0).position[0], maxX = minX;
    float minY = vertices.At(0).position[1], maxY = minY;
    for (uint32_t i = 1; i < vertices.Count(); ++i) {
        if (vertices.At(i).position[0] < minX) minX = vertices.At(i).position[0];
        if (vertices.At(i).position[0] > maxX) maxX = vertices.At(i).position[0];
        if (vertices.At(i).position[1] < minY) minY = vertices.At(i).position[1];
        if (vertices.At(i).position[1] > maxY) maxY = vertices.At(i).position[1];
    }
    Require(std::fabs((maxX - minX) - 0.4f) < 0.0002f, "quarter-turn sprite width follows scaled source height");
    Require(std::fabs((maxY - minY) - 0.8f) < 0.0002f, "quarter-turn sprite height follows scaled source width");
    Require(std::fabs((minX + maxX) * 0.5f) < 0.0001f, "centered sprite remains centered horizontally");
    Require(std::fabs((minY + maxY) * 0.5f) < 0.0001f, "centered sprite remains centered vertically");
}

void TestSpriteRejectsInvalidResourceAndBounds() {
    gk::detail::FramePacket frame{};
    frame.width = 640;
    frame.height = 480;
    gk::detail::DrawPacket draw{};
    draw.kind = gk::detail::DrawKind::Image;
    gk::Array<gk::render::Vertex> vertices;
    gk::String error;
    Require(!gk::render::AppendSprite(frame, draw, vertices, 32, error), "sprite without resource is rejected");
    Require(error.Length() != 0, "invalid sprite reports a diagnostic");
    gk::detail::ImageResource image{};
    image.width = 2;
    image.height = 2;
    FillImagePixels(image);
    draw.image = &image;
    draw.scaleX = 1.0e30f;
    draw.scaleY = 1.0e30f;
    Require(!gk::render::AppendSprite(frame, draw, vertices, 5, error), "sprite beyond vertex bound is rejected");
    Require(vertices.Count() == 0, "failed sprite conversion does not append partial vertices");
}

}

int main() {
    TestRectClipVertices();
    TestTriangleProjection();
    TestNearPlaneClippingAndDepth();
    TestOutOfRangeCoordinatesReportError();
    TestSpritePixelsAndTopLeftUv();
    TestCenteredRotatedSpriteGeometry();
    TestSpriteRejectsInvalidResourceAndBounds();
    return 0;
}
