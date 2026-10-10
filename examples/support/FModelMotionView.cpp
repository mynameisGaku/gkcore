// SPDX-License-Identifier: NOASSERTION
#include "examples/support/FModelMotionView.h"
#include <gkcore/ModelAnimation.h>
#include <float.h>
#include <math.h>
#include <string.h>

namespace gk::examples
{
bool FModelMotionView::Initialize(gk::ModelHandle candidateModel, gk::Vec3 center)
{
    if (!candidateModel.IsValid() || !isfinite(center.x) || !isfinite(center.y) || !isfinite(center.z))
    {
        return false;
    }
    // 初期化後に役割表を再検索せず、腰の骨番号を使う。
    uint32_t candidateBone = 0;
    bool found = false;
    const uint32_t boneCount = gk::GetModelBoneCount(candidateModel);
    for (uint32_t index = 0; index < boneCount; ++index)
    {
        if (gk::GetModelBoneRole(candidateModel, index) != gk::EHumanoidBone::Hips)
        {
            continue;
        }
        if (found)
        {
            return false;
        }
        found = true;
        candidateBone = index;
    }
    gk::Vec3 origin{};
    if (!found || gk::GetModelBonePosition(candidateModel, candidateBone, origin) != 0)
    {
        return false;
    }
    model = candidateModel;
    hipsBone = candidateBone;
    initialOrigin = origin;
    initialCenter = center;
    return true;
}

bool FModelMotionView::GetCenter(gk::Vec3& output) const
{
    // 公開照会は表示用SRTを含まないため、追従結果が次frameへ戻らない。
    gk::Vec3 origin{};
    if (gk::GetModelBonePosition(model, hipsBone, origin) != 0)
    {
        return false;
    }
    const double center[3] = { static_cast<double>(initialCenter.x) + origin.x - initialOrigin.x, static_cast<double>(initialCenter.y) + origin.y - initialOrigin.y, static_cast<double>(initialCenter.z) + origin.z - initialOrigin.z };
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(center[axis]) || center[axis] > FLT_MAX || center[axis] < -FLT_MAX)
        {
            return false;
        }
    }
    output = { static_cast<float>(center[0]), static_cast<float>(center[1]), static_cast<float>(center[2]) };
    return true;
}

bool ParseMotionViewOptions(int& argc, char** argv, bool& followMotion)
{
    if (argc < 1 || !argv)
    {
        return false;
    }
    // 重複の検査を終えるまで引数配列を変更しない。
    int flagIndex = -1;
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
        if (strcmp(argv[index], "--follow-motion") == 0)
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
    followMotion = flagIndex >= 0;
    return true;
}
}
