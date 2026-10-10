// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelIk.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

namespace gk::model::animation
{
namespace
{
#if defined(GKCORE_TESTING)
// このthreadのIKで実際に積算したworld回転数。
thread_local uint64_t worldRotationCountForTesting = 0;
#endif


/**
 * IK計算に使うmodel空間の倍精度位置。
 */
struct FVector3
{
    // XYZ位置または方向。
    double value[3];
};

/**
 * 回転計算に使うXYZW順の倍精度quaternion。
 */
struct FQuaternion
{
    // XYZW回転値。
    double value[4];
};

/**
 * ベクトルの差を計算する。
 */
FVector3 Subtract(const FVector3& left, const FVector3& right)
{
    FVector3 result{};
    for (uint32_t axis = 0; axis < 3; ++axis)
        result.value[axis] = left.value[axis] - right.value[axis];
    return result;
}

/**
 * ベクトルの和を計算する。
 */
FVector3 Add(const FVector3& left, const FVector3& right)
{
    FVector3 result{};
    for (uint32_t axis = 0; axis < 3; ++axis)
        result.value[axis] = left.value[axis] + right.value[axis];
    return result;
}

/**
 * ベクトルを有限な倍精度係数で拡大する。
 */
FVector3 Multiply(const FVector3& value, double scale)
{
    FVector3 result{};
    for (uint32_t axis = 0; axis < 3; ++axis)
        result.value[axis] = value.value[axis] * scale;
    return result;
}

/**
 * 3次元内積を計算する。
 */
double Dot(const FVector3& left, const FVector3& right)
{
    return left.value[0] * right.value[0] + left.value[1] * right.value[1] + left.value[2] * right.value[2];
}

/**
 * 3次元外積を計算する。
 */
FVector3 Cross(const FVector3& left, const FVector3& right)
{
    FVector3 result{};
    result.value[0] = left.value[1] * right.value[2] - left.value[2] * right.value[1];
    result.value[1] = left.value[2] * right.value[0] - left.value[0] * right.value[2];
    result.value[2] = left.value[0] * right.value[1] - left.value[1] * right.value[0];
    return result;
}

/**
 * 値の大きさを拡大縮小してoverflowを避けた長さを返す。
 */
double Length(const FVector3& value)
{
    const double largest = fmax(fabs(value.value[0]), fmax(fabs(value.value[1]), fabs(value.value[2])));
    if (!(largest > 0.0) || !isfinite(largest))
        return largest;
    const double x = value.value[0] / largest;
    const double y = value.value[1] / largest;
    const double z = value.value[2] / largest;
    return largest * sqrt(x * x + y * y + z * z);
}

/**
 * 倍精度ベクトルを安全に正規化する。
 */
bool Normalize(FVector3& value)
{
    const double length = Length(value);
    if (!(length > 0.0) || !isfinite(length))
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
        value.value[axis] /= length;
    return true;
}

/**
 * 配列の有限な3成分を倍精度位置へ読み取る。
 */
bool ReadVector(const float* source, FVector3& output)
{
    if (!source)
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!isfinite(source[axis]))
            return false;
        output.value[axis] = source[axis];
    }
    return true;
}

/**
 * quaternionをoverflowを避けて単位長へ正規化する。
 */
bool NormalizeQuaternion(FQuaternion& value)
{
    double largest = 0.0;
    for (uint32_t component = 0; component < 4; ++component)
    {
        if (!isfinite(value.value[component]))
            return false;
        largest = fmax(largest, fabs(value.value[component]));
    }
    if (!(largest > 0.0))
        return false;
    double sum = 0.0;
    for (uint32_t component = 0; component < 4; ++component)
    {
        const double scaled = value.value[component] / largest;
        sum += scaled * scaled;
    }
    const double length = sqrt(sum);
    if (!(length > 0.0) || !isfinite(length))
        return false;
    for (uint32_t component = 0; component < 4; ++component)
        value.value[component] = (value.value[component] / largest) / length;
    return true;
}

/**
 * quaternionの積を返す。
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
 * quaternion共役を返す。
 */
FQuaternion Conjugate(const FQuaternion& value)
{
    FQuaternion result = value;
    result.value[0] = -result.value[0];
    result.value[1] = -result.value[1];
    result.value[2] = -result.value[2];
    return result;
}

