// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelPose.h"

#include <math.h>
#include <limits>
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
 * 指定rotationを持つ1本骨のskeletonを作る。
 */
bool MakeOneBoneSkeleton(FModelSkeleton& skeleton, const float rotation[4])
{
    const int32_t parent = -1;
    FModelBoneTransform transform{};
    for (uint32_t axis = 0; axis < 3; ++axis)
        transform.scale[axis] = 1.0f;
    for (uint32_t component = 0; component < 4; ++component)
        transform.rotation[component] = rotation[component];
    const float morph = 0.25f;
    return skeleton.parents.Append(parent) && skeleton.restLocalTransforms.Append(transform) && skeleton.restMorphWeights.Append(morph);
}

/**
 * 指定rotationを持つ1本骨のposeを作る。
 */
bool MakeOneBonePose(FModelPose& pose, const float rotation[4], float positionX = 0.0f)
{
    FModelBoneTransform transform{};
    transform.position[0] = positionX;
    for (uint32_t axis = 0; axis < 3; ++axis)
        transform.scale[axis] = 1.0f;
    for (uint32_t component = 0; component < 4; ++component)
        transform.rotation[component] = rotation[component];
    const float morph = 0.25f;
    return pose.localTransforms.Append(transform) && pose.morphWeights.Append(morph);
}

/**
 * 失敗時の既存poseを検査するための目印poseを作る。
 */
bool MakePoseSentinel(FModelPose& pose)
{
    const float rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    FModelBoneTransform transform{};
    transform.position[0] = 9.0f;
    transform.scale[0] = 2.0f;
    transform.scale[1] = 3.0f;
    transform.scale[2] = 4.0f;
    for (uint32_t component = 0; component < 4; ++component)
        transform.rotation[component] = rotation[component];
    const float morph = 7.0f;
    return pose.localTransforms.Append(transform) && pose.morphWeights.Append(morph);
}

/**
 * 失敗によって既存poseが置き換わっていないことを調べる。
 */
bool HasPoseSentinel(const FModelPose& pose)
{
    if (pose.localTransforms.Count() != 1 || pose.morphWeights.Count() != 1)
        return false;
    const FModelBoneTransform& transform = pose.localTransforms.At(0);
    return transform.position[0] == 9.0f && transform.scale[0] == 2.0f && transform.scale[1] == 3.0f && transform.scale[2] == 4.0f && transform.rotation[3] == 1.0f && pose.morphWeights.At(0) == 7.0f;
}

/**
 * float quaternionの境界値が初期化、評価、blendで同じ規則に従うことを確認する。
 */
