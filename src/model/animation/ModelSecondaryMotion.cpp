// SPDX-License-Identifier: NOASSERTION
#include <gkcore/ModelSecondaryMotion.h>
#include "model/animation/ModelSecondaryMotion.h"
#include "model/animation/ModelSecondaryMotionSolver.h"
#include "model/animation/ModelSecondaryMotionColliders.h"
#include "model/animation/ModelSnapshot.h"
#include "model/animation/ModelIk.h"
#include "model/animation/AModelAnimationSource.h"
#include "core/Context.h"
#include <math.h>
#include "foundation/FVector3d.h"
#include "model/animation/ModelSecondaryMotionCollision.h"
#include <string.h>

namespace gk::model
{
bool ApplyModelSecondaryMotion(const FModelSecondaryMotionState& state, animation::FModelPose& pose, String& error)
{
    // 全鎖を検査してから回転だけを反映し、失敗時は元の姿勢を保つ。
    for (uint32_t chainIndex = 0; chainIndex < state.chains.Count(); ++chainIndex)
    {
        const auto* chain = state.chains.At(chainIndex);
        if (!chain || chain->bones.Count() != chain->transforms.Count())
        {
            error.Assign("secondary motion rotation cache is invalid");
            return false;
        }
        for (uint32_t item = 0; item < chain->bones.Count(); ++item)
        {
            if (chain->bones.At(item) >= pose.localTransforms.Count())
            {
                error.Assign("secondary motion bone is outside the pose");
                return false;
            }
        }
    }
    for (uint32_t chainIndex = 0; chainIndex < state.chains.Count(); ++chainIndex)
    {
        const auto& chain = *state.chains.At(chainIndex);
        for (uint32_t item = 0; item < chain.bones.Count(); ++item)
        {
            memcpy(pose.localTransforms.At(chain.bones.At(item)).rotation, chain.transforms.At(item).rotation, sizeof(float) * 4u);
        }
    }
    error.Clear();
    return true;
}
}

