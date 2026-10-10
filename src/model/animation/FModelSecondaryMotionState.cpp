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
}