/**
 * 姿勢の位置・回転・scaleとmorph値を一時copyする。
 */
bool CopyPose(const FModelPose& source, FModelPose& output, gk::String& error)
{
    if (!output.localTransforms.Reserve(source.localTransforms.Count()) || !output.morphWeights.Reserve(source.morphWeights.Count()) || !output.localTransforms.AppendRange(source.localTransforms.Data(), source.localTransforms.Count()) || !output.morphWeights.AppendRange(source.morphWeights.Data(), source.morphWeights.Count()))
    {
        error.Assign("Model IK pose allocation failed");
        return false;
    }
    return true;
}

/**
 * IK chain、目標、反復条件と現在model空間位置を検証する。
 */
bool PrepareIk(const FModelSkeleton& skeleton, const FModelPose& source, const uint32_t* chain, uint32_t chainCount, const float target[3], float weight, float tolerance, uint32_t maxIterations, gk::Array<FVector3>& positions, gk::String& error)
{
    error.Clear();
    FVector3 targetPoint{};
    if (!chain || chainCount < 2 || chainCount > skeleton.parents.Count() || !ReadVector(target, targetPoint) || !isfinite(weight) || weight < 0.0f || weight > 1.0f || !isfinite(tolerance) || !(tolerance > 0.0f) || maxIterations == 0 || maxIterations > 256)
    {
        error.Assign("Model IK chain, target, weight, or iteration settings are invalid");
        return false;
    }
    gk::Array<float> matrices;
    if (!EvaluateModelPose(skeleton, source, matrices, error))
        return false;
    if (!positions.Reserve(chainCount))
    {
        error.Assign("Model IK position allocation failed");
        return false;
    }
    for (uint32_t item = 0; item < chainCount; ++item)
    {
        const uint32_t bone = chain[item];
        if (bone >= skeleton.parents.Count() || (item && skeleton.parents.At(bone) != static_cast<int32_t>(chain[item - 1])))
        {
            error.Assign("Model IK bones are not a continuous parent-child chain");
            return false;
        }
        // IKは正の一様scaleに限定し、float精度に近い軸差だけを許容する。
        for (int32_t ancestor = static_cast<int32_t>(bone); ancestor >= 0; ancestor = skeleton.parents.At(static_cast<uint32_t>(ancestor)))
        {
            const FModelBoneTransform& transform = source.localTransforms.At(static_cast<uint32_t>(ancestor));
            const float largest = fmaxf(transform.scale[0], fmaxf(transform.scale[1], transform.scale[2]));
            const float smallest = fminf(transform.scale[0], fminf(transform.scale[1], transform.scale[2]));
            // 変換時の小さな軸差に対する相対誤差の上限。
            constexpr float scaleTolerance = 16.0f * FLT_EPSILON;
            if (!(smallest > 0.0f) || largest - smallest > largest * scaleTolerance)
            {
                error.Assign("Model IK does not support zero, negative, or non-uniform ancestor scale");
                return false;
            }
        }
        FVector3 position{};
        const uint32_t matrixOffset = bone * 16u;
        for (uint32_t axis = 0; axis < 3; ++axis)
            position.value[axis] = matrices.At(matrixOffset + 12u + axis);
        if (!positions.Append(position))
        {
            error.Assign("Model IK position allocation failed");
            return false;
        }
    }
    for (uint32_t item = 0; item + 1 < chainCount; ++item)
    {
        if (!(Length(Subtract(positions.At(item + 1), positions.At(item))) > 0.0))
        {
            error.Assign("Model IK chain contains a zero-length bone");
            return false;
        }
    }
    return true;
}

/**
 * 対象ボーンの親からrootまでの回転だけを、従来と同じ親順で積算する。
 * 呼び出し前の全骨格検査は省かない。失敗時はoutputを保つ。
 */
