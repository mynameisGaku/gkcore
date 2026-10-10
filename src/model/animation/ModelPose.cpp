// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelPose.h"

#include <float.h>
#include <math.h>

namespace gk::model::animation
{
namespace
{
#if defined(GKCORE_TESTING)
// このthreadの姿勢計算で実行した正規化数。
thread_local uint64_t quaternionNormalizationCountForTesting = 0;
// このthreadの全骨格姿勢評価数。
thread_local uint64_t modelPoseEvaluationCountForTesting = 0;
#endif

/**
 * 四成分quaternionを一時計算で扱う。
 */
struct FQuaternion
{
    // XYZW順のquaternion値。
    double value[4];
};

/**
 * quaternionをoverflowを避けて単位長へ正規化する。
 */
bool NormalizeQuaternion(FQuaternion& value);

/**
 * floatの回転4成分が有限かつ全zeroでないことを検査する。入力は変更しない。
 * 有限な非zero floatは安全に正規化できるため、検査だけにはsqrtや除算を使わない。
 */
bool HasValidRotation(const float rotation[4])
{
    bool nonzero = false;
    for (uint32_t component = 0; component < 4; ++component)
    {
        // 実際の正規化と同じdoubleへの変換で、極小値も判定する。
        const double value = rotation[component];
        if (!isfinite(value))
        {
            return false;
        }
        nonzero = nonzero || value != 0.0;
    }
    return nonzero;
}

/**
 * 骨格rest変換が有限値と有効親順を持つか調べる。
 */
bool ValidateSkeleton(const FModelSkeleton& skeleton, gk::String& error)
{
    if (skeleton.parents.Count() != skeleton.restLocalTransforms.Count() || skeleton.parents.Count() > INT32_MAX)
    {
        error.Assign("Model skeleton parent and rest-transform counts differ or exceed the index range");
        return false;
    }
    for (uint32_t bone = 0; bone < skeleton.parents.Count(); ++bone)
    {
        const int32_t parent = skeleton.parents.At(bone);
        if (parent < -1 || parent >= static_cast<int32_t>(bone))
        {
            error.Assign("Model skeleton parent must precede its child");
            return false;
        }
        const FModelBoneTransform& transform = skeleton.restLocalTransforms.At(bone);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(transform.position[axis]) || !isfinite(transform.scale[axis]))
            {
                error.Assign("Model skeleton rest transform is non-finite");
                return false;
            }
        }
        if (!HasValidRotation(transform.rotation))
        {
            error.Assign("Model skeleton rest rotation is zero or non-finite");
            return false;
        }
    }
    for (uint32_t morph = 0; morph < skeleton.restMorphWeights.Count(); ++morph)
    {
        if (!isfinite(skeleton.restMorphWeights.At(morph)))
        {
            error.Assign("Model skeleton rest morph weight is non-finite");
            return false;
        }
    }
    return true;
}

/**
 * quaternionをoverflowを避けて単位長へ正規化する。
 */
bool NormalizeQuaternion(FQuaternion& value)
{
#if defined(GKCORE_TESTING)
    ++quaternionNormalizationCountForTesting;
#endif
    double largest = 0.0;
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (!isfinite(value.value[component]))
            return false;
        const double magnitude = fabs(value.value[component]);
        if (magnitude > largest)
            largest = magnitude;
    }
    if (!(largest > 0.0))
        return false;
    double lengthSquared = 0.0;
    for (uint32_t component = 0; component < 4; ++component)
    {
        const double scaled = value.value[component] / largest;
        lengthSquared += scaled * scaled;
    }
    const double scaledLength = sqrt(lengthSquared);
    if (!(scaledLength > 0.0) || !isfinite(scaledLength))
        return false;
    for (uint32_t component = 0; component < 4; ++component)
        value.value[component] = (value.value[component] / largest) / scaledLength;
    return true;
}

/**
 * 姿勢の全配列数と有限な変換値を検証する。
 */
