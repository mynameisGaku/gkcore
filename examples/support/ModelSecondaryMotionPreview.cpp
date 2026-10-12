// SPDX-License-Identifier: NOASSERTION
#include "examples/support/ModelSecondaryMotionPreview.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

namespace gk::examples
{
/**
 * path付きoptionを引数列から取り除く。欠落・重複では引数列を保つ。
 */
static bool ParsePathOption(int& argc, char** argv, const char* name, const char*& path)
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
        if (strcmp(argv[index], name) == 0)
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

bool ParseSecondaryMotionOptions(int& argc, char** argv, const char*& path)
{
    return ParsePathOption(argc, argv, "--secondary-motion", path);
}

bool ParseSecondaryColliderOptions(int& argc, char** argv, const char*& path)
{
    return ParsePathOption(argc, argv, "--secondary-colliders", path);
}

bool ParseSecondaryLinkOptions(int& argc, char** argv, const char*& path)
{
    return ParsePathOption(argc, argv, "--secondary-links", path);
}

bool ParseSecondaryDistanceOptions(int& argc, char** argv, const char*& path)
{
    return ParsePathOption(argc, argv, "--secondary-distances", path);
}

bool ParseSecondaryContactLinkOptions(int& argc, char** argv, const char*& path)
{
    return ParsePathOption(argc, argv, "--secondary-contact-links", path);
}

bool ParseSecondaryVertexContactOptions(int& argc, char** argv, bool& enabled)
{
    // 値を持つoptionのpathをflagとして誤読しない。
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
        if (strcmp(argv[index], "--secondary-vertex-contact") == 0)
        {
            if (option >= 0)
            {
                return false;
            }
            option = index;
        }
    }
    enabled = option >= 0;
    if (option >= 0)
    {
        for (int index = option; index + 1 < argc; ++index)
        {
            argv[index] = argv[index + 1];
        }
        --argc;
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
        // 11番目の数値があるときだけ鎖の小球半径を設定する。
        bool hasCollisionRadius = false;
        float collisionRadius = 0.0f;
        int namesOffset = -1;
        if (sscanf(cursor, "%f %f %f %f %f %f %f %f %f %n", &settings.frequencyHz, &settings.dampingRatio, &settings.maxAngleDegrees, &settings.endOffset.x, &settings.endOffset.y, &settings.endOffset.z, &settings.gravity.x, &settings.gravity.y, &settings.gravity.z, &namesOffset) != 9 || namesOffset < 0)
        {
            passed = false;
            break;
        }
        // 反復数の省略時は従来の8回を使い、接触が複雑な設定では明示できる。
        cursor += namesOffset;
        if (*cursor != '|')
        {
            // 上限を読みながら確認し、長い数値でも桁あふれさせない。
            const char* iterationStart = cursor;
            settings.constraintIterations = 0;
            while (*cursor >= '0' && *cursor <= '9')
            {
                settings.constraintIterations = settings.constraintIterations * 10 + static_cast<uint32_t>(*cursor++ - '0');
                if (settings.constraintIterations > 32)
                {
                    passed = false;
                    break;
                }
            }
            if (!passed || cursor == iterationStart || settings.constraintIterations == 0 || (*cursor != ' ' && *cursor != '\t' && *cursor != '|'))
            {
                passed = false;
                break;
            }
            while (*cursor == ' ' || *cursor == '\t')
            {
                ++cursor;
            }
        }
        if (*cursor != '|')
        {
            char* radiusEnd = nullptr;
            const double radius = strtod(cursor, &radiusEnd);
            if (radiusEnd == cursor || !isfinite(radius) || radius < 0.0 || radius > FLT_MAX)
            {
                passed = false;
                break;
            }
            cursor = radiusEnd;
            while (*cursor == ' ' || *cursor == '\t')
            {
                ++cursor;
            }
            collisionRadius = static_cast<float>(radius);
            hasCollisionRadius = true;
        }
        if (*cursor++ != '|')
        {
            passed = false;
            break;
        }
        uint32_t bones[1024]{};
        uint32_t count = 0;
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
            if (passed && hasCollisionRadius)
            {
                passed = SetModelSecondaryMotionChainCollisionRadius(model, bones[0], collisionRadius) == 0;
            }
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
bool ApplySecondaryColliderPreview(ModelHandle model, const char* path)
{
    // 最大数までの形状を作り終えてから、公開APIへまとめて渡す。
    FModelSecondaryMotionCollider colliders[64]{};
    uint32_t count = 0;
    FILE* file = path ? fopen(path, "rb") : nullptr;
    if (!file)
    {
        fprintf(stderr, "cannot open secondary collider config\n");
        return false;
    }
    bool passed = true;
    char line[2048]{};
    while (passed && fgets(line, sizeof(line), file))
    {
        if (strlen(line) == sizeof(line) - 1 && line[sizeof(line) - 2] != '\n' && !feof(file))
        {
            passed = false;
            break;
        }
        char* cursor = line;
        if (strncmp(cursor, "\xef\xbb\xbf", 3) == 0)
        {
            cursor += 3;
        }
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (!*cursor || *cursor == '#' || *cursor == '\r' || *cursor == '\n')
        {
            continue;
        }
        if (count == 64)
        {
            passed = false;
            break;
        }
        // 端点・半径と、身体の取付bone名を読む。
        auto& collider = colliders[count];
        char boneName[1024]{};
        int consumed = -1;
        if (sscanf(cursor, "%f %f %f %f %f %f %f | %1023s %n", &collider.start.x, &collider.start.y, &collider.start.z, &collider.end.x, &collider.end.y, &collider.end.z, &collider.radius, boneName, &consumed) != 8 || consumed < 0)
        {
            passed = false;
            break;
        }
        for (char* remaining = cursor + consumed; *remaining; ++remaining)
        {
            if (*remaining != ' ' && *remaining != '\t' && *remaining != '\r' && *remaining != '\n')
            {
                passed = false;
            }
        }
        const int32_t bone = FindModelBone(model, boneName);
        if (bone < 0)
        {
            passed = false;
            break;
        }
        collider.bone = static_cast<uint32_t>(bone);
        ++count;
    }
    passed = passed && !ferror(file) && count > 0;
    fclose(file);
    if (passed)
    {
        passed = SetModelSecondaryMotionColliders(model, colliders, count) == 0;
    }
    if (!passed)
    {
        fprintf(stderr, "invalid secondary collider config: %s (%s)\n", path, GetLastErrorMessage());
    }
    return passed;
}

bool ApplySecondaryLinkPreview(ModelHandle model, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "cannot open secondary link config: %s\n", path);
        return false;
    }
    // 全行を検査してから一度だけ登録し、途中の設定を残さない。
    FModelSecondaryMotionLink links[1024]{};
    uint32_t count = 0;
    bool passed = true;
    char line[2048]{};
    while (passed && fgets(line, sizeof(line), file))
    {
        if (strlen(line) == sizeof(line) - 1 && line[sizeof(line) - 2] != '\n' && !feof(file))
        {
            passed = false;
            break;
        }
        char* cursor = line;
        if (strncmp(cursor, "\xef\xbb\xbf", 3) == 0)
        {
            cursor += 3;
        }
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (!*cursor || *cursor == '#' || *cursor == '\r' || *cursor == '\n')
        {
            continue;
        }
        char first[512]{}, second[512]{};
        int consumed = -1;
        if (count == 1024 || sscanf(cursor, "%f | %511s %511s %n", &links[count].radius, first, second, &consumed) != 3 || consumed < 0)
        {
            passed = false;
            break;
        }
        cursor += consumed;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        {
            ++cursor;
        }
        const int32_t firstBone = FindModelBone(model, first);
        const int32_t secondBone = FindModelBone(model, second);
        if (*cursor || firstBone < 0 || secondBone < 0)
        {
            passed = false;
            break;
        }
        links[count].firstBone = static_cast<uint32_t>(firstBone);
        links[count].secondBone = static_cast<uint32_t>(secondBone);
        ++count;
    }
    passed = passed && !ferror(file) && count > 0;
    fclose(file);
    if (passed)
    {
        passed = SetModelSecondaryMotionLinks(model, links, count, 32) == 0;
    }
    if (!passed)
    {
        fprintf(stderr, "invalid secondary link config: %s (%s)\n", path, GetLastErrorMessage());
    }
    return passed;
}