namespace gk
{
namespace
{
/**
 * 有効な骨格resourceとinstanceを借りる。失敗時は状態を作らない。
 */
bool Instance(ModelHandle handle, detail::ModelResource*& resource, detail::ModelTransform*& transform)
{
    resource = detail::FindModel(handle);
    transform = resource ? detail::FindModelTransform(handle) : nullptr;
    if (!transform || !resource->animation || !resource->animation->source || resource->animation->source->Format() == model::EModelAnimationFormat::ObjSequence)
    {
        detail::SetError("secondary motion requires a valid skeletal model instance");
        return false;
    }
    return true;
}

/**
 * エラーを公開APIの診断へ渡す。
 */
int Failure(const String& error)
{
    return detail::SetError(error.Empty() ? "secondary motion operation failed" : error.CStr());
}

/**
 * 1本の鎖を別の所有先へ複製する。元の状態を変更しない。
 */
model::FModelSecondaryMotionChain* CloneChain(const model::FModelSecondaryMotionChain* source, String& error)
{
    model::FModelSecondaryMotionChain* result = nullptr;
    try
    {
        result = new model::FModelSecondaryMotionChain;
    }
    catch (...)
    {
        error.Assign("secondary motion chain allocation failed");
        return nullptr;
    }
    if (source)
    {
        result->settings = source->settings;
        if (!result->bones.AppendRange(source->bones.Data(), source->bones.Count()) || !result->transforms.AppendRange(source->transforms.Data(), source->transforms.Count()) || !result->simulation.points.AppendRange(source->simulation.points.Data(), source->simulation.points.Count()) || !result->simulation.previousTargets.AppendRange(source->simulation.previousTargets.Data(), source->simulation.previousTargets.Count()))
        {
            delete result;
            error.Assign("secondary motion state allocation failed");
            return nullptr;
        }
    }
    return result;
}

/**
 * 候補の鎖を所有配列へ渡す。失敗時は鎖を解放する。
 */
bool AppendChain(model::FModelSecondaryMotionState& state, model::FModelSecondaryMotionChain* chain, String& error)
{
    if (!chain)
    {
        return false;
    }
    if (!state.chains.Append(chain))
    {
        delete chain;
        error.Assign("secondary motion group allocation failed");
        return false;
    }
    return true;
}

/**
 * 検証が終わった候補をinstanceへ一括反映する。
 */
bool Commit(detail::ModelTransform& transform, model::FModelSecondaryMotionState& candidate, String& error)
{
    model::FModelSecondaryMotionState* state = nullptr;
    model::FModelPlayback* playback = transform.playback;
    try
    {
        state = new model::FModelSecondaryMotionState;
        if (!playback)
        {
            playback = new model::FModelPlayback;
        }
    }
    catch (...)
    {
        delete state;
        error.Assign("secondary motion owner allocation failed");
        return false;
    }
    state->chains.MoveFrom(candidate.chains);
    state->colliders.MoveFrom(candidate.colliders);
    state->previousCollisionShapes.MoveFrom(candidate.previousCollisionShapes);
    state->colliderMargin = candidate.colliderMargin;
    delete playback->secondaryMotion;
    playback->secondaryMotion = state;
    transform.playback = playback;
    return true;
}

/**
 * 接触設定と前回の身体配置を候補へコピーする。元の状態は変更しない。
 */
bool CopyCollisionSettings(const model::FModelSecondaryMotionState* previous, model::FModelSecondaryMotionState& candidate, String& error)
{
    if (!previous)
    {
        return true;
    }
    candidate.colliderMargin = previous->colliderMargin;
    if (!candidate.colliders.AppendRange(previous->colliders.Data(), previous->colliders.Count()) || !candidate.previousCollisionShapes.AppendRange(previous->previousCollisionShapes.Data(), previous->previousCollisionShapes.Count()))
    {
        error.Assign("secondary motion collision state allocation failed");
        return false;
    }
    return true;
}

/**
 * 揺れを重ねる前の、現在のアニメーション姿勢を評価する。
 */
bool BasePose(const detail::ModelResource& resource, const detail::ModelTransform& transform, model::animation::FModelPose& pose, Array<float>& matrices, String& error)
{
    const auto& skeleton = resource.animation->source->Skeleton();
    const bool evaluated = transform.playback ? model::EvaluateModelPlaybackBasePose(resource, *transform.playback, pose, error) : model::animation::InitializeModelPose(skeleton, pose, error);
    return evaluated && model::animation::EvaluateModelPose(skeleton, pose, matrices, error);
}

/**
 * bone原点の列へ、最後のboneから延ばしたローカル末端を足す。
 */
bool Targets(const model::FModelSecondaryMotionChain& chain, const Array<float>& matrices, Array<Vec3>& output, String& error)
{
    for (uint32_t item = 0; item < chain.bones.Count(); ++item)
    {
        const uint32_t offset = chain.bones.At(item) * 16u;
        if (!output.Append(Vec3{ matrices.At(offset + 12), matrices.At(offset + 13), matrices.At(offset + 14) }))
        {
            error.Assign("secondary motion target allocation failed");
            return false;
        }
    }
    const uint32_t offset = chain.bones.At(chain.bones.Count() - 1) * 16u;
    const auto tip = chain.settings.endOffset;
    const Vec3 endpoint{ matrices.At(offset + 12) + matrices.At(offset) * tip.x + matrices.At(offset + 4) * tip.y + matrices.At(offset + 8) * tip.z, matrices.At(offset + 13) + matrices.At(offset + 1) * tip.x + matrices.At(offset + 5) * tip.y + matrices.At(offset + 9) * tip.z, matrices.At(offset + 14) + matrices.At(offset + 2) * tip.x + matrices.At(offset + 6) * tip.y + matrices.At(offset + 10) * tip.z };
    if (!output.Append(endpoint))
    {
        error.Assign("secondary motion tip allocation failed");
        return false;
    }
    return true;
}

/**
 * 鎖の祖先関係を調べる。入力骨格は事前にFKで検証しておく。
 */
bool IsAncestor(const model::animation::FModelSkeleton& skeleton, uint32_t ancestor, uint32_t descendant)
{
    for (int32_t bone = static_cast<int32_t>(descendant); bone >= 0; bone = skeleton.parents.At(static_cast<uint32_t>(bone)))
    {
        if (static_cast<uint32_t>(bone) == ancestor)
        {
            return true;
        }
    }
    return false;
}

/**
 * 指定鎖の回転だけを、更新済みの姿勢から保存する。
 */
bool CacheTransforms(model::FModelSecondaryMotionChain& chain, const model::animation::FModelPose& pose, String& error)
{
    chain.transforms.Clear();
    for (uint32_t item = 0; item < chain.bones.Count(); ++item)
    {
        if (!chain.transforms.Append(pose.localTransforms.At(chain.bones.At(item))))
        {
            error.Assign("secondary motion rotation allocation failed");
            return false;
        }
    }
    return true;
}

/**
 * 全鎖を候補側で更新し、1本でも失敗したら前回の状態を保つ。
 */
int Update(ModelHandle handle, double delta, bool reset)
{
    detail::ModelResource* resource = nullptr;
    detail::ModelTransform* transform = nullptr;
    if (!Instance(handle, resource, transform))
    {
        return -1;
    }
    if (!isfinite(delta) || delta < 0.0)
    {
        return detail::SetError("secondary motion delta must be finite and nonnegative");
    }
    const auto* previous = transform->playback ? transform->playback->secondaryMotion : nullptr;
    if (!previous)
    {
        detail::ClearError();
        return 0;
    }
    String error;
    model::animation::FModelPose base;
    Array<float> matrices;
    if (!BasePose(*resource, *transform, base, matrices, error))
    {
        return Failure(error);
    }
    model::FModelSecondaryMotionState candidate;
    // 全鎖で同じ身体姿勢を使い、途中の揺れを身体の位置へ戻さない。
    Array<model::animation::FModelSecondaryMotionCollisionShape> currentShapes;
    if (!CopyCollisionSettings(previous, candidate, error) || !model::EvaluateSecondaryMotionColliders(resource->animation->source->Skeleton(), base, matrices, previous->colliders.Data(), previous->colliders.Count(), previous->colliderMargin, currentShapes, error) || previous->previousCollisionShapes.Count() != currentShapes.Count())
    {
        return Failure(error);
    }
    for (uint32_t index = 0; index < previous->chains.Count(); ++index)
    {
        if (!AppendChain(candidate, CloneChain(previous->chains.At(index), error), error))
        {
            return Failure(error);
        }
        auto& chain = *candidate.chains.At(index);
        Array<Vec3> targets;
        if (!Targets(chain, matrices, targets, error))
        {
            return Failure(error);
        }
        if (reset)
        {
            if (!model::animation::ResetSecondaryMotionChain(targets.Data(), targets.Count(), chain.simulation, error) || !CacheTransforms(chain, base, error))
            {
                return Failure(error);
            }
        }
        else
        {
            const bool advanced = currentShapes.Count() > 0 ? model::animation::StepSecondaryMotionChainWithCollisions(targets.Data(), targets.Count(), chain.settings, delta, previous->previousCollisionShapes.Data(), currentShapes.Data(), currentShapes.Count(), chain.simulation, error) : model::animation::StepSecondaryMotionChain(targets.Data(), targets.Count(), chain.settings, delta, chain.simulation, error);
            if (!advanced)
            {
                // 設定を調整できるよう、接触を解けなかった鎖の先頭boneを示す。
                error.Append(" (chain root: ");
                error.AppendUnsigned(chain.bones.At(0));
                error.Append(")");
                return Failure(error);
            }
            Array<Vec3> points;
            for (uint32_t point = 0; point < chain.simulation.points.Count(); ++point)
            {
                if (!points.Append(chain.simulation.points.At(point).position))
                {
                    return detail::SetError("secondary motion point allocation failed");
                }
            }
            model::animation::FModelPose oriented;
            if (!model::animation::OrientModelPoseChain(resource->animation->source->Skeleton(), base, chain.bones.Data(), chain.bones.Count(), chain.settings.endOffset, points.Data(), oriented, error) || !CacheTransforms(chain, oriented, error))
            {
                return Failure(error);
            }
        }
    }
    if (!reset && currentShapes.Count() > 0)
    {
        // 描画へ渡す回転を全鎖へ重ね、実際のFK位置でも接触が成立することを確認する。
        model::animation::FModelPose combined;
        Array<float> renderedMatrices;
        if (!combined.localTransforms.AppendRange(base.localTransforms.Data(), base.localTransforms.Count()) || !combined.morphWeights.AppendRange(base.morphWeights.Data(), base.morphWeights.Count()) || !model::ApplyModelSecondaryMotion(candidate, combined, error) || !model::animation::EvaluateModelPose(resource->animation->source->Skeleton(), combined, renderedMatrices, error))
        {
            return Failure(error);
        }
        for (uint32_t index = 0; index < candidate.chains.Count(); ++index)
        {
            Array<Vec3> renderedTargets;
            Array<FVector3d> renderedPoints;
            if (!Targets(*candidate.chains.At(index), renderedMatrices, renderedTargets, error))
            {
                return Failure(error);
            }
            for (uint32_t point = 0; point < renderedTargets.Count(); ++point)
            {
                const auto position = renderedTargets.At(point);
                if (!renderedPoints.Append(FVector3d{ { position.x, position.y, position.z } }))
                {
                    return detail::SetError("secondary motion rendered contact allocation failed");
                }
            }
            if (!model::animation::CheckSecondaryMotionContacts(currentShapes.Data(), currentShapes.Count(), renderedPoints.Data(), renderedPoints.Count(), error))
            {
                // 回転を姿勢へ戻した結果で失敗した鎖も、先頭boneで識別する。
                error.Append(" (rendered chain root: ");
                error.AppendUnsigned(candidate.chains.At(index)->bones.At(0));
                error.Append(")");
                return Failure(error);
            }
        }
    }
    candidate.previousCollisionShapes.MoveFrom(currentShapes);
    if (!Commit(*transform, candidate, error))
    {
        return Failure(error);
    }
    detail::ClearError();
    return 0;
}
}

int SetModelSecondaryMotionChain(ModelHandle handle, const uint32_t* bones, uint32_t count, const FModelSecondaryMotionSettings& settings)
{
    detail::ModelResource* resource = nullptr;
    detail::ModelTransform* transform = nullptr;
    if (!Instance(handle, resource, transform))
    {
        return -1;
    }
    const auto& skeleton = resource->animation->source->Skeleton();
    if (!bones || count == 0 || count > 1024 || count > skeleton.parents.Count())
    {
        return detail::SetError("secondary motion bone count is invalid");
    }
    for (uint32_t item = 0; item < count; ++item)
    {
        if (bones[item] >= skeleton.parents.Count() || !resource->animation->source->BoneWritable(bones[item]) || (item > 0 && skeleton.parents.At(bones[item]) != static_cast<int32_t>(bones[item - 1])))
        {
            return detail::SetError("secondary motion bones must be writable and continuous");
        }
    }
    String error;
    model::animation::FModelPose base;
    Array<float> matrices;
    if (!BasePose(*resource, *transform, base, matrices, error))
    {
        return Failure(error);
    }
    const auto* previous = transform->playback ? transform->playback->secondaryMotion : nullptr;
    model::FModelSecondaryMotionState candidate;
    if (!CopyCollisionSettings(previous, candidate, error))
    {
        return Failure(error);
    }
    for (uint32_t index = 0; index < candidate.colliders.Count(); ++index)
    {
        if (IsAncestor(skeleton, bones[0], candidate.colliders.At(index).bone))
        {
            return detail::SetError("secondary motion cannot control a collision attachment or its ancestor");
        }
    }
    if (previous)
    {
        for (uint32_t index = 0; index < previous->chains.Count(); ++index)
        {
            const auto& old = *previous->chains.At(index);
            if (old.bones.At(0) == bones[0])
            {
                continue;
            }
            if (IsAncestor(skeleton, old.bones.At(0), bones[0]) || IsAncestor(skeleton, bones[0], old.bones.At(0)))
            {
                return detail::SetError("secondary motion chains must use independent branches");
            }
            if (!AppendChain(candidate, CloneChain(&old, error), error))
            {
                return Failure(error);
            }
        }
    }
    if (!AppendChain(candidate, CloneChain(nullptr, error), error))
    {
        return Failure(error);
    }
    auto& chain = *candidate.chains.At(candidate.chains.Count() - 1);
    chain.settings = settings;
    if (!chain.bones.AppendRange(bones, count))
    {
        return detail::SetError("secondary motion bone allocation failed");
    }
    Array<Vec3> targets;
    model::animation::FModelPose validated;
    if (!Targets(chain, matrices, targets, error) || !model::animation::ResetSecondaryMotionChain(targets.Data(), targets.Count(), chain.simulation, error) || !model::animation::StepSecondaryMotionChain(targets.Data(), targets.Count(), settings, 0.0, chain.simulation, error) || !model::animation::OrientModelPoseChain(skeleton, base, bones, count, settings.endOffset, targets.Data(), validated, error) || !CacheTransforms(chain, base, error) || !Commit(*transform, candidate, error))
    {
        return Failure(error);
    }
    detail::ClearError();
    return 0;
}

int UpdateModelSecondaryMotion(ModelHandle handle, double delta)
{
    return Update(handle, delta, false);
}

int ResetModelSecondaryMotion(ModelHandle handle)
{
    return Update(handle, 0.0, true);
}

int ClearModelSecondaryMotion(ModelHandle handle)
{
    // 解放は骨格形式によらず、有効なmodel instanceなら呼べる。
    auto* transform = detail::FindModel(handle) ? detail::FindModelTransform(handle) : nullptr;
    if (!transform)
    {
        return detail::SetError("invalid model handle");
    }
    if (transform->playback)
    {
        delete transform->playback->secondaryMotion;
        transform->playback->secondaryMotion = nullptr;
    }
    detail::ClearError();
    return 0;
}
int SetModelSecondaryMotionColliders(ModelHandle handle, const FModelSecondaryMotionCollider* colliders, uint32_t count, float margin)
{
    // 解除は骨格を持たないモデルにも行える。無効handleでは状態を作らない。
    auto* resource = detail::FindModel(handle);
    auto* transform = resource ? detail::FindModelTransform(handle) : nullptr;
    if (!transform || count > 64 || (count > 0 && !colliders) || !isfinite(margin) || margin < 0.0f)
    {
        return detail::SetError("invalid secondary motion collider setup");
    }
    const auto* previous = transform->playback ? transform->playback->secondaryMotion : nullptr;
    if (!previous)
    {
        if (count == 0)
        {
            detail::ClearError();
            return 0;
        }
        return detail::SetError("configure secondary motion chains before collision shapes");
    }
    String error;
    model::FModelSecondaryMotionState candidate;
    for (uint32_t index = 0; index < previous->chains.Count(); ++index)
    {
        if (!AppendChain(candidate, CloneChain(previous->chains.At(index), error), error))
        {
            return Failure(error);
        }
    }
    candidate.colliderMargin = margin;
    if (count > 0)
    {
        // 新しい形状の取付先とscaleを、現在のclip・blend・IK姿勢で検証する。
        model::animation::FModelPose base;
        Array<float> matrices;
        if (!resource->animation || !resource->animation->source || !BasePose(*resource, *transform, base, matrices, error))
        {
            return Failure(error);
        }
        const auto& skeleton = resource->animation->source->Skeleton();
        for (uint32_t shape = 0; shape < count; ++shape)
        {
            if (colliders[shape].bone >= skeleton.parents.Count())
            {
                return detail::SetError("invalid secondary motion collision attachment bone");
            }
            for (uint32_t chain = 0; chain < previous->chains.Count(); ++chain)
            {
                if (IsAncestor(skeleton, previous->chains.At(chain)->bones.At(0), colliders[shape].bone))
                {
                    return detail::SetError("collision attachment must be outside secondary motion branches");
                }
            }
        }
        if (!candidate.colliders.AppendRange(colliders, count) || !model::EvaluateSecondaryMotionColliders(skeleton, base, matrices, colliders, count, margin, candidate.previousCollisionShapes, error))
        {
            return Failure(error);
        }
    }
    if (!Commit(*transform, candidate, error))
    {
        return Failure(error);
    }
    detail::ClearError();
    return 0;
}
}
