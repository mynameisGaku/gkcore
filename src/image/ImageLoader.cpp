#include "image/ImageLoader.h"
#include "resources/ResourceIO.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <stdint.h>

namespace gk::detail
{
namespace
{
const uint32_t maxFileBytes = 64u * 1024u * 1024u;
const uint32_t maxDimension = 16384u;
const uint32_t maxDecodedBytes = 256u * 1024u * 1024u;
}

ImageResource* DecodeImagePayload(const uint8_t* bytes, uint32_t size, String& error)
{
    if (!bytes || !size || size > maxFileBytes)
    {
        error.Assign("image data is empty or exceeds the file-size limit");
        return nullptr;
    }
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    if (!stbi_info_from_memory(bytes, static_cast<int>(size), &width, &height, &sourceChannels) || width <= 0 || height <= 0 || static_cast<uint32_t>(width) > maxDimension || static_cast<uint32_t>(height) > maxDimension || static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u > maxDecodedBytes)
    {
        error.Assign("unsupported image or image dimensions exceed limits");
        return nullptr;
    }
    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes, static_cast<int>(size), &decodedWidth, &decodedHeight, &decodedChannels, STBI_rgb_alpha);
    if (!decoded || decodedWidth != width || decodedHeight != height)
    {
        if (decoded)
            stbi_image_free(decoded);
        error.Assign("image decoding failed");
        return nullptr;
    }
    ImageResource* image = CreateImageResource();
    const uint32_t pixelBytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4u;
    if (!image || !image->rgba.Reserve(pixelBytes))
    {
        if (image)
            Release(&image->reference);
        stbi_image_free(decoded);
        error.Assign("image pixel allocation failed");
        return nullptr;
    }
    image->width = static_cast<uint32_t>(width);
    image->height = static_cast<uint32_t>(height);
    if (!image->rgba.AppendRange(decoded, pixelBytes))
    {
        Release(&image->reference);
        stbi_image_free(decoded);
        error.Assign("image pixel allocation failed");
        return nullptr;
    }
    stbi_image_free(decoded);
    return image;
}

ImageResource* LoadImagePayload(const char* path, String& error)
{
    uint8_t* bytes = nullptr;
    uint32_t size = 0;
    if (!ReadResourceFile(path, maxFileBytes, bytes, size, error))
        return nullptr;
    ImageResource* image = DecodeImagePayload(bytes, size, error);
    Deallocate(bytes);
    return image;
}
}
