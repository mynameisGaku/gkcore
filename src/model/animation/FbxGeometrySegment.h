// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FBXGEOMETRYSEGMENT_H
#define GKCORE_MODEL_ANIMATION_FBXGEOMETRYSEGMENT_H

#include <stdint.h>

/**
 * FBX node meshとflatten後の頂点範囲の対応情報を定義する。
 */
namespace gk::detail
{
/**
 * FBX mesh instanceからflatten済み頂点列への対応を保持する。
 */
struct FFbxGeometrySegment
{
    // FBX scene内のnode識別番号。
    uint32_t nodeTypedId;
    // FBX scene内の共有mesh識別番号。
    uint32_t meshTypedId;
    // ModelResource頂点列でこのmeshが始まる位置。
    uint32_t firstModelVertex;
    // meshのcorner列と同じ順に並ぶ頂点数。
    uint32_t vertexCount;
};
}

#endif
