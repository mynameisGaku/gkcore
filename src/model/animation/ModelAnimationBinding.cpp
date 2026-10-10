// SPDX-License-Identifier: NOASSERTION
#include "model/animation/ModelAnimationBinding.h"
#include "model/animation/ModelPose.h"
#include <gkcore.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * 外部animationを対象モデルの骨格へ対応付ける処理。
 */
namespace gk::model
{
namespace
{
/**
 * 一意な名前を検索するための一時記録。
 */
struct FNamedIndex
{
    // 元データが所有する名前。
    const char* name;
    // 元データ内の番号。
    int32_t index;
};

/**
 * 名前を昇順に並べる比較関数。
 */
int CompareName(const void* left, const void* right)
{
    return strcmp(static_cast<const FNamedIndex*>(left)->name, static_cast<const FNamedIndex*>(right)->name);
}

/**
 * 一意な名前の番号を返し、重複名は-2で示す。
 */
int32_t FindName(const Array<FNamedIndex>& names, const char* name)
{
    if (!name || !*name)
        return -1;
    uint32_t left = 0, right = names.Count();
    while (left < right)
    {
        const uint32_t middle = left + (right - left) / 2;
        if (strcmp(names.At(middle).name, name) < 0)
            left = middle + 1;
        else
            right = middle;
    }
    if (left == names.Count() || strcmp(names.At(left).name, name) != 0)
        return -1;
    if (left + 1 < names.Count() && strcmp(names.At(left + 1).name, name) == 0)
        return -2;
    return names.At(left).index;
}

/**
 * 有効なbone名またはmorph名を集めて検索順に並べる。
 */
bool CollectNames(const AModelAnimationSource& source, bool morph, Array<FNamedIndex>& names)
{
    const uint32_t count = morph ? source.Skeleton().restMorphWeights.Count() : source.Skeleton().parents.Count();
    for (uint32_t i = 0; i < count; ++i)
    {
        const char* name = morph ? source.MorphName(i) : source.BoneName(i);
        if (name && *name && !names.Append({ name, static_cast<int32_t>(i) }))
            return false;
    }
    if (names.Count() > 1)
        qsort(names.Data(), names.Count(), sizeof(FNamedIndex), CompareName);
    return true;
}

Float4 Multiply(Float4 a, Float4 b)
{
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

Float4 Inverse(Float4 q)
{
    return { -q.x, -q.y, -q.z, q.w };
}

/**
 * 単位quaternionで位置差分の座標系を変換する。
 */
void TransformDirection(const float matrix[16], const double input[3], double output[3])
{
    for (uint32_t axis = 0; axis < 3; ++axis)
        output[axis] = matrix[axis] * input[0] + matrix[4 + axis] * input[1] + matrix[8 + axis] * input[2];
}

/**
 * 親の回転・scaleを含むモデル空間の差分を、親基準へ戻す。
 */
bool InverseDirection(const float matrix[16], const double input[3], double output[3])
{
    const double a = matrix[0], b = matrix[4], c = matrix[8], d = matrix[1], e = matrix[5], f = matrix[9], g = matrix[2], h = matrix[6], i = matrix[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!isfinite(determinant) || determinant == 0.0)
        return false;
    output[0] = ((e * i - f * h) * input[0] + (c * h - b * i) * input[1] + (b * f - c * e) * input[2]) / determinant;
    output[1] = ((f * g - d * i) * input[0] + (a * i - c * g) * input[1] + (c * d - a * f) * input[2]) / determinant;
    output[2] = ((d * h - e * g) * input[0] + (b * g - a * h) * input[1] + (a * e - b * d) * input[2]) / determinant;
    return isfinite(output[0]) && isfinite(output[1]) && isfinite(output[2]);
}

bool Normalize(Float4& q)
{
    const double length = sqrt(static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y + static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w);
    if (!(length > 0.0) || !isfinite(length))
        return false;
    q = { static_cast<float>(q.x / length), static_cast<float>(q.y / length), static_cast<float>(q.z / length), static_cast<float>(q.w / length) };
    return true;
}

bool WorldRotations(const animation::FModelSkeleton& skeleton, const Array<animation::FModelBoneTransform>& local, Array<Float4>& world)
{
    if (skeleton.parents.Count() != local.Count())
        return false;
    for (uint32_t i = 0; i < local.Count(); ++i)
    {
        const auto& value = local.At(i);
        Float4 q{ value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3] };
        if (!Normalize(q))
            return false;
        const int32_t parent = skeleton.parents.At(i);
        if (parent >= static_cast<int32_t>(i) || parent < -1)
            return false;
        if (parent >= 0)
            q = Multiply(world.At(static_cast<uint32_t>(parent)), q);
        if (!Normalize(q) || !world.Append(q))
            return false;
    }
    return true;
}

/**
 * quaternion列を4成分のflat配列へ追加する。
 */
bool AppendWorldRotations(Array<float>& output, const Array<Float4>& rotations)
{
    if (rotations.Count() > UINT32_MAX / 4u || !output.Reserve(rotations.Count() * 4u))
        return false;
    for (uint32_t index = 0; index < rotations.Count(); ++index)
    {
        const Float4& value = rotations.At(index);
        const float components[4] = { value.x, value.y, value.z, value.w };
        if (!output.AppendRange(components, 4))
            return false;
    }
    return true;
}

/**
 * flat配列に保持したquaternionを読み出す。
 */
Float4 ReadWorldRotation(const Array<float>& rotations, uint32_t index)
{
    const float* value = rotations.Data() + index * 4u;
    return { value[0], value[1], value[2], value[3] };
}

/**
 * 対応済みの役割から適用先の骨番号を探す。見つからない場合はUINT32_MAX。
 */
uint32_t FindMappedRole(const FModelClipState& state, EHumanoidBone role)
{
    for (uint32_t bone = 0; bone < state.mappedRoles.Count(); ++bone)
    {
        if (state.mappedRoles.At(bone) == static_cast<uint16_t>(role) && bone < state.bones.Count() && state.bones.At(bone) >= 0)
        {
            return bone;
        }
    }
    return UINT32_MAX;
}

/**
 * rest model行列の骨原点間距離を返す。対応範囲が不正なら0。
 */
double RestBoneDistance(const Array<float>& matrices, uint32_t first, uint32_t second)
{
    if (first >= matrices.Count() / 16u || second >= matrices.Count() / 16u)
    {
        return 0.0;
    }
    const float* a = matrices.Data() + first * 16u + 12u;
    const float* b = matrices.Data() + second * 16u + 12u;
    const double x = static_cast<double>(a[0]) - b[0];
    const double y = static_cast<double>(a[1]) - b[1];
    const double z = static_cast<double>(a[2]) - b[2];
    return sqrt(x * x + y * y + z * z);
}

/**
 * 完全に対応した左右の脚長比を求め、親の平行移動の分け方に依存しない倍率を返す。
 * 有効な脚が一つもない場合は0を返し、従来の腰位置比へ戻す。
 */
double HumanoidTranslationScale(const FModelClipState& state, const Array<float>& sourceMatrices, const Array<float>& targetMatrices)
{
    const EHumanoidBone roles[2][3] = { { EHumanoidBone::LeftUpperLeg, EHumanoidBone::LeftLowerLeg, EHumanoidBone::LeftFoot }, { EHumanoidBone::RightUpperLeg, EHumanoidBone::RightLowerLeg, EHumanoidBone::RightFoot } };
    double sourceLength = 0.0;
    double targetLength = 0.0;
    for (uint32_t side = 0; side < 2; ++side)
    {
        const uint32_t upper = FindMappedRole(state, roles[side][0]);
        const uint32_t lower = FindMappedRole(state, roles[side][1]);
        const uint32_t foot = FindMappedRole(state, roles[side][2]);
        if (upper == UINT32_MAX || lower == UINT32_MAX || foot == UINT32_MAX)
        {
            continue;
        }
        const uint32_t sourceUpper = static_cast<uint32_t>(state.bones.At(upper));
        const uint32_t sourceLower = static_cast<uint32_t>(state.bones.At(lower));
        const uint32_t sourceFoot = static_cast<uint32_t>(state.bones.At(foot));
        const double sourceUpperLength = RestBoneDistance(sourceMatrices, sourceUpper, sourceLower);
        const double sourceLowerLength = RestBoneDistance(sourceMatrices, sourceLower, sourceFoot);
        const double targetUpperLength = RestBoneDistance(targetMatrices, upper, lower);
        const double targetLowerLength = RestBoneDistance(targetMatrices, lower, foot);
        if (sourceUpperLength > 0.0 && sourceLowerLength > 0.0 && targetUpperLength > 0.0 && targetLowerLength > 0.0 && isfinite(sourceUpperLength) && isfinite(sourceLowerLength) && isfinite(targetUpperLength) && isfinite(targetLowerLength))
        {
            sourceLength += sourceUpperLength + sourceLowerLength;
            targetLength += targetUpperLength + targetLowerLength;
        }
    }
    const double ratio = sourceLength > 0.0 ? targetLength / sourceLength : 0.0;
    return isfinite(ratio) && ratio > 0.0 ? ratio : 0.0;
}

/**
 * sourceとtargetの不変rest pose行列・親階層回転を候補へ構築する。
 */
bool BuildRestPoseCache(const animation::FModelSkeleton& sourceSkeleton, const animation::FModelSkeleton& targetSkeleton, Array<float>& sourceMatrices, Array<float>& targetMatrices, Array<float>& sourceRotations, Array<float>& targetRotations, String& error)
{
    animation::FModelPose sourceRestPose, targetRestPose;
    Array<Float4> sourceWorldRotations, targetWorldRotations;
    if (!animation::InitializeModelPose(sourceSkeleton, sourceRestPose, error) || !animation::InitializeModelPose(targetSkeleton, targetRestPose, error) || !animation::EvaluateModelPose(sourceSkeleton, sourceRestPose, sourceMatrices, error) || !animation::EvaluateModelPose(targetSkeleton, targetRestPose, targetMatrices, error) || !WorldRotations(sourceSkeleton, sourceSkeleton.restLocalTransforms, sourceWorldRotations) || !WorldRotations(targetSkeleton, targetSkeleton.restLocalTransforms, targetWorldRotations) || !AppendWorldRotations(sourceRotations, sourceWorldRotations) || !AppendWorldRotations(targetRotations, targetWorldRotations))
    {
        if (error.Empty())
            error.Assign("animation rest pose precomputation failed");
        return false;
    }
    error.Clear();
    return true;
}
}

bool BuildClipBinding(FModelAnimationAsset& source, uint32_t clip, const FModelAnimationAsset* target, const Array<uint16_t>& targetRoles, FModelClipState& candidate, String& error)
{
    FModelClipState staged;
    if (!source.source || clip >= source.source->ClipCount())
    {
        error.Assign("invalid animation clip");
        return false;
    }
    if (source.source->Format() == EModelAnimationFormat::ObjSequence)
    {
        if (target && target->source->Format() != EModelAnimationFormat::ObjSequence)
        {
            error.Assign("OBJ sequence cannot drive a skeletal model");
            return false;
        }
    }
    else
    {
        if (!target || !target->source || target->source->Format() == EModelAnimationFormat::ObjSequence)
        {
            error.Assign("target model has no compatible skeleton");
            return false;
        }
        const auto& destination = *target->source;
        Array<FNamedIndex> names, morphNames;
        Array<uint8_t> usedBones;
        if (!CollectNames(*source.source, false, names) || !CollectNames(*source.source, true, morphNames))
        {
            error.Assign("animation mapping allocation failed");
            return false;
        }
        if (!staged.bones.Reserve(destination.Skeleton().parents.Count()) || !staged.requestedRoles.Reserve(destination.Skeleton().parents.Count()) || !staged.mappedRoles.Reserve(destination.Skeleton().parents.Count()) || !usedBones.Reserve(source.source->Skeleton().parents.Count()))
        {
            error.Assign("animation mapping allocation failed");
            return false;
        }
        for (uint32_t i = 0; i < source.source->Skeleton().parents.Count(); ++i)
            usedBones.Append(0);
        uint32_t matched = 0;
        for (uint32_t i = 0; i < destination.Skeleton().parents.Count(); ++i)
        {
            int32_t found = &source == target ? static_cast<int32_t>(i) : -1;
            uint16_t mappedRole = 0;
            const uint16_t role = i < targetRoles.Count() ? targetRoles.At(i) : 0;
            if (&source == target)
            {
                mappedRole = role;
            }
            if (found < 0 && role != 0)
            {
                for (uint32_t j = 0; j < source.roles.Count(); ++j)
                {
                    if (source.roles.At(j) == role)
                    {
                        if (found >= 0)
                        {
                            error.Assign("animation bone role is duplicated");
                            return false;
                        }
                        found = static_cast<int32_t>(j);
                        mappedRole = role;
                    }
                }
            }
            if (found < 0)
                found = FindName(names, destination.BoneName(i));
            if (found == -2)
            {
                error.Assign("animation bone name is ambiguous");
                return false;
            }
            if (found >= 0)
            {
                if (static_cast<uint32_t>(found) >= usedBones.Count() || usedBones.At(static_cast<uint32_t>(found)))
                {
                    error.Assign("animation bone mapping must be one-to-one");
                    return false;
                }
                usedBones.At(static_cast<uint32_t>(found)) = 1;
            }
            if (!staged.bones.Append(found) || !staged.requestedRoles.Append(role) || !staged.mappedRoles.Append(mappedRole))
            {
                error.Assign("animation bone mapping allocation failed");
                return false;
            }
            if (found >= 0)
                ++matched;
        }
        for (uint32_t i = 0; i < destination.Skeleton().restMorphWeights.Count(); ++i)
        {
            const int32_t found = &source == target ? static_cast<int32_t>(i) : FindName(morphNames, destination.MorphName(i));
            if (found == -2)
            {
                error.Assign("animation morph name is ambiguous");
                return false;
            }
            if (!staged.morphs.Append(found))
            {
                error.Assign("animation morph mapping allocation failed");
                return false;
            }
            if (found >= 0)
                ++matched;
        }
        if (&source != target && matched == 0)
        {
            error.Assign("animation has no matching bone or morph names or roles");
            return false;
        }
        if (&source != target && !BuildRestPoseCache(source.source->Skeleton(), destination.Skeleton(), staged.sourceRestModelMatrices, staged.targetRestModelMatrices, staged.sourceRestWorldRotations, staged.targetRestWorldRotations, error))
            return false;
        if (&source != target)
        {
            staged.humanoidTranslationScale = HumanoidTranslationScale(staged, staged.sourceRestModelMatrices, staged.targetRestModelMatrices);
        }
    }
    if (!Retain(&source.reference))
    {
        error.Assign("animation reference limit exceeded");
        return false;
    }
    if (candidate.asset)
        Release(&candidate.asset->reference);
    candidate.bones.MoveFrom(staged.bones);
    candidate.requestedRoles.MoveFrom(staged.requestedRoles);
    candidate.mappedRoles.MoveFrom(staged.mappedRoles);
    candidate.morphs.MoveFrom(staged.morphs);
    candidate.sourceRestModelMatrices.MoveFrom(staged.sourceRestModelMatrices);
    candidate.targetRestModelMatrices.MoveFrom(staged.targetRestModelMatrices);
    candidate.sourceRestWorldRotations.MoveFrom(staged.sourceRestWorldRotations);
    candidate.targetRestWorldRotations.MoveFrom(staged.targetRestWorldRotations);
    candidate.humanoidTranslationScale = staged.humanoidTranslationScale;
    candidate.asset = &source;
    candidate.clip = clip;
    error.Clear();
    return true;
}

bool SampleBoundClip(const FModelClipState& state, const FModelAnimationAsset& target, animation::FModelPose& output, String& error)
{
    if (!state.asset || !state.asset->source || !target.source)
    {
        error.Assign("animation source is missing");
        return false;
    }
    if (state.asset == &target)
        return state.asset->source->Sample(state.clip, state.seconds, output, error);
    animation::FModelPose sampled, candidate;
    const auto& sourceSkeleton = state.asset->source->Skeleton();
    const auto& targetSkeleton = target.source->Skeleton();
    if (!state.asset->source->Sample(state.clip, state.seconds, sampled, error) || !animation::InitializeModelPose(targetSkeleton, candidate, error))
        return false;
    const uint64_t sourceMatrixCount = static_cast<uint64_t>(sourceSkeleton.parents.Count()) * 16u;
    const uint64_t targetMatrixCount = static_cast<uint64_t>(targetSkeleton.parents.Count()) * 16u;
    const uint64_t sourceRotationCount = static_cast<uint64_t>(sourceSkeleton.parents.Count()) * 4u;
    const uint64_t targetRotationCount = static_cast<uint64_t>(targetSkeleton.parents.Count()) * 4u;
    if (state.bones.Count() != candidate.localTransforms.Count() || state.mappedRoles.Count() != candidate.localTransforms.Count() || state.morphs.Count() != candidate.morphWeights.Count() || sourceMatrixCount > UINT32_MAX || targetMatrixCount > UINT32_MAX || sourceRotationCount > UINT32_MAX || targetRotationCount > UINT32_MAX)
    {
        error.Assign("animation mapping count mismatch");
        return false;
    }
    const bool hasCachedRestData = state.sourceRestModelMatrices.Count() || state.targetRestModelMatrices.Count() || state.sourceRestWorldRotations.Count() || state.targetRestWorldRotations.Count();
    const bool cacheCountsMatch = state.sourceRestModelMatrices.Count() == sourceMatrixCount && state.targetRestModelMatrices.Count() == targetMatrixCount && state.sourceRestWorldRotations.Count() == sourceRotationCount && state.targetRestWorldRotations.Count() == targetRotationCount;
    if (hasCachedRestData && !cacheCountsMatch)
    {
        error.Assign("animation rest pose cache count mismatch");
        return false;
    }
    Array<float> fallbackSourceMatrices, fallbackTargetMatrices, fallbackSourceRotations, fallbackTargetRotations;
    if (!hasCachedRestData && !BuildRestPoseCache(sourceSkeleton, targetSkeleton, fallbackSourceMatrices, fallbackTargetMatrices, fallbackSourceRotations, fallbackTargetRotations, error))
        return false;
    const Array<float>& sourceRestMatrices = hasCachedRestData ? state.sourceRestModelMatrices : fallbackSourceMatrices;
    const Array<float>& targetRestMatrices = hasCachedRestData ? state.targetRestModelMatrices : fallbackTargetMatrices;
    const Array<float>& sourceRestRotations = hasCachedRestData ? state.sourceRestWorldRotations : fallbackSourceRotations;
    const Array<float>& targetRestRotations = hasCachedRestData ? state.targetRestWorldRotations : fallbackTargetRotations;
    const double humanoidScale = hasCachedRestData ? state.humanoidTranslationScale : HumanoidTranslationScale(state, sourceRestMatrices, targetRestMatrices);
    Array<Float4> sourceAnimated, targetAnimated;
    Array<float> sourceMatrices, targetMatrices;
    if (!animation::EvaluateModelPose(sourceSkeleton, sampled, sourceMatrices, error))
        return false;
    if (!WorldRotations(sourceSkeleton, sampled.localTransforms, sourceAnimated))
    {
        error.Assign("animation rotation evaluation failed");
        return false;
    }
    for (uint32_t i = 0; i < candidate.localTransforms.Count(); ++i)
    {
        auto& value = candidate.localTransforms.At(i);
        const int32_t sourceIndex = state.bones.At(i);
        const int32_t parent = targetSkeleton.parents.At(i);
        Float4 local{ value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3] };
        Float4 world = parent < 0 ? local : Multiply(targetAnimated.At(static_cast<uint32_t>(parent)), local);
        if (sourceIndex >= 0)
        {
            const uint32_t sourceBone = static_cast<uint32_t>(sourceIndex);
            if (sourceBone >= sampled.localTransforms.Count())
            {
                error.Assign("animation bone mapping exceeds source");
                return false;
            }
            world = Multiply(Multiply(sourceAnimated.At(sourceBone), Inverse(ReadWorldRotation(sourceRestRotations, sourceBone))), ReadWorldRotation(targetRestRotations, i));
            local = parent < 0 ? world : Multiply(Inverse(targetAnimated.At(static_cast<uint32_t>(parent))), world);
            const auto& rest = sourceSkeleton.restLocalTransforms.At(sourceBone);
            const auto& animated = sampled.localTransforms.At(sourceBone);
            const uint16_t role = state.mappedRoles.At(i);
            // 人型の手足は適用先の骨長を保ち、腰とrootだけ位置の移動を転送する。
            if (role == 0 || role == static_cast<uint16_t>(EHumanoidBone::Hips) || parent < 0)
            {
                double delta[3] = { static_cast<double>(animated.position[0]) - rest.position[0], static_cast<double>(animated.position[1]) - rest.position[1], static_cast<double>(animated.position[2]) - rest.position[2] };
                const int32_t sourceParent = sourceSkeleton.parents.At(sourceBone);
                double modelDelta[3] = { delta[0], delta[1], delta[2] };
                if (sourceParent >= 0)
                    TransformDirection(sourceMatrices.Data() + static_cast<uint32_t>(sourceParent) * 16, delta, modelDelta);
                if (parent >= 0)
                {
                    if (!InverseDirection(targetMatrices.Data() + static_cast<uint32_t>(parent) * 16, modelDelta, delta))
                    {
                        error.Assign("cannot retarget motion through a singular parent transform");
                        return false;
                    }
                }
                else
                    memcpy(delta, modelDelta, sizeof(delta));
                double ratio = 1.0;
                if (role == static_cast<uint16_t>(EHumanoidBone::Hips) && humanoidScale > 0.0)
                {
                    ratio = humanoidScale;
                }
                else if (role == static_cast<uint16_t>(EHumanoidBone::Hips))
                {
                    const double sourceOffset[3] = { rest.position[0], rest.position[1], rest.position[2] };
                    const double targetOffset[3] = { value.position[0], value.position[1], value.position[2] };
                    double sourceWorldOffset[3] = { sourceOffset[0], sourceOffset[1], sourceOffset[2] };
                    double targetWorldOffset[3] = { targetOffset[0], targetOffset[1], targetOffset[2] };
                    if (sourceParent >= 0)
                        TransformDirection(sourceRestMatrices.Data() + static_cast<uint32_t>(sourceParent) * 16, sourceOffset, sourceWorldOffset);
                    if (parent >= 0)
                        TransformDirection(targetRestMatrices.Data() + static_cast<uint32_t>(parent) * 16, targetOffset, targetWorldOffset);
                    const double sourceLength = sqrt(sourceWorldOffset[0] * sourceWorldOffset[0] + sourceWorldOffset[1] * sourceWorldOffset[1] + sourceWorldOffset[2] * sourceWorldOffset[2]);
                    const double targetLength = sqrt(targetWorldOffset[0] * targetWorldOffset[0] + targetWorldOffset[1] * targetWorldOffset[1] + targetWorldOffset[2] * targetWorldOffset[2]);
                    if (sourceLength > 0.0 && targetLength > 0.0)
                        ratio = targetLength / sourceLength;
                }
                value.position[0] += static_cast<float>(delta[0] * ratio);
                value.position[1] += static_cast<float>(delta[1] * ratio);
                value.position[2] += static_cast<float>(delta[2] * ratio);
            }
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (rest.scale[axis] != 0.0f)
                    value.scale[axis] *= animated.scale[axis] / rest.scale[axis];
                else if (animated.scale[axis] != 0.0f)
                {
                    error.Assign("cannot retarget animation from a zero rest scale");
                    return false;
                }
            }
            if (!Normalize(local) || !Normalize(world))
            {
                error.Assign("retargeted rotation is invalid");
                return false;
            }
            value.rotation[0] = local.x;
            value.rotation[1] = local.y;
            value.rotation[2] = local.z;
            value.rotation[3] = local.w;
        }
        if (!targetAnimated.Append(world))
        {
            error.Assign("retargeted rotation allocation failed");
            return false;
        }
        float localMatrix[16]{}, modelMatrix[16]{};
        if (!animation::BuildModelBoneMatrix(value, localMatrix, error))
            return false;
        if (parent >= 0)
        {
            if (!animation::MultiplyModelBoneMatrices(targetMatrices.Data() + static_cast<uint32_t>(parent) * 16, localMatrix, modelMatrix, error))
                return false;
        }
        else
            memcpy(modelMatrix, localMatrix, sizeof(modelMatrix));
        if (!targetMatrices.AppendRange(modelMatrix, 16))
        {
            error.Assign("retargeted transform allocation failed");
            return false;
        }
    }
    for (uint32_t i = 0; i < candidate.morphWeights.Count(); ++i)
    {
        const int32_t sourceIndex = state.morphs.At(i);
        if (sourceIndex >= 0)
        {
            if (static_cast<uint32_t>(sourceIndex) >= sampled.morphWeights.Count())
            {
                error.Assign("animation morph mapping exceeds source");
                return false;
            }
            candidate.morphWeights.At(i) = sampled.morphWeights.At(static_cast<uint32_t>(sourceIndex));
        }
    }
    Array<float> validated;
    if (!animation::EvaluateModelPose(targetSkeleton, candidate, validated, error))
        return false;
    output.localTransforms.MoveFrom(candidate.localTransforms);
    output.morphWeights.MoveFrom(candidate.morphWeights);
    error.Clear();
    return true;
}
}