bool ValidatePose(const FModelSkeleton& skeleton, const FModelPose& pose, gk::String& error)
{
    if (!ValidateSkeleton(skeleton, error))
        return false;
    if (pose.localTransforms.Count() != skeleton.parents.Count() || pose.morphWeights.Count() != skeleton.restMorphWeights.Count())
    {
        error.Assign("Model pose arrays do not match the skeleton");
        return false;
    }
    for (uint32_t bone = 0; bone < pose.localTransforms.Count(); ++bone)
    {
        const FModelBoneTransform& transform = pose.localTransforms.At(bone);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!isfinite(transform.position[axis]) || !isfinite(transform.scale[axis]))
            {
                error.Assign("Model pose transform is non-finite");
                return false;
            }
        }
        if (!HasValidRotation(transform.rotation))
        {
            error.Assign("Model pose rotation is zero or non-finite");
            return false;
        }
    }
    for (uint32_t morph = 0; morph < pose.morphWeights.Count(); ++morph)
    {
        if (!isfinite(pose.morphWeights.At(morph)))
        {
            error.Assign("Model pose morph weight is non-finite");
            return false;
        }
    }
    return true;
}

/**
 * skeleton変換を正規化し、失敗時にcandidateを破棄できるposeへ複製する。
 */
bool CopyRestPose(const FModelSkeleton& skeleton, FModelPose& candidate, gk::String& error)
{
    if (!candidate.localTransforms.Reserve(skeleton.restLocalTransforms.Count()) || !candidate.morphWeights.Reserve(skeleton.restMorphWeights.Count()))
    {
        error.Assign("Model rest pose allocation failed");
        return false;
    }
    for (uint32_t bone = 0; bone < skeleton.restLocalTransforms.Count(); ++bone)
    {
        FModelBoneTransform transform = skeleton.restLocalTransforms.At(bone);
        FQuaternion rotation{};
        for (uint32_t component = 0; component < 4; ++component)
            rotation.value[component] = transform.rotation[component];
        NormalizeQuaternion(rotation);
        for (uint32_t component = 0; component < 4; ++component)
            transform.rotation[component] = static_cast<float>(rotation.value[component]);
        if (!candidate.localTransforms.Append(transform))
        {
            error.Assign("Model rest pose allocation failed");
            return false;
        }
    }
    for (uint32_t morph = 0; morph < skeleton.restMorphWeights.Count(); ++morph)
    {
        if (!candidate.morphWeights.Append(skeleton.restMorphWeights.At(morph)))
        {
            error.Assign("Model rest morph allocation failed");
            return false;
        }
    }
    return true;
}

/**
 * quaternionの積を計算する。
 */
FQuaternion MultiplyQuaternion(const FQuaternion& left, const FQuaternion& right)
{
    FQuaternion result{};
    const double x = left.value[0], y = left.value[1], z = left.value[2], w = left.value[3];
    const double a = right.value[0], b = right.value[1], c = right.value[2], d = right.value[3];
    result.value[0] = w * a + x * d + y * c - z * b;
    result.value[1] = w * b - x * c + y * d + z * a;
    result.value[2] = w * c + x * b - y * a + z * d;
    result.value[3] = w * d - x * a - y * b - z * c;
    return result;
}

/**
 * 親行列とlocal行列をcolumn-major順で乗算する。
 */
bool MultiplyMatrix(const float parent[16], const float local[16], float output[16])
{
    for (uint32_t column = 0; column < 4; ++column)
    {
        for (uint32_t row = 0; row < 4; ++row)
        {
            double value = 0.0;
            for (uint32_t inner = 0; inner < 4; ++inner)
                value += static_cast<double>(parent[inner * 4 + row]) * local[column * 4 + inner];
            if (!isfinite(value) || fabs(value) > FLT_MAX)
                return false;
            output[column * 4 + row] = static_cast<float>(value);
        }
    }
    return true;
}

/**
 * 姿勢変換からcolumn-major local 4x4行列を作る。
 */
bool BuildLocalMatrix(const FModelBoneTransform& transform, float output[16])
{
    FQuaternion rotation{};
    for (uint32_t component = 0; component < 4; ++component)
        rotation.value[component] = transform.rotation[component];
    if (!NormalizeQuaternion(rotation))
        return false;
    const double x = rotation.value[0], y = rotation.value[1], z = rotation.value[2], w = rotation.value[3];
    const double sx = transform.scale[0], sy = transform.scale[1], sz = transform.scale[2];
    const double values[16] = { (1.0 - 2.0 * (y * y + z * z)) * sx, (2.0 * (x * y + z * w)) * sx, (2.0 * (x * z - y * w)) * sx, 0.0, (2.0 * (x * y - z * w)) * sy, (1.0 - 2.0 * (x * x + z * z)) * sy, (2.0 * (y * z + x * w)) * sy, 0.0, (2.0 * (x * z + y * w)) * sz, (2.0 * (y * z - x * w)) * sz, (1.0 - 2.0 * (x * x + y * y)) * sz, 0.0, transform.position[0], transform.position[1], transform.position[2], 1.0 };
    for (uint32_t element = 0; element < 16; ++element)
    {
        if (!isfinite(values[element]) || fabs(values[element]) > FLT_MAX)
            return false;
        output[element] = static_cast<float>(values[element]);
    }
    return true;
}

}

