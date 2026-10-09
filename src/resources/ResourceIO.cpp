#include "resources/ResourceIO.h"
#include "foundation/Memory.h"

#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gk::detail
{
namespace
{
FILE* OpenUtf8(const char* path)
{
#if defined(_WIN32)
    if (!path)
        return nullptr;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (count <= 0)
        return nullptr;
    wchar_t* wide = static_cast<wchar_t*>(Allocate(sizeof(wchar_t) * static_cast<size_t>(count)));
    if (!wide)
        return nullptr;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count))
    {
        Deallocate(wide);
        return nullptr;
    }
    FILE* file = _wfopen(wide, L"rb");
    Deallocate(wide);
    return file;
#else
    return path ? fopen(path, "rb") : nullptr;
#endif
}
}

bool ReadResourceFile(const char* path, uint32_t limit, uint8_t*& bytes, uint32_t& size, String& error)
{
    bytes = nullptr;
    size = 0;
    if (!path || !*path)
    {
        error.Assign("resource path is empty");
        return false;
    }
    FILE* file = OpenUtf8(path);
    if (!file)
    {
        error.Assign("cannot open resource file: ");
        error.Append(path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        error.Assign("cannot seek resource file");
        return false;
    }
    const long length = ftell(file);
    if (length < 0 || static_cast<unsigned long>(length) > limit || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        error.Assign("resource file exceeds its size limit or cannot be read");
        return false;
    }
    uint8_t* data = static_cast<uint8_t*>(Allocate(static_cast<size_t>(length)));
    if (length && !data)
    {
        fclose(file);
        error.Assign("resource file allocation failed");
        return false;
    }
    if (length && fread(data, 1, static_cast<size_t>(length), file) != static_cast<size_t>(length))
    {
        Deallocate(data);
        fclose(file);
        error.Assign("resource file is truncated while reading");
        return false;
    }
    fclose(file);
    bytes = data;
    size = static_cast<uint32_t>(length);
    return true;
}
}
