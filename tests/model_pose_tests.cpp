// SPDX-License-Identifier: NOASSERTION
#include "../src/model/animation/ModelPose.h"

#include <math.h>
#include <stdio.h>

namespace
{

using namespace gk::model::animation;

/**
 * 失敗理由を表示してテストを停止する。
 */
bool Fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return false;
}

/**
 * 3本の親子boneと2個のmorph defaultを作る。
 */
bool MakeSkeleton(FModelSkeleton& skeleton, bool zeroRootScale = false)
{
    const int32_t parents[3] = { -1, 0, 1 };
    FModelBoneTransform transforms[3]{};
    transforms[0].scale[0] = zeroRootScale ? 0.0f : 1.0f;
    transforms[1].position[0] = 1.0f;
    transforms[2].position[0] = 1.0f;
    const float morph[2] = { 0.0f, 0.25f };
    return skeleton.parents.AppendRange(parents, 3) && skeleton.restLocalTransforms.AppendRange(transforms, 3) && skeleton.restMorphWeights.AppendRange(morph, 2);
}

/**
 * pose内の変換とmorph値を入力から構築する。
 */
bool MakePose(FModelPose& pose, float rootX, float rootQuaternionW, float rootScale, float morph)
{
    FModelBoneTransform transforms[3]{};
    transforms[0].position[0] = rootX;
    transforms[0].rotation[3] = rootQuaternionW;
    transforms[0].scale[0] = rootScale;
    transforms[1].position[0] = 1.0f;
    transforms[2].position[0] = 1.0f;
    const float morphWeights[2] = { morph, morph + 0.25f };
    return pose.localTransforms.AppendRange(transforms, 3) && pose.morphWeights.AppendRange(morphWeights, 2);
}

/**
 * rest初期化、FKの親子変換、有限なzero scaleを確認する。
 */
bool TestRestPoseAndForwardEvaluation()
{
    FModelSkeleton skeleton;
    FModelPose pose;
    FModelPose zeroScalePose;
    gk::String error;
    if (!MakeSkeleton(skeleton) || !InitializeModelPose(skeleton, pose, error))
        return Fail(error.CStr());
    if (pose.localTransforms.Count() != 3 || pose.morphWeights.Count() != 2 || pose.morphWeights.At(1) != 0.25f)
        return Fail("rest pose did not copy skeleton transforms and morph defaults");
    gk::Array<float> matrices;
    if (!EvaluateModelPose(skeleton, pose, matrices, error) || matrices.Count() != 48)
        return Fail("forward evaluation did not return one matrix per bone");
    if (fabsf(matrices.At(12)) > 0.0001f || fabsf(matrices.At(28) - 1.0f) > 0.0001f || fabsf(matrices.At(44) - 2.0f) > 0.0001f)
        return Fail("forward evaluation did not compose parent translations");
    FModelSkeleton zeroScaleSkeleton;
    if (!MakeSkeleton(zeroScaleSkeleton, true) || !InitializeModelPose(zeroScaleSkeleton, zeroScalePose, error))
        return Fail("pose initialization rejected finite zero scale");
    gk::Array<float> zeroScaleMatrices;
    if (!EvaluateModelPose(zeroScaleSkeleton, zeroScalePose, zeroScaleMatrices, error) || zeroScaleMatrices.At(0) != 0.0f)
        return Fail("forward evaluation did not preserve finite zero scale");
    return true;
}

/**
 * 最短経路slerp、位置/morph blend、output aliasを確認する。
 */
