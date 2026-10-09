// SPDX-License-Identifier: NOASSERTION
#include "model/animation/HumanoidMapping.h"
#include <gkcore/EHumanoidBone.h>

#include <stdio.h>
#include <string.h>

namespace
{

/**
 * 骨名と親階層を固定して返すテストsource。
 */
class FHumanoidMappingSource final : public gk::model::AModelAnimationSource
{
  public:
    // 骨名推定fixtureの親階層。
    gk::model::animation::FModelSkeleton skeleton;
    // 各骨の候補名。
    const char* names[32]{};
    // 書き換え可能性をfixtureごとに指定する。
    bool writable[32]{};
    // 実際に使うfixture骨数。
    uint32_t count = 0;

    gk::model::EModelAnimationFormat Format() const override
    {
        return gk::model::EModelAnimationFormat::Glb;
    }
    const gk::model::animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton;
    }
    const char* BoneName(uint32_t bone) const override
    {
        return bone < count ? names[bone] : nullptr;
    }
    bool BoneWritable(uint32_t bone) const override
    {
        return bone < count && writable[bone];
    }
    const char* MorphName(uint32_t) const override
    {
        return nullptr;
    }
    uint32_t ClipCount() const override
    {
        return 0;
    }
    const char* ClipName(uint32_t) const override
    {
        return nullptr;
    }
    double ClipDuration(uint32_t) const override
    {
        return -1.0;
    }
    bool Sample(uint32_t, double, gk::model::animation::FModelPose&, gk::String& error) const override
    {
        error.Assign("fixture has no clips");
        return false;
    }
    bool Deform(const gk::model::animation::FModelPose&, gk::detail::ModelResource&, gk::String& error) const override
    {
        error.Assign("fixture does not deform geometry");
        return false;
    }
};

/**
 * 親順の骨格と指定名をsourceへ設定する。
 */
bool SetupSource(FHumanoidMappingSource& source, const char* const* names, const int32_t* parents, uint32_t count)
{
    source.count = count;
    for (uint32_t i = 0; i < count; ++i)
    {
        source.names[i] = names[i];
        source.writable[i] = true;
    }
    if (!source.skeleton.parents.AppendRange(parents, count) || !source.skeleton.restLocalTransforms.Reserve(count))
        return false;
    gk::model::animation::FModelBoneTransform transform{};
    for (uint32_t i = 0; i < count; ++i)
        if (!source.skeleton.restLocalTransforms.Append(transform))
            return false;
    return true;
}

/**
 * Mixamo、Blender、Yumekaの一般的な骨名を階層つきで推定する。
 */
bool TestCommonAliases()
{
    FHumanoidMappingSource source;
    const char* names[] = { "Armature", "mixamorig:Hips", "mixamorig:Spine", "Spine1", "Spine2", "Neck", "Head", "J_Bip_L_UpperArm", "lower_arm.L", "Bip01 R UpperArm", "UpperLeg_L", "LowerLeg_L" };
    const int32_t parents[] = { -1, 0, 1, 2, 3, 4, 5, 5, 7, 5, 1, 10 };
    if (!SetupSource(source, names, parents, 12))
    {
        fprintf(stderr, "alias fixture setup failed\n");
        return false;
    }
    gk::Array<uint16_t> existing;
    gk::Array<uint16_t> output;
    gk::String error;
    if (!gk::model::InferHumanoidBoneRoles(source, existing, output, error))
    {
        fprintf(stderr, "common alias inference failed: %s\n", error.CStr());
        return false;
    }
    using Bone = gk::EHumanoidBone;
    const uint16_t expected[] = { 0, static_cast<uint16_t>(Bone::Hips), static_cast<uint16_t>(Bone::Spine), static_cast<uint16_t>(Bone::Chest), static_cast<uint16_t>(Bone::UpperChest), static_cast<uint16_t>(Bone::Neck), static_cast<uint16_t>(Bone::Head), static_cast<uint16_t>(Bone::LeftUpperArm), static_cast<uint16_t>(Bone::LeftLowerArm), static_cast<uint16_t>(Bone::RightUpperArm), static_cast<uint16_t>(Bone::LeftUpperLeg), static_cast<uint16_t>(Bone::LeftLowerLeg) };
    if (output.Count() != 12)
        return false;
    for (uint32_t i = 0; i < 12; ++i)
        if (output.At(i) != expected[i])
        {
            fprintf(stderr, "alias role mismatch at %u\n", i);
            return false;
        }
    return true;
}

/**
 * 日本語のMMD骨名をUTF-8表記のまま照合する。
 */
bool TestJapaneseAliases()
{
    FHumanoidMappingSource source;
    const char* names[] = { "腰", "上半身", "上半身2", "首", "頭", "左肩", "左腕", "左ひじ", "左手首", "全身" };
    const int32_t parents[] = { -1, 0, 1, 2, 3, 2, 5, 6, 7, -1 };
    if (!SetupSource(source, names, parents, 10))
        return false;
    gk::Array<uint16_t> existing, output;
    gk::String error;
    if (!gk::model::InferHumanoidBoneRoles(source, existing, output, error))
        return false;
    using Bone = gk::EHumanoidBone;
    const uint16_t expected[] = { static_cast<uint16_t>(Bone::Hips), static_cast<uint16_t>(Bone::Spine), static_cast<uint16_t>(Bone::Chest), static_cast<uint16_t>(Bone::Neck), static_cast<uint16_t>(Bone::Head), static_cast<uint16_t>(Bone::LeftShoulder), static_cast<uint16_t>(Bone::LeftUpperArm), static_cast<uint16_t>(Bone::LeftLowerArm), static_cast<uint16_t>(Bone::LeftHand), 0 };
    for (uint32_t i = 0; i < 10; ++i)
        if (output.At(i) != expected[i])
            return false;
    return true;
}

