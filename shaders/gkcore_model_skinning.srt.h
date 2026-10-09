#ifndef GKCORE_SHADERS_MODEL_SKINNING_SRT_H
#define GKCORE_SHADERS_MODEL_SKINNING_SRT_H

BEGIN_SRT_NO_AB(ModelSkinningResources)
BEGIN_SRT_SET(PerDraw)
DECL_BUFFER(PerDraw, Buffer(uint4), gSkinRecords)
DECL_BUFFER(PerDraw, Buffer(uint4), gSkinMatrices)
DECL_RWBUFFER(PerDraw, RWBuffer(float4), gSkinOutput)
DECL_RWBUFFER(PerDraw, RWBuffer(double4), gSkinPrecisePositions)
DECL_RWBUFFER(PerDraw, RWBuffer(double4), gSkinFaceNormals)
DECL_RWBUFFER(PerDraw, RWBuffer(uint), gSkinErrors)
DECL_CBUFFER(PerDraw, CBUFFER(ModelSkinningConstants), gSkinConstants)
END_SRT_SET(PerDraw)
END_SRT(ModelSkinningResources)

#endif
