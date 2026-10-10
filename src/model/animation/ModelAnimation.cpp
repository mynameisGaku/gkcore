// SPDX-License-Identifier: NOASSERTION
#include <gkcore.h>
#include "model/animation/AModelAnimationSource.h"
#include "model/animation/FModelPlayback.h"
#include "model/animation/ModelAnimationBinding.h"
#include "model/animation/HumanoidMapping.h"
#include "model/animation/HumanoidBoneMap.h"
#include "model/animation/ModelAnimationResources.h"
#include "model/animation/ModelSnapshot.h"
#include "model/animation/ModelIk.h"
#include "model/animation/ObjSequence.h"
#include "model/animation/GlbAnimation.h"
#include "model/animation/FbxAnimation.h"
#include "core/Context.h"
#include "resources/ResourceIO.h"
#include "foundation/Memory.h"
#include <math.h>
#include <string.h>

/**
 * モデルanimationを公開APIから操作する関数群。
 */
namespace gk
{
namespace
{
/**
 * API failureへ内部診断を渡す。
 */
int Failure(const String& error)
{
    return detail::SetError(error.Empty() ? "model animation operation failed" : error.CStr());
}

/**
 * 有効なmodel handleからinstance変換を得る。
 */
detail::ModelTransform* Transform(ModelHandle model)
{
    auto* transform = detail::FindModel(model) ? detail::FindModelTransform(model) : nullptr;
    if (!transform)
        detail::SetError("invalid model handle or missing instance state");
    return transform;
}

/**
 * modelに埋め込まれたanimation assetを借用参照で返す。
 */
model::FModelAnimationAsset* Embedded(ModelHandle handle)
{
    auto* resource = detail::FindModel(handle);
    if (!resource)
    {
        detail::SetError("invalid model handle");
        return nullptr;
    }
    return resource->animation;
}

/**
 * instanceの再生状態を返し、要求時だけ新規作成する。
 */
model::FModelPlayback* Playback(ModelHandle handle, bool create)
{
    auto* transform = Transform(handle);
    if (!transform)
        return nullptr;
    if (!transform->playback && create)
    {
        try
        {
            transform->playback = new model::FModelPlayback;
        }
        catch (...)
        {
            detail::SetError("model playback allocation failed");
            return nullptr;
        }
    }
    return transform->playback;
}

/**
 * model resourceをhandleと初期instance状態へ登録する。
 */
ModelHandle AdoptModel(detail::ModelResource* resource, String& error)
{
    const auto handle = detail::RegisterModelResource(resource, error);
    if (!handle.IsValid())
    {
        Failure(error);
        return {};
    }
    detail::ModelTransform transform{};
    transform.handle = handle;
    transform.scale = { 1.0f, 1.0f, 1.0f };
    if (!detail::GetContext().modelTransforms.Append(transform))
    {
        detail::DeleteModel(handle, error);
        detail::SetError("model instance allocation failed");
        return {};
    }
    detail::ClearError();
    return handle;
}

/**
 * 再生時刻をclip長とloop設定に合わせて制限する。
 */
bool Clock(const model::FModelClipState& state, double seconds, double& output)
{
    if (!isfinite(seconds) || !state.asset || !state.asset->source)
        return false;
    const double duration = state.asset->source->ClipDuration(state.clip);
    if (!isfinite(duration) || duration < 0.0)
        return false;
    output = duration == 0.0 ? 0.0 : state.loop ? fmod(seconds, duration) : fmax(0.0, fmin(duration, seconds));
    if (output < 0.0)
        output += duration;
    return true;
}

/**
 * 設定済みの再生枠を範囲確認して返す。
 */
model::FModelClipState* Clip(ModelHandle handle, uint32_t slot)
{
    auto* playback = Playback(handle, false);
    if (!playback || slot > 1 || !playback->clips[slot].asset)
    {
        detail::SetError("animation slot is not configured");
        return nullptr;
    }
    return &playback->clips[slot];
}

/**
 * 再生枠を候補検証後に置き換える。
 */
int Bind(ModelHandle handle, model::FModelAnimationAsset* asset, uint32_t clip, bool loop, uint32_t slot, float weight)
{
    auto* target = detail::FindModel(handle);
    if (!target || !Transform(handle))
        return detail::SetError("invalid model handle");
    if (!asset || !asset->source || slot > 1 || !isfinite(weight) || weight < 0.0f || weight > 1.0f)
        return detail::SetError("invalid animation source, slot, or blend weight");
    auto* playback = Playback(handle, true);
    if (!playback)
        return -1;
    if (slot == 1 && !playback->clips[0].asset)
        return detail::SetError("play a primary animation before blending");
    if (slot == 1 && playback->clips[0].asset->source->Format() == model::EModelAnimationFormat::ObjSequence && asset->source->Format() != model::EModelAnimationFormat::ObjSequence)
        return detail::SetError("cannot blend a sequence and skeletal clip");
    model::FModelClipState candidate;
    String error;
    if (!model::BuildClipBinding(*asset, clip, target->animation, playback->roles, candidate, error))
        return Failure(error);
    candidate.loop = loop;
    // 実際の頂点対応まで検査してから再生枠を置き換える。
    model::animation::FModelPose pose;
    auto* validation = model::CloneModelSnapshot(*target, error);
    const bool sequence = asset->source->Format() == model::EModelAnimationFormat::ObjSequence;
    bool valid = validation && (sequence ? asset->source->Sample(clip, 0.0, pose, error) : model::SampleBoundClip(candidate, *target->animation, pose, error));
    if (valid)
        valid = (sequence ? asset->source : target->animation->source)->Deform(pose, *validation, error);
    if (validation)
        Release(&validation->reference);
    if (!valid)
    {
        Release(&candidate.asset->reference);
        return Failure(error);
    }
    auto& destination = playback->clips[slot];
    if (destination.asset)
        Release(&destination.asset->reference);
    destination.asset = candidate.asset;
    destination.clip = candidate.clip;
    destination.seconds = 0.0;
    destination.speed = 1.0;
    destination.loop = loop;
    destination.bones.MoveFrom(candidate.bones);
    destination.requestedRoles.MoveFrom(candidate.requestedRoles);
    destination.mappedRoles.MoveFrom(candidate.mappedRoles);
    destination.morphs.MoveFrom(candidate.morphs);
    destination.sourceRestModelMatrices.MoveFrom(candidate.sourceRestModelMatrices);
    destination.targetRestModelMatrices.MoveFrom(candidate.targetRestModelMatrices);
    destination.sourceRestWorldRotations.MoveFrom(candidate.sourceRestWorldRotations);
    destination.targetRestWorldRotations.MoveFrom(candidate.targetRestWorldRotations);
    destination.humanoidTranslationScale = candidate.humanoidTranslationScale;
    if (slot == 0)
    {
        if (playback->clips[1].asset)
            Release(&playback->clips[1].asset->reference);
        playback->clips[1].asset = nullptr;
        playback->clips[1].bones.Clear();
        playback->clips[1].requestedRoles.Clear();
        playback->clips[1].mappedRoles.Clear();
        playback->clips[1].morphs.Clear();
        playback->clips[1].sourceRestModelMatrices.Clear();
        playback->clips[1].targetRestModelMatrices.Clear();
        playback->clips[1].sourceRestWorldRotations.Clear();
        playback->clips[1].targetRestWorldRotations.Clear();
        playback->clips[1].humanoidTranslationScale = 0.0;
        playback->blendWeight = 0.0f;
    }
    else
        playback->blendWeight = weight;
    detail::ClearError();
    return 0;
}

/**
 * 重複しない人型役割を指定boneへ割り当てる。
 */
bool SetRole(Array<uint16_t>& roles, uint32_t count, uint32_t bone, EHumanoidBone role, String& error)
{
    if (bone >= count || role >= EHumanoidBone::Count)
    {
        error.Assign("invalid humanoid role or bone");
        return false;
    }
    if (role != EHumanoidBone::None)
    {
        for (uint32_t i = 0; i < roles.Count(); ++i)
        {
            if (i != bone && roles.At(i) == static_cast<uint16_t>(role))
            {
                error.Assign("humanoid role is already assigned to another bone");
                return false;
            }
        }
    }
    if (roles.Count() == 0)
    {
        if (!roles.Reserve(count))
        {
            error.Assign("humanoid role allocation failed");
            return false;
        }
        for (uint32_t i = 0; i < count; ++i)
            roles.Append(0);
    }
    if (roles.Count() != count)
    {
        error.Assign("humanoid role count mismatch");
        return false;
    }
    roles.At(bone) = static_cast<uint16_t>(role);
    return true;
}

/**
 * IK chainを検証し、同じrootの以前の命令と入れ替える。
 */
int StoreIk(ModelHandle handle, const uint32_t* bones, uint32_t count, Vec3 target, Vec3 pole, float weight, bool twoBone)
{
    auto* asset = Embedded(handle);
    if (!asset || !asset->source || !bones || count < 2 || count > 1024 || !detail::IsFinite(target) || !detail::IsFinite(pole) || !isfinite(weight) || weight < 0.0f || weight > 1.0f)
        return detail::SetError("invalid IK skeleton, chain, target, or weight");
    model::animation::FModelPose rest, solved;
    String error;
    const auto& skeleton = asset->source->Skeleton();
    for (uint32_t i = 0; i + 1 < count; ++i)
    {
        if (!asset->source->BoneWritable(bones[i]))
            return detail::SetError("IK chain includes a fixed format transform");
    }
    const float point[3] = { target.x, target.y, target.z };
    const float bend[3] = { pole.x, pole.y, pole.z };
    if (!model::animation::InitializeModelPose(skeleton, rest, error))
        return Failure(error);
    const bool valid = twoBone ? model::animation::SolveTwoBoneIk(skeleton, rest, bones[0], bones[1], bones[2], point, bend, weight, solved, error) : model::animation::SolveFabrikIk(skeleton, rest, bones, count, point, weight, 0.0001f, 64, solved, error);
    if (!valid)
        return Failure(error);
    auto* playback = Playback(handle, true);
    if (!playback)
        return -1;
    Array<model::FModelIkCommand> commands;
    Array<uint32_t> chain;
    for (uint32_t i = 0; i < playback->ik.Count(); ++i)
    {
        auto command = playback->ik.At(i);
        const auto* old = playback->ikBones.Data() + command.offset;
        if (old[0] == bones[0])
            continue;
        command.offset = chain.Count();
        if (!chain.AppendRange(old, command.count) || !commands.Append(command))
            return detail::SetError("IK command allocation failed");
    }
    if (commands.Count() >= 64 || chain.Count() > 65536u - count)
        return detail::SetError("IK command count exceeds its limit");
    model::FModelIkCommand command{};
    command.offset = chain.Count();
    command.count = count;
    command.weight = weight;
    command.twoBone = twoBone;
    memcpy(command.target, point, sizeof(point));
    memcpy(command.pole, bend, sizeof(bend));
    if (!chain.AppendRange(bones, count) || !commands.Append(command))
        return detail::SetError("IK command allocation failed");
    playback->ik.MoveFrom(commands);
    playback->ikBones.MoveFrom(chain);
    detail::ClearError();
    return 0;
}
}

ModelHandle CreateModelInstance(ModelHandle handle)
{
    auto* resource = detail::FindModel(handle);
    if (!resource || !Transform(handle) || !Retain(&resource->reference))
    {
        detail::SetError("invalid model or reference limit exceeded");
        return {};
    }
    String error;
    return AdoptModel(resource, error);
}

ModelHandle LoadModelSequence(const char* const* paths, uint32_t count, float fps)
{
    String error;
    detail::ModelResource* base = nullptr;
    auto* asset = model::LoadObjSequence(paths, count, fps, base, error);
    if (!asset)
    {
        Failure(error);
        return {};
    }
    base->animation = asset;
    return AdoptModel(base, error);
}

ModelAnimationHandle LoadModelAnimation(const char* path)
{
    String error;
    uint8_t* bytes = nullptr;
    uint32_t size = 0;
    if (!detail::ReadResourceFile(path, 64u * 1024u * 1024u, bytes, size, error))
    {
        Failure(error);
        return {};
    }
    auto* asset = size >= 4 && memcmp(bytes, "glTF", 4) == 0 ? model::LoadGlbAnimation(bytes, size, error) : model::LoadFbxAnimation(bytes, size, path, error);
    Deallocate(bytes);
    const auto handle = model::RegisterAnimation(asset, error);
    if (!handle.IsValid())
        Failure(error);
    else
        detail::ClearError();
    return handle;
}

ModelAnimationHandle LoadModelSequenceAnimation(const char* const* paths, uint32_t count, float fps)
{
    String error;
    detail::ModelResource* base = nullptr;
    auto* asset = model::LoadObjSequence(paths, count, fps, base, error);
    if (base)
        Release(&base->reference);
    const auto handle = model::RegisterAnimation(asset, error);
    if (!handle.IsValid())
        Failure(error);
    else
        detail::ClearError();
    return handle;
}

int DeleteModelAnimation(ModelAnimationHandle handle)
{
    String error;
    if (!model::DeleteAnimation(handle, error))
        return Failure(error);
    detail::ClearError();
    return 0;
}

uint32_t GetAnimationClipCount(ModelAnimationHandle handle)
{
    auto* asset = model::FindAnimation(handle);
    return asset && asset->source ? asset->source->ClipCount() : 0;
}
const char* GetAnimationClipName(ModelAnimationHandle handle, uint32_t clip)
{
    auto* asset = model::FindAnimation(handle);
    return asset && asset->source ? asset->source->ClipName(clip) : nullptr;
}
double GetAnimationClipDuration(ModelAnimationHandle handle, uint32_t clip)
{
    auto* asset = model::FindAnimation(handle);
    return asset && asset->source ? asset->source->ClipDuration(clip) : -1.0;
}

uint32_t GetModelAnimationCount(ModelHandle handle)
{
    auto* asset = Embedded(handle);
    return asset && asset->source ? asset->source->ClipCount() : 0;
}
const char* GetModelAnimationName(ModelHandle handle, uint32_t clip)
{
    auto* asset = Embedded(handle);
    return asset && asset->source ? asset->source->ClipName(clip) : nullptr;
}
double GetModelAnimationDuration(ModelHandle handle, uint32_t clip)
{
    auto* asset = Embedded(handle);
    return asset && asset->source ? asset->source->ClipDuration(clip) : -1.0;
}
int PlayModelAnimation(ModelHandle handle, uint32_t clip, bool loop)
{
    return Bind(handle, Embedded(handle), clip, loop, 0, 0.0f);
}
int ApplyModelAnimation(ModelHandle handle, ModelAnimationHandle animation, uint32_t clip, bool loop)
{
    return Bind(handle, model::FindAnimation(animation), clip, loop, 0, 0.0f);
}
int SetModelAnimationBlend(ModelHandle handle, uint32_t clip, float weight)
{
    return Bind(handle, Embedded(handle), clip, true, 1, weight);
}
int SetModelAnimationBlend(ModelHandle handle, ModelAnimationHandle animation, uint32_t clip, float weight)
{
    return Bind(handle, model::FindAnimation(animation), clip, true, 1, weight);
}

int SetModelAnimationBlendWeight(ModelHandle handle, float weight)
{
    auto* playback = Playback(handle, false);
    if (!playback || !playback->clips[0].asset || !playback->clips[1].asset || !isfinite(weight) || weight < 0.0f || weight > 1.0f)
        return detail::SetError("blend slots are not configured or weight is invalid");
    playback->blendWeight = weight;
    detail::ClearError();
    return 0;
}

int StopModelAnimation(ModelHandle handle)
{
    auto* transform = Transform(handle);
    if (!transform)
        return -1;
    if (transform->playback)
    {
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto& state = transform->playback->clips[i];
            if (state.asset)
                Release(&state.asset->reference);
            state.asset = nullptr;
            state.bones.Clear();
            state.requestedRoles.Clear();
            state.mappedRoles.Clear();
            state.morphs.Clear();
            state.sourceRestModelMatrices.Clear();
            state.targetRestModelMatrices.Clear();
            state.sourceRestWorldRotations.Clear();
            state.targetRestWorldRotations.Clear();
            state.humanoidTranslationScale = 0.0;
        }
        transform->playback->blendWeight = 0.0f;
    }
    detail::ClearError();
    return 0;
}

