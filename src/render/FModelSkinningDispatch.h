// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELSKINNINGDISPATCH_H
#define GKCORE_RENDER_FMODELSKINNINGDISPATCH_H

#include "render/FModelSkinningRecord.h"
#include <stdint.h>

namespace gk::render
{

/**
 * 1個のmodelをskinし、法線を作るGPU入力範囲を表す。
 * 静的geometryとframe別行列bufferの各offsetはrecord要素番号、countは要素数。行列は3行のdouble4、
 * normal face idは重複とufbx順を保持したincidence列を参照する。
 */
struct FModelSkinningDispatch
{
    uint32_t positionsOffset;         // bind pose位置recordの先頭。
    uint32_t positionsCount;          // bind pose位置の数。
    uint32_t influenceRangesOffset;   // positionごとのinfluence範囲。
    uint32_t influenceRangesCount;    // influence範囲recordの数。
    uint32_t influencesOffset;        // cluster番号とweight record列。
    uint32_t influencesCount;         // influence recordの数。
    uint32_t matricesOffset;          // cluster行列recordの先頭。
    uint32_t matricesCount;           // 3 recordで1行列となるcluster数。
    uint32_t facesOffset;             // 面のcorner範囲recordの先頭。
    uint32_t facesCount;              // 面数。
    uint32_t cornersOffset;           // 面corner recordの先頭。
    uint32_t cornersCount;            // corner数。
    uint32_t normalGroupRangesOffset; // normal groupごとのincidence範囲。
    uint32_t normalGroupRangesCount;  // normal group数。
    uint32_t normalFaceIdsOffset;     // incidence順のface番号列。
    uint32_t normalFaceIdsCount;      // incidence数。重複を含む。
    uint32_t outputPositionsOffset;   // float4出力内の位置先頭。
    uint32_t outputPositionsCount;    // 位置出力数。
    uint32_t outputNormalsOffset;     // float4出力内の法線先頭。
    uint32_t outputNormalsCount;      // 法線出力数。
};

static_assert(sizeof(FModelSkinningDispatch) == 80, "skin dispatch ABI must remain 20 uint32 values");

}

#endif
