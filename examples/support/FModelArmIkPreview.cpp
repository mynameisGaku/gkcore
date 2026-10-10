// SPDX-License-Identifier: NOASSERTION
#include "examples/support/FModelArmIkPreview.h"

#include <float.h>
#include <math.h>

namespace gk::examples
{
namespace
{

/**
 * 値が有限範囲にあるか調べる。
 */
bool IsFinite(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

/**
 * 3成分すべてが有限か調べる。
 */
bool IsFinite(gk::Vec3 value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

/**
 * 2点間の距離を桁あふれしにくい倍精度で計算する。
 */
double Distance(gk::Vec3 first, gk::Vec3 second)
{
    const double x = static_cast<double>(first.x) - second.x;
    const double y = static_cast<double>(first.y) - second.y;
    const double z = static_cast<double>(first.z) - second.z;
    return sqrt(x * x + y * y + z * z);
}

/**
 * 倍精度値を有限なfloatへ安全に変換する。
 */
bool ToFiniteFloat(double value, float& output)
{
    if (value != value || value > FLT_MAX || value < -FLT_MAX)
    {
        return false;
    }
    output = static_cast<float>(value);
    return IsFinite(output);
}

/**
 * 役割の検索が正常かを返し、割当の有無はfoundで区別する。
 */
bool FindUniqueRole(gk::ModelHandle model, gk::EHumanoidBone role, uint32_t& bone, bool& found)
{
    const uint32_t count = gk::GetModelBoneCount(model);
    bool hasRole = false;
    uint32_t result = 0;
    for (uint32_t index = 0; index < count; ++index)
    {
        if (gk::GetModelBoneRole(model, index) != role)
        {
            continue;
        }
        if (hasRole)
        {
            return false;
        }
        hasRole = true;
        result = index;
    }
    found = hasRole;
    if (!hasRole)
    {
        return true;
    }
    bone = result;
    return true;
}

}

bool MakeModelArmIkPreview(gk::Vec3 torso, const gk::Vec3 joints[3], FModelArmIkPreview& output)
{
    if (!joints || !IsFinite(torso) || !IsFinite(joints[0]) || !IsFinite(joints[1]) || !IsFinite(joints[2]))
    {
        return false;
    }

    const double upperLength = Distance(joints[0], joints[1]);
    const double lowerLength = Distance(joints[1], joints[2]);
    const double armLength = upperLength + lowerLength;
    const double outwardX = static_cast<double>(joints[0].x) - torso.x;
    const double outwardZ = static_cast<double>(joints[0].z) - torso.z;
    const double outwardLength = sqrt(outwardX * outwardX + outwardZ * outwardZ);
    const double goalDistance = sqrt(0.625) * armLength;
    if (upperLength <= 0.0 || lowerLength <= 0.0 || armLength > FLT_MAX || armLength <= 0.0 || outwardLength <= 0.0 || fabs(upperLength - lowerLength) > goalDistance)
    {
        return false;
    }

    FModelArmIkPreview result{};
    result.joints[0] = joints[0];
    result.joints[1] = joints[1];
    result.joints[2] = joints[2];
    result.armLength = static_cast<float>(armLength);
    const double outwardUnitX = outwardX / outwardLength;
    const double outwardUnitZ = outwardZ / outwardLength;
    const double targetX = joints[0].x + 0.75 * armLength * outwardUnitX;
    const double targetY = joints[0].y - 0.25 * armLength;
    const double targetZ = joints[0].z + 0.75 * armLength * outwardUnitZ;
    double forwardX = -outwardUnitZ;
    double forwardZ = outwardUnitX;
    if (forwardZ < 0.0f)
    {
        forwardX = -forwardX;
        forwardZ = -forwardZ;
    }
    const double poleX = joints[0].x + armLength * forwardX;
    const double poleZ = joints[0].z + armLength * forwardZ;
    if (!ToFiniteFloat(targetX, result.target.x) || !ToFiniteFloat(targetY, result.target.y) || !ToFiniteFloat(targetZ, result.target.z) || !ToFiniteFloat(poleX, result.pole.x) || !ToFiniteFloat(joints[0].y, result.pole.y) || !ToFiniteFloat(poleZ, result.pole.z))
    {
        return false;
    }
    output = result;
    return true;
}

bool BuildModelArmIkPreview(gk::ModelHandle model, FModelArmIkPreview& output)
{
    if (!model.IsValid())
    {
        return false;
    }

    FModelArmIkPreview result{};
    uint32_t bones[3]{};
    gk::Vec3 joints[3]{};
    const gk::EHumanoidBone armRoles[3] = { gk::EHumanoidBone::RightUpperArm, gk::EHumanoidBone::RightLowerArm, gk::EHumanoidBone::RightHand };
    for (uint32_t index = 0; index < 3; ++index)
    {
        bool foundRole = false;
        if (!FindUniqueRole(model, armRoles[index], bones[index], foundRole) || !foundRole)
        {
            return false;
        }
        if (gk::GetModelBonePosition(model, bones[index], joints[index]) != 0 || !IsFinite(joints[index]))
        {
            return false;
        }
    }

    const gk::EHumanoidBone torsoRoles[4] = { gk::EHumanoidBone::UpperChest, gk::EHumanoidBone::Chest, gk::EHumanoidBone::Spine, gk::EHumanoidBone::Hips };
    gk::Vec3 torso{};
    bool foundTorso = false;
    for (uint32_t index = 0; index < 4; ++index)
    {
        uint32_t torsoBone = 0;
        bool foundRole = false;
        if (!FindUniqueRole(model, torsoRoles[index], torsoBone, foundRole))
        {
            return false;
        }
        if (foundRole)
        {
            if (gk::GetModelBonePosition(model, torsoBone, torso) != 0 || !IsFinite(torso))
            {
                return false;
            }
            foundTorso = true;
            break;
        }
    }
    if (!foundTorso || !MakeModelArmIkPreview(torso, joints, result))
    {
        return false;
    }
    result.bones[0] = bones[0];
    result.bones[1] = bones[1];
    result.bones[2] = bones[2];
    output = result;
    return true;
}

bool DrawModelArmIkTarget(const FModelArmIkPreview& preview, float scale, gk::Vec3 center, float rotationY)
{
    if (!IsFinite(scale) || scale <= 0.0f || !IsFinite(center) || !IsFinite(rotationY) || !IsFinite(preview.target))
    {
        return false;
    }
    const float localX = (preview.target.x - center.x) * scale;
    const float localY = (preview.target.y - center.y) * scale;
    const float localZ = (preview.target.z - center.z) * scale;
    const float cosine = cosf(rotationY);
    const float sine = sinf(rotationY);
    const float worldX = localX * cosine + localZ * sine;
    const float worldY = localY;
    const float worldZ = -localX * sine + localZ * cosine;
    const float outerRadius = 0.024f;
    const float innerRadius = 0.017f;
    const gk::Vec3 outer[4] = { { worldX, worldY + outerRadius, worldZ }, { worldX + outerRadius, worldY, worldZ }, { worldX, worldY - outerRadius, worldZ }, { worldX - outerRadius, worldY, worldZ } };
    const gk::Vec3 inner[4] = { { worldX, worldY + innerRadius, worldZ }, { worldX + innerRadius, worldY, worldZ }, { worldX, worldY - innerRadius, worldZ }, { worldX - innerRadius, worldY, worldZ } };
    const uint32_t orange = gk::ColorRGB(255, 150, 35);
    for (uint32_t index = 0; index < 4; ++index)
    {
        const uint32_t next = (index + 1) % 4;
        if (gk::DrawTriangle3D(outer[index], outer[next], inner[next], orange, true) != 0 || gk::DrawTriangle3D(outer[index], inner[next], inner[index], orange, true) != 0)
        {
            return false;
        }
    }
    return true;
}

}
