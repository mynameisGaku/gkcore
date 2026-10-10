// SPDX-License-Identifier: NOASSERTION
#include "examples/support/FHumanoidMapOptions.h"

#include <string.h>

namespace gk::examples
{
namespace
{

/**
 * 対応表optionの名前か判定する。
 */
bool IsHumanoidMapOption(const char* argument)
{
    return strcmp(argument, "--model-bones") == 0 || strcmp(argument, "--motion-bones") == 0 || strcmp(argument, "--blend-bones") == 0;
}

/**
 * 失敗理由を呼び出し側へ設定する。
 */
bool Fail(const char** error, const char* message)
{
    if (error)
        *error = message;
    return false;
}

}

/**
 * 対応表optionをargvから除去し、残る引数の順序を保つ。
 */
bool ParseHumanoidMapOptions(int& argc, char** argv, FHumanoidMapOptions& options, const char** error)
{
    if (error)
        *error = nullptr;
    if (argc < 1 || !argv)
        return Fail(error, "invalid argument vector");

    // 検証中に確定した各対応表path。
    FHumanoidMapOptions parsed{};
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const char* const argument = argv[argumentIndex];
        if (!argument)
            return Fail(error, "null argument");
        if (!IsHumanoidMapOption(argument))
            continue;
        if (argumentIndex + 1 >= argc || !argv[argumentIndex + 1] || !argv[argumentIndex + 1][0] || strncmp(argv[argumentIndex + 1], "--", 2) == 0)
            return Fail(error, "humanoid bone option requires a non-empty path");
        const char* const path = argv[argumentIndex + 1];
        const char** destination = strcmp(argument, "--model-bones") == 0 ? &parsed.modelPath : (strcmp(argument, "--motion-bones") == 0 ? &parsed.motionPath : &parsed.blendPath);
        if (*destination)
            return Fail(error, "humanoid bone option cannot be repeated");
        *destination = path;
        ++argumentIndex;
    }

    // 検証が済んだoption pairだけを除き、その他の引数順を保つ。
    int writeIndex = 1;
    for (int readIndex = 1; readIndex < argc; ++readIndex)
    {
        if (IsHumanoidMapOption(argv[readIndex]))
        {
            readIndex += 1;
            continue;
        }
        argv[writeIndex++] = argv[readIndex];
    }
    if (writeIndex < argc)
    {
        argv[writeIndex] = nullptr;
    }
    argc = writeIndex;
    options = parsed;
    return true;
}

/**
 * profile pathが利用できるanimation slotに対応することを確かめる。
 */
bool ValidateHumanoidMapOptions(const FHumanoidMapOptions& options, bool hasExternalMotion, bool hasExternalBlend, const char** error)
{
    if (error)
        *error = nullptr;
    if (options.motionPath && !hasExternalMotion)
        return Fail(error, "--motion-bones requires external or external-blend mode");
    if (options.blendPath && !hasExternalBlend)
        return Fail(error, "--blend-bones requires external-blend mode");
    if (hasExternalBlend && !hasExternalMotion)
        return Fail(error, "external-blend mode requires a primary external motion");
    return true;
}

}
