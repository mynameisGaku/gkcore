#pragma once

#include "internal/Backend.hpp"
#include "platform/WindowsWindow.h"

#if defined(_WIN32) && defined(DIRECT3D12)
#include "render/ForgeRenderer.h"

/**
 * Production backend factory and platform service coordination.
 */
namespace gk::detail
{

/**
 * Coordinates Win32 startup with the native Forge renderer.
 */
class ForgeBackend final : public Backend
{
  public:
    /**
     * Initializes The Forge services, Win32 window, and GPU renderer.
     */
    bool Initialize(uint32_t width, uint32_t height, uint32_t colorDepth, String& error) override;
    /**
     * sparse姿勢の位置・法線をGPUで参照するmodel描画を提供する。
     */
    bool SupportsSparseModelPoses() const override
    {
        return true;
    }
    /**
     * rendererが倍精度GPU skinningを使えるか返す。
     */
    bool SupportsGpuModelSkinning() const override;
    /**
     * Releases platform and renderer state in reverse initialization order.
     */
    void Shutdown() override;
    /**
     * Pumps native messages and applies any client-size change.
     */
    int ProcessMessage() override;
    /**
     * Returns nonzero client dimensions for the active window.
     */
    bool GetClientSize(uint32_t& width, uint32_t& height) const override;
    /**
     * Reads a platform virtual-key state while initialized.
     */
    bool IsKeyDown(uint32_t keyCode) const override;
    /**
     * 直近のWin32イベント処理で記録した短いキー押下を返す。
     */
    bool WasKeyPressed(uint32_t keyCode) const override;
    /**
     * Reports whether this runtime window currently owns input focus.
     */
    bool HasInputFocus() const override;
    /**
     * Reports that the Win32 adapter implements mouse input queries.
     */
    bool SupportsMouseInput() const override;
    /**
     * Reads a supported native mouse button while focused.
     */
    bool IsMouseButtonDown(uint32_t button) const override;
    /**
     * Returns focused client-relative cursor coordinates.
     */
    bool GetMousePosition(int32_t& x, int32_t& y) const override;
    /**
     * Rasterizes UTF-8 text into an owned RGBA image for ordinary image drawing.
     */
    ImageResource* RasterizeText(const char* utf8Text, uint32_t pixelSize, uint32_t packedColor, String& error) override;
    /**
     * Forwards a complete frame snapshot to the renderer.
     */
    bool Present(const FramePacket& frame, String& error) override;
    /**
     * Reports that this renderer does not accept custom pixel shaders.
     */
    ShaderHandle LoadPixelShader(const char* path, String& error) override;
    /**
     * Reports that this renderer does not accept custom pixel shaders.
     */
    bool ReleasePixelShader(ShaderHandle shader, String& error) override;

  private:
    bool ConfigureResourcePaths(String& error);
    bool initialized_ = false;
    bool memoryInitialized_ = false;
    bool fileSystemInitialized_ = false;
    bool logInitialized_ = false;
    bool gpuConfigurationInitialized_ = false;
    bool resizeFailed_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    wchar_t moduleDirectoryWide_[MAX_PATH + 1]{};
    char moduleDirectoryUtf8_[MAX_PATH * 4 + 4]{};
    HMODULE d3d12RuntimeModule_ = nullptr;
    platform::WindowsWindow window_;
    render::ForgeRenderer renderer_;
};

}

#endif