bool EvaluateParentWorldRotation(const FModelSkeleton& skeleton, const FModelPose& pose, uint32_t bone, FQuaternion& output, gk::String& error)
{
    if (bone >= skeleton.parents.Count() || pose.localTransforms.Count() != skeleton.parents.Count())
    {
        error.Assign("Model IK parent rotation input is invalid");
        return false;
    }
    // 祖先の番号だけを一時保存し、子孫や他の枝の回転配列を作らない。
    gk::Array<uint32_t> ancestors;
    int32_t parent = skeleton.parents.At(bone);
    while (parent >= 0)
    {
        if (static_cast<uint32_t>(parent) >= skeleton.parents.Count() || ancestors.Count() >= skeleton.parents.Count())
        {
            error.Assign("Model IK parent rotation hierarchy is invalid");
            return false;
        }
        if (!ancestors.Append(static_cast<uint32_t>(parent)))
        {
            error.Assign("Model IK ancestor allocation failed");
            return false;
        }
        parent = skeleton.parents.At(static_cast<uint32_t>(parent));
    }
    FQuaternion parentWorld{ { 0.0, 0.0, 0.0, 1.0 } };
    for (uint32_t remaining = ancestors.Count(); remaining > 0; --remaining)
    {
        const uint32_t ancestor = ancestors.At(remaining - 1);
#if defined(GKCORE_TESTING)
        ++worldRotationCountForTesting;
#endif
        FQuaternion local{};
        for (uint32_t component = 0; component < 4; ++component)
        {
            local.value[component] = pose.localTransforms.At(ancestor).rotation[component];
        }
        if (!NormalizeQuaternion(local))
        {
            error.Assign("Model IK ancestor rotation is invalid");
            return false;
        }
        // rootも2回正規化し、既存の積算順とquaternionの符号を維持する。
        FQuaternion world = skeleton.parents.At(ancestor) < 0 ? local : MultiplyQuaternion(parentWorld, local);
        if (!NormalizeQuaternion(world))
        {
            error.Assign("Model IK parent world rotation is invalid");
            return false;
        }
        parentWorld = world;
    }
    output = parentWorld;
    return true;
}

/**
 * 任意軸を使って二方向を結ぶworld-space回転を作る。
 */
bool RotationBetween(FVector3 from, FVector3 to, FVector3 fallbackAxis, FQuaternion& output)
{
    if (!Normalize(from) || !Normalize(to))
        return false;
    double dot = Dot(from, to);
    if (dot > 1.0)
        dot = 1.0;
    if (dot < -1.0)
        dot = -1.0;
    if (dot < -0.999999999999)
    {
        FVector3 axis = Cross(from, fallbackAxis);
        if (!Normalize(axis))
        {
            const FVector3 candidate = fabs(from.value[0]) < fabs(from.value[1]) ? FVector3{ { 1.0, 0.0, 0.0 } } : FVector3{ { 0.0, 1.0, 0.0 } };
            axis = Cross(from, candidate);
            if (!Normalize(axis))
                return false;
        }
        output.value[0] = axis.value[0];
        output.value[1] = axis.value[1];
        output.value[2] = axis.value[2];
        output.value[3] = 0.0;
        return true;
    }
    const FVector3 cross = Cross(from, to);
    output.value[0] = cross.value[0];
    output.value[1] = cross.value[1];
    output.value[2] = cross.value[2];
    output.value[3] = 1.0 + dot;
    return NormalizeQuaternion(output);
}

/**
 * world-spaceの回転をboneの親基準rotationへ適用する。
 */
bool ApplyWorldDelta(const FModelSkeleton& skeleton, FModelPose& pose, uint32_t bone, const FVector3& currentDirection, const FVector3& desiredDirection, const FVector3& fallbackAxis, gk::String& error)
{
    FQuaternion delta{};
    if (!RotationBetween(currentDirection, desiredDirection, fallbackAxis, delta))
    {
        error.Assign("Model IK cannot orient a zero-length bone");
        return false;
    }
    FQuaternion parentWorld{};
    if (!EvaluateParentWorldRotation(skeleton, pose, bone, parentWorld, error))
    {
        return false;
    }
    FQuaternion local{};
    for (uint32_t component = 0; component < 4; ++component)
        local.value[component] = pose.localTransforms.At(bone).rotation[component];
    NormalizeQuaternion(local);
    FQuaternion updated = MultiplyQuaternion(Conjugate(parentWorld), MultiplyQuaternion(delta, MultiplyQuaternion(parentWorld, local)));
    if (!NormalizeQuaternion(updated))
    {
        error.Assign("Model IK produced an invalid bone rotation");
        return false;
    }
    for (uint32_t component = 0; component < 4; ++component)
        pose.localTransforms.At(bone).rotation[component] = static_cast<float>(updated.value[component]);
    return true;
}

