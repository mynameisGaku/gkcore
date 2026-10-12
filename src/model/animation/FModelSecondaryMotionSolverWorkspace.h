// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSOLVERWORKSPACE_H
#define GKCORE_MODEL_ANIMATION_FMODELSECONDARYMOTIONSOLVERWORKSPACE_H

#include "foundation/Array.h"
#include "foundation/FVector3d.h"
#include "model/animation/FSecondaryMotionReferenceSegment.h"

/**
 * 1回のUpdate中に揺れもの鎖solverの作業配列容量を共有する。
 */
namespace gk::model::animation
{

/**
 * 鎖の計算中だけ使う配列の容量を保持し、各呼出しで要素数だけ空にする。
 */
class FModelSecondaryMotionSolverWorkspace
{
  public:
    /**
     * 前回の容量を保ったまま全scratch配列を空にする。
     */
    void Clear();

    // 倍精度位置として保持する候補。
    gk::Array<gk::FVector3d> positions;
    // 倍精度速度として保持する候補。
    gk::Array<gk::FVector3d> velocities;
    // substep開始時の位置を保持する候補。
    gk::Array<gk::FVector3d> oldPositions;
    // 現在substepのanimation目標を保持する候補。
    gk::Array<gk::FVector3d> stepTargets;
    // 制約反復で共有する目標線分。
    gk::Array<FSecondaryMotionReferenceSegment> referenceSegments;
};

}

#endif
