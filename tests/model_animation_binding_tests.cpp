// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelAnimationBinding.h"
#include "model/animation/ModelPose.h"
#include <gkcore/EHumanoidBone.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

using namespace gk::model;

/**
 * 固定姿勢を返す骨格source。
 */
class FBindingTestSource final : public AModelAnimationSource
{
  public:
    // 固定sourceが返す親順骨格。
    animation::FModelSkeleton skeleton;
    // clip評価時に返す変換。
    animation::FModelPose sampled;
    // Fixtureが宣言する骨名の要素数。
    uint32_t boneCount = 2;
    // BoneNameが返す固定名。
    const char* names[8] = { "parent", "arm" };
    // 固定sourceが返す形式。
    EModelAnimationFormat format = EModelAnimationFormat::Glb;
    // fixture内のmorph名。
    const char* morphNames[2] = { nullptr, nullptr };

    EModelAnimationFormat Format() const override
    {
        return format;
    }
    const animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton;
    }
    const char* BoneName(uint32_t bone) const override
    {
        return bone < boneCount ? names[bone] : nullptr;
    }
    const char* MorphName(uint32_t morph) const override
    {
        return morph < 2 ? morphNames[morph] : nullptr;
    }
    uint32_t ClipCount() const override
    {
        return 1;
    }
    const char* ClipName(uint32_t clip) const override
    {
        return clip == 0 ? "test" : nullptr;
    }
    double ClipDuration(uint32_t clip) const override
    {
        return clip == 0 ? 1.0 : -1.0;
    }

    bool Sample(uint32_t clip, double seconds, animation::FModelPose& output, gk::String& error) const override
    {
        if (clip != 0 || !isfinite(seconds) || !output.localTransforms.AppendRange(sampled.localTransforms.Data(), sampled.localTransforms.Count()) || !output.morphWeights.AppendRange(sampled.morphWeights.Data(), sampled.morphWeights.Count()))
        {
            error.Assign("binding test source could not sample its pose");
            return false;
        }
        error.Clear();
        return true;
    }

    bool Deform(const animation::FModelPose&, gk::detail::ModelResource&, gk::String& error) const override
    {
        error.Assign("binding test source does not deform geometry");
        return false;
    }
};

/**
 * 90度・180度のZ回転を作る。
 */
void SetRotationZ(animation::FModelBoneTransform& transform, float degrees)
{
    // 度数をquaternionの半角へ変換する。
    const double halfAngle = degrees * 3.14159265358979323846 / 360.0;
    transform.rotation[2] = static_cast<float>(sin(halfAngle));
    transform.rotation[3] = static_cast<float>(cos(halfAngle));
}

/**
 * 骨のZ回転が独立に計算した角度と一致することを確認する。
 */
bool MatchesRotationZ(const animation::FModelBoneTransform& transform, double degrees)
{
    // 期待回転のquaternion。
    const double halfAngle = degrees * 3.14159265358979323846 / 360.0;
    const double expectedZ = sin(halfAngle);
    const double expectedW = cos(halfAngle);
    return fabs(transform.rotation[0]) < 0.0001 && fabs(transform.rotation[1]) < 0.0001 && fabs(transform.rotation[2] - expectedZ) < 0.0001 && fabs(transform.rotation[3] - expectedW) < 0.0001;
}

/**
 * 親回転と移動を持つ別骨格へのrest相対retargetを確認する。
 */
