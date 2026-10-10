// SPDX-License-Identifier: NOASSERTION
#include "model/animation/HumanoidBoneMap.h"
#include "foundation/Memory.h"
#include "resources/ResourceIO.h"
#include <gkcore/EHumanoidBone.h>

#include <string.h>

/**
 * 人型役割の対応表を読み込む処理。
 */
namespace gk::model
{
namespace
{

/**
 * 役割名から対応する公開enum値を返す。
 */
uint16_t FindRole(const char* name, uint32_t length)
{
    using Bone = gk::EHumanoidBone;
    struct FRoleName
    {
        // ファイルに記述する公開役割名。
        const char* name;
        // 対応する公開enum値。
        uint16_t value;
    };
    static const FRoleName names[] = { { "Hips", static_cast<uint16_t>(Bone::Hips) }, { "Spine", static_cast<uint16_t>(Bone::Spine) }, { "Chest", static_cast<uint16_t>(Bone::Chest) }, { "UpperChest", static_cast<uint16_t>(Bone::UpperChest) }, { "Neck", static_cast<uint16_t>(Bone::Neck) }, { "Head", static_cast<uint16_t>(Bone::Head) }, { "LeftShoulder", static_cast<uint16_t>(Bone::LeftShoulder) }, { "LeftUpperArm", static_cast<uint16_t>(Bone::LeftUpperArm) }, { "LeftLowerArm", static_cast<uint16_t>(Bone::LeftLowerArm) }, { "LeftHand", static_cast<uint16_t>(Bone::LeftHand) }, { "RightShoulder", static_cast<uint16_t>(Bone::RightShoulder) }, { "RightUpperArm", static_cast<uint16_t>(Bone::RightUpperArm) }, { "RightLowerArm", static_cast<uint16_t>(Bone::RightLowerArm) }, { "RightHand", static_cast<uint16_t>(Bone::RightHand) }, { "LeftUpperLeg", static_cast<uint16_t>(Bone::LeftUpperLeg) }, { "LeftLowerLeg", static_cast<uint16_t>(Bone::LeftLowerLeg) }, { "LeftFoot", static_cast<uint16_t>(Bone::LeftFoot) }, { "LeftToes", static_cast<uint16_t>(Bone::LeftToes) }, { "RightUpperLeg", static_cast<uint16_t>(Bone::RightUpperLeg) }, { "RightLowerLeg", static_cast<uint16_t>(Bone::RightLowerLeg) }, { "RightFoot", static_cast<uint16_t>(Bone::RightFoot) }, { "RightToes", static_cast<uint16_t>(Bone::RightToes) }, { "LeftEye", static_cast<uint16_t>(Bone::LeftEye) }, { "RightEye", static_cast<uint16_t>(Bone::RightEye) }, { "Jaw", static_cast<uint16_t>(Bone::Jaw) }, { "LeftThumbProximal", static_cast<uint16_t>(Bone::LeftThumbProximal) }, { "LeftThumbIntermediate", static_cast<uint16_t>(Bone::LeftThumbIntermediate) }, { "LeftThumbDistal", static_cast<uint16_t>(Bone::LeftThumbDistal) }, { "LeftIndexProximal", static_cast<uint16_t>(Bone::LeftIndexProximal) }, { "LeftIndexIntermediate", static_cast<uint16_t>(Bone::LeftIndexIntermediate) }, { "LeftIndexDistal", static_cast<uint16_t>(Bone::LeftIndexDistal) }, { "LeftMiddleProximal", static_cast<uint16_t>(Bone::LeftMiddleProximal) }, { "LeftMiddleIntermediate", static_cast<uint16_t>(Bone::LeftMiddleIntermediate) }, { "LeftMiddleDistal", static_cast<uint16_t>(Bone::LeftMiddleDistal) }, { "LeftRingProximal", static_cast<uint16_t>(Bone::LeftRingProximal) }, { "LeftRingIntermediate", static_cast<uint16_t>(Bone::LeftRingIntermediate) }, { "LeftRingDistal", static_cast<uint16_t>(Bone::LeftRingDistal) }, { "LeftLittleProximal", static_cast<uint16_t>(Bone::LeftLittleProximal) }, { "LeftLittleIntermediate", static_cast<uint16_t>(Bone::LeftLittleIntermediate) }, { "LeftLittleDistal", static_cast<uint16_t>(Bone::LeftLittleDistal) }, { "RightThumbProximal", static_cast<uint16_t>(Bone::RightThumbProximal) }, { "RightThumbIntermediate", static_cast<uint16_t>(Bone::RightThumbIntermediate) }, { "RightThumbDistal", static_cast<uint16_t>(Bone::RightThumbDistal) }, { "RightIndexProximal", static_cast<uint16_t>(Bone::RightIndexProximal) }, { "RightIndexIntermediate", static_cast<uint16_t>(Bone::RightIndexIntermediate) }, { "RightIndexDistal", static_cast<uint16_t>(Bone::RightIndexDistal) }, { "RightMiddleProximal", static_cast<uint16_t>(Bone::RightMiddleProximal) }, { "RightMiddleIntermediate", static_cast<uint16_t>(Bone::RightMiddleIntermediate) }, { "RightMiddleDistal", static_cast<uint16_t>(Bone::RightMiddleDistal) }, { "RightRingProximal", static_cast<uint16_t>(Bone::RightRingProximal) }, { "RightRingIntermediate", static_cast<uint16_t>(Bone::RightRingIntermediate) }, { "RightRingDistal", static_cast<uint16_t>(Bone::RightRingDistal) }, { "RightLittleProximal", static_cast<uint16_t>(Bone::RightLittleProximal) }, { "RightLittleIntermediate", static_cast<uint16_t>(Bone::RightLittleIntermediate) }, { "RightLittleDistal", static_cast<uint16_t>(Bone::RightLittleDistal) } };
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        const uint32_t nameLength = static_cast<uint32_t>(strlen(names[i].name));
        if (length == nameLength && memcmp(name, names[i].name, length) == 0)
            return names[i].value;
    }
    return 0;
}

/**
 * 入力全体が正しいUTF-8で、NULを含まないことを確認する。
 */
bool IsValidUtf8(const uint8_t* bytes, uint32_t size)
{
    for (uint32_t i = 0; i < size;)
    {
        const uint8_t first = bytes[i];
        if (first == 0)
            return false;
        if (first < 0x80)
        {
            ++i;
            continue;
        }
        uint32_t count = 0;
        uint32_t codepoint = 0;
        if (first >= 0xC2 && first <= 0xDF)
        {
            count = 2;
            codepoint = first & 0x1F;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            count = 3;
            codepoint = first & 0x0F;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            count = 4;
            codepoint = first & 0x07;
        }
        else
            return false;
        if (size - i < count)
            return false;
        for (uint32_t j = 1; j < count; ++j)
        {
            const uint8_t continuation = bytes[i + j];
            if ((continuation & 0xC0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        if ((count == 3 && codepoint < 0x800) || (count == 4 && codepoint < 0x10000) || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF)
            return false;
        i += count;
    }
    return true;
}

/**
 * 行端のASCII空白として扱うバイトか返す。
 */
bool IsAsciiWhitespace(uint8_t value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\v' || value == '\f';
}

/**
 * 読み込み失敗の理由を設定してfalseを返す。
 */
bool Fail(String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

bool LoadHumanoidBoneMap(const char* path, const AModelAnimationSource& source, Array<uint16_t>& output, String& error)
{
    constexpr uint32_t maximumBytes = 64u * 1024u;
    const uint32_t boneCount = source.Skeleton().parents.Count();
    if (boneCount == 0)
        return Fail(error, "humanoid role map requires a non-empty skeleton");

    uint8_t* bytes = nullptr;
    uint32_t size = 0;
    if (!detail::ReadResourceFile(path, maximumBytes, bytes, size, error))
        return false;
    if (!IsValidUtf8(bytes, size))
    {
        Deallocate(bytes);
        return Fail(error, "humanoid role map is not valid UTF-8");
    }

    uint32_t offset = size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
    Array<uint16_t> candidate;
    Array<uint8_t> usedBones;
    bool usedRoles[static_cast<uint16_t>(gk::EHumanoidBone::Count)]{};
    if (!candidate.Reserve(boneCount) || !usedBones.Reserve(boneCount))
    {
        Deallocate(bytes);
        return Fail(error, "humanoid role map allocation failed");
    }
    for (uint32_t i = 0; i < boneCount; ++i)
    {
        const uint16_t none = 0;
        const uint8_t unused = 0;
        if (!candidate.Append(none) || !usedBones.Append(unused))
        {
            Deallocate(bytes);
            return Fail(error, "humanoid role map allocation failed");
        }
    }

    uint32_t assignmentCount = 0;
    while (offset < size)
    {
        const uint32_t lineStart = offset;
        while (offset < size && bytes[offset] != '\n')
            ++offset;
        const uint32_t lineEnd = offset;
        if (offset < size)
            ++offset;

        uint32_t rowEnd = lineEnd;
        if (rowEnd > lineStart && bytes[rowEnd - 1] == '\r')
            --rowEnd;
        uint32_t contentStart = lineStart;
        uint32_t contentEnd = rowEnd;
        while (contentStart < contentEnd && IsAsciiWhitespace(bytes[contentStart]))
            ++contentStart;
        while (contentEnd > contentStart && IsAsciiWhitespace(bytes[contentEnd - 1]))
            --contentEnd;
        if (contentStart == contentEnd || bytes[contentStart] == '#')
            continue;

        uint32_t separator = lineStart;
        while (separator < rowEnd && bytes[separator] != '\t')
            ++separator;
        if (separator == rowEnd)
        {
            Deallocate(bytes);
            return Fail(error, "humanoid role map row is missing a tab separator");
        }
        const uint16_t role = FindRole(reinterpret_cast<const char*>(bytes + lineStart), separator - lineStart);
        if (role == 0)
        {
            Deallocate(bytes);
            return Fail(error, "humanoid role map contains an unknown role name");
        }
        if (usedRoles[role])
        {
            Deallocate(bytes);
            return Fail(error, "humanoid role map contains a duplicate role");
        }

        uint32_t boneStart = separator + 1;
        uint32_t boneEnd = rowEnd;
        while (boneStart < boneEnd && IsAsciiWhitespace(bytes[boneStart]))
            ++boneStart;
        while (boneEnd > boneStart && IsAsciiWhitespace(bytes[boneEnd - 1]))
            --boneEnd;
        if (boneStart == boneEnd)
        {
            Deallocate(bytes);
            return Fail(error, "humanoid role map row has an empty bone name");
        }
        for (uint32_t i = boneStart; i < boneEnd; ++i)
            if (bytes[i] == '\t')
            {
                Deallocate(bytes);
                return Fail(error, "humanoid role map bone name contains a tab");
            }

        uint32_t matchedBone = boneCount;
        for (uint32_t bone = 0; bone < boneCount; ++bone)
        {
            const char* sourceName = source.BoneName(bone);
            if (!sourceName)
                continue;
            const uint32_t sourceLength = static_cast<uint32_t>(strlen(sourceName));
            if (sourceLength != boneEnd - boneStart || memcmp(sourceName, bytes + boneStart, sourceLength) != 0)
                continue;
            if (matchedBone != boneCount)
            {
                Deallocate(bytes);
                return Fail(error, "humanoid role map bone name is ambiguous");
            }
            matchedBone = bone;
        }
        if (matchedBone == boneCount)
        {
            Deallocate(bytes);
            return Fail(error, "humanoid role map bone name was not found");
        }
        if (usedBones.At(matchedBone))
        {
            Deallocate(bytes);
            return Fail(error, "humanoid role map contains a duplicate bone row");
        }
        if (!source.BoneWritable(matchedBone))
        {
            Deallocate(bytes);
            return Fail(error, "humanoid role map references an unwritable bone");
        }

        usedRoles[role] = true;
        usedBones.At(matchedBone) = 1;
        candidate.At(matchedBone) = role;
        ++assignmentCount;
    }
    Deallocate(bytes);
    if (assignmentCount == 0)
        return Fail(error, "humanoid role map has no assignments");

    output.MoveFrom(candidate);
    error.Clear();
    return true;
}

}
