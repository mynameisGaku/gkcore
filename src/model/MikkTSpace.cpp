// SPDX-License-Identifier: NOASSERTION
#include "../foundation/Memory.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
#include "../../third_party/mikktspace/mikktspace.h"
}

// vendor実装の割当もfoundationの失敗検査を通す。
#define malloc(size) gk::Allocate(size)
#define free(memory) gk::Deallocate(memory)

#include "../../third_party/mikktspace/mikktspace.c"

#undef malloc
#undef free