/**
 * 指定poseのchain位置を新しい配列へ評価する。
 */
bool EvaluateChainPositions(const FModelSkeleton& skeleton, const FModelPose& pose, const uint32_t* chain, uint32_t chainCount, gk::Array<FVector3>& positions, gk::String& error)
{
    gk::Array<float> matrices;
    if (!EvaluateModelPose(skeleton, pose, matrices, error) || !positions.Reserve(chainCount))
    {
        if (error.Empty())
            error.Assign("Model IK position allocation failed");
        return false;
    }
    for (uint32_t item = 0; item < chainCount; ++item)
    {
        FVector3 position{};
        const uint32_t offset = chain[item] * 16u + 12u;
        for (uint32_t axis = 0; axis < 3; ++axis)
            position.value[axis] = matrices.At(offset + axis);
        if (!positions.Append(position))
        {
            error.Assign("Model IK position allocation failed");
            return false;
        }
    }
    return true;
}

/**
 * 各chain linkを指定方向へ回し、姿勢を順番に更新する。
 */
bool ApplyDesiredChain(const FModelSkeleton& skeleton, FModelPose& pose, const uint32_t* chain, const gk::Array<FVector3>& desired, uint32_t chainCount, gk::String& error)
{
    for (uint32_t item = 0; item + 1 < chainCount; ++item)
    {
        gk::Array<FVector3> current;
        if (!EvaluateChainPositions(skeleton, pose, chain, chainCount, current, error))
            return false;
        const FVector3 currentDirection = Subtract(current.At(item + 1), current.At(item));
        const FVector3 desiredDirection = Subtract(desired.At(item + 1), desired.At(item));
        const FVector3 fallback = fabs(currentDirection.value[0]) < fabs(currentDirection.value[1]) ? FVector3{ { 1.0, 0.0, 0.0 } } : FVector3{ { 0.0, 1.0, 0.0 } };
        if (!ApplyWorldDelta(skeleton, pose, chain[item], currentDirection, desiredDirection, fallback, error))
            return false;
    }
    return true;
}

/**
 * source poseと解いたposeをweightで補間してoutputへ確定する。
 */
bool BlendIkResult(const FModelSkeleton& skeleton, const FModelPose& source, const FModelPose& solved, float weight, FModelPose& output, gk::String& error)
{
    return BlendModelPoses(skeleton, source, solved, weight, output, error);
}

/**
 * two-bone chainが連続で、scaleがIK対応範囲かを確認する。
 */
bool ValidateTwoBoneChain(const FModelSkeleton& skeleton, uint32_t root, uint32_t middle, uint32_t end, gk::String& error)
{
    if (root >= skeleton.parents.Count() || middle >= skeleton.parents.Count() || end >= skeleton.parents.Count() || skeleton.parents.At(middle) != static_cast<int32_t>(root) || skeleton.parents.At(end) != static_cast<int32_t>(middle))
    {
        error.Assign("Two-bone IK requires a continuous root-middle-end chain");
        return false;
    }
    return true;
}

}

#if defined(GKCORE_TESTING)
void ResetModelIkWorkForTesting()
{
    worldRotationCountForTesting = 0;
}

uint64_t GetModelIkWorldRotationCountForTesting()
{
    return worldRotationCountForTesting;
}
#endif

/**
 * 連続したroot、middle、endボーンをpoleで曲げる2ボーンIKを解く。失敗時はoutputを保つ。
 */
