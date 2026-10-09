// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODELCLIPSTATE_H
#define GKCORE_MODEL_ANIMATION_FMODELCLIPSTATE_H

#include "model/animation/FModelAnimationAsset.h"
#include "foundation/Array.h"

/**
 * 個別clipの再生情報を持つ内部型。
 */
namespace gk::model
{
/**
 * 1再生枠のclip、時刻、適用先への対応表を保持する。
 */
struct FModelClipState
{
    // この枠が保持する元データ。
    FModelAnimationAsset* asset = nullptr;
    // 元データ内のclip番号。
    uint32_t clip = 0;
    // clip先頭からの秒。
    double seconds = 0.0;
    // 1秒の経過に対する倍率。
    double speed = 1.0;
    // 両端を循環して再生するか。
    bool loop = true;
    // 対象ボーンごとのsourceボーン番号。未対応は-1。
    Array<int32_t> bones;
    // 役割で対応付けたボーンの役割値。名前による対応は0。
    Array<uint16_t> mappedRoles;
    // 対象morphごとのsource番号。未対応は-1。
    Array<int32_t> morphs;
    // 外部clipを結ぶ時に固定されるsource rest poseのmodel行列。
    Array<float> sourceRestModelMatrices;
    // 外部clipを結ぶ時に固定されるtarget rest poseのmodel行列。
    Array<float> targetRestModelMatrices;
    // source rest poseの親階層回転を4成分ずつ保持する。
    Array<float> sourceRestWorldRotations;
    // target rest poseの親階層回転を4成分ずつ保持する。
    Array<float> targetRestWorldRotations;
};
}

#endif
