// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_MODELANIMATIONBINDING_H
#define GKCORE_MODEL_ANIMATION_MODELANIMATIONBINDING_H

#include "FModelClipState.h"
#include "AModelAnimationSource.h"

/**
 * 外部clipを対象modelへ対応付ける内部機能。
 */
namespace gk::model
{
/**
 * 名前・役割で外部clipの対応表を作る。candidateは成功後に元データを保持する。
 */
bool BuildClipBinding(FModelAnimationAsset& source, uint32_t clip, const FModelAnimationAsset* target, const Array<uint16_t>& targetRoles, FModelClipState& candidate, String& error);
/**
 * clipを評価し、対象骨格のrest姿勢を基準にボーン・morphを対応付ける。
 */
bool SampleBoundClip(const FModelClipState& state, const FModelAnimationAsset& target, animation::FModelPose& output, String& error);
}

#endif
