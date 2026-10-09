#include "platform/WindowsText.h"
#include "resources/Resources.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

void Require(bool condition, const char* message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

bool HasVisiblePixel(const gk::detail::ImageResource& image)
{
    const uint8_t* pixels = image.rgba.Data();
    for (uint32_t i = 3; i < image.rgba.Count(); i += 4)
    {
        if (pixels[i] != 0)
            return true;
    }
    return false;
}

void TestRasterizerProducesVisiblePixels()
{
    gk::String error;
    gk::detail::ImageResource* image = gk::detail::RasterizeWindowsText("gkcore", 24, 0x00ffffffu, error);
    Require(image != nullptr, error.Empty() ? "GDI rasterizer returns an image" : error.CStr());
    Require(image->width != 0 && image->height != 0, "GDI rasterizer returns positive image dimensions");
    Require(HasVisiblePixel(*image), "GDI drawing call writes nonzero glyph coverage");
    gk::Release(&image->reference);
}

void TestTabsMatchFourSpaces()
{
    gk::String error;
    gk::detail::ImageResource* withTab = gk::detail::RasterizeWindowsText("a\tb", 24, 0x00123456u, error);
    Require(withTab != nullptr, error.Empty() ? "GDI rasterizes tabbed text" : error.CStr());
    gk::detail::ImageResource* withSpaces = gk::detail::RasterizeWindowsText("a    b", 24, 0x00123456u, error);
    Require(withSpaces != nullptr, error.Empty() ? "GDI rasterizes spaced text" : error.CStr());

    const bool equal = withTab->width == withSpaces->width && withTab->height == withSpaces->height && withTab->rgba.Count() == withSpaces->rgba.Count() && std::memcmp(withTab->rgba.Data(), withSpaces->rgba.Data(), withTab->rgba.Count()) == 0;
    gk::Release(&withTab->reference);
    gk::Release(&withSpaces->reference);
    Require(equal, "each tab rasterizes exactly like four spaces");
}

}

int main()
{
    TestRasterizerProducesVisiblePixels();
    TestTabsMatchFourSpaces();
    return 0;
}