bool TestQuaternionValidityBoundaries()
{
    const float maximum = std::numeric_limits<float>::max();
    const float minimumSubnormal = std::numeric_limits<float>::denorm_min();
    const float infinity = std::numeric_limits<float>::infinity();
    const float notANumber = std::numeric_limits<float>::quiet_NaN();
    const float positiveZero = 0.0f;
    const float negativeZero = -0.0f;
    struct FQuaternionCase
    {
        const char* name;
        float rotation[4];
        bool valid;
    };
    const FQuaternionCase cases[] = {
        { "four maximum components", { maximum, maximum, maximum, maximum }, true },
        { "minimum subnormal component", { minimumSubnormal, positiveZero, positiveZero, positiveZero }, true },
        { "mixed maximum and minimum subnormal", { maximum, minimumSubnormal, -maximum, negativeZero }, true },
        { "negative quaternion sign", { negativeZero, negativeZero, -1.0f, -1.0f }, true },
        { "positive zero quaternion", { positiveZero, positiveZero, positiveZero, positiveZero }, false },
        { "negative zero quaternion", { negativeZero, negativeZero, negativeZero, negativeZero }, false },
        { "mixed signed zero quaternion", { positiveZero, negativeZero, positiveZero, negativeZero }, false },
        { "NaN component", { notANumber, positiveZero, positiveZero, 1.0f }, false },
        { "positive infinity component", { infinity, positiveZero, positiveZero, 1.0f }, false },
        { "negative infinity component", { -infinity, positiveZero, positiveZero, 1.0f }, false }
    };
    const float identityRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    for (uint32_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        const FQuaternionCase& testCase = cases[index];
        FModelSkeleton skeleton;
        FModelPose initializedPose;
        FModelPose sourcePose;
        FModelPose identityPose;
        FModelPose blendedPose;
        gk::String error;
        if (!MakeOneBoneSkeleton(skeleton, testCase.rotation) || !MakeOneBonePose(sourcePose, testCase.rotation) || !MakeOneBonePose(identityPose, identityRotation))
            return Fail("quaternion boundary fixture allocation failed");

        const bool initialized = InitializeModelPose(skeleton, initializedPose, error);
        if (initialized != testCase.valid)
            return Fail("quaternion boundary initialization acceptance changed");
        if (testCase.valid)
        {
            gk::Array<float> matrices;
            if (!EvaluateModelPose(skeleton, initializedPose, matrices, error) || matrices.Count() != 16)
                return Fail("valid quaternion boundary did not produce a bone matrix");
            if (!BlendModelPoses(skeleton, sourcePose, identityPose, 0.0f, blendedPose, error))
                return Fail("valid quaternion boundary was rejected by pose blending");
            if (testCase.rotation[2] == -1.0f && testCase.rotation[3] == -1.0f && (fabsf(matrices.At(0)) > 0.0001f || fabsf(matrices.At(1) - 1.0f) > 0.0001f || fabsf(matrices.At(4) + 1.0f) > 0.0001f || fabsf(matrices.At(5)) > 0.0001f))
                return Fail("negative quaternion sign did not preserve its expected rotation");
            continue;
        }

        FModelPose sentinelPose;
        if (!MakePoseSentinel(sentinelPose))
            return Fail("quaternion boundary sentinel allocation failed");
        FModelPose initializeOutput;
        if (!MakePoseSentinel(initializeOutput))
            return Fail("quaternion initialization output allocation failed");
        if (InitializeModelPose(skeleton, initializeOutput, error) || !HasPoseSentinel(initializeOutput) || error.Empty())
            return Fail("invalid skeleton quaternion changed initialization output or omitted its diagnostic");

        gk::Array<float> skeletonMatrices;
        const float matrixSentinel = 123.0f;
        if (!skeletonMatrices.Append(matrixSentinel))
            return Fail("skeleton matrix sentinel allocation failed");
        error.Clear();
        if (EvaluateModelPose(skeleton, identityPose, skeletonMatrices, error) || skeletonMatrices.Count() != 1 || skeletonMatrices.At(0) != matrixSentinel || error.Empty())
            return Fail("invalid skeleton quaternion changed evaluation output or omitted its diagnostic");

        FModelSkeleton validSkeleton;
        if (!MakeOneBoneSkeleton(validSkeleton, identityRotation))
            return Fail("valid quaternion skeleton allocation failed");
        FModelPose invalidPose;
        if (!MakeOneBonePose(invalidPose, testCase.rotation))
            return Fail("invalid quaternion pose allocation failed");
        gk::Array<float> poseMatrices;
        if (!poseMatrices.Append(matrixSentinel))
            return Fail("pose matrix sentinel allocation failed");
        error.Clear();
        if (EvaluateModelPose(validSkeleton, invalidPose, poseMatrices, error) || poseMatrices.Count() != 1 || poseMatrices.At(0) != matrixSentinel || error.Empty())
            return Fail("invalid pose quaternion changed evaluation output or omitted its diagnostic");

        for (uint32_t invalidSlot = 0; invalidSlot < 2; ++invalidSlot)
        {
            FModelPose first;
            FModelPose second;
            FModelPose blendOutput;
            if (!MakeOneBonePose(first, invalidSlot == 0 ? testCase.rotation : identityRotation) || !MakeOneBonePose(second, invalidSlot == 1 ? testCase.rotation : identityRotation) || !MakePoseSentinel(blendOutput))
                return Fail("quaternion blend fixture allocation failed");
            error.Clear();
            if (BlendModelPoses(validSkeleton, first, second, 0.5f, blendOutput, error) || !HasPoseSentinel(blendOutput) || error.Empty())
                return Fail("invalid blend quaternion changed output or omitted its diagnostic");
        }
    }
    return true;
}

/**
 * 1本骨のFKが検査用の正規化を避け、行列生成時だけ正規化することを確認する。
 */
bool TestForwardKinematicsNormalizesOnlyForMatrixBuild()
{
    const float identityRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    FModelSkeleton skeleton;
    FModelPose pose;
    gk::Array<float> matrices;
    gk::String error;
    if (!MakeOneBoneSkeleton(skeleton, identityRotation) || !MakeOneBonePose(pose, identityRotation))
        return Fail("normalization budget fixture allocation failed");
    ResetModelPoseWorkForTesting();
    if (!EvaluateModelPose(skeleton, pose, matrices, error) || matrices.Count() != 16)
        return Fail(error.CStr());
    const uint64_t normalizationCount = GetModelPoseQuaternionNormalizationCountForTesting();
    if (normalizationCount != 1)
    {
        char message[128]{};
        snprintf(message, sizeof(message), "one-bone forward evaluation normalized quaternion %llu times instead of once", static_cast<unsigned long long>(normalizationCount));
        return Fail(message);
    }
    return true;
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
    return TestRestPoseAndForwardEvaluation() && TestBlendAndAliasedOutput() && TestInvalidInputsPreserveOutput() && TestModelBoneMatrixHelpers() && TestQuaternionValidityBoundaries() && TestForwardKinematicsNormalizesOnlyForMatrixBuild() ? 0 : 1;
}