bool TestRestRelativeRetargetWithDifferentParents()
{
    // 外部clip sourceと適用先の骨格。
    FBindingTestSource source;
    FBindingTestSource target;
    // 各検査の診断先。
    gk::String error;
    // 親が先に並ぶ2節の階層。
    const int32_t parents[2] = { -1, 0 };
    // sourceの初期、clip後、targetの初期変換。
    animation::FModelBoneTransform sourceRest[2]{};
    animation::FModelBoneTransform sourceAnimated[2]{};
    animation::FModelBoneTransform targetRest[2]{};
    SetRotationZ(sourceRest[0], 90.0f);
    SetRotationZ(sourceAnimated[0], 180.0f);
    sourceRest[1].position[0] = 1.0f;
    sourceAnimated[1].position[0] = 2.0f;
    targetRest[0].position[0] = 10.0f;
    targetRest[0].position[1] = -3.0f;
    SetRotationZ(targetRest[0], -90.0f);
    targetRest[1].position[0] = 2.0f;
    if (!source.skeleton.parents.AppendRange(parents, 2) || !source.skeleton.restLocalTransforms.AppendRange(sourceRest, 2) || !source.sampled.localTransforms.AppendRange(sourceAnimated, 2) || !target.skeleton.parents.AppendRange(parents, 2) || !target.skeleton.restLocalTransforms.AppendRange(targetRest, 2))
    {
        fprintf(stderr, "retarget fixture allocation failed\n");
        return false;
    }
    // sourceとtargetの寿命をfixture内に保つasset参照。
    FModelAnimationAsset sourceAsset, targetAsset;
    sourceAsset.source = &source;
    targetAsset.source = &target;
    // 名前対応済みの2骨と、役割なしの状態。
    FModelClipState state;
    state.asset = &sourceAsset;
    const int32_t boneMap[2] = { 0, 1 };
    const uint16_t roles[2] = { 0, 0 };
    if (!state.bones.AppendRange(boneMap, 2) || !state.mappedRoles.AppendRange(roles, 2))
    {
        fprintf(stderr, "retarget mapping allocation failed\n");
        return false;
    }
    // 評価後のtarget姿勢。
    animation::FModelPose output;
    if (!SampleBoundClip(state, targetAsset, output, error))
    {
        fprintf(stderr, "retarget evaluation failed: %s\n", error.CStr());
        return false;
    }
    const auto& parent = output.localTransforms.At(0);
    const auto& arm = output.localTransforms.At(1);
    if (fabsf(parent.rotation[2]) > 0.0001f || fabsf(parent.rotation[3] - 1.0f) > 0.0001f || fabsf(arm.position[0] - 1.0f) > 0.0001f || fabsf(arm.position[1]) > 0.0001f)
    {
        fprintf(stderr, "retarget did not map source parent-space motion into target local space\n");
        return false;
    }
    // target階層へ合成したモデル空間行列。
    gk::Array<float> matrices;
    if (!animation::EvaluateModelPose(target.skeleton, output, matrices, error) || fabsf(matrices.At(28) - 11.0f) > 0.0001f || fabsf(matrices.At(29) + 3.0f) > 0.0001f)
    {
        fprintf(stderr, "retarget world-space child position did not follow the translated target parent\n");
        return false;
    }
    return true;
}

/**
 * 人型の腕を別骨格へ移す際、適用先の骨長を保つ。
 */
bool TestHumanoidRolePreservesTargetLimbLength()
{
    // 同じ構造だが腕長が異なるsourceとtarget。
    FBindingTestSource source;
    FBindingTestSource target;
    // 評価診断を受け取る文字列。
    gk::String error;
    // 親と腕からなる階層。
    const int32_t parents[2] = { -1, 0 };
    // sourceのrest/sampleとtargetのrest pose。
    animation::FModelBoneTransform sourceRest[2]{};
    animation::FModelBoneTransform sourceAnimated[2]{};
    animation::FModelBoneTransform targetRest[2]{};
    SetRotationZ(sourceRest[0], 90.0f);
    SetRotationZ(sourceAnimated[0], 180.0f);
    sourceRest[1].position[0] = 1.0f;
    sourceAnimated[1].position[0] = 8.0f;
    targetRest[1].position[0] = 2.0f;
    if (!source.skeleton.parents.AppendRange(parents, 2) || !source.skeleton.restLocalTransforms.AppendRange(sourceRest, 2) || !source.sampled.localTransforms.AppendRange(sourceAnimated, 2) || !target.skeleton.parents.AppendRange(parents, 2) || !target.skeleton.restLocalTransforms.AppendRange(targetRest, 2))
    {
        fprintf(stderr, "humanoid retarget fixture allocation failed\n");
        return false;
    }
    // 腕の役割を指定して適用先の骨長を維持する。
    FModelAnimationAsset sourceAsset, targetAsset;
    sourceAsset.source = &source;
    targetAsset.source = &target;
    // 腕の対応先役割を明示したclip状態。
    FModelClipState state;
    state.asset = &sourceAsset;
    const int32_t boneMap[2] = { 0, 1 };
    const uint16_t roles[2] = { 0, static_cast<uint16_t>(gk::EHumanoidBone::LeftUpperArm) };
    if (!state.bones.AppendRange(boneMap, 2) || !state.mappedRoles.AppendRange(roles, 2))
    {
        fprintf(stderr, "humanoid retarget mapping allocation failed\n");
        return false;
    }
    // targetへ出力された姿勢。
    animation::FModelPose output;
    if (!SampleBoundClip(state, targetAsset, output, error))
    {
        fprintf(stderr, "humanoid retarget evaluation failed: %s\n", error.CStr());
        return false;
    }
    if (fabsf(output.localTransforms.At(1).position[0] - 2.0f) > 0.0001f || fabsf(output.localTransforms.At(1).position[1]) > 0.0001f)
    {
        fprintf(stderr, "humanoid retarget changed the target arm length\n");
        return false;
    }
    return true;
}

