// SPDX-License-Identifier: NOASSERTION
#include "model/animation/HumanoidMapping.h"
#include <gkcore/HumanoidBoneTypes.h>
#include <string.h>

namespace gk::model
{
namespace
{

/**
 * 左右を含む意味名を既存の骨役割へ対応付ける。
 */
uint16_t MakeRole(bool left, uint16_t leftRole, uint16_t rightRole)
{
    return left ? leftRole : rightRole;
}

/**
 * ASCIIの大小文字と区切りを無視した骨名を作る。
 */
bool NormalizeName(const char* name, char normalized[256])
{
    uint32_t length = 0;
    for (uint32_t i = 0; name[i]; ++i)
    {
        const unsigned char value = static_cast<unsigned char>(name[i]);
        if (value >= 'A' && value <= 'Z')
        {
            if (length + 1 >= 256)
                return false;
            normalized[length++] = static_cast<char>(value - 'A' + 'a');
        }
        else if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9'))
        {
            if (length + 1 >= 256)
                return false;
            normalized[length++] = static_cast<char>(value);
        }
    }
    normalized[length] = 0;

    // よくあるArmature、Biped、VRMの接頭辞を一度だけ除く。
    const char* prefixes[] = { "mixamorig1", "mixamorig2", "mixamorig", "mixamo", "jbip", "bip001", "bip01", "bip", "armature", "skeleton" };
    for (uint32_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
    {
        const uint32_t prefixLength = static_cast<uint32_t>(strlen(prefixes[i]));
        if (strncmp(normalized, prefixes[i], prefixLength) == 0)
        {
            memmove(normalized, normalized + prefixLength, length - prefixLength + 1);
            break;
        }
    }
    return true;
}

/**
 * twistや補助骨を自動役割割当の候補から外す。
 */
bool IsHelperName(const char* normalized)
{
    return strstr(normalized, "twist") != nullptr || strstr(normalized, "helper") != nullptr || strstr(normalized, "upperleg2") != nullptr || strstr(normalized, "lowerleg2") != nullptr;
}

/**
 * 接頭または接尾の左右記号を取り除き、残りの意味名を返す。
 */
bool ExtractSide(char name[256], bool& left)
{
    if (strncmp(name, "left", 4) == 0)
    {
        left = true;
        memmove(name, name + 4, strlen(name + 4) + 1);
        return true;
    }
    if (strncmp(name, "right", 5) == 0)
    {
        left = false;
        memmove(name, name + 5, strlen(name + 5) + 1);
        return true;
    }
    const uint32_t length = static_cast<uint32_t>(strlen(name));
    if (length > 1 && (name[length - 1] == 'l' || name[length - 1] == 'r'))
    {
        char candidate[256];
        memcpy(candidate, name, length);
        candidate[length - 1] = 0;
        const char* bases[] = { "shoulder", "clavicle", "upperarm", "lowerarm", "forearm", "hand", "wrist", "upperleg", "lowerleg", "thigh", "calf", "knee", "leg", "foot", "ball", "toe", "toebase", "toes", "eye", "thumb", "thumb01", "thumb02", "thumb03", "thumbproximal", "thumbintermediate", "thumbdistal", "index", "index01", "index02", "index03", "indexproximal", "indexintermediate", "indexdistal", "middle", "middle01", "middle02", "middle03", "middleproximal", "middleintermediate", "middledistal", "ring", "ring01", "ring02", "ring03", "ringproximal", "ringintermediate", "ringdistal", "little", "little01", "little02", "little03", "littleproximal", "littleintermediate", "littledistal", "pinky", "pinky01", "pinky02", "pinky03" };
        for (uint32_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i)
        {
            if (strcmp(candidate, bases[i]) == 0)
            {
                left = name[length - 1] == 'l';
                name[length - 1] = 0;
                return true;
            }
        }
    }
    if ((name[0] == 'l' || name[0] == 'r') && name[1])
    {
        left = name[0] == 'l';
        memmove(name, name + 1, strlen(name + 1) + 1);
        return true;
    }
    return false;
}

/**
 * 日本語MMD名を文字列の完全一致で役割へ対応付ける。
 */
uint16_t JapaneseRole(const char* name)
{
    using Bone = gk::EHumanoidBone;
    struct FAlias
    {
        const char* name;
        uint16_t role;
    };
    const FAlias aliases[] = { { "腰", static_cast<uint16_t>(Bone::Hips) }, { "上半身", static_cast<uint16_t>(Bone::Spine) }, { "上半身2", static_cast<uint16_t>(Bone::Chest) }, { "上半身3", static_cast<uint16_t>(Bone::UpperChest) }, { "首", static_cast<uint16_t>(Bone::Neck) }, { "頭", static_cast<uint16_t>(Bone::Head) }, { "左肩", static_cast<uint16_t>(Bone::LeftShoulder) }, { "右肩", static_cast<uint16_t>(Bone::RightShoulder) }, { "左腕", static_cast<uint16_t>(Bone::LeftUpperArm) }, { "右腕", static_cast<uint16_t>(Bone::RightUpperArm) }, { "左ひじ", static_cast<uint16_t>(Bone::LeftLowerArm) }, { "右ひじ", static_cast<uint16_t>(Bone::RightLowerArm) }, { "左手首", static_cast<uint16_t>(Bone::LeftHand) }, { "右手首", static_cast<uint16_t>(Bone::RightHand) }, { "左足", static_cast<uint16_t>(Bone::LeftUpperLeg) }, { "右足", static_cast<uint16_t>(Bone::RightUpperLeg) }, { "左ひざ", static_cast<uint16_t>(Bone::LeftLowerLeg) }, { "右ひざ", static_cast<uint16_t>(Bone::RightLowerLeg) }, { "左足首", static_cast<uint16_t>(Bone::LeftFoot) }, { "右足首", static_cast<uint16_t>(Bone::RightFoot) }, { "左つま先", static_cast<uint16_t>(Bone::LeftToes) }, { "右つま先", static_cast<uint16_t>(Bone::RightToes) }, { "左目", static_cast<uint16_t>(Bone::LeftEye) }, { "右目", static_cast<uint16_t>(Bone::RightEye) }, { "あご", static_cast<uint16_t>(Bone::Jaw) }, { "顎", static_cast<uint16_t>(Bone::Jaw) }, { "左親指０", static_cast<uint16_t>(Bone::LeftThumbProximal) }, { "左親指１", static_cast<uint16_t>(Bone::LeftThumbIntermediate) }, { "左親指２", static_cast<uint16_t>(Bone::LeftThumbDistal) }, { "右親指０", static_cast<uint16_t>(Bone::RightThumbProximal) }, { "右親指１", static_cast<uint16_t>(Bone::RightThumbIntermediate) }, { "右親指２", static_cast<uint16_t>(Bone::RightThumbDistal) }, { "左人指１", static_cast<uint16_t>(Bone::LeftIndexProximal) }, { "左人指２", static_cast<uint16_t>(Bone::LeftIndexIntermediate) }, { "左人指３", static_cast<uint16_t>(Bone::LeftIndexDistal) }, { "右人指１", static_cast<uint16_t>(Bone::RightIndexProximal) }, { "右人指２", static_cast<uint16_t>(Bone::RightIndexIntermediate) }, { "右人指３", static_cast<uint16_t>(Bone::RightIndexDistal) }, { "左中指１", static_cast<uint16_t>(Bone::LeftMiddleProximal) }, { "左中指２", static_cast<uint16_t>(Bone::LeftMiddleIntermediate) }, { "左中指３", static_cast<uint16_t>(Bone::LeftMiddleDistal) }, { "右中指１", static_cast<uint16_t>(Bone::RightMiddleProximal) }, { "右中指２", static_cast<uint16_t>(Bone::RightMiddleIntermediate) }, { "右中指３", static_cast<uint16_t>(Bone::RightMiddleDistal) }, { "左薬指１", static_cast<uint16_t>(Bone::LeftRingProximal) }, { "左薬指２", static_cast<uint16_t>(Bone::LeftRingIntermediate) }, { "左薬指３", static_cast<uint16_t>(Bone::LeftRingDistal) }, { "右薬指１", static_cast<uint16_t>(Bone::RightRingProximal) }, { "右薬指２", static_cast<uint16_t>(Bone::RightRingIntermediate) }, { "右薬指３", static_cast<uint16_t>(Bone::RightRingDistal) }, { "左小指１", static_cast<uint16_t>(Bone::LeftLittleProximal) }, { "左小指２", static_cast<uint16_t>(Bone::LeftLittleIntermediate) }, { "左小指３", static_cast<uint16_t>(Bone::LeftLittleDistal) }, { "右小指１", static_cast<uint16_t>(Bone::RightLittleProximal) }, { "右小指２", static_cast<uint16_t>(Bone::RightLittleIntermediate) }, { "右小指３", static_cast<uint16_t>(Bone::RightLittleDistal) } };
    for (uint32_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
        if (strcmp(name, aliases[i].name) == 0)
            return aliases[i].role;
    return 0;
}

/**
 * 正規化した骨名と既に推定した親役割から候補を一つ返す。
 */
uint16_t MatchRole(const char* sourceName, uint16_t parentRole)
{
    using Bone = gk::EHumanoidBone;
    const uint16_t japanese = JapaneseRole(sourceName);
    if (japanese)
        return japanese;
    char name[256];
    if (!NormalizeName(sourceName, name) || IsHelperName(name))
        return 0;
    if (strcmp(name, "hips") == 0 || strcmp(name, "pelvis") == 0 || strcmp(name, "chips") == 0)
        return static_cast<uint16_t>(Bone::Hips);
    if (strcmp(name, "spine") == 0 || strcmp(name, "cspine") == 0 || strcmp(name, "spine01") == 0)
        return static_cast<uint16_t>(Bone::Spine);
    if (strcmp(name, "spine1") == 0 || strcmp(name, "spine02") == 0 || strcmp(name, "chest") == 0 || strcmp(name, "cchest") == 0)
        return static_cast<uint16_t>(Bone::Chest);
    if (strcmp(name, "spine2") == 0 || strcmp(name, "spine03") == 0 || strcmp(name, "upperchest") == 0 || strcmp(name, "cupperchest") == 0)
        return static_cast<uint16_t>(Bone::UpperChest);
    if (strcmp(name, "neck") == 0 || strcmp(name, "cneck") == 0 || strcmp(name, "neck01") == 0)
        return static_cast<uint16_t>(Bone::Neck);
    if (strcmp(name, "head") == 0 || strcmp(name, "chead") == 0)
        return static_cast<uint16_t>(Bone::Head);
    if (strcmp(name, "jaw") == 0 || strcmp(name, "cjaw") == 0)
        return static_cast<uint16_t>(Bone::Jaw);

    bool left = false;
    if (!ExtractSide(name, left))
        return 0;
    const bool isLeft = left;
    if (strcmp(name, "shoulder") == 0 || strcmp(name, "clavicle") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftShoulder), static_cast<uint16_t>(Bone::RightShoulder));
    if (strcmp(name, "upperarm") == 0 || strcmp(name, "uparm") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftUpperArm), static_cast<uint16_t>(Bone::RightUpperArm));
    if (strcmp(name, "arm") == 0)
    {
        const uint16_t shoulderRole = MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftShoulder), static_cast<uint16_t>(Bone::RightShoulder));
        return parentRole == shoulderRole ? MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftUpperArm), static_cast<uint16_t>(Bone::RightUpperArm)) : 0;
    }
    if (strcmp(name, "lowerarm") == 0 || strcmp(name, "forearm") == 0 || strcmp(name, "elbow") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftLowerArm), static_cast<uint16_t>(Bone::RightLowerArm));
    if (strcmp(name, "hand") == 0 || strcmp(name, "wrist") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftHand), static_cast<uint16_t>(Bone::RightHand));
    if (strcmp(name, "upperleg") == 0 || strcmp(name, "upleg") == 0 || strcmp(name, "thigh") == 0)
    {
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftUpperLeg), static_cast<uint16_t>(Bone::RightUpperLeg));
    }
    if (strcmp(name, "lowerleg") == 0 || strcmp(name, "calf") == 0 || strcmp(name, "knee") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftLowerLeg), static_cast<uint16_t>(Bone::RightLowerLeg));
    if (strcmp(name, "leg") == 0)
    {
        if (parentRole == MakeRole(isLeft, static_cast<uint16_t>(Bone::Hips), static_cast<uint16_t>(Bone::Hips)))
            return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftUpperLeg), static_cast<uint16_t>(Bone::RightUpperLeg));
        if (parentRole == MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftUpperLeg), static_cast<uint16_t>(Bone::RightUpperLeg)))
            return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftLowerLeg), static_cast<uint16_t>(Bone::RightLowerLeg));
        return 0;
    }
    if (strcmp(name, "foot") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftFoot), static_cast<uint16_t>(Bone::RightFoot));
    if (strcmp(name, "ball") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftToes), static_cast<uint16_t>(Bone::RightToes));
    if (strcmp(name, "toe") == 0 || strcmp(name, "toebase") == 0 || strcmp(name, "toes") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftToes), static_cast<uint16_t>(Bone::RightToes));
    if (strcmp(name, "eye") == 0)
        return MakeRole(isLeft, static_cast<uint16_t>(Bone::LeftEye), static_cast<uint16_t>(Bone::RightEye));
    if (strncmp(name, "hand", 4) == 0)
        memmove(name, name + 4, strlen(name + 4) + 1);

    struct FFingerAlias
    {
        const char* name;
        uint16_t left[3];
        uint16_t right[3];
    };
    const FFingerAlias fingers[] = { { "thumb", { static_cast<uint16_t>(Bone::LeftThumbProximal), static_cast<uint16_t>(Bone::LeftThumbIntermediate), static_cast<uint16_t>(Bone::LeftThumbDistal) }, { static_cast<uint16_t>(Bone::RightThumbProximal), static_cast<uint16_t>(Bone::RightThumbIntermediate), static_cast<uint16_t>(Bone::RightThumbDistal) } }, { "index", { static_cast<uint16_t>(Bone::LeftIndexProximal), static_cast<uint16_t>(Bone::LeftIndexIntermediate), static_cast<uint16_t>(Bone::LeftIndexDistal) }, { static_cast<uint16_t>(Bone::RightIndexProximal), static_cast<uint16_t>(Bone::RightIndexIntermediate), static_cast<uint16_t>(Bone::RightIndexDistal) } }, { "middle", { static_cast<uint16_t>(Bone::LeftMiddleProximal), static_cast<uint16_t>(Bone::LeftMiddleIntermediate), static_cast<uint16_t>(Bone::LeftMiddleDistal) }, { static_cast<uint16_t>(Bone::RightMiddleProximal), static_cast<uint16_t>(Bone::RightMiddleIntermediate), static_cast<uint16_t>(Bone::RightMiddleDistal) } }, { "ring", { static_cast<uint16_t>(Bone::LeftRingProximal), static_cast<uint16_t>(Bone::LeftRingIntermediate), static_cast<uint16_t>(Bone::LeftRingDistal) }, { static_cast<uint16_t>(Bone::RightRingProximal), static_cast<uint16_t>(Bone::RightRingIntermediate), static_cast<uint16_t>(Bone::RightRingDistal) } }, { "little", { static_cast<uint16_t>(Bone::LeftLittleProximal), static_cast<uint16_t>(Bone::LeftLittleIntermediate), static_cast<uint16_t>(Bone::LeftLittleDistal) }, { static_cast<uint16_t>(Bone::RightLittleProximal), static_cast<uint16_t>(Bone::RightLittleIntermediate), static_cast<uint16_t>(Bone::RightLittleDistal) } }, { "pinky", { static_cast<uint16_t>(Bone::LeftLittleProximal), static_cast<uint16_t>(Bone::LeftLittleIntermediate), static_cast<uint16_t>(Bone::LeftLittleDistal) }, { static_cast<uint16_t>(Bone::RightLittleProximal), static_cast<uint16_t>(Bone::RightLittleIntermediate), static_cast<uint16_t>(Bone::RightLittleDistal) } } };
    for (uint32_t i = 0; i < sizeof(fingers) / sizeof(fingers[0]); ++i)
    {
        const uint32_t length = static_cast<uint32_t>(strlen(fingers[i].name));
        if (strncmp(name, fingers[i].name, length) == 0)
        {
            const char* suffix = name + length;
            uint32_t joint = suffix[0] == '1' || strcmp(suffix, "01") == 0 || strcmp(suffix, "proximal") == 0 ? 0 : suffix[0] == '2' || strcmp(suffix, "02") == 0 || strcmp(suffix, "intermediate") == 0 ? 1 : suffix[0] == '3' || strcmp(suffix, "03") == 0 || strcmp(suffix, "distal") == 0 ? 2 : 3;
            if (joint < 3 && (!suffix[0] || suffix[0] == '1' || suffix[0] == '2' || suffix[0] == '3' || strcmp(suffix, "01") == 0 || strcmp(suffix, "02") == 0 || strcmp(suffix, "03") == 0 || strcmp(suffix, "proximal") == 0 || strcmp(suffix, "intermediate") == 0 || strcmp(suffix, "distal") == 0))
                return isLeft ? fingers[i].left[joint] : fingers[i].right[joint];
        }
    }
    return 0;
}

