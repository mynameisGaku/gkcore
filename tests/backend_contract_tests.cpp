// Windows hardware-GPU smoke test for the real Runtime backend.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gkcore.h>

#include <cstdio>

#ifndef GKCORE_TEST_SOURCE_DIR
#define GKCORE_TEST_SOURCE_DIR "."
#endif

namespace {

struct WindowSearch {
    HWND window = nullptr;
};

BOOL CALLBACK FindThreadWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    if (IsWindowVisible(window)) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND FindRuntimeWindow() {
    WindowSearch search{};
    EnumThreadWindows(GetCurrentThreadId(), FindThreadWindow, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

bool Fail(const char* operation) {
    const char* detail = gk::GetLastErrorMessage();
    std::fprintf(stderr, "%s failed%s%s\n", operation,
                 detail && detail[0] ? ": " : "", detail && detail[0] ? detail : "");
    return false;
}

bool Check(int result, const char* operation) {
    return result == 0 || Fail(operation);
}

bool Win32Fail(const char* operation) {
    std::fprintf(stderr, "%s failed: Win32 error %lu\n", operation, GetLastError());
    return false;
}

} // namespace

int main() {
    bool initialized = false;
    bool passed = false;
    gk::ImageHandle image{};
    do {
        if (!Check(gk::SetWindowSize(800, 600), "gk::SetWindowSize") ||
            !Check(gk::Init(), "gk::Init")) break;
        initialized = true;

        if (!Check(gk::SetBloomEnabled(true), "gk::SetBloomEnabled(true)") ||
            !Check(gk::SetBloomIntensity(0.15f), "gk::SetBloomIntensity") ||
            !Check(gk::SetExposure(1.0f), "gk::SetExposure") ||
            !Check(gk::SetToneMappingEnabled(true), "gk::SetToneMappingEnabled(true)")) break;

        char imagePath[MAX_PATH * 4 + 64]{};
        const int pathLength = std::snprintf(imagePath, sizeof(imagePath),
                                             "%s/tests/assets/sprite_alpha.png",
                                             GKCORE_TEST_SOURCE_DIR);
        if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(imagePath)) {
            std::fprintf(stderr, "Could not construct the alpha PNG fixture path\n");
            break;
        }
        image = gk::LoadImage(imagePath);
        if (!image.IsValid()) {
            Fail("gk::LoadImage tests/assets/sprite_alpha.png");
            break;
        }

        HWND window = FindRuntimeWindow();
        if (!window) {
            Fail("FindRuntimeWindow: no visible gkcore window owned by this thread");
            break;
        }

        RECT outer{};
        if (!GetWindowRect(window, &outer)) {
            Win32Fail("GetWindowRect");
            break;
        }
        RECT desired{0, 0, 960, 540};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        if (!AdjustWindowRectEx(&desired, style, FALSE, exStyle) ||
            !SetWindowPos(window, nullptr, outer.left, outer.top,
                          desired.right - desired.left, desired.bottom - desired.top,
                          SWP_NOZORDER | SWP_NOACTIVATE)) {
            Win32Fail("resize gkcore window to 960x540");
            break;
        }

        bool eventsOk = true;
        for (int i = 0; i < 8; ++i) {
            if (!gk::ProcessEvents()) {
                Fail("gk::ProcessEvents after resize");
                eventsOk = false;
                break;
            }
            Sleep(10);
        }
        if (!eventsOk) break;
        if (!gk::ProcessEvents()) {
            Fail("gk::ProcessEvents after resize settle");
            break;
        }
        RECT client{};
        if (!GetClientRect(window, &client)) {
            Win32Fail("GetClientRect after resize");
            break;
        }
        if (client.right - client.left != 960 || client.bottom - client.top != 540) {
            std::fprintf(stderr, "Resized client area is %ldx%ld, expected 960x540\n",
                         client.right - client.left, client.bottom - client.top);
            break;
        }

        if (!Check(gk::BeginFrame(), "gk::BeginFrame") ||
            !Check(gk::SetDrawLayer(gk::DrawLayer::Scene), "gk::SetDrawLayer(Scene)") ||
            !Check(gk::DrawRect(24.0f, 24.0f, 156.0f, 116.0f,
                                gk::ColorRGB(230, 80, 40), true), "gk::DrawRect scene") ||
            !Check(gk::DrawTriangle3D(gk::Vec3{-1.0f, 0.0f, 2.0f},
                                      gk::Vec3{1.0f, 0.0f, 2.0f},
                                      gk::Vec3{0.0f, 1.5f, 2.0f},
                                      gk::ColorRGB(40, 180, 240), true), "gk::DrawTriangle3D") ||
            !Check(gk::DrawImage(image, 210.0f, 32.0f, true), "gk::DrawImage alpha PNG") ||
            !Check(gk::SetDrawLayer(gk::DrawLayer::UI), "gk::SetDrawLayer(UI)") ||
            !Check(gk::DrawString(24.0f, 180.0f,
                                  "\xE6\x8F\x8F\xE7\x94\xBB\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88",
                                  gk::ColorRGB(255, 255, 255)), "gk::DrawString Japanese UI text")) break;
        if (!Check(gk::DeleteImage(image), "gk::DeleteImage after queuing")) break;
        image = {};
        if (!Check(gk::Present(), "gk::Present effects, alpha sprite, Japanese UI, and resized window")) break;

        if (!PostMessageW(window, WM_CLOSE, 0, 0)) {
            Win32Fail("PostMessageW(WM_CLOSE)");
            break;
        }
        bool closed = false;
        for (int i = 0; i < 100; ++i) {
            if (!gk::ProcessEvents()) {
                closed = true;
                break;
            }
            Sleep(1);
        }
        if (!closed) {
            Fail("event polling did not report WM_CLOSE");
            break;
        }

        gk::Shutdown();
        initialized = false;
        if (!Check(gk::Init(), "gk::Init after a complete shutdown")) break;
        initialized = true;
        if (!gk::ProcessEvents()) {
            Fail("gk::ProcessEvents after shutdown and reinitialization");
            break;
        }
        if (!Check(gk::BeginFrame(), "gk::BeginFrame after reinitialization") ||
            !Check(gk::Present(), "gk::Present with default effects after reinitialization")) break;
        passed = true;
    } while (false);

    if (image.IsValid()) gk::DeleteImage(image);
    if (initialized) gk::Shutdown();
    return passed ? 0 : 1;
}