/**
 * 名前の異なる骨格を役割で結び、腰移動比とmorph名順を確認する。
 */
bool TestRoleBindingAndMorphNameOrder()
{
    // source clipと適用先の骨格source。
    FBindingTestSource source;
    FBindingTestSource target;
    // 評価時の診断先。
    gk::String error;
    // 親子関係と各source/targetのrest・sample変換。
    const int32_t parents[2] = { -1, 0 };
    animation::FModelBoneTransform sourceRest[2]{};
    animation::FModelBoneTransform sourceAnimated[2]{};
    animation::FModelBoneTransform targetRest[2]{};
    source.names[0] = "source pelvis";
    source.names[1] = "source arm";
    target.names[0] = "target hips";
    target.names[1] = "target upper arm";
    source.morphNames[0] = "smile";
    source.morphNames[1] = "blink";
    target.morphNames[0] = "blink";
    target.morphNames[1] = "smile";
    sourceRest[0].position[1] = 1.0f;
    sourceAnimated[0].position[1] = 2.0f;
    sourceRest[1].position[0] = 1.0f;
    sourceAnimated[1].position[0] = 8.0f;
    targetRest[0].position[1] = 2.0f;
    targetRest[1].position[0] = 2.0f;
    // sourceとtargetの既定morph数を揃える。
    const float sourceMorphRest[2] = { 0.0f, 0.0f };
    const float sourceMorphSample[2] = { 0.2f, 0.8f };
    const float targetMorphRest[2] = { 0.0f, 0.0f };
    if (!source.skeleton.parents.AppendRange(parents, 2) || !source.skeleton.restLocalTransforms.AppendRange(sourceRest, 2) || !source.skeleton.restMorphWeights.AppendRange(sourceMorphRest, 2) || !source.sampled.localTransforms.AppendRange(sourceAnimated, 2) || !source.sampled.morphWeights.AppendRange(sourceMorphSample, 2) || !target.skeleton.parents.AppendRange(parents, 2) || !target.skeleton.restLocalTransforms.AppendRange(targetRest, 2) || !target.skeleton.restMorphWeights.AppendRange(targetMorphRest, 2))
    {
        fprintf(stderr, "role and morph fixture allocation failed\n");
        return false;
    }
    // role sourceとtarget assetを用意する。
    FModelAnimationAsset sourceAsset, targetAsset;
    sourceAsset.reference = { 1, nullptr };
    sourceAsset.source = &source;
    targetAsset.source = &target;
    const uint16_t roles[2] = { static_cast<uint16_t>(gk::EHumanoidBone::Hips), static_cast<uint16_t>(gk::EHumanoidBone::LeftUpperArm) };
    gk::Array<uint16_t> targetRoles;
    if (!sourceAsset.roles.AppendRange(roles, 2) || !targetRoles.AppendRange(roles, 2))
    {
        fprintf(stderr, "role fixture allocation failed\n");
        return false;
    }
    // BuildClipBindingが生成する対応表と結果pose。
    FModelClipState state;
    animation::FModelPose output;
    if (!BuildClipBinding(sourceAsset, 0, &targetAsset, targetRoles, state, error))
    {
        fprintf(stderr, "role binding failed: %s\n", error.CStr());
        return false;
    }
    if (state.sourceRestModelMatrices.Count() != 32 || state.targetRestModelMatrices.Count() != 32 || state.sourceRestWorldRotations.Count() != 8 || state.targetRestWorldRotations.Count() != 8)
    {
        fprintf(stderr, "role binding did not retain precomputed rest transforms\n");
        return false;
    }
    const bool sampled = SampleBoundClip(state, targetAsset, output, error);
    Release(&sourceAsset.reference);
    if (!sampled)
    {
        fprintf(stderr, "role-bound sample failed: %s\n", error.CStr());
        return false;
    }
    if (state.bones.Count() != 2 || state.bones.At(0) != 0 || state.bones.At(1) != 1 || state.mappedRoles.At(0) != roles[0] || state.mappedRoles.At(1) != roles[1] || fabsf(output.localTransforms.At(0).position[1] - 4.0f) > 0.0001f || fabsf(output.localTransforms.At(1).position[0] - 2.0f) > 0.0001f || fabsf(output.morphWeights.At(0) - 0.8f) > 0.0001f || fabsf(output.morphWeights.At(1) - 0.2f) > 0.0001f)
    {
        fprintf(stderr, "role binding changed target limb length, root-motion ratio, or morph-name order\n");
        return false;
    }
    return true;
}

