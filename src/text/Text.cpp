#include "Text.h"

#include "TextCache.h"
#include "../core/Context.h"
#include "../core/Frame.h"

#include <math.h>

namespace gk::detail {
namespace {
/**
 * Decodes one Unicode scalar and rejects malformed or non-shortest UTF-8.
 */
bool DecodeOne(const unsigned char*& cursor, uint32_t& codepoint) {
    const unsigned char first = *cursor;
    if (first < 0x80) {
        codepoint = first;
        ++cursor;
        return true;
    }
    uint32_t remaining;
    uint32_t value;
    uint32_t minimum;
    if (first >= 0xc2 && first <= 0xdf) { remaining = 1; value = first & 0x1f; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { remaining = 2; value = first & 0x0f; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; value = first & 0x07; minimum = 0x10000; }
    else return false;
    ++cursor;
    for (uint32_t i = 0; i < remaining; ++i) {
        if (!cursor[i] || (cursor[i] & 0xc0) != 0x80) return false;
        value = (value << 6) | (cursor[i] & 0x3f);
    }
    cursor += remaining;
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    codepoint = value;
    return true;
}
} // namespace

bool ValidateText(const char* text, uint32_t pixelSize, uint32_t& byteLength) {
    byteLength = 0;
    if (!text || pixelSize < 1 || pixelSize > 256) return false;
    while (byteLength <= 4096 && text[byteLength]) ++byteLength;
    if (byteLength > 4096) return false;

    uint32_t lineColumns = 0;
    uint32_t maximumColumns = 0;
    uint32_t lines = 1;
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text);
    const unsigned char* end = cursor + byteLength;
    while (cursor < end) {
        uint32_t codepoint = 0;
        if (!DecodeOne(cursor, codepoint)) return false;
        if (codepoint == '\n') {
            if (lineColumns > maximumColumns) maximumColumns = lineColumns;
            lineColumns = 0;
            ++lines;
            continue;
        }
        if ((codepoint < 0x20 && codepoint != '\t') || (codepoint >= 0x7f && codepoint <= 0x9f))
            return false;
        lineColumns += codepoint == '\t' ? 4u : 1u;
    }
    if (lineColumns > maximumColumns) maximumColumns = lineColumns;
    const uint64_t estimatedWidth = static_cast<uint64_t>(maximumColumns) * pixelSize * 2u + 2u;
    const uint64_t estimatedHeight = static_cast<uint64_t>(lines) * pixelSize * 2u + 2u;
    return estimatedWidth <= 8192 && estimatedHeight <= 4096 &&
           estimatedWidth * estimatedHeight <= 16ull * 1024ull * 1024ull;
}
} // namespace gk::detail

namespace gk {

int DrawString(float x, float y, const char* text, uint32_t color, uint32_t pixelSize) {
    detail::Context& context = detail::GetContext();
    if (!context.initialized || !context.backend) return detail::SetError("framework is not initialized");
    if (!context.frameOpen) return detail::SetError("BeginFrame must be called before drawing");
    if (!isfinite(x) || !isfinite(y)) return detail::SetError("text coordinates must be finite");
    uint32_t byteLength = 0;
    if (!detail::ValidateText(text, pixelSize, byteLength))
        return detail::SetError("text must be valid UTF-8 and fit supported raster bounds");
    if (!byteLength) {
        detail::ClearError();
        return 0;
    }

    String error;
    detail::ImageResource* image = detail::GetCachedTextImage(*context.backend, text, pixelSize, color, error);
    if (!image) return detail::SetError(error.Empty() ? "system font rasterization failed" : error.CStr());
    detail::DrawPacket packet{};
    packet.kind = detail::DrawKind::Image;
    packet.flags = detail::DrawAlphaBlend;
    packet.image = image;
    packet.rect[0] = x;
    packet.rect[1] = y;
    packet.scaleX = 1.0f;
    packet.scaleY = 1.0f;
    const int result = detail::QueueDraw(packet);
    gk::Release(&image->reference);
    return result;
}

} // namespace gk