bool SolveTwoBoneIk(const FModelSkeleton& skeleton, const FModelPose& source, uint32_t rootBone, uint32_t middleBone, uint32_t endBone, const float target[3], const float pole[3], float weight, FModelPose& output, gk::String& error)
{
    error.Clear();
    FVector3 targetPoint{};
    FVector3 polePoint{};
    if (!ReadVector(target, targetPoint) || !ReadVector(pole, polePoint) || !isfinite(weight) || weight < 0.0f || weight > 1.0f || !ValidateTwoBoneChain(skeleton, rootBone, middleBone, endBone, error))
    {
        if (error.Empty())
            error.Assign("Two-bone IK target, pole, or weight is invalid");
        return false;
    }
    const uint32_t chain[3] = { rootBone, middleBone, endBone };
    gk::Array<FVector3> positions;
    if (!PrepareIk(skeleton, source, chain, 3, target, weight, 0.001f, 1, positions, error))
        return false;
    const FVector3 root = positions.At(0);
    const FVector3 middle = positions.At(1);
    const FVector3 end = positions.At(2);
    const double firstLength = Length(Subtract(middle, root));
    const double secondLength = Length(Subtract(end, middle));
    FVector3 targetDirection = Subtract(targetPoint, root);
    double requestedDistance = Length(targetDirection);
    if (!(requestedDistance > 0.0))
    {
        targetDirection = Subtract(end, root);
        if (!Normalize(targetDirection))
            targetDirection = Subtract(middle, root);
        if (!Normalize(targetDirection))
        {
            error.Assign("Two-bone IK has no stable target direction");
            return false;
        }
    }
    else
    {
        Normalize(targetDirection);
    }
    const double minimumReach = fabs(firstLength - secondLength);
    const double maximumReach = firstLength + secondLength;
    const double solvedDistance = fmin(maximumReach, fmax(minimumReach, requestedDistance));
    double along = 0.0;
    double height = firstLength;
    if (solvedDistance > 0.0)
    {
        along = (firstLength * firstLength - secondLength * secondLength + solvedDistance * solvedDistance) / (2.0 * solvedDistance);
        height = sqrt(fmax(0.0, firstLength * firstLength - along * along));
    }
    FVector3 poleDirection = Subtract(polePoint, root);
    const double poleLength = Length(poleDirection);
    const double poleProjection = Dot(poleDirection, targetDirection);
    poleDirection = Subtract(poleDirection, Multiply(targetDirection, poleProjection));
    const double projectedPoleLength = Length(poleDirection);
    if (!(projectedPoleLength > poleLength * DBL_EPSILON * 64.0) || !Normalize(poleDirection))
    {
        poleDirection = Subtract(middle, root);
        const double middleLength = Length(poleDirection);
        poleDirection = Subtract(poleDirection, Multiply(targetDirection, Dot(poleDirection, targetDirection)));
        const double projectedMiddleLength = Length(poleDirection);
        if (!(projectedMiddleLength > middleLength * DBL_EPSILON * 64.0) || !Normalize(poleDirection))
        {
            FVector3 basis{};
            const double x = fabs(targetDirection.value[0]);
            const double y = fabs(targetDirection.value[1]);
            const double z = fabs(targetDirection.value[2]);
            if (x <= y && x <= z)
                basis.value[0] = 1.0;
            else if (y <= z)
                basis.value[1] = 1.0;
            else
                basis.value[2] = 1.0;
            poleDirection = Cross(targetDirection, basis);
            if (!Normalize(poleDirection))
            {
                error.Assign("Two-bone IK could not choose a stable bend direction");
                return false;
            }
        }
    }
    FVector3 desiredMiddle = Add(root, Add(Multiply(targetDirection, along), Multiply(poleDirection, height)));
    FVector3 desiredEnd = Add(root, Multiply(targetDirection, solvedDistance));
    FModelPose solved;
    if (!CopyPose(source, solved, error))
        return false;
    if (weight == 0.0f)
        return BlendIkResult(skeleton, source, source, 0.0f, output, error);
    FVector3 currentFirst = Subtract(middle, root);
    FVector3 desiredFirst = Subtract(desiredMiddle, root);
    if (!ApplyWorldDelta(skeleton, solved, rootBone, currentFirst, desiredFirst, poleDirection, error))
        return false;
    gk::Array<FVector3> updatedPositions;
    if (!EvaluateChainPositions(skeleton, solved, chain, 3, updatedPositions, error))
        return false;
    const FVector3 desiredSecond = Subtract(desiredEnd, updatedPositions.At(1));
    const FVector3 currentSecond = Subtract(updatedPositions.At(2), updatedPositions.At(1));
    if (!ApplyWorldDelta(skeleton, solved, middleBone, currentSecond, desiredSecond, poleDirection, error))
        return false;
    return BlendIkResult(skeleton, source, solved, weight, output, error);
}

/**
 * rootからendまでの連続chainをFABRIKで解く。失敗時はoutputを保つ。
 */