#if defined(GKCORE_TESTING)
void ResetModelPoseWorkForTesting()
{
    quaternionNormalizationCountForTesting = 0;
    modelPoseEvaluationCountForTesting = 0;
}

uint64_t GetModelPoseEvaluationCountForTesting()
{
    return modelPoseEvaluationCountForTesting;
}

uint64_t GetModelPoseQuaternionNormalizationCountForTesting()
{
    return quaternionNormalizationCountForTesting;
}
#endif

/**
 * skeletonのrest姿勢とdefault morph係数からposeを作る。失敗時はoutputを保つ。
 */
bool InitializeModelPose(const FModelSkeleton& skeleton, FModelPose& output, gk::String& error)
{
    error.Clear();
    if (!ValidateSkeleton(skeleton, error))
        return false;
    FModelPose candidate;
    if (!CopyRestPose(skeleton, candidate, error))
        return false;
    output.localTransforms.MoveFrom(candidate.localTransforms);
    output.morphWeights.MoveFrom(candidate.morphWeights);
    error.Clear();
    return true;
}

/**
 * 位置・拡大率を線形補間し、quaternionを最短経路でslerpする。失敗時はoutputを保つ。
 */
bool BlendModelPoses(const FModelSkeleton& skeleton, const FModelPose& first, const FModelPose& second, float weight, FModelPose& output, gk::String& error)
{
    error.Clear();
    if (!isfinite(weight) || weight < 0.0f || weight > 1.0f || !ValidatePose(skeleton, first, error) || !ValidatePose(skeleton, second, error))
    {
        if (error.Empty())
            error.Assign("Model pose blend weight is invalid");
        return false;
    }
    FModelPose candidate;
    if (!candidate.localTransforms.Reserve(first.localTransforms.Count()) || !candidate.morphWeights.Reserve(first.morphWeights.Count()))
    {
        error.Assign("Model pose blend allocation failed");
        return false;
    }
    for (uint32_t bone = 0; bone < first.localTransforms.Count(); ++bone)
    {
        const FModelBoneTransform& a = first.localTransforms.At(bone);
        const FModelBoneTransform& b = second.localTransforms.At(bone);
        FModelBoneTransform result{};
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const double position = static_cast<double>(a.position[axis]) + (static_cast<double>(b.position[axis]) - a.position[axis]) * weight;
            const double scale = static_cast<double>(a.scale[axis]) + (static_cast<double>(b.scale[axis]) - a.scale[axis]) * weight;
            if (!isfinite(position) || !isfinite(scale) || fabs(position) > FLT_MAX || fabs(scale) > FLT_MAX)
            {
                error.Assign("Model pose blend exceeds the numeric range");
                return false;
            }
            result.position[axis] = static_cast<float>(position);
            result.scale[axis] = static_cast<float>(scale);
        }
        FQuaternion rotationA{}, rotationB{};
        for (uint32_t component = 0; component < 4; ++component)
        {
            rotationA.value[component] = a.rotation[component];
            rotationB.value[component] = b.rotation[component];
        }
        NormalizeQuaternion(rotationA);
        NormalizeQuaternion(rotationB);
        double dot = 0.0;
        for (uint32_t component = 0; component < 4; ++component)
            dot += rotationA.value[component] * rotationB.value[component];
        if (dot < 0.0)
        {
            dot = -dot;
            for (uint32_t component = 0; component < 4; ++component)
                rotationB.value[component] = -rotationB.value[component];
        }
        if (dot > 1.0)
            dot = 1.0;
        if (dot > 0.9995)
        {
            for (uint32_t component = 0; component < 4; ++component)
                result.rotation[component] = static_cast<float>(rotationA.value[component] + (rotationB.value[component] - rotationA.value[component]) * weight);
        }
        else
        {
            const double angle = acos(dot);
            const double denominator = sin(angle);
            const double factorA = sin((1.0 - weight) * angle) / denominator;
            const double factorB = sin(weight * angle) / denominator;
            for (uint32_t component = 0; component < 4; ++component)
                result.rotation[component] = static_cast<float>(rotationA.value[component] * factorA + rotationB.value[component] * factorB);
        }
        FQuaternion normalized{};
        for (uint32_t component = 0; component < 4; ++component)
            normalized.value[component] = result.rotation[component];
        if (!NormalizeQuaternion(normalized))
        {
            error.Assign("Model pose blend produced an invalid rotation");
            return false;
        }
        for (uint32_t component = 0; component < 4; ++component)
            result.rotation[component] = static_cast<float>(normalized.value[component]);
        if (!candidate.localTransforms.Append(result))
        {
            error.Assign("Model pose blend allocation failed");
            return false;
        }
    }
    for (uint32_t morph = 0; morph < first.morphWeights.Count(); ++morph)
    {
        const double value = static_cast<double>(first.morphWeights.At(morph)) + (static_cast<double>(second.morphWeights.At(morph)) - first.morphWeights.At(morph)) * weight;
        if (!isfinite(value) || fabs(value) > FLT_MAX || !candidate.morphWeights.Append(static_cast<float>(value)))
        {
            error.Assign("Model morph blend failed or exceeds the numeric range");
            return false;
        }
    }
    output.localTransforms.MoveFrom(candidate.localTransforms);
    output.morphWeights.MoveFrom(candidate.morphWeights);
    error.Clear();
    return true;
}

