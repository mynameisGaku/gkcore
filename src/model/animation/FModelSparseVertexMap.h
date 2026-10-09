// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_MODEL_ANIMATION_FMODEL_SPARSE_VERTEX_MAP_H
#define GKCORE_MODEL_ANIMATION_FMODEL_SPARSE_VERTEX_MAP_H

#include <stdint.h>

namespace gk::model::animation
{

/**
 * 描画cornerをunique位置とnormal groupへ対応付ける。
 */
struct FModelSparseVertexMap
{
    // positions配列内のunique vertex番号。
    uint32_t positionIndex;
    // normals配列内のnormal group番号。
    uint32_t normalIndex;
};

}

#endif