/**
 * 異なる任意名と形式の人型骨格を手動roleで結び、膝の曲げと適用先の脚長を確認する。
 */
bool TestGenericHumanoidRoleRetarget()
{
    // source clipと、名称・姿勢・骨長が異なるtarget。
    FBindingTestSource source;
    FBindingTestSource target;
    source.boneCount = 5;
    target.boneCount = 5;
    source.format = EModelAnimationFormat::Fbx;
    target.format = EModelAnimationFormat::Glb;
    const char* sourceNames[5] = { "source origin q", "pelvis amber", "joint orbit seven", "segment cobalt", "terminal ivory" };
    const char* targetNames[5] = { "destination root m", "waist cedar", "stratum violet", "member copper", "end pearl" };
    for (uint32_t i = 0; i < 5; ++i)
    {
        source.names[i] = sourceNames[i];
        target.names[i] = targetNames[i];
    }
    // root、腰、大腿、すね、足首からなる別名の階層。
    const int32_t parents[5] = { -1, 0, 1, 2, 3 };
    animation::FModelBoneTransform sourceRest[5]{};
    animation::FModelBoneTransform sourceAnimated[5]{};
    animation::FModelBoneTransform targetRest[5]{};
    SetRotationZ(sourceRest[1], 20.0f);
    SetRotationZ(sourceRest[2], -10.0f);
    SetRotationZ(sourceRest[3], 5.0f);
    sourceRest[1].position[1] = 2.0f;
    sourceRest[2].position[1] = -4.0f;
    sourceRest[3].position[1] = -5.0f;
    sourceRest[4].position[1] = -1.5f;
    memcpy(sourceAnimated, sourceRest, sizeof(sourceRest));
    SetRotationZ(sourceAnimated[3], 65.0f);
    SetRotationZ(targetRest[0], 90.0f);
    SetRotationZ(targetRest[1], -15.0f);
    SetRotationZ(targetRest[2], 30.0f);
    SetRotationZ(targetRest[3], -20.0f);
    SetRotationZ(targetRest[4], 10.0f);
    targetRest[1].position[1] = 1.0f;
    targetRest[2].position[1] = -2.0f;
    targetRest[3].position[1] = -3.0f;
    targetRest[4].position[1] = -0.75f;
    if (!source.skeleton.parents.AppendRange(parents, 5) || !source.skeleton.restLocalTransforms.AppendRange(sourceRest, 5) || !source.sampled.localTransforms.AppendRange(sourceAnimated, 5) || !target.skeleton.parents.AppendRange(parents, 5) || !target.skeleton.restLocalTransforms.AppendRange(targetRest, 5))
    {
        fprintf(stderr, "generic humanoid fixture allocation failed\n");
        return false;
    }
    // 異なる形式間でもbone名に頼らず手動roleで対応する。
    FModelAnimationAsset sourceAsset, targetAsset;
    sourceAsset.reference = { 1, nullptr };
    sourceAsset.source = &source;
    targetAsset.source = &target;
    using Bone = gk::EHumanoidBone;
    const uint16_t roles[5] = { 0, static_cast<uint16_t>(Bone::Hips), static_cast<uint16_t>(Bone::LeftUpperLeg), static_cast<uint16_t>(Bone::LeftLowerLeg), static_cast<uint16_t>(Bone::LeftFoot) };
    gk::Array<uint16_t> targetRoles;
    if (!sourceAsset.roles.AppendRange(roles, 5) || !targetRoles.AppendRange(roles, 5))
    {
        fprintf(stderr, "generic humanoid role allocation failed\n");
        return false;
    }
    // role対応を構築し、別体型のsample姿勢へ適用する。
    FModelClipState state;
    gk::String error;
    if (!BuildClipBinding(sourceAsset, 0, &targetAsset, targetRoles, state, error))
    {
        fprintf(stderr, "generic humanoid role binding failed: %s\n", error.CStr());
        return false;
    }
    animation::FModelPose output;
    const bool sampled = SampleBoundClip(state, targetAsset, output, error);
    Release(&sourceAsset.reference);
    if (!sampled)
    {
        fprintf(stderr, "generic humanoid role sample failed: %s\n", error.CStr());
        return false;
    }
    if (output.localTransforms.Count() != 5)
    {
        fprintf(stderr, "generic humanoid role sample returned an unexpected bone count\n");
        return false;
    }
    // 想定されるtarget local回転はroot90、hips-15、thigh30、knee40、foot10度。
    const double expectedAngles[5] = { 90.0, -15.0, 30.0, 40.0, 10.0 };
    for (uint32_t i = 0; i < 5; ++i)
        if (!MatchesRotationZ(output.localTransforms.At(i), expectedAngles[i]))
        {
            fprintf(stderr, "generic humanoid retarget rotation mismatch at %s\n", targetNames[i]);
            return false;
        }
    // roleで対応した腰から足首までの親基準位置はtarget restの長さを保つ。
    for (uint32_t i = 1; i < 5; ++i)
        if (fabsf(output.localTransforms.At(i).position[0] - targetRest[i].position[0]) > 0.0001f || fabsf(output.localTransforms.At(i).position[1] - targetRest[i].position[1]) > 0.0001f || fabsf(output.localTransforms.At(i).position[2] - targetRest[i].position[2]) > 0.0001f)
        {
            fprintf(stderr, "generic humanoid retarget changed target limb length at %s\n", targetNames[i]);
            return false;
        }
    // 期待足首位置はtargetの各rest長を独立に回転・加算した座標。
    const double expectedFootX = -1.0 + 2.0 * sin(75.0 * 3.14159265358979323846 / 180.0) + 3.0 * sin(105.0 * 3.14159265358979323846 / 180.0) + 0.75 * sin(145.0 * 3.14159265358979323846 / 180.0);
    const double expectedFootY = -2.0 * cos(75.0 * 3.14159265358979323846 / 180.0) - 3.0 * cos(105.0 * 3.14159265358979323846 / 180.0) - 0.75 * cos(145.0 * 3.14159265358979323846 / 180.0);
    gk::Array<float> matrices;
    if (!animation::EvaluateModelPose(target.skeleton, output, matrices, error) || matrices.Count() != 80 || fabs(matrices.At(4u * 16u + 12u) - expectedFootX) > 0.0002 || fabs(matrices.At(4u * 16u + 13u) - expectedFootY) > 0.0002)
    {
        fprintf(stderr, "generic humanoid foot position disagreed with the independent rest-length calculation\n");
        return false;
    }
    return true;
}