int SetModelAnimationTime(ModelHandle handle, double seconds, uint32_t slot)
{
    auto* state = Clip(handle, slot);
    double time = 0.0;
    if (!state)
        return -1;
    if (!Clock(*state, seconds, time))
        return detail::SetError("invalid animation time or duration");
    state->seconds = time;
    detail::ClearError();
    return 0;
}

double GetModelAnimationTime(ModelHandle handle, uint32_t slot)
{
    auto* state = Clip(handle, slot);
    return state ? state->seconds : -1.0;
}

int SetModelAnimationSpeed(ModelHandle handle, double speed, uint32_t slot)
{
    auto* state = Clip(handle, slot);
    if (!state)
        return -1;
    if (!isfinite(speed))
        return detail::SetError("animation speed must be finite");
    state->speed = speed;
    detail::ClearError();
    return 0;
}

int SetModelAnimationLoop(ModelHandle handle, bool loop, uint32_t slot)
{
    auto* state = Clip(handle, slot);
    if (!state)
        return -1;
    state->loop = loop;
    double time = 0.0;
    if (!Clock(*state, state->seconds, time))
        return detail::SetError("invalid animation duration");
    state->seconds = time;
    detail::ClearError();
    return 0;
}

int UpdateModelAnimation(ModelHandle handle, double delta)
{
    auto* playback = Playback(handle, false);
    if (!playback || !playback->clips[0].asset || !isfinite(delta) || delta < 0.0)
        return detail::SetError("animation is not playing or delta time is invalid");
    double times[2]{};
    for (uint32_t i = 0; i < 2; ++i)
    {
        const auto& state = playback->clips[i];
        if (state.asset && !Clock(state, state.seconds + delta * state.speed, times[i]))
            return detail::SetError("animation clock exceeds its finite range");
    }
    for (uint32_t i = 0; i < 2; ++i)
        if (playback->clips[i].asset)
            playback->clips[i].seconds = times[i];
    detail::ClearError();
    return 0;
}

