#include "ForgeBackend.h"
#include "../platform/WindowsText.h"

#if defined(_WIN32) && defined(DIRECT3D12)

#include <Utilities/Interfaces/IFileSystem.h>
#include <Utilities/Interfaces/ILog.h>
#include <Utilities/Interfaces/IMemory.h>

#include <stdint.h>
#include <string.h>

#ifdef new
#undef new
#endif
#ifdef delete
#undef delete
#endif

namespace gk::detail
{
namespace
{

void GkCoreModuleAnchor()
{
}

bool SetError(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

bool ForgeBackend::Initialize(uint32_t width, uint32_t height, uint32_t colorDepth, String& error)
{
    if (initialized_)
        return SetError(error, "gkcore is already initialized");
    if (width == 0 || height == 0)
        return SetError(error, "gkcore requires a positive window size");
    if (colorDepth != 32)
        return SetError(error, "The Direct3D 12 renderer requires 32-bit color mode");

    if (!initMemAlloc("gkcore"))
        return SetError(error, "The Forge memory allocator failed to initialize");
    memoryInitialized_ = true;
    FileSystemInitDesc fileSystemDesc{};
    fileSystemDesc.pAppName = "gkcore";
    fileSystemDesc.mIsTool = true;
    if (!initFileSystem(&fileSystemDesc))
    {
        SetError(error, "The Forge file system failed to initialize");
        Shutdown();
        return false;
    }
    fileSystemInitialized_ = true;
    // ログの同期処理を先に初期化し、パスの再設定時にも警告を出せるようにする。
    initLog(nullptr, DEFAULT_LOG_LEVEL);
    logInitialized_ = true;
    if (!ConfigureResourcePaths(error))
    {
        Shutdown();
        return false;
    }
    addLogFile("gkcore.log", FM_WRITE_ALLOW_READ, eALL);
    initGPUConfiguration(nullptr);
    gpuConfigurationInitialized_ = true;

    HMODULE runtimeModule = nullptr;
    uint32_t platformError = ERROR_SUCCESS;
    if (!platform::ConfigureAgilitySdk(moduleDirectoryWide_, 715, runtimeModule, platformError))
    {
        d3d12RuntimeModule_ = runtimeModule;
        SetError(error, "Could not configure bundled Direct3D 12 SDK 715");
        Shutdown();
        return false;
    }
    d3d12RuntimeModule_ = runtimeModule;

    if (!window_.Initialize(static_cast<int>(width), static_cast<int>(height), platformError))
    {
        SetError(error, "Could not create the gkcore Win32 window");
        Shutdown();
        return false;
    }
    if (!renderer_.Initialize(window_.NativeHandle(), width, height, error))
    {
        Shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    initialized_ = true;
    resizeFailed_ = false;
    error.Clear();
    return true;
}

void ForgeBackend::Shutdown()
{
    renderer_.Shutdown();
    window_.Shutdown();
    if (d3d12RuntimeModule_)
    {
        FreeLibrary(d3d12RuntimeModule_);
        d3d12RuntimeModule_ = nullptr;
    }
    if (gpuConfigurationInitialized_)
    {
        exitGPUConfiguration();
        gpuConfigurationInitialized_ = false;
    }
    if (logInitialized_)
    {
        exitLog();
        logInitialized_ = false;
    }
    if (fileSystemInitialized_)
    {
        exitFileSystem();
        fileSystemInitialized_ = false;
    }
    if (memoryInitialized_)
    {
        exitMemAlloc();
        memoryInitialized_ = false;
    }
    initialized_ = false;
    resizeFailed_ = false;
    width_ = height_ = 0;
}

int ForgeBackend::ProcessMessage()
{
    if (!initialized_)
        return -2;
    const int messageStatus = window_.ProcessMessages();
    if (messageStatus != 0)
        return messageStatus;
    int clientWidth = 0, clientHeight = 0;
    if (window_.GetClientSize(clientWidth, clientHeight) && (static_cast<uint32_t>(clientWidth) != width_ || static_cast<uint32_t>(clientHeight) != height_))
    {
        String error;
        if (!renderer_.Resize(static_cast<uint32_t>(clientWidth), static_cast<uint32_t>(clientHeight), error))
        {
            resizeFailed_ = true;
            return -2;
        }
        width_ = static_cast<uint32_t>(clientWidth);
        height_ = static_cast<uint32_t>(clientHeight);
        resizeFailed_ = false;
    }
    return resizeFailed_ ? -2 : 0;
}

bool ForgeBackend::GetClientSize(uint32_t& width, uint32_t& height) const
{
    int clientWidth = 0, clientHeight = 0;
    if (!initialized_ || !window_.GetClientSize(clientWidth, clientHeight))
        return false;
    width = static_cast<uint32_t>(clientWidth);
    height = static_cast<uint32_t>(clientHeight);
    return true;
}

bool ForgeBackend::IsKeyDown(uint32_t keyCode) const
{
    return initialized_ && window_.IsKeyDown(static_cast<int>(keyCode));
}

bool ForgeBackend::HasInputFocus() const
{
    return initialized_ && window_.HasInputFocus();
}

/**
 * 初期化済みのウィンドウから押下イベントを問い合わせる。
 */
bool ForgeBackend::WasKeyPressed(uint32_t keyCode) const
{
    return initialized_ && window_.WasKeyPressed(static_cast<int>(keyCode));
}

bool ForgeBackend::SupportsMouseInput() const
{
    return initialized_;
}

bool ForgeBackend::IsMouseButtonDown(uint32_t button) const
{
    if (!initialized_ || (button != VK_LBUTTON && button != VK_RBUTTON && button != VK_MBUTTON))
        return false;
    return window_.IsMouseButtonDown(static_cast<int>(button));
}

bool ForgeBackend::GetMousePosition(int32_t& x, int32_t& y) const
{
    if (!initialized_)
        return false;
    int mouseX = 0, mouseY = 0;
    if (!window_.GetMousePosition(mouseX, mouseY))
        return false;
    x = static_cast<int32_t>(mouseX);
    y = static_cast<int32_t>(mouseY);
    return true;
}

ImageResource* ForgeBackend::RasterizeText(const char* utf8Text, uint32_t pixelSize, uint32_t packedColor, String& error)
{
    if (!initialized_)
    {
        error.Assign("The Forge renderer is not initialized");
        return nullptr;
    }
    return RasterizeWindowsText(utf8Text, pixelSize, packedColor, error);
}

bool ForgeBackend::Present(const FramePacket& frame, String& error)
{
    if (!initialized_)
        return SetError(error, "The Forge renderer is not initialized");
    return renderer_.Present(frame, error);
}

ShaderHandle ForgeBackend::LoadPixelShader(const char* path, String& error)
{
    if (!initialized_)
    {
        error.Assign("The Forge renderer is not initialized");
        return ShaderHandle();
    }
    return renderer_.LoadPixelShader(path, error);
}

bool ForgeBackend::ReleasePixelShader(ShaderHandle shader, String& error)
{
    if (!initialized_)
        return SetError(error, "The Forge renderer is not initialized");
    return renderer_.ReleasePixelShader(shader, error);
}

bool ForgeBackend::ConfigureResourcePaths(String& error)
{
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCWSTR>(&GkCoreModuleAnchor);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, address, &module))
        return SetError(error, "Could not locate the gkcore runtime module");
    uint32_t platformError = ERROR_SUCCESS;
    if (!platform::GetModuleDirectory(module, moduleDirectoryWide_, MAX_PATH + 1, platformError))
        return SetError(error, "Could not determine the gkcore runtime module directory");
    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, moduleDirectoryWide_, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 1 || static_cast<uint32_t>(utf8Length) > sizeof(moduleDirectoryUtf8_))
        return SetError(error, "Could not convert the gkcore runtime directory to UTF-8");
    if (!WideCharToMultiByte(CP_UTF8, 0, moduleDirectoryWide_, -1, moduleDirectoryUtf8_, sizeof(moduleDirectoryUtf8_), nullptr, nullptr))
        return SetError(error, "Could not convert the gkcore runtime directory to UTF-8");
    fsSetPathForResourceDir(pSystemFileIO, RD_OTHER_FILES, moduleDirectoryUtf8_);
    fsSetPathForResourceDir(pSystemFileIO, RD_GPU_CONFIG, moduleDirectoryUtf8_);
    fsSetPathForResourceDir(pSystemFileIO, RD_LOG, moduleDirectoryUtf8_);
    char shaderDirectory[MAX_PATH * 4 + 32]{};
    const uint32_t length = static_cast<uint32_t>(strlen(moduleDirectoryUtf8_));
    if (length + sizeof("CompiledShaders\\") > sizeof(shaderDirectory))
        return SetError(error, "The runtime shader directory path is too long");
    memcpy(shaderDirectory, moduleDirectoryUtf8_, length);
    memcpy(shaderDirectory + length, "CompiledShaders\\", sizeof("CompiledShaders\\"));
    fsSetPathForResourceDir(pSystemFileIO, RD_SHADER_BINARIES, shaderDirectory);
    error.Clear();
    return true;
}

Backend* CreateNativeBackend()
{
    try
    {
        return new ForgeBackend();
    }
    catch (...)
    {
        return nullptr;
    }
}

}

#else

namespace gk::detail
{
namespace
{

class UnsupportedForgeBackend final : public Backend
{
  public:
    bool Initialize(uint32_t, uint32_t, uint32_t, String& error) override
    {
        error.Assign("The Forge Direct3D 12 backend is available only in a Windows runtime build");
        return false;
    }
    void Shutdown() override
    {
    }
    int ProcessMessage() override
    {
        return -2;
    }
    bool IsKeyDown(uint32_t) const override
    {
        return false;
    }
    bool Present(const FramePacket&, String& error) override
    {
        error.Assign("The Forge Direct3D 12 backend is unavailable on this platform");
        return false;
    }
    ShaderHandle LoadPixelShader(const char*, String& error) override
    {
        error.Assign("The Forge Direct3D 12 backend is unavailable on this platform");
        return ShaderHandle();
    }
    bool ReleasePixelShader(ShaderHandle, String& error) override
    {
        error.Assign("The Forge Direct3D 12 backend is unavailable on this platform");
        return false;
    }
};

}

Backend* CreateNativeBackend()
{
    try
    {
        return new UnsupportedForgeBackend();
    }
    catch (...)
    {
        return nullptr;
    }
}

}

#endif