bool SolveFabrikIk(const FModelSkeleton& skeleton, const FModelPose& source, const uint32_t* chain, uint32_t chainCount, const float target[3], float weight, float tolerance, uint32_t maxIterations, FModelPose& output, gk::String& error)
{
    error.Clear();
    gk::Array<FVector3> original;
    if (!PrepareIk(skeleton, source, chain, chainCount, target, weight, tolerance, maxIterations, original, error))
        return false;
    FVector3 targetPoint{};
    ReadVector(target, targetPoint);
    FModelPose solved;
    if (!CopyPose(source, solved, error))
        return false;
    if (weight == 0.0f)
        return BlendIkResult(skeleton, source, source, 0.0f, output, error);
    gk::Array<FVector3> desired;
    if (!desired.Reserve(chainCount))
    {
        error.Assign("FABRIK workspace allocation failed");
        return false;
    }
    double totalLength = 0.0;
    for (uint32_t item = 0; item + 1 < chainCount; ++item)
        totalLength += Length(Subtract(original.At(item + 1), original.At(item)));
    const FVector3 root = original.At(0);
    FVector3 rootToTarget = Subtract(targetPoint, root);
    const double targetDistance = Length(rootToTarget);
    if (targetDistance > totalLength)
    {
        if (!Normalize(rootToTarget))
        {
            error.Assign("FABRIK cannot normalize an unreachable target direction");
            return false;
        }
        if (!desired.Append(root))
        {
            error.Assign("FABRIK workspace allocation failed");
            return false;
        }
        double distance = 0.0;
        for (uint32_t item = 1; item < chainCount; ++item)
        {
            distance += Length(Subtract(original.At(item), original.At(item - 1)));
            if (!desired.Append(Add(root, Multiply(rootToTarget, distance))))
            {
                error.Assign("FABRIK workspace allocation failed");
                return false;
            }
        }
    }
    else
    {
        if (!desired.AppendRange(original.Data(), chainCount))
        {
            error.Assign("FABRIK workspace allocation failed");
            return false;
        }
        bool converged = false;
        for (uint32_t iteration = 0; iteration < maxIterations; ++iteration)
        {
            desired.At(chainCount - 1) = targetPoint;
            for (uint32_t item = chainCount - 1; item > 0; --item)
            {
                FVector3 direction = Subtract(desired.At(item - 1), desired.At(item));
                if (!Normalize(direction))
                {
                    direction = Subtract(original.At(item - 1), original.At(item));
                    Normalize(direction);
                }
                desired.At(item - 1) = Add(desired.At(item), Multiply(direction, Length(Subtract(original.At(item), original.At(item - 1)))));
            }
            desired.At(0) = root;
            for (uint32_t item = 1; item < chainCount; ++item)
            {
                FVector3 direction = Subtract(desired.At(item), desired.At(item - 1));
                if (!Normalize(direction))
                {
                    direction = Subtract(original.At(item), original.At(item - 1));
                    Normalize(direction);
                }
                desired.At(item) = Add(desired.At(item - 1), Multiply(direction, Length(Subtract(original.At(item), original.At(item - 1)))));
            }
            if (Length(Subtract(desired.At(chainCount - 1), targetPoint)) <= tolerance)
            {
                converged = true;
                break;
            }
        }
        if (!converged)
        {
            error.Assign("FABRIK did not converge within its iteration limit");
            return false;
        }
    }
    if (!ApplyDesiredChain(skeleton, solved, chain, desired, chainCount, error))
        return false;
    return BlendIkResult(skeleton, source, solved, weight, output, error);
}

/**
 * rootからendまでの連続chainをCCDで解く。失敗時はoutputを保つ。
 */