uint32_t GetModelBoneCount(ModelHandle handle)
{
    auto* asset = Embedded(handle);
    return asset && asset->source ? asset->source->Skeleton().parents.Count() : 0;
}
const char* GetModelBoneName(ModelHandle handle, uint32_t bone)
{
    auto* asset = Embedded(handle);
    return asset && asset->source ? asset->source->BoneName(bone) : nullptr;
}

int GetModelBonePosition(ModelHandle handle, uint32_t bone, Vec3& output)
{
    // 所有元の骨格とこのinstanceの状態を借り、照会では状態を作らない。
    auto* resource = detail::FindModel(handle);
    auto* transform = Transform(handle);
    auto* asset = resource ? resource->animation : nullptr;
    if (!transform || !asset || !asset->source || bone >= asset->source->Skeleton().parents.Count())
    {
        return detail::SetError("invalid model bone or missing skeleton");
    }
    // 描画と共通の順序で姿勢を評価し、頂点変形を省いて骨の原点だけを読む。
    model::animation::FModelPose pose;
    String error;
    const auto& skeleton = asset->source->Skeleton();
    const bool evaluated = transform->playback ? model::EvaluateModelPlaybackPose(*resource, *transform->playback, pose, error) : model::animation::InitializeModelPose(skeleton, pose, error);
    Array<float> matrices;
    if (!evaluated || !model::animation::EvaluateModelPose(skeleton, pose, matrices, error))
    {
        return Failure(error);
    }
    // 全処理の成功後に、呼び出し側の出力位置を置き換える。
    const uint32_t offset = bone * 16u + 12u;
    const Vec3 candidate{ matrices.At(offset), matrices.At(offset + 1u), matrices.At(offset + 2u) };
    if (!detail::IsFinite(candidate))
    {
        return detail::SetError("model bone position is not finite");
    }
    output = candidate;
    detail::ClearError();
    return 0;
}