/**
 * 親基準poseをcolumn-major 4x4モデル空間行列へ展開する。失敗時はoutputを保つ。
 */
bool EvaluateModelPose(const FModelSkeleton& skeleton, const FModelPose& pose, gk::Array<float>& worldMatrices, gk::String& error)
{
#if defined(GKCORE_TESTING)
    ++modelPoseEvaluationCountForTesting;
#endif
    error.Clear();
    if (!ValidatePose(skeleton, pose, error))
        return false;
    if (pose.localTransforms.Count() > UINT32_MAX / 16u)
    {
        error.Assign("Model pose matrix count exceeds its limit");
        return false;
    }
    gk::Array<float> candidate;
    const uint32_t matrixCount = pose.localTransforms.Count() * 16u;
    if (!candidate.Reserve(matrixCount))
    {
        error.Assign("Model pose matrix allocation failed");
        return false;
    }
    for (uint32_t bone = 0; bone < pose.localTransforms.Count(); ++bone)
    {
        float local[16]{};
        float world[16]{};
        if (!BuildLocalMatrix(pose.localTransforms.At(bone), local))
        {
            error.Assign("Model pose local transform exceeds the numeric range");
            return false;
        }
        const int32_t parent = skeleton.parents.At(bone);
        if (parent < 0)
        {
            for (uint32_t element = 0; element < 16; ++element)
                world[element] = local[element];
        }
        else if (!MultiplyMatrix(candidate.Data() + static_cast<uint32_t>(parent) * 16u, local, world))
        {
            error.Assign("Model pose world transform exceeds the numeric range");
            return false;
        }
        if (!candidate.AppendRange(world, 16))
        {
            error.Assign("Model pose matrix allocation failed");
            return false;
        }
    }
    worldMatrices.MoveFrom(candidate);
    error.Clear();
    return true;
}

/**
 * 一つの骨変換をcolumn-major行列へ変換する。有限値でない入力ではoutputを保つ。
 */
bool BuildModelBoneMatrix(const FModelBoneTransform& transform, float output[16], gk::String& error)
{
    error.Clear();
    if (!output)
    {
        error.Assign("Model bone matrix output is null");
        return false;
    }
    float candidate[16]{};
    if (!BuildLocalMatrix(transform, candidate))
    {
        error.Assign("Model bone transform is invalid or exceeds the numeric range");
        return false;
    }
    for (uint32_t element = 0; element < 16; ++element)
        output[element] = candidate[element];
    return true;
}

/**
 * 親とlocalのcolumn-major行列を乗算する。範囲外結果ではoutputを保つ。
 */
bool MultiplyModelBoneMatrices(const float parent[16], const float local[16], float output[16], gk::String& error)
{
    error.Clear();
    if (!parent || !local || !output)
    {
        error.Assign("Model bone matrix input or output is null");
        return false;
    }
    float candidate[16]{};
    if (!MultiplyMatrix(parent, local, candidate))
    {
        error.Assign("Model bone matrix multiplication is invalid or exceeds the numeric range");
        return false;
    }
    for (uint32_t element = 0; element < 16; ++element)
        output[element] = candidate[element];
    return true;
}

}