bool SolveCcdIk(const FModelSkeleton& skeleton, const FModelPose& source, const uint32_t* chain, uint32_t chainCount, const float target[3], float weight, float tolerance, uint32_t maxIterations, FModelPose& output, gk::String& error)
{
    error.Clear();
    gk::Array<FVector3> original;
    if (!PrepareIk(skeleton, source, chain, chainCount, target, weight, tolerance, maxIterations, original, error))
        return false;
    FVector3 targetPoint{};
    ReadVector(target, targetPoint);
    double totalLength = 0.0;
    for (uint32_t item = 0; item + 1 < chainCount; ++item)
        totalLength += Length(Subtract(original.At(item + 1), original.At(item)));
    FVector3 rootToTarget = Subtract(targetPoint, original.At(0));
    const double requestedDistance = Length(rootToTarget);
    if (requestedDistance > totalLength)
    {
        Normalize(rootToTarget);
        targetPoint = Add(original.At(0), Multiply(rootToTarget, totalLength));
    }
    FModelPose solved;
    if (!CopyPose(source, solved, error))
        return false;
    if (weight == 0.0f)
        return BlendIkResult(skeleton, source, source, 0.0f, output, error);
    bool converged = false;
    for (uint32_t iteration = 0; iteration < maxIterations; ++iteration)
    {
        gk::Array<FVector3> current;
        if (!EvaluateChainPositions(skeleton, solved, chain, chainCount, current, error))
            return false;
        if (Length(Subtract(current.At(chainCount - 1), targetPoint)) <= tolerance)
        {
            converged = true;
            break;
        }
        for (uint32_t reverse = chainCount - 1; reverse > 0; --reverse)
        {
            const uint32_t item = reverse - 1;
            if (!EvaluateChainPositions(skeleton, solved, chain, chainCount, current, error))
                return false;
            const FVector3 currentDirection = Subtract(current.At(chainCount - 1), current.At(item));
            const FVector3 desiredDirection = Subtract(targetPoint, current.At(item));
            const FVector3 fallback = fabs(currentDirection.value[0]) < fabs(currentDirection.value[1]) ? FVector3{ { 1.0, 0.0, 0.0 } } : FVector3{ { 0.0, 1.0, 0.0 } };
            if (!ApplyWorldDelta(skeleton, solved, chain[item], currentDirection, desiredDirection, fallback, error))
                return false;
        }
    }
    if (!converged)
    {
        gk::Array<FVector3> current;
        if (!EvaluateChainPositions(skeleton, solved, chain, chainCount, current, error))
            return false;
        converged = Length(Subtract(current.At(chainCount - 1), targetPoint)) <= tolerance;
    }
    if (!converged)
    {
        error.Assign("CCD did not converge within its iteration limit");
        return false;
    }
    return BlendIkResult(skeleton, source, solved, weight, output, error);
}