int32_t FindModelBone(ModelHandle handle, const char* name)
{
    if (!name || !*name)
    {
        detail::SetError("bone name is empty");
        return -1;
    }
    int32_t found = -1;
    for (uint32_t i = 0; i < GetModelBoneCount(handle); ++i)
    {
        const char* candidate = GetModelBoneName(handle, i);
        if (candidate && strcmp(candidate, name) == 0)
        {
            if (found >= 0)
            {
                detail::SetError("bone name is ambiguous");
                return -1;
            }
            found = static_cast<int32_t>(i);
        }
    }
    if (found < 0)
        detail::SetError("bone name was not found");
    else
        detail::ClearError();
    return found;
}

uint32_t GetAnimationBoneCount(ModelAnimationHandle handle)
{
    auto* asset = model::FindAnimation(handle);
    return asset && asset->source ? asset->source->Skeleton().parents.Count() : 0;
}
const char* GetAnimationBoneName(ModelAnimationHandle handle, uint32_t bone)
{
    auto* asset = model::FindAnimation(handle);
    return asset && asset->source ? asset->source->BoneName(bone) : nullptr;
}

int SetModelBoneRole(ModelHandle handle, uint32_t bone, EHumanoidBone role)
{
    auto* playback = Playback(handle, true);
    if (!playback)
        return -1;
    String error;
    if (!SetRole(playback->roles, GetModelBoneCount(handle), bone, role, error))
        return Failure(error);
    detail::ClearError();
    return 0;
}

