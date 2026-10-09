// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELPLAYBACK_H
#define GKCORE_MODEL_ANIMATION_FMODELPLAYBACK_H

#include "model/animation/FModelClipState.h"
#include "model/animation/FModelIkCommand.h"

/**
 * model animationの内部状態型。
 */
namespace gk::model
{
/**
 * 1モデルinstanceの再生枠、ブレンド、ボーンの役割、IKを所有する。
 */
struct FModelPlayback
{
    // 主clipとブレンド相手の独立した時刻。
    FModelClipState clips[2];
    // 2番目のclipの寄与率。
    float blendWeight = 0.0f;
    // このinstanceで割り当てた人型ボーンの役割。
    Array<uint16_t> roles;
    // 順に適用するIK。
    Array<FModelIkCommand> ik;
    // IKが参照する、所有済みのボーン列。
    Array<uint32_t> ikBones;

    /**
     * 再生枠が保持する元データを解放する。
     */
    ~FModelPlayback();
};
}

#endif
