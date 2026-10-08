// SPDX-License-Identifier: NOASSERTION
#include "TextureMipChain.h"
#include "PostProcess.h"

#include <math.h>
#include <stdint.h>

/**
 * 画像の縮小段を計画・生成する描画補助処理。
 */
namespace gk::render
{

/**
 * 寸法検査、画素の平均とbyte化をこのfile内にまとめる。
 */
namespace
{

// 画像一辺に許す最大画素数。
constexpr uint32_t kMaximumDimension = 16384;
// 画像全体に許す最大画素数。
constexpr uint64_t kMaximumPixelCount = 64ull * 1024ull * 1024ull;
// 画像一画素を構成するRGBA byte数。
constexpr uint32_t kBytesPerPixel = 4;

/**
 * 指定値を0から255のRGBA byteへ丸める。
 */
uint8_t ToByte(float value)
{
    const float scaled = value * 255.0f;
    const float rounded = floorf(scaled + 0.5f);
    if (rounded <= 0.0f)
        return 0;
    if (rounded >= 255.0f)
        return 255;
    return static_cast<uint8_t>(rounded);
}

/**
 * 画像寸法とbyte上限を検証して失敗理由を設定する。
 */
bool IsMipDimensionValid(uint32_t width, uint32_t height, String& error)
{
    if (width == 0 || height == 0 || width > kMaximumDimension || height > kMaximumDimension)
    {
        error.Assign("Texture mip dimensions must be between 1 and 16384");
        return false;
    }

    const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
    if (pixelCount > kMaximumPixelCount)
    {
        error.Assign("Texture mip image exceeds 64 megapixels");
        return false;
    }

    return true;
}

/**
 * 一段を面積加重平均し、指定色空間のRGBA byte列へ書き込む。
 */
bool DownsampleLevel(const uint8_t* source, uint32_t sourceWidth, uint32_t sourceHeight, uint8_t* destination, uint32_t destinationWidth, uint32_t destinationHeight, ETextureColorSpace colorSpace)
{
    if (source == nullptr || destination == nullptr || sourceWidth == 0 || sourceHeight == 0 || destinationWidth == 0 || destinationHeight == 0)
        return false;

    // 画素あたりの色積分を蓄える4成分。
    double channelTotals[4] = {};
    for (uint32_t destinationY = 0; destinationY < destinationHeight; ++destinationY)
    {
        // 出力画素が覆う入力Y範囲。
        const double top = static_cast<double>(destinationY) * sourceHeight / destinationHeight;
        const double bottom = static_cast<double>(destinationY + 1) * sourceHeight / destinationHeight;
        const uint32_t firstSourceY = static_cast<uint32_t>(floor(top));
        const uint32_t lastSourceY = static_cast<uint32_t>(ceil(bottom));
        for (uint32_t destinationX = 0; destinationX < destinationWidth; ++destinationX)
        {
            // 出力画素が覆う入力X範囲。
            const double left = static_cast<double>(destinationX) * sourceWidth / destinationWidth;
            const double right = static_cast<double>(destinationX + 1) * sourceWidth / destinationWidth;
            const uint32_t firstSourceX = static_cast<uint32_t>(floor(left));
            const uint32_t lastSourceX = static_cast<uint32_t>(ceil(right));
            const double destinationArea = (right - left) * (bottom - top);
            channelTotals[0] = 0.0;
            channelTotals[1] = 0.0;
            channelTotals[2] = 0.0;
            channelTotals[3] = 0.0;

            for (uint32_t sourceY = firstSourceY; sourceY < lastSourceY; ++sourceY)
            {
                // 入出力画素が共有するY方向の長さ。
                const double overlapY = fmin(bottom, static_cast<double>(sourceY + 1)) - fmax(top, static_cast<double>(sourceY));
                for (uint32_t sourceX = firstSourceX; sourceX < lastSourceX; ++sourceX)
                {
                    // 入出力画素が共有する面積とRGBA位置。
                    const double overlapX = fmin(right, static_cast<double>(sourceX + 1)) - fmax(left, static_cast<double>(sourceX));
                    const double weight = overlapX * overlapY;
                    const uint8_t* sourcePixel = source + (static_cast<uint64_t>(sourceY) * sourceWidth + sourceX) * kBytesPerPixel;
                    for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel)
                    {
                        // sRGB色だけ線形光へ直してから平均する値。
                        const float encoded = static_cast<float>(sourcePixel[channel]) / 255.0f;
                        const float sample = colorSpace == ETextureColorSpace::Srgb && channel < 3 ? SrgbToLinear(encoded) : encoded;
                        channelTotals[channel] += static_cast<double>(sample) * weight;
                    }
                }
            }

            // 出力RGBA画素のbyte位置。
            uint8_t* destinationPixel = destination + (static_cast<uint64_t>(destinationY) * destinationWidth + destinationX) * kBytesPerPixel;
            for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel)
            {
                // 平均値を必要ならsRGBへ戻した出力値。
                const float average = static_cast<float>(channelTotals[channel] / destinationArea);
                const float encoded = colorSpace == ETextureColorSpace::Srgb && channel < 3 ? LinearToSrgb(average) : average;
                destinationPixel[channel] = ToByte(encoded);
            }
        }
    }

