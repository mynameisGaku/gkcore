// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FModelSecondaryMotionState.h"

namespace gk::model
{
FModelSecondaryMotionState::~FModelSecondaryMotionState()
{
    for (uint32_t index = 0; index < chains.Count(); ++index)
    {
        delete chains.At(index);
    }
}

void FModelSecondaryMotionState::MoveFrom(FModelSecondaryMotionState& source)
{
    if (this == &source)
    {
        return;
    }
    for (uint32_t index = 0; index < chains.Count(); ++index)
    {
        delete chains.At(index);
    }
    chains.MoveFrom(source.chains);
    colliders.MoveFrom(source.colliders);
    links.MoveFrom(source.links);
    distanceConstraints.MoveFrom(source.distanceConstraints);
    contactLinks.MoveFrom(source.contactLinks);
    contactPoints.MoveFrom(source.contactPoints);
    contactAnchors.MoveFrom(source.contactAnchors);
    distanceLambdas.MoveFrom(source.distanceLambdas);
    distanceStepSeconds = source.distanceStepSeconds;
    linkIterations = source.linkIterations;
    previousCollisionShapes.MoveFrom(source.previousCollisionShapes);
    previousBaseMatrices.MoveFrom(source.previousBaseMatrices);
    previousBasePose.localTransforms.MoveFrom(source.previousBasePose.localTransforms);
    previousBasePose.morphWeights.MoveFrom(source.previousBasePose.morphWeights);
    velocityHistoryValid = source.velocityHistoryValid;
    colliderMargin = source.colliderMargin;
    skinPointContactsEnabled = source.skinPointContactsEnabled;
    skinContactPositionIds.MoveFrom(source.skinContactPositionIds);
    skinContactGeometry = source.skinContactGeometry;
    source.velocityHistoryValid = false;
    source.skinContactGeometry = nullptr;
}

bool FModelSecondaryMotionState::CloneFrom(const FModelSecondaryMotionState& source, String& error)
{
    // 自身を複製する場合も候補を完成させ、失敗時は所有状態を保つ。
    FModelSecondaryMotionState candidate;
    candidate.distanceStepSeconds = source.distanceStepSeconds;
    candidate.linkIterations = source.linkIterations;
    candidate.velocityHistoryValid = source.velocityHistoryValid;
    candidate.colliderMargin = source.colliderMargin;
    candidate.skinPointContactsEnabled = source.skinPointContactsEnabled;
    candidate.skinContactGeometry = source.skinContactGeometry;
    if (!candidate.colliders.AppendRange(source.colliders.Data(), source.colliders.Count()) || !candidate.links.AppendRange(source.links.Data(), source.links.Count()) || !candidate.distanceConstraints.AppendRange(source.distanceConstraints.Data(), source.distanceConstraints.Count()) || !candidate.contactLinks.AppendRange(source.contactLinks.Data(), source.contactLinks.Count()) || !candidate.contactPoints.AppendRange(source.contactPoints.Data(), source.contactPoints.Count()) || !candidate.contactAnchors.AppendRange(source.contactAnchors.Data(), source.contactAnchors.Count()) || !candidate.distanceLambdas.AppendRange(source.distanceLambdas.Data(), source.distanceLambdas.Count()) || !candidate.previousCollisionShapes.AppendRange(source.previousCollisionShapes.Data(), source.previousCollisionShapes.Count()) || !candidate.previousBaseMatrices.AppendRange(source.previousBaseMatrices.Data(), source.previousBaseMatrices.Count()) || !candidate.previousBasePose.localTransforms.AppendRange(source.previousBasePose.localTransforms.Data(), source.previousBasePose.localTransforms.Count()) || !candidate.previousBasePose.morphWeights.AppendRange(source.previousBasePose.morphWeights.Data(), source.previousBasePose.morphWeights.Count()) || !candidate.skinContactPositionIds.AppendRange(source.skinContactPositionIds.Data(), source.skinContactPositionIds.Count()) || !candidate.chains.Reserve(source.chains.Count()))
    {
        error.Assign("secondary motion state copy allocation failed");
        return false;
    }
    for (uint32_t index = 0; index < source.chains.Count(); ++index)
    {
        // 所有配列には、複製途中の鎖も直ちに登録して解放漏れを防ぐ。
        const auto* original = source.chains.At(index);
        if (!original || original->bones.Count() == 0u || original->transforms.Count() != original->bones.Count() || original->simulation.points.Count() != static_cast<uint64_t>(original->bones.Count()) + 1u || original->simulation.previousTargets.Count() != original->simulation.points.Count())
        {
            error.Assign("secondary motion chain copy source is invalid");
            return false;
        }
        FModelSecondaryMotionChain* chain = nullptr;
        try
        {
            chain = new FModelSecondaryMotionChain;
        }
        catch (...)
        {
            error.Assign("secondary motion chain copy allocation failed");
            return false;
        }
        if (!candidate.chains.Append(chain))
        {
            delete chain;
            error.Assign("secondary motion chain owner copy allocation failed");
            return false;
        }
        chain->settings = original->settings;
        chain->collisionRadius = original->collisionRadius;
        chain->colliderMask = original->colliderMask;
        if (!chain->bones.AppendRange(original->bones.Data(), original->bones.Count()) || !chain->transforms.AppendRange(original->transforms.Data(), original->transforms.Count()) || !chain->simulation.points.AppendRange(original->simulation.points.Data(), original->simulation.points.Count()) || !chain->simulation.previousTargets.AppendRange(original->simulation.previousTargets.Data(), original->simulation.previousTargets.Count()))
        {
            error.Assign("secondary motion chain state copy allocation failed");
            return false;
        }
    }
    MoveFrom(candidate);
    error.Clear();
    return true;
}
}
