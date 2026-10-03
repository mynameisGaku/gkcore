// Compile-only Windows SDK/header compatibility check: Windows.h first.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gkcore.h>

int gkcore_header_after_windows_h() {
    return gk::ColorRGB(1, 2, 3) == 0x010203u &&
                   gk::DrawString(0.0f, 0.0f, "text", gk::ColorRGB(255, 255, 255)) == 0
               ? 0
               : 1;
}