    return true;
}

}

/**
 * 寸法からmip段のbyte配置を計画する。無効寸法や確保失敗時は出力を保つ。
 */
bool PlanTextureMipChain(uint32_t width, uint32_t height, bool fullChain, Array<FTextureMipLevel>& levels, uint64_t& byteCount, String& error)
{
    if (!IsMipDimensionValid(width, height, error))
        return false;

    // 既存出力へ影響を与えない仮の段配置。
    Array<FTextureMipLevel> candidate;
    uint64_t totalBytes = 0;
    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    while (true)
    {
        // 現在段のRGBA byte数。
        const uint64_t levelBytes = static_cast<uint64_t>(mipWidth) * mipHeight * kBytesPerPixel;
        if (totalBytes + levelBytes > UINT32_MAX)
        {
            error.Assign("Texture mip chain exceeds the 32-bit byte layout limit");
            return false;
        }

        // 段の寸法と連続RGBA配列上のbyte範囲。
        FTextureMipLevel level{};
        level.width = mipWidth;
        level.height = mipHeight;
        level.offset = static_cast<uint32_t>(totalBytes);
        level.byteSize = static_cast<uint32_t>(levelBytes);
        if (!candidate.Append(level))
        {
            error.Assign("Texture mip layout allocation failed");
            return false;
        }
        totalBytes += levelBytes;
        if (!fullChain || (mipWidth == 1 && mipHeight == 1))
            break;
        mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
    }

    levels.MoveFrom(candidate);
    byteCount = totalBytes;
    error.Clear();
    return true;
}

/**
 * level 0を保ち、面積加重と色空間変換で縮小段を作る。
 */
bool BuildTextureMipChain(const detail::ImageResource& image, ETextureColorSpace colorSpace, bool fullChain, FTextureMipChain& output, String& error)
{
    if (colorSpace != ETextureColorSpace::Srgb && colorSpace != ETextureColorSpace::Linear)
    {
        error.Assign("Texture mip color space is invalid");
        return false;
    }

    // 仮計画と必要な連続byte数。
    FTextureMipChain candidate{};
    uint64_t totalBytes = 0;
    if (!PlanTextureMipChain(image.width, image.height, fullChain, candidate.levels, totalBytes, error))
        return false;
    if (image.rgba.Count() != candidate.levels.At(0).byteSize || image.rgba.Data() == nullptr)
    {
        error.Assign("Texture mip source RGBA size does not match its dimensions");
        return false;
    }
    if (totalBytes > UINT32_MAX || !candidate.rgba.Reserve(static_cast<uint32_t>(totalBytes)))
    {
        error.Assign("Texture mip pixel allocation failed");
        return false;
    }
    if (!candidate.rgba.AppendRange(image.rgba.Data(), image.rgba.Count()))
    {
        error.Assign("Texture mip base level copy failed");
        return false;
    }

    for (uint32_t levelIndex = 1; levelIndex < candidate.levels.Count(); ++levelIndex)
    {
        // 直前段と今回段の配置。
        const FTextureMipLevel& sourceLevel = candidate.levels.At(levelIndex - 1);
        const FTextureMipLevel& destinationLevel = candidate.levels.At(levelIndex);
        // 追加前の段データを一時保持する配列。
        const uint8_t* sourcePixels = candidate.rgba.Data() + sourceLevel.offset;
        Array<uint8_t> destinationPixels;
        if (!destinationPixels.Reserve(destinationLevel.byteSize))
        {
            error.Assign("Texture mip level allocation failed");
            return false;
        }
        // 初期byte列を用意して面積加重縮小を書き込む。
        uint8_t zeroPixels[4] = {};
        for (uint32_t byteIndex = 0; byteIndex < destinationLevel.byteSize; byteIndex += kBytesPerPixel)
        {
            if (!destinationPixels.AppendRange(zeroPixels, kBytesPerPixel))
            {
                error.Assign("Texture mip level allocation failed");
                return false;
            }
        }
        if (!DownsampleLevel(sourcePixels, sourceLevel.width, sourceLevel.height, destinationPixels.Data(), destinationLevel.width, destinationLevel.height, colorSpace))
        {
            error.Assign("Texture mip downsample failed");
            return false;
        }
        if (!candidate.rgba.AppendRange(destinationPixels.Data(), destinationLevel.byteSize))
        {
            error.Assign("Texture mip level append failed");
            return false;
        }
    }

    output.levels.MoveFrom(candidate.levels);
    output.rgba.MoveFrom(candidate.rgba);
    error.Clear();
    return true;
}

}