/**
 * 入力不正または割当衝突を短い診断で返す。
 */
bool Fail(gk::String& error, const char* message)
{
    error.Assign(message);
    return false;
}

}

/**
 * 骨名と親子関係から人型の役割を推定する。既存指定は維持し、曖昧さや重複時は出力を変更しない。
 */
bool InferHumanoidBoneRoles(const AModelAnimationSource& source, const gk::Array<uint16_t>& existing, gk::Array<uint16_t>& output, gk::String& error)
{
    using Bone = gk::EHumanoidBone;
    const animation::FModelSkeleton& skeleton = source.Skeleton();
    const uint32_t count = skeleton.parents.Count();
    const uint32_t roleCount = static_cast<uint16_t>(Bone::Count);
    if (count != skeleton.restLocalTransforms.Count() || (existing.Count() != 0 && count != existing.Count()))
        return Fail(error, "人型role配列または骨格の要素数が一致しません");

    gk::Array<uint16_t> candidate;
    gk::Array<int32_t> roleOwners;
    if (!candidate.Reserve(count) || !roleOwners.Reserve(roleCount))
        return Fail(error, "人型role推定の作業領域を確保できません");
    for (uint32_t role = 0; role < roleCount; ++role)
        if (!roleOwners.Append(-1))
            return Fail(error, "人型role推定の作業領域を確保できません");

    for (uint32_t bone = 0; bone < count; ++bone)
    {
        const int32_t parent = skeleton.parents.At(bone);
        const uint16_t role = existing.Count() ? existing.At(bone) : 0;
        if (parent < -1 || parent >= static_cast<int32_t>(bone) || role >= roleCount)
            return Fail(error, "人型role推定に不正な骨格またはrole値があります");
        if (!candidate.Append(role))
            return Fail(error, "人型role推定の作業領域を確保できません");
        if (role && roleOwners.At(role) >= 0)
            return Fail(error, "手動設定された人型roleが重複しています");
        if (role)
            roleOwners.At(role) = static_cast<int32_t>(bone);
    }

    for (uint32_t bone = 0; bone < count; ++bone)
    {
        if (candidate.At(bone) || !source.BoneWritable(bone))
            continue;
        const char* name = source.BoneName(bone);
        if (!name || !*name)
            continue;
        const int32_t parent = skeleton.parents.At(bone);
        const uint16_t parentRole = parent >= 0 ? candidate.At(static_cast<uint32_t>(parent)) : 0;
        const uint16_t role = MatchRole(name, parentRole);
        if (!role)
            continue;
        if (roleOwners.At(role) >= 0)
            continue;
        if (roleOwners.At(role) < -1)
            return Fail(error, "骨名から同じ人型roleが複数推定されました");
        candidate.At(bone) = role;
        roleOwners.At(role) = -2 - static_cast<int32_t>(bone);
    }

    output.MoveFrom(candidate);
    error.Clear();
    return true;
}

}
