// SPDX-License-Identifier: NOASSERTION
#include "model/animation/FModelSecondaryMotionSolverWorkspace.h"

namespace gk::model::animation
{

void FModelSecondaryMotionSolverWorkspace::Clear()
{
    positions.Clear();
    velocities.Clear();
    oldPositions.Clear();
    stepTargets.Clear();
    referenceSegments.Clear();
}

}