int SetAnimationBoneRole(ModelAnimationHandle handle, uint32_t bone, EHumanoidBone role)
{
    auto* asset = model::FindAnimation(handle);
    if (!asset)
        return detail::SetError("invalid animation handle");
    String error;
    if (!SetRole(asset->roles, GetAnimationBoneCount(handle), bone, role, error))
        return Failure(error);
    detail::ClearError();
    return 0;
}

int SetModelHumanoidBoneMap(ModelHandle handle, const char* path)
{
    auto* asset = Embedded(handle);
    if (!asset || !asset->source)
    {
        return detail::SetError("model has no skeleton");
    }
    // 対応表が完成してからinstanceへ反映し、失敗時は既存設定を保つ。
    Array<uint16_t> candidate;
    String error;
    if (!model::LoadHumanoidBoneMap(path, *asset->source, candidate, error))
    {
        return Failure(error);
    }
    auto* playback = Playback(handle, true);
    if (!playback)
    {
        return -1;
    }
    playback->roles.MoveFrom(candidate);
    detail::ClearError();
    return 0;
}

int SetAnimationHumanoidBoneMap(ModelAnimationHandle handle, const char* path)
{
    auto* asset = model::FindAnimation(handle);
    if (!asset || !asset->source)
    {
        return detail::SetError("invalid animation handle");
    }
    // 適用済みclipの対応表は維持し、次回の登録に使う役割だけを更新する。
    Array<uint16_t> candidate;
    String error;
    if (!model::LoadHumanoidBoneMap(path, *asset->source, candidate, error))
    {
        return Failure(error);
    }
    asset->roles.MoveFrom(candidate);
    detail::ClearError();
    return 0;
}

