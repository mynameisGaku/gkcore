// SPDX-License-Identifier: NOASSERTION
#include "../src/model/animation/ModelAnimationBinding.h"
#include "../src/model/animation/ModelPose.h"
#include "../include/gkcore/EHumanoidBone.h"

#include <math.h>
#include <stdio.h>

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
    // fixture内の親・腕名。
    const char* names[2] = { "parent", "arm" };
    // fixture内のmorph名。
    const char* morphNames[2] = { nullptr, nullptr };

    EModelAnimationFormat Format() const override
    {
        return EModelAnimationFormat::Glb;
    }
    const animation::FModelSkeleton& Skeleton() const override
    {
        return skeleton;
    }
    const char* BoneName(uint32_t bone) const override
    {
        return bone < 2 ? names[bone] : nullptr;
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

}

int main()
{
    return TestRestRelativeRetargetWithDifferentParents() && TestHumanoidRolePreservesTargetLimbLength() && TestRoleBindingAndMorphNameOrder() && TestDuplicateRolesPreserveCandidate() && TestDuplicateTargetNamesAreRejected() && TestScaledSourceParentTranslation() ? 0 : 1;
}
