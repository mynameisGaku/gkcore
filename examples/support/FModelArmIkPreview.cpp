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
    result.outward = gk::Vec3{ static_cast<float>(outwardUnitX), 0.0f, static_cast<float>(outwardUnitZ) };
    result.up = gk::Vec3{ 0.0f, 1.0f, 0.0f };
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
    uint32_t bones[4]{};
    gk::Vec3 positions[4]{};
    const gk::EHumanoidBone armRoles[3] = { gk::EHumanoidBone::RightUpperArm, gk::EHumanoidBone::RightLowerArm, gk::EHumanoidBone::RightHand };
    for (uint32_t index = 0; index < 3; ++index)
    {
        bool foundRole = false;
        if (!FindUniqueRole(model, armRoles[index], bones[index], foundRole) || !foundRole)
        {
            return false;
        }
    }

    const gk::EHumanoidBone torsoRoles[4] = { gk::EHumanoidBone::UpperChest, gk::EHumanoidBone::Chest, gk::EHumanoidBone::Spine, gk::EHumanoidBone::Hips };
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
            bones[3] = torsoBone;
            foundTorso = true;
            break;
        }
    }
    if (!foundTorso || gk::GetModelBonePositions(model, bones, 4, positions) != 0 || !MakeModelArmIkPreview(positions[3], positions, result))
    {
        return false;
    }
    result.bones[0] = bones[0];
    result.bones[1] = bones[1];
    result.bones[2] = bones[2];
    output = result;
    return true;
}

/**
 * 腰・胸・肩と腕位置から胴体の向きと到達範囲を計算する。退化した入力はfalseを返す。
 */
static bool ComputeAnimatedArmFrame(gk::Vec3 hips, gk::Vec3 torso, gk::Vec3 leftShoulder, const gk::Vec3 joints[3], FModelArmIkPreview& output)
{
    if (!joints || !IsFinite(hips) || !IsFinite(torso) || !IsFinite(leftShoulder) || !IsFinite(joints[0]) || !IsFinite(joints[1]) || !IsFinite(joints[2]))
    {
        return false;
    }
    // 腰から胸への方向と左右の肩幅から、胴体に追従する直交した向きを作る。
    double up[3] = { static_cast<double>(torso.x) - hips.x, static_cast<double>(torso.y) - hips.y, static_cast<double>(torso.z) - hips.z };
    double outward[3] = { static_cast<double>(joints[0].x) - leftShoulder.x, static_cast<double>(joints[0].y) - leftShoulder.y, static_cast<double>(joints[0].z) - leftShoulder.z };
    const double upLength = sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    const double shoulderLength = sqrt(outward[0] * outward[0] + outward[1] * outward[1] + outward[2] * outward[2]);
    if (!(upLength > 0.0) || !(shoulderLength > 0.0))
    {
        return false;
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        up[axis] /= upLength;
    }
    const double projection = outward[0] * up[0] + outward[1] * up[1] + outward[2] * up[2];
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        outward[axis] -= projection * up[axis];
    }
    const double outwardLength = sqrt(outward[0] * outward[0] + outward[1] * outward[1] + outward[2] * outward[2]);
    // 肩幅が胴体の縦方向とほぼ重なると、安定した外向きを定められない。
    if (!(outwardLength > shoulderLength * 16.0 * FLT_EPSILON))
    {
        return false;
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        outward[axis] /= outwardLength;
    }
    const double upperLength = Distance(joints[0], joints[1]);
    const double lowerLength = Distance(joints[1], joints[2]);
    const double armLength = upperLength + lowerLength;
    if (!(upperLength > 0.0) || !(lowerLength > 0.0) || armLength > FLT_MAX)
    {
        return false;
    }
    FModelArmIkPreview result{};
    for (uint32_t index = 0; index < 3; ++index)
    {
        result.joints[index] = joints[index];
    }
    result.armLength = static_cast<float>(armLength);
    result.outward = gk::Vec3{ static_cast<float>(outward[0]), static_cast<float>(outward[1]), static_cast<float>(outward[2]) };
    result.up = gk::Vec3{ static_cast<float>(up[0]), static_cast<float>(up[1]), static_cast<float>(up[2]) };
    output = result;
    return true;
}

