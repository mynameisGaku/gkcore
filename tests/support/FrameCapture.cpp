#include "tests/support/FrameCapture.h"

#include <Windows.h>

#include <cstdio>
#include <vector>

/**
 * 開発テスト専用のGPU画素読み戻しを実装する。
 */
namespace gk::render::test_support
{

/**
 * ファイル内だけで使う補助処理をまとめる。
 */
namespace
{

/**
 * キャプチャ失敗の理由を呼び出し元へ渡す。
 */
bool SetCaptureError(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * 開発用の数値設定を読み、省略時の値と許可範囲を適用する。
 */
bool ReadUnsignedSetting(const char* name, uint32_t defaultValue, uint32_t minimum, uint32_t maximum, uint32_t& output, String& error)
{
    // 不正値にも設定名と許可範囲が分かる診断を返す。
    char diagnostic[160]{};
    std::snprintf(diagnostic, sizeof(diagnostic), "%s must be an integer from %u through %u", name, minimum, maximum);
    SetLastError(ERROR_SUCCESS);
    const DWORD requiredSize = GetEnvironmentVariableA(name, nullptr, 0);
    const DWORD environmentError = GetLastError();
    if (requiredSize == 0)
    {
        if (environmentError == ERROR_ENVVAR_NOT_FOUND)
        {
            output = defaultValue;
            return true;
        }
        return SetCaptureError(error, diagnostic);
    }
    // 環境変数をコピーし、途中で変わった値や空の値を拒否する。
    std::vector<char> value(requiredSize);
    const DWORD copiedSize = GetEnvironmentVariableA(name, value.data(), requiredSize);
    if (copiedSize == 0 || copiedSize >= requiredSize)
        return SetCaptureError(error, diagnostic);
    uint32_t parsed = 0;
    for (DWORD index = 0; index < copiedSize; ++index)
    {
        const char digit = value[index];
        if (digit < '0' || digit > '9')
            return SetCaptureError(error, diagnostic);
        // 各桁で上限を確認するため、長い入力でも整数があふれない。
        const uint32_t digitValue = static_cast<uint32_t>(digit - '0');
        if (parsed > maximum / 10u || (parsed == maximum / 10u && digitValue > maximum % 10u))
            return SetCaptureError(error, diagnostic);
        parsed = parsed * 10u + digitValue;
    }
    if (parsed < minimum)
        return SetCaptureError(error, diagnostic);
    output = parsed;
    return true;
}

}

/**
 * 保持中のGPU readback resourceを解放する。
 */
FFrameCapture::~FFrameCapture()
{
    Reset();
}

/**
 * 初回に取得枚数と出力先を読み、次のフレームを取得するか判定する。
 */
bool FFrameCapture::ReadRequest(String& error)
{
    if (!requestChecked_)
    {
        requestChecked_ = true;
        if (!ReadUnsignedSetting("GKCORE_TEST_CAPTURE_FRAMES", 1, 1, 16, requestedFrames_, error) || !ReadUnsignedSetting("GKCORE_TEST_CAPTURE_START_FRAME", 0, 0, 65535, firstCaptureFrame_, error))
            return false;
        // 終端NULを含む出力先の必要bufferサイズ。
        const DWORD requiredSize = GetEnvironmentVariableA("GKCORE_TEST_CAPTURE_PATH", nullptr, 0);
        if (requiredSize == 0)
            return false;
        std::vector<char> path(requiredSize);
        const DWORD copiedSize = GetEnvironmentVariableA("GKCORE_TEST_CAPTURE_PATH", path.data(), requiredSize);
        if (copiedSize == 0 || copiedSize >= requiredSize)
            return SetCaptureError(error, "GKCORE_TEST_CAPTURE_PATH could not be read completely");
        if (!outputPath_.Assign(path.data(), copiedSize))
            return SetCaptureError(error, "The GPU capture output path could not be copied");
    }
    // 指定フレームまではreadbackの待機を入れず、通常の描画処理を続ける。
    const uint64_t frame = observedFrames_++;
    return frame >= firstCaptureFrame_ && completedFrames_ < requestedFrames_ && !outputPath_.Empty();
}

/**
 * 描画済みswapchain画像をGPU readback bufferへcopyする。
 */
bool FFrameCapture::Record(Renderer* renderer, Cmd* command, RenderTarget* target, String& error)
{
    if (!renderer || !renderer->mDx.pDevice || !command || !command->mDx.pCmdList || !target || !target->pTexture || !target->pTexture->mDx.pResource)
        return SetCaptureError(error, "The GPU capture received an invalid Direct3D 12 resource");
    if (target->mWidth == 0 || target->mHeight == 0)
        return SetCaptureError(error, "The GPU capture target has an empty size");

#if defined(ENABLE_GRAPHICS_VALIDATION) && defined(_WINDOWS)
    // Debugの設定だけでなく、実際に診断キューを取得できたかを記録する。
    std::fprintf(stderr, "GKCORE_TEST_D3D12_INFOQUEUE=%s\n", renderer->mDx.pDebugValidation ? "active" : "unavailable");
    if (!renderer->mDx.pDebugValidation)
        return SetCaptureError(error, "The D3D12 Debug InfoQueue is unavailable");
#endif

    // D3D12 copyに使う実resourceのformatとsample数。
    const D3D12_RESOURCE_DESC sourceDesc = target->pTexture->mDx.pResource->GetDesc();
    if (target->mFormat == TinyImageFormat_R8G8B8A8_SRGB && (sourceDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || sourceDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM))
        blueFirst_ = false;
    else if (target->mFormat == TinyImageFormat_B8G8R8A8_SRGB && (sourceDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || sourceDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM))
        blueFirst_ = true;
    else
        return SetCaptureError(error, "The GPU capture supports only matching 8-bit sRGB swapchain formats");
    if (sourceDesc.SampleDesc.Count != 1)
        return SetCaptureError(error, "The GPU capture does not support multisampled swapchains");

    width_ = target->mWidth;
    height_ = target->mHeight;
    // GPUが要求するrow pitchとreadback領域のlayout。
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rowCount = 0;
    // paddingなしで必要になる1行のbyte数。
    UINT64 unpaddedRowSize = 0;
    // GPU copy footprint全体のbyte数。
    UINT64 totalBytes = 0;
    renderer->mDx.pDevice->GetCopyableFootprints(&sourceDesc, 0, 1, 0, &layout, &rowCount, &unpaddedRowSize, &totalBytes);
    if (rowCount < height_ || unpaddedRowSize < static_cast<UINT64>(width_) * 4u || totalBytes == 0 || layout.Footprint.RowPitch < static_cast<UINT64>(width_) * 4u)
        return SetCaptureError(error, "The Direct3D 12 copy footprint does not match the swapchain size");

    // CPUから読めるD3D12 readback heapの設定。
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;
    // copy footprint全体を保持するlinear buffer。
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    // queue完了後にCPU mapするreadback resourceを作成する。
    const HRESULT createResult = renderer->mDx.pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_));
    if (FAILED(createResult))
        return SetCaptureError(error, "The Direct3D 12 readback buffer could not be created");

