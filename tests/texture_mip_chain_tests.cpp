// SPDX-License-Identifier: NOASSERTION
#include "render/TextureMipChain.h"
#include "foundation/Memory.h"

#include <stdio.h>
#include <string.h>

namespace
{

/**
 * 検査失敗の理由を表示してfalseを返す。
 */
bool Check(bool condition, const char* message)
{
    if (!condition)
        fprintf(stderr, "%s\n", message);
    return condition;
}

/**
 * 画像へ1画素をappendする。
 */
bool AppendPixel(gk::detail::ImageResource& image, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    // 追加するRGBA一画素。
    const uint8_t pixel[4] = { red, green, blue, alpha };
    return image.rgba.AppendRange(pixel, 4);
}

/**
 * 計画段の寸法、offset、byte数を一つずつ確認する。
 */
bool CheckMipLayout(gk::Array<gk::render::FTextureMipLevel>& levels, uint32_t index, uint32_t width, uint32_t height, uint32_t offset, uint32_t byteSize)
{
    if (index >= levels.Count())
        return false;
    // 指定位置のmip配置値。
    const gk::render::FTextureMipLevel& level = levels.At(index);
    return level.width == width && level.height == height && level.offset == offset && level.byteSize == byteSize;
}

/**
 * level 0を保ち、sRGB平均を線形光で計算するか確かめる。
 */
bool CheckColorSpaceAverages()
{
    // 白黒とalphaを交互に置く小さな入力画像。
    gk::detail::ImageResource image{};
    image.width = 2;
    image.height = 2;
    if (!AppendPixel(image, 0, 0, 0, 0) || !AppendPixel(image, 255, 255, 255, 255) || !AppendPixel(image, 255, 255, 255, 0) || !AppendPixel(image, 0, 0, 0, 255))
        return Check(false, "mip test image allocation failed");
    // Build呼出し前の入力byteを保存する比較値。
    uint8_t original[16]{};
    memcpy(original, image.rgba.Data(), sizeof(original));
    // sRGB出力chainと失敗診断。
    gk::render::FTextureMipChain srgb{};
    gk::String error;
    if (!gk::render::BuildTextureMipChain(image, gk::render::ETextureColorSpace::Srgb, true, srgb, error))
        return Check(false, "sRGB mip chain was rejected");
    if (!Check(srgb.levels.Count() == 2 && srgb.rgba.Count() == 20, "sRGB mip layout is incorrect"))
        return false;
    // sRGB出力の先頭から続く画素byte列。
    const uint8_t* srgbPixels = srgb.rgba.Data();
    if (!Check(memcmp(srgbPixels, original, 16) == 0 && memcmp(image.rgba.Data(), original, 16) == 0, "mip level 0 changed source bytes"))
        return false;
    if (!Check(srgbPixels[16] == 188 && srgbPixels[17] == 188 && srgbPixels[18] == 188 && srgbPixels[19] == 128, "sRGB mip average did not use linear light and linear alpha"))
        return false;
    // 線形色spaceで計算する出力chain。
    gk::render::FTextureMipChain linear{};
    if (!gk::render::BuildTextureMipChain(image, gk::render::ETextureColorSpace::Linear, true, linear, error))
        return Check(false, "linear mip chain was rejected");
    return Check(linear.rgba.Count() == 20 && linear.rgba.At(16) == 128 && linear.rgba.At(17) == 128 && linear.rgba.At(18) == 128 && linear.rgba.At(19) == 128, "linear mip average is incorrect");
}

/**
 * NPOT downsampleが端画素を含み、決まったbyte列を返すか確かめる。
 */
bool CheckNpotAveragesAndDeterminism()
{
    // 値を10ずつ増やした3x3線形画像。
    gk::detail::ImageResource image{};
    image.width = 3;
    image.height = 3;
    // 全9画素へ既知の値を順に設定するloop。
    for (uint32_t value = 0; value < 9; ++value)
    {
        // 現在画素へ入れる同一RGB値。
        const uint8_t channel = static_cast<uint8_t>(value * 10);
        if (!AppendPixel(image, channel, channel, channel, channel))
            return Check(false, "NPOT mip test image allocation failed");
    }
    // 同じ入力から作る二つの比較chain。
    gk::render::FTextureMipChain first{};
    gk::render::FTextureMipChain second{};
    // builderの失敗理由。
    gk::String error;
    if (!gk::render::BuildTextureMipChain(image, gk::render::ETextureColorSpace::Linear, true, first, error) || !gk::render::BuildTextureMipChain(image, gk::render::ETextureColorSpace::Linear, true, second, error))
        return Check(false, "NPOT mip chain was rejected");
    if (!Check(first.levels.Count() == 2 && first.levels.At(0).width == 3 && first.levels.At(0).height == 3 && first.levels.At(1).width == 1 && first.levels.At(1).height == 1 && first.rgba.Count() == 40, "3x3 mip dimensions are incorrect"))
        return false;
    if (!Check(first.rgba.At(36) == 40 && first.rgba.At(37) == 40 && first.rgba.At(38) == 40 && first.rgba.At(39) == 40, "3x3 downsample omitted an edge texel"))
        return false;
    return Check(first.rgba.Count() == second.rgba.Count() && memcmp(first.rgba.Data(), second.rgba.Data(), first.rgba.Count()) == 0, "mip generation is not deterministic");
}

/**
 * 5x3の既知値から各縮小段の面積加重結果を独立した値で照合する。
 */
bool CheckFiveByThreeAreaOracle()
{
    // 0から14までを10刻みで並べた線形RGBA画像。
    gk::detail::ImageResource image{};
    image.width = 5;
    image.height = 3;
    // 入力画素ごとの既知RGB値。
    for (uint32_t value = 0; value < 15; ++value)
    {
        // 現在の行優先画素値。
        const uint8_t channel = static_cast<uint8_t>(value * 10);
        if (!AppendPixel(image, channel, channel, channel, 255))
            return Check(false, "5x3 mip oracle image allocation failed");
    }
    // builder前の全画素byteを保つ比較値。
    uint8_t original[60]{};
    memcpy(original, image.rgba.Data(), sizeof(original));
    // 面積加重縮小後の段配置。
    gk::render::FTextureMipChain chain{};
    gk::String error;
    if (!gk::render::BuildTextureMipChain(image, gk::render::ETextureColorSpace::Linear, true, chain, error))
        return Check(false, "5x3 mip oracle chain was rejected");
    if (!Check(chain.levels.Count() == 3 && chain.levels.At(1).width == 2 && chain.levels.At(1).height == 1 && chain.levels.At(2).width == 1 && chain.levels.At(2).height == 1, "5x3 mip oracle dimensions are incorrect"))
        return false;
    // 2x1段の独立計算結果58と82を各RGB成分で確認する。
    if (!Check(chain.rgba.At(60) == 58 && chain.rgba.At(61) == 58 && chain.rgba.At(62) == 58 && chain.rgba.At(64) == 82 && chain.rgba.At(65) == 82 && chain.rgba.At(66) == 82, "5x3 mip oracle did not preserve area-weighted edge contributions"))
        return false;
    if (!Check(chain.rgba.At(68) == 70 && chain.rgba.At(69) == 70 && chain.rgba.At(70) == 70 && chain.rgba.At(71) == 255, "5x3 final mip oracle value is incorrect"))
        return false;
    return Check(memcmp(image.rgba.Data(), original, sizeof(original)) == 0, "5x3 mip oracle changed source pixels");
}

/**
 * 1xNとNx1の縮小で寸法を1以上に保ち、5画素の端を含むか調べる。
 */
bool CheckOneDimensionalNpot()
{
    // 水平方向に5段階の値を置く入力画像。
    gk::detail::ImageResource image{};
    image.width = 5;
    image.height = 1;
    // 端を含む5画素を順に追加するloop。
    for (uint32_t value = 0; value < 5; ++value)
    {
        // 現在画素へ入れる同一RGB値。
        const uint8_t channel = static_cast<uint8_t>(value * 40);
        if (!AppendPixel(image, channel, channel, channel, 255))
            return Check(false, "one-dimensional mip image allocation failed");
    }
    // 横長画像のmip chainと失敗診断。
    gk::render::FTextureMipChain horizontal{};
    gk::String error;
    if (!gk::render::BuildTextureMipChain(image, gk::render::ETextureColorSpace::Linear, true, horizontal, error))
        return Check(false, "5x1 mip chain was rejected");
    if (!Check(horizontal.levels.Count() == 3 && horizontal.levels.At(0).width == 5 && horizontal.levels.At(0).height == 1 && horizontal.levels.At(1).width == 2 && horizontal.levels.At(1).height == 1 && horizontal.levels.At(2).width == 1 && horizontal.levels.At(2).height == 1, "5x1 mip dimensions are incorrect"))
        return false;
    if (!Check(horizontal.rgba.At(20) == 32 && horizontal.rgba.At(24) == 128 && horizontal.rgba.At(28) == 80, "5x1 area average did not include edge texels"))
        return false;
    // 同じ値を縦方向へ置く入力画像。
    gk::detail::ImageResource vertical{};
    vertical.width = 1;
    vertical.height = 5;
    // 端を含む縦5画素を順に追加するloop。
    for (uint32_t value = 0; value < 5; ++value)
    {
        // 現在画素へ入れる同一RGB値。
        const uint8_t channel = static_cast<uint8_t>(value * 40);
        if (!AppendPixel(vertical, channel, channel, channel, 255))
            return Check(false, "vertical mip image allocation failed");
    }
    // 縦長画像のmip chain。
    gk::render::FTextureMipChain verticalChain{};
    if (!gk::render::BuildTextureMipChain(vertical, gk::render::ETextureColorSpace::Linear, true, verticalChain, error))
        return Check(false, "1x5 mip chain was rejected");
    return Check(verticalChain.levels.Count() == 3 && verticalChain.levels.At(1).width == 1 && verticalChain.levels.At(1).height == 2 && verticalChain.levels.At(2).width == 1 && verticalChain.levels.At(2).height == 1, "1x5 mip dimensions are incorrect");
}

/**
 * full chainとlevel 0のみの計画を確認し、上限違反時の出力を保つ。
 */
bool CheckMipPlanning()
{
    // 寸法計画の出力配列、診断、合計byte数。
    gk::Array<gk::render::FTextureMipLevel> levels;
    gk::String error;
    uint64_t byteCount = 0;
    if (!gk::render::PlanTextureMipChain(5, 3, true, levels, byteCount, error))
        return Check(false, "5x3 mip layout planning failed");
    if (!Check(levels.Count() == 3 && CheckMipLayout(levels, 0, 5, 3, 0, 60) && CheckMipLayout(levels, 1, 2, 1, 60, 8) && CheckMipLayout(levels, 2, 1, 1, 68, 4) && byteCount == 72, "5x3 mip layout offsets are incorrect"))
        return false;
    if (!gk::render::PlanTextureMipChain(5, 3, false, levels, byteCount, error) || !Check(levels.Count() == 1 && CheckMipLayout(levels, 0, 5, 3, 0, 60) && byteCount == 60, "base-only mip layout is incorrect"))
        return false;
    if (!gk::render::PlanTextureMipChain(16384, 4096, true, levels, byteCount, error) || !Check(byteCount <= UINT32_MAX && levels.Count() > 1 && levels.At(levels.Count() - 1).width == 1 && levels.At(levels.Count() - 1).height == 1, "64-megapixel layout exceeded byte range or missed 1x1"))
        return false;
    levels.Clear();
    // failure時に残る配置sentinel。
    gk::render::FTextureMipLevel sentinel{};
    sentinel.width = 7;
    sentinel.height = 9;
    sentinel.offset = 123;
    sentinel.byteSize = 252;
    levels.Append(sentinel);
    byteCount = 0x12345678u;
    if (gk::render::PlanTextureMipChain(16384, 4097, true, levels, byteCount, error))
        return Check(false, "mip planner accepted more than 64 megapixels");
    if (!Check(levels.Count() == 1 && levels.At(0).width == 7 && levels.At(0).offset == 123 && byteCount == 0x12345678u && !error.Empty(), "failed mip planning changed its outputs"))
        return false;
#if defined(GKCORE_TESTING)
    // allocation failureを注入する前の出力値。
    gk::SetAllocationFailureAfterForTesting(0);
    const bool allocationSucceeded = gk::render::PlanTextureMipChain(2, 2, true, levels, byteCount, error);
    gk::ResetAllocationFailureForTesting();
    if (allocationSucceeded)
        return Check(false, "mip planner ignored an allocation failure");
    if (!Check(levels.Count() == 1 && levels.At(0).width == 7 && levels.At(0).offset == 123 && byteCount == 0x12345678u && !error.Empty(), "allocation failure changed mip plan outputs"))
        return false;
#endif
    return true;
}

/**
 * 不正画像、色空間、寸法でmip chain出力を変更しないか確かめる。
 */
bool CheckBuildFailurePreservesOutput()
{
    // failure後に残す既存chainと配置sentinel。
    gk::render::FTextureMipChain output{};
    // 既存chainのlevel配置。
    gk::render::FTextureMipLevel level{};
    level.width = 11;
    level.height = 13;
    level.offset = 17;
    level.byteSize = 572;
    // 既存chain内で変化を検出する画素byte。
    const uint8_t rgba[4] = { 3, 5, 7, 9 };
    if (!output.levels.Append(level) || !output.rgba.AppendRange(rgba, 4))
        return Check(false, "mip output sentinel allocation failed");
    // 寸法に対して画素byteが不足する不正画像。
    gk::detail::ImageResource invalid{};
    invalid.width = 2;
    invalid.height = 2;
    if (!invalid.rgba.AppendRange(rgba, 4))
        return Check(false, "invalid image setup failed");
    // 各失敗で返るdiagnostic。
    gk::String error;
    if (gk::render::BuildTextureMipChain(invalid, gk::render::ETextureColorSpace::Srgb, true, output, error))
        return Check(false, "mip builder accepted an incomplete source image");
    if (!Check(output.levels.Count() == 1 && output.levels.At(0).width == 11 && output.rgba.Count() == 4 && memcmp(output.rgba.Data(), rgba, 4) == 0, "invalid image changed existing mip output"))
        return false;
    // 1x1の正常画像。
    gk::detail::ImageResource valid{};
    valid.width = 1;
    valid.height = 1;
    if (!AppendPixel(valid, 20, 30, 40, 255))
        return Check(false, "valid image setup failed");
    if (gk::render::BuildTextureMipChain(valid, static_cast<gk::render::ETextureColorSpace>(99), true, output, error))
        return Check(false, "mip builder accepted an invalid color space");
    valid.width = 0;
    if (gk::render::BuildTextureMipChain(valid, gk::render::ETextureColorSpace::Linear, true, output, error))
        return Check(false, "mip builder accepted an invalid image dimension");
    if (!Check(output.levels.Count() == 1 && output.levels.At(0).width == 11 && output.rgba.Count() == 4 && memcmp(output.rgba.Data(), rgba, 4) == 0, "failed mip build replaced existing output"))
        return false;
#if defined(GKCORE_TESTING)
    // sourceを正常寸法へ戻し、chain構築途中のallocation失敗を試す。
    valid.width = 1;
    gk::SetAllocationFailureAfterForTesting(0);
    const bool allocationSucceeded = gk::render::BuildTextureMipChain(valid, gk::render::ETextureColorSpace::Linear, true, output, error);
    gk::ResetAllocationFailureForTesting();
    if (allocationSucceeded)
        return Check(false, "mip builder ignored an allocation failure");
    if (!Check(output.levels.Count() == 1 && output.levels.At(0).width == 11 && output.rgba.Count() == 4 && memcmp(output.rgba.Data(), rgba, 4) == 0, "allocation failure changed mip chain output"))
        return false;
#endif
    return true;
}

#if defined(GKCORE_TESTING)
/**
 * 各確保位置の失敗が入力と既存出力を保ち、最後は通常結果へ到達する。
 */
bool CheckBuildAllocationFailureSweep()
{
    // 縮小段ごとの確保位置を通る8x8線形画像。
    gk::detail::ImageResource image{};
    image.width = 8;
    image.height = 8;
    // 入力画像の全64画素を決定値で埋めるloop。
    for (uint32_t value = 0; value < 64; ++value)
    {
        // 現在画素の各成分。
        const uint8_t red = static_cast<uint8_t>(value * 3);
        const uint8_t green = static_cast<uint8_t>(value * 2);
        const uint8_t blue = static_cast<uint8_t>(255 - value);
        const uint8_t alpha = static_cast<uint8_t>(100 + value);
        if (!AppendPixel(image, red, green, blue, alpha))
            return Check(false, "allocation sweep source image setup failed");
    }
    // 失敗注入前に保存する入力画素byte。
    uint8_t original[256]{};
    memcpy(original, image.rgba.Data(), sizeof(original));
    // 失敗のない基準出力chain。
    gk::render::FTextureMipChain baseline{};
    // builder呼出し後の診断。
    gk::String error;
    if (!gk::render::BuildTextureMipChain(image, gk::render::ETextureColorSpace::Linear, true, baseline, error))
        return Check(false, "allocation sweep baseline chain failed");

    // 確保成功数を変えながら各失敗点と成功到達を調べる。
    uint32_t failureCount = 0;
    uint32_t successCount = 0;
    for (uint32_t allowedAllocations = 0; allowedAllocations < 12; ++allowedAllocations)
    {
        // 各試行の前に作る既存出力sentinel。
        gk::render::FTextureMipChain output{};
        // 失敗時に残る配置値。
        gk::render::FTextureMipLevel sentinelLevel{};
        sentinelLevel.width = 17;
        sentinelLevel.height = 19;
        sentinelLevel.offset = 23;
        sentinelLevel.byteSize = 1292;
        // 失敗時に残る画素byte。
        const uint8_t sentinelPixel[4] = { 7, 11, 13, 17 };
        if (!output.levels.Append(sentinelLevel) || !output.rgba.AppendRange(sentinelPixel, 4))
            return Check(false, "allocation sweep output setup failed");
        if (!error.Assign("preallocated diagnostic storage for mip allocation failure checks"))
            return Check(false, "allocation sweep diagnostic setup failed");

        // builder内で指定数の確保後に失敗させる。
        gk::SetAllocationFailureAfterForTesting(allowedAllocations);
        const bool succeeded = gk::render::BuildTextureMipChain(image, gk::render::ETextureColorSpace::Linear, true, output, error);
        gk::ResetAllocationFailureForTesting();
        if (!Check(memcmp(image.rgba.Data(), original, sizeof(original)) == 0, "allocation sweep changed source pixels"))
            return false;
        if (!succeeded)
        {
            ++failureCount;
            if (!Check(!error.Empty() && output.levels.Count() == 1 && output.levels.At(0).width == 17 && output.levels.At(0).offset == 23 && output.rgba.Count() == 4 && memcmp(output.rgba.Data(), sentinelPixel, 4) == 0, "allocation failure changed the prior mip output or omitted its diagnostic"))
                return false;
            continue;
        }

        ++successCount;
        if (!Check(error.Empty() && output.levels.Count() == baseline.levels.Count() && output.rgba.Count() == baseline.rgba.Count() && memcmp(output.rgba.Data(), baseline.rgba.Data(), baseline.rgba.Count()) == 0, "allocation sweep success differs from the baseline mip chain"))
            return false;
        for (uint32_t levelIndex = 0; levelIndex < baseline.levels.Count(); ++levelIndex)
        {
            // 成功時に基準chainと照合する段の配置。
            const gk::render::FTextureMipLevel& expected = baseline.levels.At(levelIndex);
            const gk::render::FTextureMipLevel& actual = output.levels.At(levelIndex);
            if (!Check(actual.width == expected.width && actual.height == expected.height && actual.offset == expected.offset && actual.byteSize == expected.byteSize, "allocation sweep success changed a mip level layout"))
                return false;
        }
    }

    return Check(failureCount >= 3 && successCount >= 1, "allocation sweep did not cover both late failures and successful completion");
}
#endif

}

int main()
{
    if (!CheckMipPlanning() || !CheckColorSpaceAverages() || !CheckNpotAveragesAndDeterminism() || !CheckFiveByThreeAreaOracle() || !CheckOneDimensionalNpot() || !CheckBuildFailurePreservesOutput())
        return 1;
#if defined(GKCORE_TESTING)
    if (!CheckBuildAllocationFailureSweep())
        return 1;
#endif
    return 0;
}