bool ApplySecondaryDistancePreview(ModelHandle model, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "cannot open secondary distance config: %s\n", path);
        return false;
    }
    // 全行を検査してから一度だけ登録し、途中の設定を残さない。
    FModelSecondaryMotionDistanceConstraint constraints[1024]{};
    uint32_t count = 0;
    bool passed = true;
    char line[2048]{};
    while (passed && fgets(line, sizeof(line), file))
    {
        if (strlen(line) == sizeof(line) - 1 && line[sizeof(line) - 2] != '\n' && !feof(file))
        {
            passed = false;
            break;
        }
        char* cursor = line;
        if (strncmp(cursor, "\xef\xbb\xbf", 3) == 0)
        {
            cursor += 3;
        }
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (!*cursor || *cursor == '#' || *cursor == '\r' || *cursor == '\n')
        {
            continue;
        }
        char first[512]{}, second[512]{};
        int consumed = -1;
        if (count == 1024 || sscanf(cursor, "%f %f | %511s %511s %n", &constraints[count].restDistance, &constraints[count].compliance, first, second, &consumed) != 4 || consumed < 0)
        {
            passed = false;
            break;
        }
        cursor += consumed;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        {
            ++cursor;
        }
        const int32_t firstBone = FindModelBone(model, first);
        const int32_t secondBone = FindModelBone(model, second);
        if (*cursor || firstBone < 0 || secondBone < 0)
        {
            passed = false;
            break;
        }
        constraints[count].firstBone = static_cast<uint32_t>(firstBone);
        constraints[count].secondBone = static_cast<uint32_t>(secondBone);
        ++count;
    }
    passed = passed && !ferror(file) && count > 0;
    fclose(file);
    if (passed)
    {
        passed = SetModelSecondaryMotionDistanceConstraints(model, constraints, count, 32) == 0;
    }
    if (!passed)
    {
        fprintf(stderr, "invalid secondary distance config: %s (%s)\n", path, GetLastErrorMessage());
    }
    return passed;
}