/**
 * 重複roleでclip bindingを拒否し、candidateを元の状態に保つ。
 */
bool TestDuplicateRolesPreserveCandidate()
{
    // 名前が一致しないsource/targetを用意する。
    FBindingTestSource source;
    FBindingTestSource target;
    // binding失敗の診断を受け取る。
    gk::String error;
    // 親子階層と有限な初期変換。
    const int32_t parents[2] = { -1, 0 };
    const animation::FModelBoneTransform transforms[2]{};
    if (!source.skeleton.parents.AppendRange(parents, 2) || !source.skeleton.restLocalTransforms.AppendRange(transforms, 2) || !target.skeleton.parents.AppendRange(parents, 2) || !target.skeleton.restLocalTransforms.AppendRange(transforms, 2))
    {
        fprintf(stderr, "duplicate-role fixture allocation failed\n");
        return false;
    }
    source.names[0] = "source parent";
    source.names[1] = "source arm";
    target.names[0] = "target parent";
    target.names[1] = "target arm";
    // 同じhips roleを持つsource asset。
    FModelAnimationAsset sourceAsset, targetAsset;
    sourceAsset.reference = { 1, nullptr };
    sourceAsset.source = &source;
    targetAsset.source = &target;
    const uint16_t duplicateRoles[2] = { static_cast<uint16_t>(gk::EHumanoidBone::Hips), static_cast<uint16_t>(gk::EHumanoidBone::Hips) };
    const uint16_t requestedRoles[2] = { 0, static_cast<uint16_t>(gk::EHumanoidBone::Hips) };
    gk::Array<uint16_t> targetRoles;
    if (!sourceAsset.roles.AppendRange(duplicateRoles, 2) || !targetRoles.AppendRange(requestedRoles, 2))
    {
        fprintf(stderr, "duplicate-role values allocation failed\n");
        return false;
    }
    // 既存candidateの各配列へ失敗後の比較値を入れる。
    FModelClipState candidate;
    const int32_t boneSentinel = 77, morphSentinel = 88;
    const uint16_t roleSentinel = 99;
    if (!candidate.bones.Append(boneSentinel) || !candidate.mappedRoles.Append(roleSentinel) || !candidate.morphs.Append(morphSentinel))
    {
        fprintf(stderr, "duplicate-role sentinel allocation failed\n");
        return false;
    }
    const bool accepted = BuildClipBinding(sourceAsset, 0, &targetAsset, targetRoles, candidate, error);
    if (accepted || error.Empty() || candidate.asset || candidate.bones.Count() != 1 || candidate.bones.At(0) != boneSentinel || candidate.mappedRoles.Count() != 1 || candidate.mappedRoles.At(0) != roleSentinel || candidate.morphs.Count() != 1 || candidate.morphs.At(0) != morphSentinel)
    {
        fprintf(stderr, "duplicate-role rejection changed the binding candidate or omitted its error\n");
        return false;
    }
    return true;
}

