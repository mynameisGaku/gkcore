#include "Draw2D.h"

#include "../core/Context.h"
#include "../core/Frame.h"
#include "../resources/Resources.h"

#include <math.h>

namespace gk {
namespace {
/**
 * Rejects non-finite values before they are stored in a draw packet.
 */
bool IsFinite(float value) { return isfinite(value) != 0; }
}

int DrawRect(float x, float y, float width, float height, uint32_t color, bool filled) {
    if (!IsFinite(x) || !IsFinite(y) || !IsFinite(width) || !IsFinite(height) ||
        width <= 0.0f || height <= 0.0f || !IsFinite(x + width) || !IsFinite(y + height))
        return detail::SetError("rectangle values must be finite with positive dimensions");
    detail::DrawPacket packet{};
    packet.kind = detail::DrawKind::Rect;
    packet.flags = filled ? static_cast<uint8_t>(detail::DrawFilled) : 0u;
    packet.rect[0] = x;
    packet.rect[1] = y;
    packet.rect[2] = width;
    packet.rect[3] = height;
    packet.color = color;
    return detail::QueueDraw(packet);
}

int DrawImage(ImageHandle image, float x, float y, bool alphaBlend) {
    if (!image.IsValid()) return detail::SetError("invalid image handle");
    if (!IsFinite(x) || !IsFinite(y)) return detail::SetError("image coordinates must be finite");
    detail::ImageResource* resource = detail::FindImage(image);
    if (!resource) return detail::SetError("invalid image handle");
    detail::DrawPacket packet{};
    packet.kind = detail::DrawKind::Image;
    packet.flags = alphaBlend ? static_cast<uint8_t>(detail::DrawAlphaBlend) : 0u;
    packet.resource = image.value;
    packet.image = resource;
    packet.rect[0] = x;
    packet.rect[1] = y;
    packet.scaleX = 1.0f;
    packet.scaleY = 1.0f;
    return detail::QueueDraw(packet);
}

int DrawImageRotated(ImageHandle image, float centerX, float centerY,
                     float scale, float angleRadians, bool alphaBlend) {
    if (!image.IsValid()) return detail::SetError("invalid image handle");
    if (!IsFinite(centerX) || !IsFinite(centerY) || !IsFinite(scale) ||
        !IsFinite(angleRadians) || scale <= 0.0f)
        return detail::SetError("image transform must be finite and scale must be positive");
    detail::ImageResource* resource = detail::FindImage(image);
    if (!resource) return detail::SetError("invalid image handle");
    detail::DrawPacket packet{};
    packet.kind = detail::DrawKind::Image;
    packet.flags = static_cast<uint8_t>((alphaBlend ? static_cast<uint8_t>(detail::DrawAlphaBlend) : 0u) |
                                         static_cast<uint8_t>(detail::DrawImageCentered));
    packet.resource = image.value;
    packet.image = resource;
    packet.rect[0] = centerX;
    packet.rect[1] = centerY;
    packet.rotation = angleRadians;
    packet.scaleX = scale;
    packet.scaleY = scale;
    return detail::QueueDraw(packet);
}

} // namespace gk
