#include "WindowsWindow.h"

#if defined(_WIN32)

#include <windows.h>
#include <shlwapi.h>
#include <d3d12.h>

#include <stdint.h>

namespace gk::platform {
namespace {

constexpr wchar_t kWindowClassName[] = L"GkCoreWindowClass";
bool agilitySdkConfigured = false;

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}

bool WindowsWindow::Initialize(int width, int height, uint32_t& errorCode) {
    errorCode = ERROR_SUCCESS;
    if (window_ || width <= 0 || height <= 0) {
        errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }

    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&windowClass)) {
        errorCode = GetLastError();
        if (errorCode != ERROR_CLASS_ALREADY_EXISTS) return false;
    } else {
        classRegistered_ = true;
    }

    RECT rectangle{0, 0, width, height};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    if (!AdjustWindowRect(&rectangle, style, FALSE)) {
        errorCode = GetLastError();
        return false;
    }
    window_ = CreateWindowExW(0, kWindowClassName, L"gkcore", style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              rectangle.right - rectangle.left,
                              rectangle.bottom - rectangle.top,
                              nullptr, nullptr, instance_, nullptr);
    if (!window_) {
        errorCode = GetLastError();
        return false;
    }
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    closing_ = false;
    return true;
}

void WindowsWindow::Shutdown() {
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (classRegistered_) {
        UnregisterClassW(kWindowClassName, instance_);
        classRegistered_ = false;
    }
    instance_ = nullptr;
    closing_ = false;
}

int WindowsWindow::ProcessMessages() {
    if (!window_ || closing_) return -1;
    while (PeekMessageW(&message_, nullptr, 0, 0, PM_REMOVE)) {
        if (message_.message == WM_QUIT) {
            closing_ = true;
            return -1;
        }
        TranslateMessage(&message_);
        DispatchMessageW(&message_);
        if (!IsWindow(window_)) {
            window_ = nullptr;
            closing_ = true;
            return -1;
        }
    }
    return closing_ ? -1 : 0;
}

bool WindowsWindow::IsKeyDown(int virtualKey) const {
    return HasInputFocus() && virtualKey >= 0 && virtualKey <= 255 &&
           (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool WindowsWindow::GetClientSize(int& width, int& height) const {
    RECT client{};
    if (!window_ || !GetClientRect(window_, &client)) return false;
    width = client.right - client.left;
    height = client.bottom - client.top;
    return width > 0 && height > 0;
}

bool WindowsWindow::HasInputFocus() const {
    return window_ && GetFocus() == window_;
}

bool WindowsWindow::IsMouseButtonDown(int virtualKey) const {
    return HasInputFocus() && virtualKey >= 0 && virtualKey <= 255 &&
           (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool WindowsWindow::GetMousePosition(int& x, int& y) const {
    if (!HasInputFocus()) return false;
    POINT position{};
    if (!GetCursorPos(&position) || !ScreenToClient(window_, &position)) return false;
    x = position.x;
    y = position.y;
    return true;
}

bool GetModuleDirectory(HMODULE module, wchar_t* directory, uint32_t capacity, uint32_t& errorCode) {
    errorCode = ERROR_SUCCESS;
    if (!module || !directory || capacity < 2) {
        errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }
    const DWORD length = GetModuleFileNameW(module, directory, capacity);
    if (length == 0 || length >= capacity) {
        errorCode = GetLastError();
        return false;
    }
    uint32_t slash = length;
    while (slash > 0 && directory[slash - 1] != L'\\' && directory[slash - 1] != L'/') --slash;
    if (slash == 0) {
        errorCode = ERROR_BAD_PATHNAME;
        return false;
    }
    directory[slash] = L'\0';
    return true;
}

bool ConfigureAgilitySdk(const wchar_t* runtimeDirectory, uint32_t sdkVersion,
                         HMODULE& runtimeModule, uint32_t& errorCode) {
    errorCode = ERROR_SUCCESS;
    if (agilitySdkConfigured) return true;
    wchar_t executableDirectory[MAX_PATH + 1]{};
    if (!GetModuleDirectory(GetModuleHandleW(nullptr), executableDirectory,
                            static_cast<uint32_t>(sizeof(executableDirectory) / sizeof(executableDirectory[0])), errorCode))
        return false;
    wchar_t relativeSdkDirectory[MAX_PATH + 2]{};
    if (_wcsicmp(executableDirectory, runtimeDirectory) != 0 &&
        !PathRelativePathToW(relativeSdkDirectory, executableDirectory, FILE_ATTRIBUTE_DIRECTORY,
                             runtimeDirectory, FILE_ATTRIBUTE_DIRECTORY)) {
        errorCode = ERROR_BAD_PATHNAME;
        return false;
    }
    wchar_t* sdkDirectory = relativeSdkDirectory;
    uint32_t pathLength = 0;
    while (sdkDirectory[pathLength] != L'\0') ++pathLength;
    if (pathLength >= MAX_PATH) {
        errorCode = ERROR_BUFFER_OVERFLOW;
        return false;
    }
    if (pathLength > 0 && sdkDirectory[pathLength - 1] != L'\\' && sdkDirectory[pathLength - 1] != L'/') {
        sdkDirectory[pathLength++] = L'\\';
        sdkDirectory[pathLength] = L'\0';
    }
    if (pathLength == 0) {
        sdkDirectory[0] = L'.';
        sdkDirectory[1] = L'\\';
        sdkDirectory[2] = L'\0';
        pathLength = 2;
    }
    char sdkDirectoryUtf8[MAX_PATH * 4]{};
    if (pathLength > 0 && !WideCharToMultiByte(CP_UTF8, 0, sdkDirectory, -1, sdkDirectoryUtf8,
                                                static_cast<int>(sizeof(sdkDirectoryUtf8)), nullptr, nullptr)) {
        errorCode = GetLastError();
        return false;
    }

    runtimeModule = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!runtimeModule) {
        errorCode = GetLastError();
        return false;
    }
    using GetInterfaceFunction = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    const auto getInterface = reinterpret_cast<GetInterfaceFunction>(GetProcAddress(runtimeModule, "D3D12GetInterface"));
    if (!getInterface) {
        errorCode = ERROR_PROC_NOT_FOUND;
        return false;
    }
    const GUID configurationClass = {0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}};
    const IID configurationInterface = {0xe9eb5314, 0x33aa, 0x42b2, {0xa7, 0x18, 0xd7, 0x7f, 0x58, 0xb1, 0xf1, 0xc7}};
    ID3D12SDKConfiguration* configuration = nullptr;
    const HRESULT queryResult = getInterface(configurationClass, configurationInterface,
                                              reinterpret_cast<void**>(&configuration));
    if (FAILED(queryResult) || !configuration) {
        if (configuration) configuration->Release();
        errorCode = static_cast<uint32_t>(queryResult);
        return false;
    }
    const HRESULT sdkResult = configuration->SetSDKVersion(sdkVersion, sdkDirectoryUtf8);
    configuration->Release();
    if (FAILED(sdkResult)) {
        errorCode = static_cast<uint32_t>(sdkResult);
        return false;
    }
    agilitySdkConfigured = true;
    return true;
}

}

#endif
