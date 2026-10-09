#include "render/Fxaa.h"

#include "foundation/Array.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/**
 * CPU reference filtering for the final linear-light image.
 */
namespace gk::render
{
/**
 * Internal helpers for validation and edge-aware color sampling.
 */
namespace
{

/**
 * Stores a diagnostic while leaving the destination image untouched.
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

/**
 * Restricts a scalar to an inclusive range.
 */
float Clamp(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

/**
 * Rejects NaN and infinite channel values.
 */
bool IsFinite(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

/**
 * Bounds RGB output to the normalized display range.
 */
LinearColor ClampColor(LinearColor color)
{
    return { Clamp(color.r, 0.0f, 1.0f), Clamp(color.g, 0.0f, 1.0f), Clamp(color.b, 0.0f, 1.0f), color.a };
}

/**
 * Measures edge contrast using perceptual sRGB channel values.
 */
float PerceptualLuminance(LinearColor color)
{
    const float red = LinearToSrgb(Clamp(color.r, 0.0f, 1.0f));
    const float green = LinearToSrgb(Clamp(color.g, 0.0f, 1.0f));
    const float blue = LinearToSrgb(Clamp(color.b, 0.0f, 1.0f));
    return red * 0.299f + green * 0.587f + blue * 0.114f;
}

/**
 * Interpolates all four color channels.
 */
LinearColor Mix(LinearColor left, LinearColor right, float amount)
{
    return { left.r + (right.r - left.r) * amount, left.g + (right.g - left.g) * amount, left.b + (right.b - left.b) * amount, left.a + (right.a - left.a) * amount };
}

/**
 * Samples bilinearly and clamps coordinates to the image border.
 */
LinearColor SampleClamped(const LinearColor* image, uint32_t width, uint32_t height, float x, float y)
{
    const float sampleX = Clamp(x - 0.5f, 0.0f, static_cast<float>(width - 1));
    const float sampleY = Clamp(y - 0.5f, 0.0f, static_cast<float>(height - 1));
    const uint32_t x0 = static_cast<uint32_t>(floorf(sampleX));
    const uint32_t y0 = static_cast<uint32_t>(floorf(sampleY));
    const uint32_t x1 = x0 + 1 < width ? x0 + 1 : x0;
    const uint32_t y1 = y0 + 1 < height ? y0 + 1 : y0;
    const float tx = sampleX - static_cast<float>(x0);
    const float ty = sampleY - static_cast<float>(y0);
    const LinearColor top = Mix(image[y0 * width + x0], image[y0 * width + x1], tx);
    const LinearColor bottom = Mix(image[y1 * width + x0], image[y1 * width + x1], tx);
    return Mix(top, bottom, ty);
}

/**
 * Averages two color samples for the narrow FXAA candidate.
 */
LinearColor Average(LinearColor first, LinearColor second)
{
    return { (first.r + second.r) * 0.5f, (first.g + second.g) * 0.5f, (first.b + second.b) * 0.5f, (first.a + second.a) * 0.5f };
}

}

bool ApplyFxaaReference(const LinearColor* source, uint32_t sourceCount, uint32_t width, uint32_t height, LinearColor* destination, uint32_t destinationCount, String& error)
{
    const uint64_t pixelCount64 = static_cast<uint64_t>(width) * height;
    if (!source || !destination || width == 0 || height == 0 || width > 16384 || height > 16384 || pixelCount64 > UINT32_MAX || pixelCount64 > SIZE_MAX / sizeof(LinearColor) || sourceCount < pixelCount64 || destinationCount < pixelCount64)
    {
        return Fail(error, "FXAA image dimensions or storage are invalid");
    }
    const uint32_t pixelCount = static_cast<uint32_t>(pixelCount64);
    const size_t imageBytes = static_cast<size_t>(pixelCount) * sizeof(LinearColor);
    const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(source);
    const uintptr_t destinationAddress = reinterpret_cast<uintptr_t>(destination);
    if (sourceAddress > UINTPTR_MAX - imageBytes || destinationAddress > UINTPTR_MAX - imageBytes)
        return Fail(error, "FXAA image storage address range overflows");

    Array<LinearColor> sourceCopy;
    const LinearColor* input = source;
    if (sourceAddress < destinationAddress + imageBytes && destinationAddress < sourceAddress + imageBytes)
    {
        if (!sourceCopy.Reserve(pixelCount) || !sourceCopy.AppendRange(source, pixelCount))
            return Fail(error, "not enough memory to copy overlapping FXAA input");
        input = sourceCopy.Data();
    }
    for (uint32_t i = 0; i < pixelCount; ++i)
    {
        if (!IsFinite(input[i].r) || !IsFinite(input[i].g) || !IsFinite(input[i].b) || !IsFinite(input[i].a))
            return Fail(error, "FXAA input contains a non-finite color value");
    }

    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const LinearColor center = input[y * width + x];
            const float centerX = static_cast<float>(x) + 0.5f;
            const float centerY = static_cast<float>(y) + 0.5f;
            const float centerLuma = PerceptualLuminance(center);
            const float northwest = PerceptualLuminance(SampleClamped(input, width, height, centerX - 1.0f, centerY - 1.0f));
            const float northeast = PerceptualLuminance(SampleClamped(input, width, height, centerX + 1.0f, centerY - 1.0f));
            const float southwest = PerceptualLuminance(SampleClamped(input, width, height, centerX - 1.0f, centerY + 1.0f));
            const float southeast = PerceptualLuminance(SampleClamped(input, width, height, centerX + 1.0f, centerY + 1.0f));
            const float north = PerceptualLuminance(SampleClamped(input, width, height, centerX, centerY - 1.0f));
            const float south = PerceptualLuminance(SampleClamped(input, width, height, centerX, centerY + 1.0f));
            const float west = PerceptualLuminance(SampleClamped(input, width, height, centerX - 1.0f, centerY));
            const float east = PerceptualLuminance(SampleClamped(input, width, height, centerX + 1.0f, centerY));
            const float minimum = fminf(centerLuma, fminf(fminf(northwest, northeast), fminf(fminf(southwest, southeast), fminf(fminf(north, south), fminf(west, east)))));
            const float maximum = fmaxf(centerLuma, fmaxf(fmaxf(northwest, northeast), fmaxf(fmaxf(southwest, southeast), fmaxf(fmaxf(north, south), fmaxf(west, east)))));
            if (maximum - minimum < fmaxf(0.0312f, maximum * 0.125f))
            {
                destination[y * width + x] = ClampColor(center);
                continue;
            }

            float directionX = ((southwest + southeast) - (northwest + northeast)) * 0.5f;
            float directionY = ((northwest + southwest) - (northeast + southeast)) * 0.5f;
            const float magnitude = fmaxf(fabsf(directionX), fabsf(directionY));
            if (magnitude <= 1.0e-6f)
            {
                destination[y * width + x] = ClampColor(center);
                continue;
            }
            const float reduction = fmaxf((northwest + northeast + southwest + southeast) * 0.03125f, 1.0f / 128.0f);
            const float minimumDirection = fminf(fabsf(directionX), fabsf(directionY));
            const float scale = fminf(1.0f / (minimumDirection + reduction), kFxaaMaximumSpan / magnitude);
            directionX = Clamp(directionX * scale, -kFxaaMaximumSpan, kFxaaMaximumSpan);
            directionY = Clamp(directionY * scale, -kFxaaMaximumSpan, kFxaaMaximumSpan);

            const LinearColor sampleA = Average(SampleClamped(input, width, height, centerX - directionX / 6.0f, centerY - directionY / 6.0f), SampleClamped(input, width, height, centerX + directionX / 6.0f, centerY + directionY / 6.0f));
            LinearColor sampleB = { sampleA.r * 0.5f, sampleA.g * 0.5f, sampleA.b * 0.5f, center.a * 0.5f };
            const LinearColor farA = SampleClamped(input, width, height, centerX - directionX * 0.5f, centerY - directionY * 0.5f);
            const LinearColor farB = SampleClamped(input, width, height, centerX + directionX * 0.5f, centerY + directionY * 0.5f);
            sampleB.r += (farA.r + farB.r) * 0.25f;
            sampleB.g += (farA.g + farB.g) * 0.25f;
            sampleB.b += (farA.b + farB.b) * 0.25f;
            const float candidateLuma = PerceptualLuminance(sampleB);
            destination[y * width + x] = ClampColor(candidateLuma < minimum || candidateLuma > maximum ? sampleA : sampleB);
            destination[y * width + x].a = center.a;
        }
    }
    error.Clear();
    return true;
}

} // namespace gk::render
