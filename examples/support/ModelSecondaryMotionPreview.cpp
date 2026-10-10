// SPDX-License-Identifier: NOASSERTION
#include "examples/support/ModelSecondaryMotionPreview.h"
#include <stdio.h>
#include <string.h>

namespace gk::examples
{
bool ParseSecondaryMotionOptions(int& argc, char** argv, const char*& path)
{
    int option = -1;
    for (int index = 6; index < argc; ++index)
    {
        if (!argv[index])
        {
            return false;
        }
        if (strcmp(argv[index], "--materials") == 0 && index + 1 < argc)
        {
            ++index;
            continue;
        }
        if (strcmp(argv[index], "--secondary-motion") == 0)
        {
            if (option >= 0 || index + 1 >= argc || !argv[index + 1] || !argv[index + 1][0] || argv[index + 1][0] == '-')
            {
                return false;
            }
            option = index;
            ++index;
        }
    }
    path = option >= 0 ? argv[option + 1] : nullptr;
    if (option >= 0)
    {
        for (int index = option; index + 2 < argc; ++index)
        {
            argv[index] = argv[index + 2];
        }
        argc -= 2;
        argv[argc] = nullptr;
    }
    return true;
}

bool ApplySecondaryMotionPreview(ModelHandle model, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "cannot open secondary motion config: %s\n", path);
        return false;
    }
    bool passed = true;
    uint32_t chainCount = 0;
    // 1行の設定とbone列を読み込む固定作業領域。
    char line[16384]{};
    while (passed && fgets(line, sizeof(line), file))
    {
        if (strlen(line) == sizeof(line) - 1 && line[sizeof(line) - 2] != '\n' && !feof(file))
        {
            passed = false;
            break;
        }
        char* cursor = line;
        // UTF-8 BOMと空白・コメントを読み飛ばす。
        if (strncmp(cursor, "\xef\xbb\xbf", 3) == 0)
        {
            cursor += 3;
        }
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (*cursor == '#' || *cursor == '\r' || *cursor == '\n' || !*cursor)
        {
            continue;
        }
        FModelSecondaryMotionSettings settings;
        int namesOffset = -1;
        if (sscanf(cursor, "%f %f %f %f %f %f %f %f %f | %n", &settings.frequencyHz, &settings.dampingRatio, &settings.maxAngleDegrees, &settings.endOffset.x, &settings.endOffset.y, &settings.endOffset.z, &settings.gravity.x, &settings.gravity.y, &settings.gravity.z, &namesOffset) != 9 || namesOffset < 0)
        {
            passed = false;
            break;
        }
        uint32_t bones[1024]{};
        uint32_t count = 0;
        cursor += namesOffset;
        while (*cursor && passed)
        {
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            {
                ++cursor;
            }
            if (!*cursor)
            {
                break;
            }
            char* name = cursor;
            while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n')
            {
                ++cursor;
            }
            if (*cursor)
            {
                *cursor++ = '\0';
            }
            const int32_t bone = FindModelBone(model, name);
            if (bone < 0 || count == 1024)
            {
                passed = false;
                break;
            }
            bones[count++] = static_cast<uint32_t>(bone);
        }
        if (passed)
        {
            passed = SetModelSecondaryMotionChain(model, bones, count, settings) == 0;
            if (!passed)
            {
                fprintf(stderr, "secondary motion chain rejected: %s\n", count > 0 ? GetModelBoneName(model, bones[0]) : "empty");
            }
            ++chainCount;
        }
    }
    passed = passed && !ferror(file) && chainCount > 0;
    fclose(file);
    if (!passed)
    {
        fprintf(stderr, "invalid secondary motion config: %s (%s)\n", path, GetLastErrorMessage());
    }
    return passed;
}
}