/**
 * Yumekaの主要骨名を読み、twistと脚補助骨を役割から除外する。
 */
bool TestYumekaBoneAliases()
{
    FHumanoidMappingSource source;
    const char* names[] = { "Armature", "Hips", "Spine", "Chest", "Neck", "Head", "Shoulder_L", "UpperArm_L", "LowerArm_L", "Hand_L", "UpperLeg_L", "LowerLeg_L", "Foot_L", "Toe_L", "UpperArm_twist_L", "UpperLeg_2_L", "ThumbProximal_L", "IndexIntermediate_R" };
    const int32_t parents[] = { -1, 0, 1, 2, 3, 4, 3, 6, 7, 8, 1, 10, 11, 12, 7, 10, 9, 0 };
    if (!SetupSource(source, names, parents, 18))
        return false;
    gk::Array<uint16_t> existing, output;
    gk::String error;
    if (!gk::model::InferHumanoidBoneRoles(source, existing, output, error))
        return false;
    using Bone = gk::EHumanoidBone;
    const uint16_t expected[] = { 0, static_cast<uint16_t>(Bone::Hips), static_cast<uint16_t>(Bone::Spine), static_cast<uint16_t>(Bone::Chest), static_cast<uint16_t>(Bone::Neck), static_cast<uint16_t>(Bone::Head), static_cast<uint16_t>(Bone::LeftShoulder), static_cast<uint16_t>(Bone::LeftUpperArm), static_cast<uint16_t>(Bone::LeftLowerArm), static_cast<uint16_t>(Bone::LeftHand), static_cast<uint16_t>(Bone::LeftUpperLeg), static_cast<uint16_t>(Bone::LeftLowerLeg), static_cast<uint16_t>(Bone::LeftFoot), static_cast<uint16_t>(Bone::LeftToes), 0, 0, static_cast<uint16_t>(Bone::LeftThumbProximal), static_cast<uint16_t>(Bone::RightIndexIntermediate) };
    if (output.Count() != 18)
        return false;
    for (uint32_t i = 0; i < 18; ++i)
        if (output.At(i) != expected[i])
        {
            fprintf(stderr, "Yumeka role mismatch at %u\n", i);
            return false;
        }
    return true;
}

/**
 * 手動指定を優先し、固定骨と一般的なRoot名を割り当てない。
 */
bool TestManualAndUnwritableBones()
{
    FHumanoidMappingSource source;
    const char* names[] = { "Root", "Hips", "mixamorig1:LeftArm" };
    const int32_t parents[] = { -1, 0, 1 };
    if (!SetupSource(source, names, parents, 3))
        return false;
    source.writable[1] = false;
    gk::Array<uint16_t> existing, output;
    gk::String error;
    const uint16_t values[] = { 0, static_cast<uint16_t>(gk::EHumanoidBone::LeftShoulder), 0 };
    if (!existing.AppendRange(values, 3))
        return false;
    if (!gk::model::InferHumanoidBoneRoles(source, existing, output, error))
        return false;
    return output.Count() == 3 && output.At(0) == 0 && output.At(1) == values[1] && output.At(2) == static_cast<uint16_t>(gk::EHumanoidBone::LeftUpperArm);
}

/**
 * 自動推定された同一役割の重複を拒否し、以前の出力を保つ。
 */
bool TestDuplicateRoleIsAtomicFailure()
{
    FHumanoidMappingSource source;
    const char* names[] = { "Hips", "mixamorig:Hips" };
    const int32_t parents[] = { -1, 0 };
    if (!SetupSource(source, names, parents, 2))
        return false;
    gk::Array<uint16_t> existing, output;
    gk::String error;
    if (!output.Append(77))
        return false;
    if (gk::model::InferHumanoidBoneRoles(source, existing, output, error) || output.Count() != 1 || output.At(0) != 77 || !error.Length())
    {
        fprintf(stderr, "duplicate role did not fail atomically\n");
        return false;
    }
    return true;
}

/**
 * 入力role配列の長さ不一致を拒否し、出力を維持する。
 */
bool TestInvalidInputIsAtomicFailure()
{
    FHumanoidMappingSource source;
    const char* names[] = { "Hips" };
    const int32_t parents[] = { -1 };
    if (!SetupSource(source, names, parents, 1))
        return false;
    gk::Array<uint16_t> existing, output;
    gk::String error;
    if (!existing.Append(0) || !existing.Append(0) || !output.Append(31) || gk::model::InferHumanoidBoneRoles(source, existing, output, error) || output.Count() != 1 || output.At(0) != 31 || !error.Length())
        return false;
    return true;
}

}

int main()
{
    if (!TestCommonAliases() || !TestJapaneseAliases() || !TestYumekaBoneAliases() || !TestManualAndUnwritableBones() || !TestDuplicateRoleIsAtomicFailure() || !TestInvalidInputIsAtomicFailure())
        return 1;
    return 0;
}
