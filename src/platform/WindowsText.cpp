#include "WindowsText.h"

#if defined(_WIN32)
#include "../foundation/Memory.h"
#include "../image/Image.h"
#include "../text/TextLayout.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gk::detail {
namespace {
/**
 * Owns temporary GDI objects and restores selected objects before deletion.
 */
struct GdiObjects {
    HDC dc = nullptr;
    HFONT font = nullptr;
    HGDIOBJ previousFont = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    void* pixels = nullptr;
    ~GdiObjects() {
        if (dc && previousFont) SelectObject(dc, previousFont);
        if (dc && previousBitmap) SelectObject(dc, previousBitmap);
        if (font) DeleteObject(font);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
};
}

ImageResource* RasterizeWindowsText(const char* text, uint32_t pixelSize,
                                    uint32_t color, String& error) {
    if (!text || pixelSize < 1 || pixelSize > 256) {
        error.Assign("invalid text or system font size");
        return nullptr;
    }
    String expandedText;
    if (!ExpandTextTabs(text, expandedText, error)) return nullptr;
    const char* rasterText = expandedText.CStr();
    const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rasterText, -1, nullptr, 0);
    if (wideCount <= 1 || wideCount > 16385) {
        error.Assign("text is not valid bounded UTF-8");
        return nullptr;
    }
    wchar_t* wideText = static_cast<wchar_t*>(Allocate(static_cast<size_t>(wideCount) * sizeof(wchar_t)));
    if (!wideText) {
        error.Assign("not enough memory to convert UTF-8 text");
        return nullptr;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rasterText, -1, wideText, wideCount)) {
        Deallocate(wideText);
        error.Assign("UTF-8 conversion failed");
        return nullptr;
    }

    GdiObjects objects;
    objects.dc = CreateCompatibleDC(nullptr);
    if (!objects.dc) {
        Deallocate(wideText);
        error.Assign("could not create a system font drawing context");
        return nullptr;
    }
    LOGFONTW description{};
    HFONT defaultFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (defaultFont) GetObjectW(defaultFont, sizeof(description), &description);
    description.lfHeight = -static_cast<LONG>(pixelSize);
    description.lfWidth = 0;
    description.lfCharSet = DEFAULT_CHARSET;
    description.lfQuality = ANTIALIASED_QUALITY;
    objects.font = CreateFontIndirectW(&description);
    if (!objects.font) {
        Deallocate(wideText);
        error.Assign("could not create the default system font");
        return nullptr;
    }
    objects.previousFont = SelectObject(objects.dc, objects.font);
    SetBkMode(objects.dc, TRANSPARENT);
    SetTextColor(objects.dc, RGB(255, 255, 255));

    RECT bounds{0, 0, 8192, 4096};
    const UINT drawFlags = DT_LEFT | DT_TOP | DT_NOPREFIX;
    const int measuredHeight = DrawTextW(objects.dc, wideText, -1, &bounds,
                                          DT_CALCRECT | drawFlags);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (measuredHeight <= 0 || width <= 0 || height <= 0 || width > 8192 || height > 4096 ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > 16ull * 1024ull * 1024ull) {
        Deallocate(wideText);
        error.Assign("text raster exceeds supported image bounds");
        return nullptr;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    objects.bitmap = CreateDIBSection(objects.dc, &bitmapInfo, DIB_RGB_COLORS,
                                      &objects.pixels, nullptr, 0);
    if (!objects.bitmap || !objects.pixels) {
        Deallocate(wideText);
        error.Assign("could not allocate the text bitmap");
        return nullptr;
    }
    objects.previousBitmap = SelectObject(objects.dc, objects.bitmap);
    const size_t sourceBytes = static_cast<size_t>(width) * height * 4u;
    ZeroMemory(objects.pixels, sourceBytes);
    RECT drawBounds{0, 0, width, height};
    const int drawnHeight = DrawTextW(objects.dc, wideText, -1, &drawBounds, drawFlags);
    Deallocate(wideText);
    if (drawnHeight <= 0) {
        error.Assign("system font drawing failed");
        return nullptr;
    }
    if (!GdiFlush()) {
        error.Assign("could not synchronize system font drawing");
        return nullptr;
    }

    ImageResource* image = CreateImageResource();
    if (!image) {
        error.Assign("not enough memory to create text image");
        return nullptr;
    }
    image->width = static_cast<uint32_t>(width);
    image->height = static_cast<uint32_t>(height);
    if (!image->rgba.Reserve(static_cast<uint32_t>(sourceBytes))) {
        Release(&image->reference);
        error.Assign("not enough memory to retain text pixels");
        return nullptr;
    }
    const unsigned char* bgra = static_cast<const unsigned char*>(objects.pixels);
    const uint8_t red = static_cast<uint8_t>((color >> 16) & 0xffu);
    const uint8_t green = static_cast<uint8_t>((color >> 8) & 0xffu);
    const uint8_t blue = static_cast<uint8_t>(color & 0xffu);
    for (uint64_t pixel = 0; pixel < static_cast<uint64_t>(width) * height; ++pixel) {
        const uint8_t coverage = static_cast<uint8_t>(
            (static_cast<uint32_t>(bgra[pixel * 4]) + bgra[pixel * 4 + 1] + bgra[pixel * 4 + 2]) / 3u);
        const uint8_t rgba[4] = {red, green, blue, coverage};
        if (!image->rgba.AppendRange(rgba, 4)) {
            Release(&image->reference);
            error.Assign("not enough memory to retain text pixels");
            return nullptr;
        }
    }
    return image;
}
} // namespace gk::detail
#endif
