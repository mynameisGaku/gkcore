/*
 * 固定版ufbx実装をRuntime内で一度だけコンパイルする。
 */
#define UFBX_MINIMAL
#define UFBX_ENABLE_TRIANGULATION
#define UFBX_ENABLE_SCENE_EVALUATION
#define UFBX_ENABLE_SKINNING_EVALUATION
#define UFBX_NO_STDIO
#include "../../third_party/ufbx/ufbx.c"