bool MakeMotionFollowingArmIkPreview(gk::Vec3 hips, gk::Vec3 torso, gk::Vec3 leftShoulder, const gk::Vec3 joints[3], FModelArmIkPreview& output, float offsetRatio)
{
    FModelArmIkPreview result{};
    if (!IsFinite(offsetRatio) || offsetRatio < 0.0f || offsetRatio > 1.0f || !ComputeAnimatedArmFrame(hips, torso, leftShoulder, joints, result))
    {
        return false;
    }
    const double upper = Distance(joints[0], joints[1]), lower = Distance(joints[1], joints[2]);
    const double maximumReach = upper + lower, minimumReach = fabs(upper - lower);
    const double root[3] = { joints[0].x, joints[0].y, joints[0].z };
    const double hand[3] = { joints[2].x, joints[2].y, joints[2].z };
    double forward[3] = { static_cast<double>(result.up.y) * result.outward.z - static_cast<double>(result.up.z) * result.outward.y, static_cast<double>(result.up.z) * result.outward.x - static_cast<double>(result.up.x) * result.outward.z, static_cast<double>(result.up.x) * result.outward.y - static_cast<double>(result.up.y) * result.outward.x };
    const double forwardLength = sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (!(forwardLength > 0.0))
    {
        return false;
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        forward[axis] /= forwardLength;
    }
    double radius[3] = { hand[0] - root[0], hand[1] - root[1], hand[2] - root[2] };
    const double squared = radius[0] * radius[0] + radius[1] * radius[1] + radius[2] * radius[2];
    const double along = radius[0] * forward[0] + radius[1] * forward[1] + radius[2] * forward[2];
    // 手先から前方へ進む直線上で到達範囲と交差させ、元の手先を大きく作り替えない。
    double step = fmin(static_cast<double>(offsetRatio) * maximumReach, fmax(0.0, -along + sqrt(fmax(0.0, along * along + maximumReach * maximumReach - squared))));
    const double innerDiscriminant = along * along + minimumReach * minimumReach - squared;
    if (along < 0.0 && innerDiscriminant >= 0.0)
    {
        step = fmin(step, fmax(0.0, -along - sqrt(innerDiscriminant)));
    }
    float target[3]{}, pole[3]{};
    double targetDirection[3]{};
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!ToFiniteFloat(hand[axis] + step * forward[axis], target[axis]))
        {
            return false;
        }
        targetDirection[axis] = static_cast<double>(target[axis]) - root[axis];
    }
    const double targetLength = sqrt(targetDirection[0] * targetDirection[0] + targetDirection[1] * targetDirection[1] + targetDirection[2] * targetDirection[2]);
    if (!(targetLength > 0.0))
    {
        return false;
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        targetDirection[axis] /= targetLength;
    }
    // 元motionの順序付き曲げ面を、新しい目標方向に直交する面へ近づける。
    const double first[3] = { static_cast<double>(joints[1].x) - root[0], static_cast<double>(joints[1].y) - root[1], static_cast<double>(joints[1].z) - root[2] };
    const double second[3] = { hand[0] - joints[1].x, hand[1] - joints[1].y, hand[2] - joints[1].z };
    double normal[3] = { first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0] };
    const double normalAlong = normal[0] * targetDirection[0] + normal[1] * targetDirection[1] + normal[2] * targetDirection[2];
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        normal[axis] -= normalAlong * targetDirection[axis];
    }
    const double normalLength = sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    double bend[3]{};
    if (normalLength > upper * lower * 64.0 * FLT_EPSILON)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            normal[axis] /= normalLength;
        }
        bend[0] = targetDirection[1] * normal[2] - targetDirection[2] * normal[1];
        bend[1] = targetDirection[2] * normal[0] - targetDirection[0] * normal[2];
        bend[2] = targetDirection[0] * normal[1] - targetDirection[1] * normal[0];
    }
    else
    {
        // 直線姿勢だけは胴体の前方を使い、世界の固定軸へ反転させない。
        const double projection = forward[0] * targetDirection[0] + forward[1] * targetDirection[1] + forward[2] * targetDirection[2];
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            bend[axis] = forward[axis] - projection * targetDirection[axis];
        }
    }
    double bendLength = sqrt(bend[0] * bend[0] + bend[1] * bend[1] + bend[2] * bend[2]);
    if (!(bendLength > 16.0 * FLT_EPSILON))
    {
        // 前方へ伸ばした腕には、目標方向と重なりにくい胴体の横または縦方向を使う。
        const double side[3] = { result.outward.x, result.outward.y, result.outward.z };
        const double up[3] = { result.up.x, result.up.y, result.up.z };
        const double sideAlong = side[0] * targetDirection[0] + side[1] * targetDirection[1] + side[2] * targetDirection[2];
        const double upAlong = up[0] * targetDirection[0] + up[1] * targetDirection[1] + up[2] * targetDirection[2];
        const bool useSide = fabs(sideAlong) <= fabs(upAlong);
        const double* basis = useSide ? side : up;
        const double projection = useSide ? sideAlong : upAlong;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            bend[axis] = basis[axis] - projection * targetDirection[axis];
        }
        bendLength = sqrt(bend[0] * bend[0] + bend[1] * bend[1] + bend[2] * bend[2]);
    }
    if (!(bendLength > 0.0))
    {
        return false;
    }
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!ToFiniteFloat(root[axis] + maximumReach * bend[axis] / bendLength, pole[axis]))
        {
            return false;
        }
    }
    result.target = gk::Vec3{ target[0], target[1], target[2] };
    result.pole = gk::Vec3{ pole[0], pole[1], pole[2] };
    output = result;
    return true;
}

bool BuildAnimatedModelArmIkPreview(gk::ModelHandle model, FModelArmIkPreview& output)
{
    // 腕3点、腰、胸、反対側の肩を一つの姿勢から取得する。
    uint32_t bones[6]{};
    const gk::EHumanoidBone roles[6] = { gk::EHumanoidBone::RightUpperArm, gk::EHumanoidBone::RightLowerArm, gk::EHumanoidBone::RightHand, gk::EHumanoidBone::Hips, gk::EHumanoidBone::None, gk::EHumanoidBone::LeftUpperArm };
    for (uint32_t index = 0; index < 6; ++index)
    {
        if (index == 4)
        {
            continue;
        }
        bool found = false;
        if (!FindUniqueRole(model, roles[index], bones[index], found) || !found)
        {
            return false;
        }
    }
    const gk::EHumanoidBone torsoRoles[3] = { gk::EHumanoidBone::UpperChest, gk::EHumanoidBone::Chest, gk::EHumanoidBone::Spine };
    bool foundTorso = false;
    for (uint32_t index = 0; index < 3; ++index)
    {
        if (!FindUniqueRole(model, torsoRoles[index], bones[4], foundTorso))
        {
            return false;
        }
        if (foundTorso)
        {
            break;
        }
    }
    gk::Vec3 positions[6]{};
    FModelArmIkPreview result{};
    if (!foundTorso || gk::GetModelBonePositions(model, bones, 6, positions) != 0 || !MakeMotionFollowingArmIkPreview(positions[3], positions[4], positions[5], positions, result))
    {
        return false;
    }
    for (uint32_t index = 0; index < 3; ++index)
    {
        result.bones[index] = bones[index];
    }
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