bool ApplySecondaryContactLinkPreview(ModelHandle model, const char* path)
{
    FILE* file = path ? fopen(path, "rb") : nullptr;
    if (!file)
    {
        fprintf(stderr, "cannot open secondary contact link config: %s\n", path ? path : "(null)");
        return false;
    }
    // 全行を検証してからAPIを一度だけ呼び、途中登録を残さない。
    FModelSecondaryMotionContactLink links[1024]{};
    uint32_t count = 0;
    bool passed = true;
    char line[2048]{};
    while (passed && fgets(line, sizeof(line), file))
    {
        if (strlen(line) == sizeof(line) - 1 && line[sizeof(line) - 2] != '\n' && !feof(file))
        {
            passed = false;
            break;
        }
        char* cursor = line;
        if (strncmp(cursor, "\xef\xbb\xbf", 3) == 0)
        {
            cursor += 3;
        }
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (!*cursor || *cursor == '#' || *cursor == '\r' || *cursor == '\n')
        {
            continue;
        }
        if (count == 1024)
        {
            passed = false;
            break;
        }
        // 半径と両端のboneローカル座標、bone名を1行から読む。
        FModelSecondaryMotionContactLink& link = links[count];
        char first[512]{}, second[512]{};
        int consumed = -1;
        if (sscanf(cursor, "%f %f %f %f %f %f %f | %511s %511s %n", &link.radius, &link.firstOffset.x, &link.firstOffset.y, &link.firstOffset.z, &link.secondOffset.x, &link.secondOffset.y, &link.secondOffset.z, first, second, &consumed) != 9 || consumed < 0)
        {
            passed = false;
            break;
        }
        cursor += consumed;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        {
            ++cursor;
        }
        const int32_t firstBone = FindModelBone(model, first);
        const int32_t secondBone = FindModelBone(model, second);
        if (*cursor || !isfinite(link.radius) || link.radius < 0.0f || !isfinite(link.firstOffset.x) || !isfinite(link.firstOffset.y) || !isfinite(link.firstOffset.z) || !isfinite(link.secondOffset.x) || !isfinite(link.secondOffset.y) || !isfinite(link.secondOffset.z) || firstBone < 0 || secondBone < 0)
        {
            passed = false;
            break;
        }
        link.firstBone = static_cast<uint32_t>(firstBone);
        link.secondBone = static_cast<uint32_t>(secondBone);
        ++count;
    }
    passed = passed && !ferror(file) && count > 0;
    fclose(file);
    if (passed)
    {
        passed = SetModelSecondaryMotionContactLinks(model, links, count, 32) == 0;
    }
    if (!passed)
    {
        fprintf(stderr, "invalid secondary contact link config: %s (%s)\n", path, GetLastErrorMessage());
    }
    return passed;
}

}