int SetModelTwoBoneIk(ModelHandle handle, uint32_t root, uint32_t middle, uint32_t end, Vec3 target, Vec3 pole, float weight)
{
    const uint32_t bones[3] = { root, middle, end };
    return StoreIk(handle, bones, 3, target, pole, weight, true);
}

int SetModelHumanoidTwoBoneIk(ModelHandle handle, EHumanoidBone root, EHumanoidBone middle, EHumanoidBone end, Vec3 target, Vec3 pole, float weight)
{
    // 設定済みの役割だけを借用し、解決に失敗しても再生状態を作らない。
    auto* asset = Embedded(handle);
    auto* playback = Playback(handle, false);
    if (!asset || !asset->source || !playback || playback->roles.Count() != asset->source->Skeleton().parents.Count())
    {
        return detail::SetError("model humanoid roles are not configured");
    }
    // 3つの役割を骨番号へ確定してから、既存のIK検証へ渡す。
    const EHumanoidBone roles[3] = { root, middle, end };
    uint32_t bones[3]{};
    for (uint32_t index = 0; index < 3; ++index)
    {
        if (roles[index] == EHumanoidBone::None || roles[index] >= EHumanoidBone::Count)
        {
            return detail::SetError("invalid humanoid IK role");
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            if (roles[index] == roles[previous])
            {
                return detail::SetError("humanoid IK roles must be distinct");
            }
        }
        bool found = false;
        for (uint32_t bone = 0; bone < playback->roles.Count(); ++bone)
        {
            if (playback->roles.At(bone) == static_cast<uint16_t>(roles[index]))
            {
                bones[index] = bone;
                found = true;
                break;
            }
        }
        if (!found)
        {
            return detail::SetError("requested humanoid IK role is not assigned");
        }
    }
    return StoreIk(handle, bones, 3, target, pole, weight, true);
}

/**
 * 推定結果の空振りを検査し、成功後だけ役割配列を置き換える。
 */
static int InferRoles(const model::AModelAnimationSource& source, Array<uint16_t>& roles)
{
    String error;
    Array<uint16_t> candidate;
    if (!model::InferHumanoidBoneRoles(source, roles, candidate, error))
        return Failure(error);
    uint32_t recognized = 0;
    for (uint32_t i = 0; i < candidate.Count(); ++i)
        if (candidate.At(i) != 0)
            ++recognized;
    if (recognized == 0)
        return detail::SetError("no recognized humanoid bone names; set bone roles manually");
    roles.MoveFrom(candidate);
    detail::ClearError();
    return 0;
}