bool TestBlendAndAliasedOutput()
{
    FModelSkeleton skeleton;
    FModelPose first;
    FModelPose second;
    FModelPose output;
    gk::String error;
    if (!MakeSkeleton(skeleton) || !MakePose(first, 0.0f, 1.0f, 1.0f, 0.0f) || !MakePose(second, 2.0f, -1.0f, 3.0f, 1.0f))
        return Fail("pose blend fixture allocation failed");
    if (!BlendModelPoses(skeleton, first, second, 0.5f, output, error))
        return Fail(error.CStr());
    const FModelBoneTransform& root = output.localTransforms.At(0);
    if (fabsf(root.position[0] - 1.0f) > 0.0001f || fabsf(root.scale[0] - 2.0f) > 0.0001f || fabsf(root.rotation[3] - 1.0f) > 0.0001f || fabsf(output.morphWeights.At(0) - 0.5f) > 0.0001f)
        return Fail("pose blend did not linearly interpolate or use shortest quaternion path");
    if (!BlendModelPoses(skeleton, first, second, 0.5f, first, error))
        return Fail(error.CStr());
    if (fabsf(first.localTransforms.At(0).position[0] - 1.0f) > 0.0001f || fabsf(first.morphWeights.At(0) - 0.5f) > 0.0001f)
        return Fail("pose blending failed when output aliases its source");
    return true;
}

/**
 * 不正階層と不正poseを診断し、既存出力を保つ。
 */
bool TestInvalidInputsPreserveOutput()
{
    FModelSkeleton skeleton;
    FModelPose pose;
    FModelPose output;
    gk::String error;
    if (!MakeSkeleton(skeleton) || !MakePose(pose, 0.0f, 1.0f, 1.0f, 0.0f) || !MakePose(output, 9.0f, 1.0f, 1.0f, 7.0f))
        return Fail("atomic pose fixture allocation failed");
    FModelSkeleton invalidSkeleton;
    const int32_t badParents[3] = { 0, 0, 1 };
    const FModelBoneTransform transforms[3]{};
    const float morph[2]{};
    if (!invalidSkeleton.parents.AppendRange(badParents, 3) || !invalidSkeleton.restLocalTransforms.AppendRange(transforms, 3) || !invalidSkeleton.restMorphWeights.AppendRange(morph, 2))
        return Fail("invalid hierarchy fixture allocation failed");
    if (InitializeModelPose(invalidSkeleton, output, error) || output.localTransforms.At(0).position[0] != 9.0f || error.Empty())
        return Fail("invalid skeleton changed pose output or omitted its diagnostic");
    FModelPose invalidPose;
    if (!MakePose(invalidPose, 0.0f, 1.0f, 1.0f, 0.0f))
        return Fail("invalid pose fixture allocation failed");
    invalidPose.localTransforms.At(1).rotation[0] = NAN;
    gk::Array<float> sentinelMatrices;
    const float sentinel = 123.0f;
    if (!sentinelMatrices.Append(sentinel))
        return Fail("matrix sentinel allocation failed");
    error.Clear();
    if (EvaluateModelPose(skeleton, invalidPose, sentinelMatrices, error) || sentinelMatrices.Count() != 1 || sentinelMatrices.At(0) != sentinel || error.Empty())
        return Fail("invalid pose changed matrix output or omitted its diagnostic");
    return true;
}

/**
 * 公開した行列helperの値と失敗時の出力保持を確認する。
 */
bool TestModelBoneMatrixHelpers()
{
    FModelBoneTransform transform{};
    transform.position[0] = 2.0f;
    float local[16]{};
    float output[16]{};
    gk::String error;
    if (!BuildModelBoneMatrix(transform, local, error) || !MultiplyModelBoneMatrices(local, local, output, error))
        return Fail(error.CStr());
    if (fabsf(output[12] - 4.0f) > 0.0001f || fabsf(output[15] - 1.0f) > 0.0001f)
        return Fail("model bone matrix helpers returned an incorrect composed transform");
    const float sentinel = 42.0f;
    for (uint32_t element = 0; element < 16; ++element)
        output[element] = sentinel;
    transform.rotation[3] = 0.0f;
    if (BuildModelBoneMatrix(transform, output, error) || error.Empty())
        return Fail("invalid local transform was accepted by the model bone matrix helper");
    for (uint32_t element = 0; element < 16; ++element)
        if (output[element] != sentinel)
            return Fail("failed local matrix conversion changed its output");
    return true;
}

}

int main()
{
    return TestRestPoseAndForwardEvaluation() && TestBlendAndAliasedOutput() && TestInvalidInputsPreserveOutput() && TestModelBoneMatrixHelpers() ? 0 : 1;
}
