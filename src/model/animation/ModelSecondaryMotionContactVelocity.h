// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCONTACTVELOCITY_H
#define GKCORE_MODEL_ANIMATION_MODELSECONDARYMOTIONCONTACTVELOCITY_H

#include <stdint.h>
#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "foundation/String.h"
#include "model/animation/ESecondaryMotionContactVelocityResult.h"
#include "model/animation/FModelSecondaryMotionCollisionShape.h"
#include "model/animation/FSecondaryMotionContactChain.h"
#include "model/animation/FSecondaryMotionReferenceSegment.h"
#include "model/animation/FSecondaryMotionWeightedContactAnchor.h"
#include "model/animation/FSecondaryMotionWeightedContactPoint.h"

/**
 * 揺れものの鎖・接触制約を保存済み速度へ投影する。
 */
namespace gk::model::animation
{

/**
 * 鎖の長さ・角度、身体接触、重み付き点接触を有限回の投影で満たす。
 * Unresolvedは速度配列を変えず、Failedは入力不正・数値異常・確保失敗を示す。
 * attachmentMotionVelocitiesは鎖・attachment順に各始点、終点を並べる。
 */
ESecondaryMotionContactVelocityResult ProjectSecondaryMotionContactVelocities(const Array<FSecondaryMotionContactChain>& chains, const Array<FSecondaryMotionReferenceSegment>& references, const Array<FSecondaryMotionReferenceSegment>& previousReferences, const Array<uint64_t>& colliderMasks, const Array<FSecondaryMotionWeightedContactAnchor>& anchors, const Array<FVector3d>& anchorMotionVelocities, const Array<FVector3d>& attachmentMotionVelocities, const Array<FSecondaryMotionWeightedContactPoint>& contacts, const Array<FModelSecondaryMotionCollisionShape>& previousShapes, const FModelSecondaryMotionCollisionShape* currentShapes, uint32_t shapeCount, double delta, const Array<FVector3d>& positions, Array<Vec3>& velocities, String& error);

}

#endif