    // textureからpadded buffer footprintへcopyする配置。
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback_;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = layout;
    // copy元のswapchain subresource。
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = target->pTexture->mDx.pResource;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;

    // copy前後でswapchain targetのresource stateを切り替える。
    RenderTargetBarrier toCopy{ target, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_COPY_SOURCE };
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &toCopy);
    command->mDx.pCmdList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    RenderTargetBarrier toRender{ target, RESOURCE_STATE_COPY_SOURCE, RESOURCE_STATE_RENDER_TARGET };
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &toRender);

    readbackSize_ = totalBytes;
    rowPitch_ = layout.Footprint.RowPitch;
    return true;
}

/**
 * frame fence完了後にRGB画素をP6 PPMへ保存する。
 */
bool FFrameCapture::Complete(Renderer* renderer, Fence* fence, String& error)
{
    if (!renderer || !fence || !readback_ || outputPath_.Empty())
        return SetCaptureError(error, "The GPU capture was not recorded before completion");
    waitForFences(renderer, 1, &fence);

    // 2枚目以降は基準pathへ連番suffixを付ける。
    String frameOutputPath;
    if (!frameOutputPath.Assign(outputPath_.CStr()))
        return SetCaptureError(error, "The GPU capture output path could not be copied");
    if (completedFrames_ != 0 && (!frameOutputPath.Append(".frame") || !frameOutputPath.AppendUnsigned(completedFrames_) || !frameOutputPath.Append(".ppm")))
        return SetCaptureError(error, "The GPU capture output path could not be extended");

    // GPUが書いたreadback領域をCPUから読む。
    const D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(readbackSize_) };
    void* mapped = nullptr;
    // GPU readback bufferをCPU addressへmapする結果。
    const HRESULT mapResult = readback_->Map(0, &readRange, &mapped);
    if (FAILED(mapResult) || !mapped)
        return SetCaptureError(error, "The Direct3D 12 readback buffer could not be mapped");

    // PPMを書き込む出力stream。
    FILE* output = std::fopen(frameOutputPath.CStr(), "wb");
    if (!output)
    {
        const D3D12_RANGE writtenRange{ 0, 0 };
        readback_->Unmap(0, &writtenRange);
        return SetCaptureError(error, "The GPU capture output file could not be opened");
    }

    // 1行分だけRGBA/BGRAからRGBへ詰め替える。
    bool succeeded = std::fprintf(output, "P6\n%u %u\n255\n", width_, height_) > 0;
    // row pitchのpaddingを除いたRGBの一時行。
    std::vector<uint8_t> rgb(static_cast<size_t>(width_) * 3u);
    // readback resourceをmapした先頭address。
    const auto* pixels = static_cast<const uint8_t*>(mapped);
    for (uint32_t y = 0; succeeded && y < height_; ++y)
    {
        // GPU pitchで指定された現在行の先頭address。
        const uint8_t* row = pixels + static_cast<size_t>(y) * rowPitch_;
        for (uint32_t x = 0; x < width_; ++x)
        {
            const size_t sourceOffset = static_cast<size_t>(x) * 4u;
            const size_t destinationOffset = static_cast<size_t>(x) * 3u;
            rgb[destinationOffset] = row[sourceOffset + (blueFirst_ ? 2u : 0u)];
            rgb[destinationOffset + 1u] = row[sourceOffset + 1u];
            rgb[destinationOffset + 2u] = row[sourceOffset + (blueFirst_ ? 0u : 2u)];
        }
        succeeded = std::fwrite(rgb.data(), 1, rgb.size(), output) == rgb.size();
    }
    succeeded = std::fclose(output) == 0 && succeeded;
    // readback memoryはCPUから変更していないため書込範囲は空。
    const D3D12_RANGE writtenRange{ 0, 0 };
    readback_->Unmap(0, &writtenRange);
    if (!succeeded)
        return SetCaptureError(error, "The GPU capture image could not be written completely");

    Reset();
    ++completedFrames_;
    if (completedFrames_ >= requestedFrames_)
        outputPath_.Clear();
    return true;
}

/**
 * 保持するreadback resourceと一時状態を解放する。
 */
void FFrameCapture::Reset()
{
    if (readback_)
    {
        readback_->Release();
        readback_ = nullptr;
    }
    readbackSize_ = 0;
    width_ = 0;
    height_ = 0;
    rowPitch_ = 0;
    blueFirst_ = false;
}

}
