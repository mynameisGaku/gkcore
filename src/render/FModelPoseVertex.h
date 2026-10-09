// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_RENDER_FMODELPOSEVERTEX_H
#define GKCORE_RENDER_FMODELPOSEVERTEX_H
/**
 * 評価済みmodel姿勢の頂点属性。
 */
namespace gk::render
{
/**
 * 変形した位置・法線・接線を3つのfloat4としてGPUへ渡す。
 */
struct FModelPoseVertex
{
    // model空間での位置。
    float position[4]{};
    // model空間での法線。
    float normal[4]{};
    // model空間の接線と縦方向の符号。
    float tangent[4]{};
};
static_assert(sizeof(FModelPoseVertex) == 48, "model pose vertex must match three float4 entries");
}
#endif