int AutoMapModelHumanoidBones(ModelHandle handle)
{
    auto* asset = Embedded(handle);
    if (!asset || !asset->source)
        return detail::SetError("model has no skeleton");
    auto* playback = Playback(handle, true);
    if (!playback)
        return -1;
    return InferRoles(*asset->source, playback->roles);
}

int AutoMapAnimationHumanoidBones(ModelAnimationHandle handle)
{
    auto* asset = model::FindAnimation(handle);
    if (!asset || !asset->source)
        return detail::SetError("invalid animation handle");
    return InferRoles(*asset->source, asset->roles);
}

EHumanoidBone GetModelBoneRole(ModelHandle handle, uint32_t bone)
{
    auto* playback = Playback(handle, false);
    return playback && bone < playback->roles.Count() ? static_cast<EHumanoidBone>(playback->roles.At(bone)) : EHumanoidBone::None;
}

EHumanoidBone GetAnimationBoneRole(ModelAnimationHandle handle, uint32_t bone)
{
    auto* asset = model::FindAnimation(handle);
    return asset && bone < asset->roles.Count() ? static_cast<EHumanoidBone>(asset->roles.At(bone)) : EHumanoidBone::None;
}

int32_t GetModelAnimationSourceBone(ModelHandle handle, uint32_t targetBone, uint32_t slot)
{
    auto* state = Clip(handle, slot);
    return state && targetBone < state->bones.Count() ? state->bones.At(targetBone) : -1;
}

int GetModelAnimationMappingInfo(ModelHandle handle, FModelAnimationMappingInfo& output, uint32_t slot)
{
    auto* state = Clip(handle, slot);
    if (!state || !state->asset || !state->asset->source)
    {
        return detail::SetError("animation is not playing or slot is invalid");
    }
    if (state->requestedRoles.Count() != state->bones.Count() || state->mappedRoles.Count() != state->bones.Count())
    {
        return detail::SetError("animation mapping snapshot count mismatch");
    }
    // 保存した対応だけを数え、照会失敗時には呼び出し側の値を保つ。
    FModelAnimationMappingInfo candidate;
    candidate.targetBoneCount = state->bones.Count();
    const uint32_t sourceBoneCount = state->asset->source->Skeleton().parents.Count();
    for (uint32_t bone = 0; bone < candidate.targetBoneCount; ++bone)
    {
        const int32_t sourceBone = state->bones.At(bone);
        const uint16_t requested = state->requestedRoles.At(bone);
        if (sourceBone < -1 || (sourceBone >= 0 && static_cast<uint32_t>(sourceBone) >= sourceBoneCount) || requested >= static_cast<uint16_t>(EHumanoidBone::Count))
        {
            return detail::SetError("animation mapping snapshot is invalid");
        }
        if (sourceBone >= 0)
        {
            ++candidate.mappedBoneCount;
        }
        if (requested != 0)
        {
            ++candidate.humanoidBoneCount;
            if (sourceBone >= 0 && state->mappedRoles.At(bone) == requested)
            {
                ++candidate.mappedHumanoidBoneCount;
            }
        }
    }
    output = candidate;
    detail::ClearError();
    return 0;
}

EHumanoidBone GetModelAnimationMissingHumanoidRole(ModelHandle handle, uint32_t index, uint32_t slot)
{
    FModelAnimationMappingInfo info;
    if (GetModelAnimationMappingInfo(handle, info, slot) != 0)
    {
        return EHumanoidBone::None;
    }
    auto* state = Clip(handle, slot);
    for (uint32_t bone = 0; bone < info.targetBoneCount; ++bone)
    {
        const uint16_t requested = state->requestedRoles.At(bone);
        if (requested != 0 && (state->bones.At(bone) < 0 || state->mappedRoles.At(bone) != requested))
        {
            if (index == 0)
            {
                return static_cast<EHumanoidBone>(requested);
            }
            --index;
        }
    }
    return EHumanoidBone::None;
}

int SetModelIkChain(ModelHandle handle, const uint32_t* bones, uint32_t count, Vec3 target, float weight)
{
    return StoreIk(handle, bones, count, target, {}, weight, false);
}

int ClearModelIk(ModelHandle handle)
{
    auto* transform = Transform(handle);
    if (!transform)
        return -1;
    if (transform->playback)
    {
        transform->playback->ik.Clear();
        transform->playback->ikBones.Clear();
    }
    detail::ClearError();
    return 0;
}
}
