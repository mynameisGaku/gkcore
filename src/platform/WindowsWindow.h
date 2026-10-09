// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_PLATFORM_WINDOWSWINDOW_H
#define GKCORE_PLATFORM_WINDOWSWINDOW_H

/**
 * Win32 window表示の判定を初期化処理とCPU契約テストで共有する。
 */
namespace gk::platform
{
/**
 * capture用buildだけwindowを隠し、通常buildでは表示するかを返す。
 */
constexpr bool ShouldShowWindowForBuild(bool frameCaptureBuild)
{
    return !frameCaptureBuild;
}
}

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>

#include "platform/FKeyboardState.h"

/**
 * Native window, module path, and SDK configuration utilities.
 */
namespace gk::platform
{

/**
 * Owns the Win32 window and dispatches its thread messages.
 */
class WindowsWindow
{
  public:
    /**
     * Creates a resizable window with the requested client size.
     */
    bool Initialize(int width, int height, uint32_t& errorCode);
    /**
     * Releases the window and its registered class without queuing WM_QUIT.
     */
    void Shutdown();
    /**
     * Dispatches queued thread messages and reports whether the window closed.
     */
    int ProcessMessages();
    /**
     * Reads the current state of a Win32 virtual key.
     */
    bool IsKeyDown(int virtualKey) const;
    /**
     * 直近のイベント処理で受け取った押下を、次の処理まで保持して返す。
     */
    bool WasKeyPressed(int virtualKey) const;
    /**
     * Returns the current positive client-area dimensions.
     */
    bool GetClientSize(int& width, int& height) const;
    /**
     * Reports whether the native window owns keyboard and mouse input focus.
     */
    bool HasInputFocus() const;
    /**
     * Reads a mouse button while this window owns input focus.
     */
    bool IsMouseButtonDown(int virtualKey) const;
    /**
     * Reads the cursor position relative to the client area while focused.
     */
    bool GetMousePosition(int& x, int& y) const;
    /**
     * Returns the native HWND passed to The Forge.
     */
    HWND NativeHandle() const
    {
        return window_;
    }

  private:
    /**
     * Win32のメッセージをキー状態へ反映し、ウィンドウの終了を処理する。
     */
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    /**
     * focus取得時の押下中キーを、新しい押下として扱わず登録する。
     */
    void SeedHeldKeys();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    bool classRegistered_ = false;
    bool closing_ = false;
    FKeyboardState keyboardState_; // ウィンドウのイベント処理で保持する押下履歴。
    MSG message_{};
};

/**
 * Returns a module's containing directory with a trailing separator.
 */
bool GetModuleDirectory(HMODULE module, wchar_t* directory, uint32_t capacity, uint32_t& errorCode);
/**
 * Selects the bundled Direct3D 12 Agility SDK for this process.
 */
bool ConfigureAgilitySdk(const wchar_t* runtimeDirectory, uint32_t sdkVersion, HMODULE& runtimeModule, uint32_t& errorCode);

}

#endif

#endif
