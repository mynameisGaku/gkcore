#include "model/ModelLoader.h"
#include "model/GlbLoader.h"
#include "model/FbxLoader.h"
#include "model/ObjLoader.h"
#include "resources/ResourceIO.h"
#include "foundation/Memory.h"
#include <string.h>

/**
 * Model file dispatch into bounded format-specific payload loaders.
 */
namespace gk::detail
{

/**
 * Private format signatures and path suffix checks.
 */
namespace
{

/**
 * Checks a path suffix without locale-dependent case conversion.
 */
bool HasFbxExtension(const char* path)
{
    if (!path)
        return false;
    const char* name = path;
    for (const char* current = path; *current; ++current)
        if (*current == '/' || *current == '\\')
            name = current + 1;
    size_t length = 0;
    while (name[length])
        ++length;
    if (length < 4)
        return false;
    const char* extension = name + length - 4;
    return extension[0] == '.' && (extension[1] == 'f' || extension[1] == 'F') && (extension[2] == 'b' || extension[2] == 'B') && (extension[3] == 'x' || extension[3] == 'X');
}

/**
 * Recognizes the two FBX signatures without interpreting arbitrary text files.
 */
bool HasFbxSignature(const uint8_t* bytes, uint32_t size)
{
    static const uint8_t binarySignature[] = { 'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F', 'B', 'X', ' ', 'B', 'i', 'n', 'a', 'r', 'y', ' ', ' ', 0, 0x1a, 0 };
    if (size >= sizeof(binarySignature) && memcmp(bytes, binarySignature, sizeof(binarySignature)) == 0)
        return true;
    uint32_t offset = 0;
    if (size >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        offset = 3;
    while (offset < size && (bytes[offset] == ' ' || bytes[offset] == '\t' || bytes[offset] == '\r' || bytes[offset] == '\n'))
        ++offset;
    static const char asciiSignature[] = "; FBX ";
    return size - offset >= sizeof(asciiSignature) - 1 && memcmp(bytes + offset, asciiSignature, sizeof(asciiSignature) - 1) == 0;
}

} // namespace

/**
 * Reads a UTF-8 model path once, chooses its format, and releases input bytes on every path.
 */
ModelResource* LoadModelPayload(const char* path, String& error)
{
    uint8_t* bytes = nullptr;
    uint32_t size = 0;
    const uint32_t maxModelFileBytes = 64u * 1024u * 1024u;
    if (!ReadResourceFile(path, maxModelFileBytes, bytes, size, error))
        return nullptr;
    ModelResource* model = CreateModelResource();
    if (!model)
    {
        Deallocate(bytes);
        error.Assign("model allocation failed");
        return nullptr;
    }
    bool success = false;
    if (HasFbxExtension(path))
        success = LoadFbxPayload(bytes, size, path, *model, error);
    else if (size >= 4 && bytes[0] == 'g' && bytes[1] == 'l' && bytes[2] == 'T' && bytes[3] == 'F')
        success = LoadGlbPayload(bytes, size, *model, error);
    else if (HasFbxSignature(bytes, size))
        success = LoadFbxPayload(bytes, size, path, *model, error);
    else
        success = LoadObjPayload(bytes, size, *model, error);
    Deallocate(bytes);
    if (!success)
    {
        Release(&model->reference);
        return nullptr;
    }
    if (!model->primitives.Count())
    {
        ModelPrimitive primitive = { 0, model->indices.Count(), 0 };
        if (!model->primitives.Append(primitive))
        {
            Release(&model->reference);
            error.Assign("model primitive allocation failed");
            return nullptr;
        }
    }
    return model;
}
}