bool OrientModelPoseChain(const FModelSkeleton& skeleton, const FModelPose& source, const uint32_t* bones, uint32_t count, gk::Vec3 endOffset, const gk::Vec3* points, FModelPose& output, gk::String& error)
{
    if (!bones || !points || count == 0 || count > skeleton.parents.Count() || !isfinite(endOffset.x) || !isfinite(endOffset.y) || !isfinite(endOffset.z))
    {
        error.Assign("secondary chain orientation input is invalid");
        return false;
    }
    gk::Array<float> matrices;
    if (!EvaluateModelPose(skeleton, source, matrices, error))
    {
        return false;
    }
    for (uint32_t item = 0; item < count; ++item)
    {
        const uint32_t bone = bones[item];
        if (bone >= skeleton.parents.Count() || (item > 0 && skeleton.parents.At(bone) != static_cast<int32_t>(bones[item - 1])))
        {
            error.Assign("secondary bones must form a continuous parent-child chain");
            return false;
        }
        for (int32_t ancestor = static_cast<int32_t>(bone); ancestor >= 0; ancestor = skeleton.parents.At(static_cast<uint32_t>(ancestor)))
        {
            const auto& transform = source.localTransforms.At(static_cast<uint32_t>(ancestor));
            const float largest = fmaxf(transform.scale[0], fmaxf(transform.scale[1], transform.scale[2]));
            const float smallest = fminf(transform.scale[0], fminf(transform.scale[1], transform.scale[2]));
            // 小さな入力軸差だけを許し、非一様scaleによる変形誤差を抑える。
            if (!(smallest > 0.0f) || largest - smallest > largest * (128.0f * FLT_EPSILON))
            {
                char diagnosis[192]{};
                snprintf(diagnosis, sizeof(diagnosis), "secondary motion requires positive near-uniform ancestor scale: bone=%d scale=(%.9g,%.9g,%.9g)", ancestor, transform.scale[0], transform.scale[1], transform.scale[2]);
                error.Assign(diagnosis);
                return false;
            }
        }
    }
    for (uint32_t item = 0; item <= count; ++item)
    {
        if (!isfinite(points[item].x) || !isfinite(points[item].y) || !isfinite(points[item].z))
        {
            error.Assign("secondary point is not finite");
            return false;
        }
    }
    FModelPose candidate;
    if (!CopyPose(source, candidate, error))
    {
        return false;
    }
    // 先頭の祖先だけを積算し、連続した鎖は前の節のworld回転を引き継ぐ。
    FQuaternion baseParent{};
    if (!EvaluateParentWorldRotation(skeleton, source, bones[0], baseParent, error))
    {
        return false;
    }
    FQuaternion updatedParent = baseParent;
    for (uint32_t item = 0; item < count; ++item)
    {
        const uint32_t offset = bones[item] * 16u;
        const FVector3 origin{ { matrices.At(offset + 12), matrices.At(offset + 13), matrices.At(offset + 14) } };
        FVector3 currentDirection{};
        if (item + 1 < count)
        {
            const uint32_t childOffset = bones[item + 1] * 16u;
            currentDirection = Subtract(FVector3{ { matrices.At(childOffset + 12), matrices.At(childOffset + 13), matrices.At(childOffset + 14) } }, origin);
        }
        else
        {
            currentDirection = { { matrices.At(offset) * static_cast<double>(endOffset.x) + matrices.At(offset + 4) * static_cast<double>(endOffset.y) + matrices.At(offset + 8) * static_cast<double>(endOffset.z), matrices.At(offset + 1) * static_cast<double>(endOffset.x) + matrices.At(offset + 5) * static_cast<double>(endOffset.y) + matrices.At(offset + 9) * static_cast<double>(endOffset.z), matrices.At(offset + 2) * static_cast<double>(endOffset.x) + matrices.At(offset + 6) * static_cast<double>(endOffset.y) + matrices.At(offset + 10) * static_cast<double>(endOffset.z) } };
        }
        const FVector3 desiredDirection{ { static_cast<double>(points[item + 1].x) - points[item].x, static_cast<double>(points[item + 1].y) - points[item].y, static_cast<double>(points[item + 1].z) - points[item].z } };
        // ねじれは元の姿勢から保ち、節の向きを結ぶ最小回転だけを加える。
        const FVector3 fallback{ { matrices.At(offset + 4), matrices.At(offset + 5), matrices.At(offset + 6) } };
        FQuaternion delta{}, local{};
        if (!RotationBetween(currentDirection, desiredDirection, fallback, delta))
        {
            if (error.Empty())
            {
                error.Assign("secondary motion cannot orient a zero-length segment");
            }
            return false;
        }
        for (uint32_t component = 0; component < 4; ++component)
        {
            local.value[component] = source.localTransforms.At(bones[item]).rotation[component];
        }
        if (!NormalizeQuaternion(local))
        {
            error.Assign("secondary motion source rotation is invalid");
            return false;
        }
        // 元のworld回転を最小回転で曲げ、新しい親に対するlocal回転へ戻す。
        FQuaternion rotation = MultiplyQuaternion(Conjugate(updatedParent), MultiplyQuaternion(delta, MultiplyQuaternion(baseParent, local)));
        if (!NormalizeQuaternion(rotation))
        {
            error.Assign("secondary motion produced an invalid rotation");
            return false;
        }
        for (uint32_t component = 0; component < 4; ++component)
        {
            candidate.localTransforms.At(bones[item]).rotation[component] = static_cast<float>(rotation.value[component]);
        }
        // 次の節では、floatへ保存した実際のlocal回転を親の積算に使う。
        FQuaternion storedLocal{};
        for (uint32_t component = 0; component < 4; ++component)
        {
            storedLocal.value[component] = candidate.localTransforms.At(bones[item]).rotation[component];
        }
        if (!NormalizeQuaternion(storedLocal))
        {
            error.Assign("secondary motion stored rotation is invalid");
            return false;
        }
        baseParent = skeleton.parents.At(bones[item]) < 0 ? local : MultiplyQuaternion(baseParent, local);
        updatedParent = skeleton.parents.At(bones[item]) < 0 ? storedLocal : MultiplyQuaternion(updatedParent, storedLocal);
        if (!NormalizeQuaternion(baseParent) || !NormalizeQuaternion(updatedParent))
        {
            error.Assign("secondary motion chain world rotation is invalid");
            return false;
        }
    }
    output.localTransforms.MoveFrom(candidate.localTransforms);
    output.morphWeights.MoveFrom(candidate.morphWeights);
    error.Clear();
    return true;
}

}