/**
 * 一意でないtarget名が同じsource骨へ二重対応するのを拒否する。
 */
bool TestDuplicateTargetNamesAreRejected()
{
    // 異なる親子階層を持つsourceとtarget。
    FBindingTestSource source;
    FBindingTestSource target;
    // binding結果の診断先。
    gk::String error;
    // 両骨とも名前対応のみを使う階層。
    const int32_t parents[2] = { -1, 0 };
    const animation::FModelBoneTransform transforms[2]{};
    if (!source.skeleton.parents.AppendRange(parents, 2) || !source.skeleton.restLocalTransforms.AppendRange(transforms, 2) || !target.skeleton.parents.AppendRange(parents, 2) || !target.skeleton.restLocalTransforms.AppendRange(transforms, 2))
    {
        fprintf(stderr, "duplicate-target-name fixture allocation failed\n");
        return false;
    }
    source.names[0] = "source parent";
    source.names[1] = "source arm";
    target.names[0] = "source parent";
    target.names[1] = "source parent";
    // target roleを空にし、名前だけで対応付ける。
    FModelAnimationAsset sourceAsset, targetAsset;
    sourceAsset.reference = { 1, nullptr };
    sourceAsset.source = &source;
    targetAsset.source = &target;
    gk::Array<uint16_t> targetRoles;
    FModelClipState candidate;
    const bool accepted = BuildClipBinding(sourceAsset, 0, &targetAsset, targetRoles, candidate, error);
    if (accepted || error.Empty() || candidate.asset || candidate.bones.Count() != 0 || candidate.mappedRoles.Count() != 0 || candidate.morphs.Count() != 0)
    {
        fprintf(stderr, "duplicate target names were mapped to one source bone\n");
        return false;
    }
    return true;
}

/**
 * sourceの親scaleを通したtranslation差分をtarget親座標へ戻す。
 */
bool TestScaledSourceParentTranslation()
{
    // 同じ階層で親の単位scaleだけ異なるsourceとtarget。
    FBindingTestSource source;
    FBindingTestSource target;
    // 評価の診断先。
    gk::String error;
    // 親子2骨の階層とrest/sample値。
    const int32_t parents[2] = { -1, 0 };
    animation::FModelBoneTransform sourceRest[2]{};
    animation::FModelBoneTransform sourceAnimated[2]{};
    animation::FModelBoneTransform targetRest[2]{};
    sourceRest[0].scale[0] = 0.01f;
    sourceAnimated[0].scale[0] = 0.01f;
    sourceRest[1].position[0] = 1.0f;
    sourceAnimated[1].position[0] = 101.0f;
    targetRest[1].position[0] = 2.0f;
    if (!source.skeleton.parents.AppendRange(parents, 2) || !source.skeleton.restLocalTransforms.AppendRange(sourceRest, 2) || !source.sampled.localTransforms.AppendRange(sourceAnimated, 2) || !target.skeleton.parents.AppendRange(parents, 2) || !target.skeleton.restLocalTransforms.AppendRange(targetRest, 2))
    {
        fprintf(stderr, "scaled source-parent fixture allocation failed\n");
        return false;
    }
    // 名前対応により親scaleを含むmodel-space移動をretargetする。
    FModelAnimationAsset sourceAsset, targetAsset;
    sourceAsset.source = &source;
    targetAsset.source = &target;
    FModelClipState state;
    state.asset = &sourceAsset;
    const int32_t boneMap[2] = { 0, 1 };
    const uint16_t roles[2] = { 0, 0 };
    if (!state.bones.AppendRange(boneMap, 2) || !state.mappedRoles.AppendRange(roles, 2))
    {
        fprintf(stderr, "scaled source-parent map allocation failed\n");
        return false;
    }
    // target parent scale1ではsourceの100単位差分が1単位になる。
    animation::FModelPose output;
    if (!SampleBoundClip(state, targetAsset, output, error))
    {
        fprintf(stderr, "scaled source-parent retarget failed: %s\n", error.CStr());
        return false;
    }
    if (fabsf(output.localTransforms.At(1).position[0] - 3.0f) > 0.0001f || fabsf(output.localTransforms.At(1).position[1]) > 0.0001f)
    {
        fprintf(stderr, "source parent scale was not included in retarget translation\n");
        return false;
    }
    return true;
}

