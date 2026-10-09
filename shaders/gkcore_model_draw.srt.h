// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_SHADERS_MODEL_DRAW_SRT_H
#define GKCORE_SHADERS_MODEL_DRAW_SRT_H

BEGIN_SRT_NO_AB(ModelDrawResources)
BEGIN_SRT_SET(PerDraw)
DECL_CBUFFER(PerDraw, CBUFFER(ModelDrawConstants), gModelDraw)
// poseの位置・法線・接線をfloat4単位で保持する。
DECL_BUFFER(PerDraw, Buffer(float4), gModelPoseVertices)
// source頂点からpose位置・法線のcompact indexを得る。
DECL_BUFFER(PerDraw, Buffer(uint2), gModelSparseVertexMap)
END_SRT_SET(PerDraw)
END_SRT(ModelDrawResources)

#endif
