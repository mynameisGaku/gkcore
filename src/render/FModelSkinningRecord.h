// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELSKINNINGRECORD_H
#define GKCORE_RENDER_FMODELSKINNINGRECORD_H

#include <stdint.h>

namespace gk::render
{

/**
 * GPU入力を16byte単位で格納する整数record。
 */
struct FModelSkinningRecord
{
    uint32_t values[4]; // index、範囲、またはfloatのbit pattern。
};

static_assert(sizeof(FModelSkinningRecord) == 16, "skin input record must remain uint4");

}

#endif
