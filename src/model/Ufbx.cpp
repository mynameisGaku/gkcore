/*
 * Compiles the pinned ufbx implementation once as part of the runtime.
 */
#define UFBX_MINIMAL
#define UFBX_ENABLE_TRIANGULATION
#define UFBX_NO_STDIO
#include "../../third_party/ufbx/ufbx.c"