/**
 * 腰の移動を腰より上の補助骨の有無に左右されず体格比で転送する。
 */
bool EvaluateHumanoidHipsTranslationScale(bool withHelper, float& hipsDelta)
{
    // 任意の骨名を持つsourceと、脚の長さが2倍のtarget。
    FBindingTestSource source;
    FBindingTestSource target;
    // 補助骨ありは5骨、なしは腰から始まる4骨。
    const uint32_t boneCount = withHelper ? 5u : 4u;
    source.boneCount = boneCount;
    target.boneCount = boneCount;
    const char* sourceNames[5] = { "amber root", "source pelvis", "source thigh", "source shin", "source foot" };
    const char* targetNames[5] = { "violet root", "destination waist", "destination upper", "destination lower", "destination end" };
    for (uint32_t i = 0; i < boneCount; ++i)
    {
        source.names[i] = sourceNames[i];
        target.names[i] = targetNames[i];
    }
    // 腰より上の補助骨がある場合だけ骨番号を1つずらす。
    const int32_t helperParents[5] = { -1, 0, 1, 2, 3 };
    const int32_t directParents[4] = { -1, 0, 1, 2 };
    const int32_t* parents = withHelper ? helperParents : directParents;
    animation::FModelBoneTransform sourceRest[5]{};
    animation::FModelBoneTransform sourceAnimated[5]{};
    animation::FModelBoneTransform targetRest[5]{};
    const uint32_t hips = withHelper ? 1u : 0u;
    const uint32_t upperLeg = hips + 1u;
    const uint32_t lowerLeg = hips + 2u;
    const uint32_t foot = hips + 3u;
    if (withHelper)
    {
        sourceRest[0].position[1] = 1.0f;
        targetRest[0].position[1] = 2.0f;
    }
    else
    {
        sourceRest[hips].position[1] = 1.0f;
        targetRest[hips].position[1] = 2.0f;
    }
    sourceRest[lowerLeg].position[1] = -0.5f;
    sourceRest[foot].position[1] = -0.5f;
    targetRest[lowerLeg].position[1] = -1.0f;
    targetRest[foot].position[1] = -1.0f;
    memcpy(sourceAnimated, sourceRest, sizeof(sourceRest));
    sourceAnimated[hips].position[1] += 0.25f;
    if (!source.skeleton.parents.AppendRange(parents, boneCount) || !source.skeleton.restLocalTransforms.AppendRange(sourceRest, boneCount) || !source.sampled.localTransforms.AppendRange(sourceAnimated, boneCount) || !target.skeleton.parents.AppendRange(parents, boneCount) || !target.skeleton.restLocalTransforms.AppendRange(targetRest, boneCount))
    {
        fprintf(stderr, "hips-scale fixture allocation failed\n");
        return false;
    }
    // role対応を使い、sourceとtargetの骨名は一致させない。
    FModelAnimationAsset sourceAsset, targetAsset;
    sourceAsset.source = &source;
    targetAsset.source = &target;
    using Bone = gk::EHumanoidBone;
    const uint16_t helperRoles[5] = { 0, static_cast<uint16_t>(Bone::Hips), static_cast<uint16_t>(Bone::LeftUpperLeg), static_cast<uint16_t>(Bone::LeftLowerLeg), static_cast<uint16_t>(Bone::LeftFoot) };
    const uint16_t directRoles[4] = { static_cast<uint16_t>(Bone::Hips), static_cast<uint16_t>(Bone::LeftUpperLeg), static_cast<uint16_t>(Bone::LeftLowerLeg), static_cast<uint16_t>(Bone::LeftFoot) };
    const uint16_t* roles = withHelper ? helperRoles : directRoles;
    gk::Array<uint16_t> targetRoles;
    if (!sourceAsset.roles.AppendRange(roles, boneCount) || !targetRoles.AppendRange(roles, boneCount))
    {
        fprintf(stderr, "hips-scale role allocation failed\n");
        return false;
    }
    // cache付きbindingを作り、同じ対応表からcacheなし状態も用意する。
    FModelClipState cachedState;
    gk::String error;
    if (!BuildClipBinding(sourceAsset, 0, &targetAsset, targetRoles, cachedState, error))
    {
        fprintf(stderr, "hips-scale binding failed: %s\n", error.CStr());
        return false;
    }
    FModelClipState fallbackState;
    fallbackState.asset = &sourceAsset;
    fallbackState.clip = 0;
    if (!fallbackState.bones.AppendRange(cachedState.bones.Data(), cachedState.bones.Count()) || !fallbackState.mappedRoles.AppendRange(cachedState.mappedRoles.Data(), cachedState.mappedRoles.Count()) || !fallbackState.morphs.AppendRange(cachedState.morphs.Data(), cachedState.morphs.Count()))
    {
        fprintf(stderr, "hips-scale fallback mapping allocation failed\n");
        return false;
    }
    animation::FModelPose cachedOutput, fallbackOutput;
    const bool sampledCached = SampleBoundClip(cachedState, targetAsset, cachedOutput, error);
    const bool sampledFallback = sampledCached && SampleBoundClip(fallbackState, targetAsset, fallbackOutput, error);
    Release(&sourceAsset.reference);
    if (!sampledCached || !sampledFallback)
    {
        fprintf(stderr, "hips-scale sample failed: %s\n", error.CStr());
        return false;
    }
    hipsDelta = cachedOutput.localTransforms.At(hips).position[1] - targetRest[hips].position[1];
    if (fabsf(hipsDelta - 0.5f) > 0.0001f)
    {
        fprintf(stderr, "hips translation did not scale by the source-to-target body height ratio\n");
        return false;
    }
    for (uint32_t i = upperLeg; i <= foot; ++i)
        if (fabsf(cachedOutput.localTransforms.At(i).position[1] - targetRest[i].position[1]) > 0.0001f)
        {
            fprintf(stderr, "hips-scale retarget changed an independently sized target leg offset\n");
            return false;
        }
    for (uint32_t i = 0; i < boneCount; ++i)
        for (uint32_t axis = 0; axis < 3; ++axis)
            if (fabsf(cachedOutput.localTransforms.At(i).position[axis] - fallbackOutput.localTransforms.At(i).position[axis]) > 0.0001f)
            {
                fprintf(stderr, "cached and fallback rest-pose paths disagreed for hips translation\n");
                return false;
            }
    // target脚先のモデル位置は、target restの脚長と転送した腰移動から独立に求める。
    gk::Array<float> matrices;
    const float expectedFootY = 2.0f + 0.5f - 1.0f - 1.0f;
    if (!animation::EvaluateModelPose(target.skeleton, cachedOutput, matrices, error) || fabsf(matrices.At(foot * 16u + 13u) - expectedFootY) > 0.0001f)
    {
        fprintf(stderr, "hips-scale target foot position disagreed with its rest offsets and hips motion\n");
        return false;
    }
    return true;
}

/**
 * 腰の親に補助骨がある場合もない場合も同じ体格比で移動する。
 */
bool TestHumanoidHipsTranslationScale()
{
    // 2種類の階層で得た腰移動量。
    float helperHipsDelta = 0.0f;
    float directHipsDelta = 0.0f;
    if (!EvaluateHumanoidHipsTranslationScale(true, helperHipsDelta) || !EvaluateHumanoidHipsTranslationScale(false, directHipsDelta))
        return false;
    if (fabsf(helperHipsDelta - directHipsDelta) > 0.0001f)
    {
        fprintf(stderr, "adding a helper bone changed the retargeted hips motion\n");
        return false;
    }
    return true;
}

}

int main()
{
    return TestRestRelativeRetargetWithDifferentParents() && TestHumanoidRolePreservesTargetLimbLength() && TestRoleBindingAndMorphNameOrder() && TestGenericHumanoidRoleRetarget() && TestDuplicateRolesPreserveCandidate() && TestDuplicateTargetNamesAreRejected() && TestScaledSourceParentTranslation() && TestHumanoidHipsTranslationScale() ? 0 : 1;
}
