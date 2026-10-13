// SPDX-License-Identifier: NOASSERTION
#include "examples/support/ModelPingPongMotion.h"

#include <float.h>
#include <math.h>
#include <string.h>

namespace gk::examples
{
/**
 * 次の位置引数を値として消費するoptionかを返す。
 */
static bool TakesValue(const char* argument)
{
    return strcmp(argument, "--materials") == 0 || strcmp(argument, "--model-bones") == 0 || strcmp(argument, "--motion-bones") == 0 || strcmp(argument, "--blend-bones") == 0 || strcmp(argument, "--secondary-motion") == 0 || strcmp(argument, "--secondary-colliders") == 0 || strcmp(argument, "--secondary-links") == 0 || strcmp(argument, "--secondary-distances") == 0 || strcmp(argument, "--secondary-contact-links") == 0 || strcmp(argument, "--secondary-contact-points") == 0;
}

bool EvaluatePingPongMotionTime(double phaseSeconds, double durationSeconds, double& outputSeconds)
{
    if (!isfinite(phaseSeconds) || phaseSeconds < 0.0 || !isfinite(durationSeconds) || durationSeconds <= 0.0)
    {
        return false;
    }
    // durationが半分の最大値より大きい場合は、2倍しても有限にならない。
    if (durationSeconds > DBL_MAX * 0.5)
    {
        outputSeconds = phaseSeconds <= durationSeconds ? phaseSeconds : durationSeconds - (phaseSeconds - durationSeconds);
        return true;
    }
    // clipの往復にかかる有限な周期。
    const double periodSeconds = durationSeconds * 2.0;
    // 位相を周期内へ折り返した位置。
    const double wrappedSeconds = fmod(phaseSeconds, periodSeconds);
    outputSeconds = wrappedSeconds <= durationSeconds ? wrappedSeconds : periodSeconds - wrappedSeconds;
    return true;
}

bool ParsePingPongMotionOptions(int& argc, char** argv, bool& enabled)
{
    if (argc < 1 || !argv)
    {
        return false;
    }
    // 重複とnullを先に検査し、失敗時には引数列と出力を保つ。
    // 重複を確かめる対象flagの位置。
    int flagIndex = -1;
    for (int index = 6; index < argc; ++index)
    {
        if (!argv[index])
        {
            return false;
        }
        if (TakesValue(argv[index]) && index + 1 < argc)
        {
            if (!argv[index + 1])
            {
                return false;
            }
            ++index;
            continue;
        }
        if (strcmp(argv[index], "--ping-pong-motion") == 0)
        {
            if (flagIndex >= 0)
            {
                return false;
            }
            flagIndex = index;
        }
    }
    if (flagIndex >= 0)
    {
        for (int index = flagIndex; index + 1 < argc; ++index)
        {
            argv[index] = argv[index + 1];
        }
        --argc;
        argv[argc] = nullptr;
    }
    enabled = flagIndex >= 0;
    return true;
}
}
