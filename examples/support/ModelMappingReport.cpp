// SPDX-License-Identifier: NOASSERTION
#include "examples/support/ModelMappingReport.h"

#include <stdio.h>
#include <string.h>

namespace gk::examples
{
namespace
{

/**
 * 基本的な人型役割を日本語名へ変換する。
 * 指や補助役割は呼び出し元で番号付きの分類名にする。
 */
const char* GetBasicRoleName(gk::EHumanoidBone role)
{
    switch (role)
    {
    case gk::EHumanoidBone::Hips:
        return u8"腰";
    case gk::EHumanoidBone::Spine:
        return u8"背骨";
    case gk::EHumanoidBone::Chest:
        return u8"胸";
    case gk::EHumanoidBone::UpperChest:
        return u8"上胸";
    case gk::EHumanoidBone::LeftShoulder:
        return u8"左肩";
    case gk::EHumanoidBone::RightShoulder:
        return u8"右肩";
    case gk::EHumanoidBone::Neck:
        return u8"首";
    case gk::EHumanoidBone::Head:
        return u8"頭";
    case gk::EHumanoidBone::LeftUpperArm:
        return u8"左上腕";
    case gk::EHumanoidBone::LeftLowerArm:
        return u8"左前腕";
    case gk::EHumanoidBone::LeftHand:
        return u8"左手";
    case gk::EHumanoidBone::RightUpperArm:
        return u8"右上腕";
    case gk::EHumanoidBone::RightLowerArm:
        return u8"右前腕";
    case gk::EHumanoidBone::RightHand:
        return u8"右手";
    case gk::EHumanoidBone::LeftUpperLeg:
        return u8"左大腿";
    case gk::EHumanoidBone::LeftLowerLeg:
        return u8"左すね";
    case gk::EHumanoidBone::LeftFoot:
        return u8"左足首";
    case gk::EHumanoidBone::LeftToes:
        return u8"左つま先";
    case gk::EHumanoidBone::RightUpperLeg:
        return u8"右大腿";
    case gk::EHumanoidBone::RightLowerLeg:
        return u8"右すね";
    case gk::EHumanoidBone::RightFoot:
        return u8"右足首";
    case gk::EHumanoidBone::RightToes:
        return u8"右つま先";
    case gk::EHumanoidBone::LeftEye:
        return u8"左目";
    case gk::EHumanoidBone::RightEye:
        return u8"右目";
    case gk::EHumanoidBone::Jaw:
        return u8"顎";
    default:
        return nullptr;
    }
}

}

bool BuildModelMappingReport(gk::ModelHandle model, uint32_t slot, const char* label, FModelMappingReport& report)
{
    FModelMappingReport result{};
    if (gk::GetModelAnimationMappingInfo(model, result.info, slot) != 0)
        return false;

    snprintf(result.summary, sizeof(result.summary), u8"%s: 人型 %u/%u (全骨 %u/%u)", label ? label : "Motion", result.info.mappedHumanoidBoneCount, result.info.humanoidBoneCount, result.info.mappedBoneCount, result.info.targetBoneCount);
    result.missingRoleCount = result.info.humanoidBoneCount - result.info.mappedHumanoidBoneCount;
    result.hasMissingRoles = result.missingRoleCount != 0;
    size_t used = 0;
    for (uint32_t index = 0; index < result.missingRoleCount; ++index)
    {
        const gk::EHumanoidBone role = gk::GetModelAnimationMissingHumanoidRole(model, index, slot);
        if (role == gk::EHumanoidBone::None)
            return false;
        const char* const name = GetBasicRoleName(role);
        // 項目全体が入る場合だけ追加し、UTF-8の文字を途中で切らない。
        char item[96]{};
        const int written = name ? snprintf(item, sizeof(item), "%s%s", index == 0 ? u8"不足: " : u8"、", name) : snprintf(item, sizeof(item), u8"%s指の役割(ID=%u)", index == 0 ? u8"不足: " : u8"、", static_cast<unsigned int>(role));
        if (written < 0 || static_cast<size_t>(written) >= sizeof(item))
        {
            return false;
        }
        if (static_cast<size_t>(written) + used >= sizeof(result.missingRoles))
        {
            break;
        }
        memcpy(result.missingRoles + used, item, static_cast<size_t>(written) + 1u);
        used += static_cast<size_t>(written);
    }
    report = result;
    return true;
}

void PrintModelMappingReport(const char* tag, uint32_t modelRoleCount, uint32_t mappedBoneCount, const FModelMappingReport& report)
{
    printf("animation-map %s: modelRoles=%u mappedBones=%u targetBoneCount=%u mappedBoneCount=%u humanoidBoneCount=%u mappedHumanoidBoneCount=%u\n", tag ? tag : "motion", modelRoleCount, mappedBoneCount, report.info.targetBoneCount, report.info.mappedBoneCount, report.info.humanoidBoneCount, report.info.mappedHumanoidBoneCount);
    if (report.hasMissingRoles)
        printf("animation-map %s: %s\n", tag ? tag : "motion", report.missingRoles);
}

}
