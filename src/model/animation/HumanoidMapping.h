// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_HUMANOIDMAPPING_H
#define GKCORE_MODEL_ANIMATION_HUMANOIDMAPPING_H

#include "model/animation/AModelAnimationSource.h"
#include <stdint.h>

namespace gk::model
{

/**
 * 骨名と親子関係から人型の役割を推定する。既存指定は維持し、曖昧さや重複時は出力を変更しない。
 */
bool InferHumanoidBoneRoles(const AModelAnimationSource& source, const gk::Array<uint16_t>& existing, gk::Array<uint16_t>& output, gk::String& error);

}

#endif
