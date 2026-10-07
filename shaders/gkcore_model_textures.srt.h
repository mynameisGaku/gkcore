// SPDX-License-Identifier: NOASSERTION
#ifndef GKCORE_SHADERS_MODEL_TEXTURES_SRT_H
#define GKCORE_SHADERS_MODEL_TEXTURES_SRT_H

// 内蔵モデルの基本色、金属度・粗さ、法線を指定する画像配置。
BEGIN_SRT_NO_AB(ModelTextureResources)
BEGIN_SRT_SET(Persistent)
DECL_TEXTURE(Persistent, Tex2D(float4), gImageTexture)
DECL_TEXTURE(Persistent, Tex2D(float4), gMetallicRoughnessTexture)
DECL_TEXTURE(Persistent, Tex2D(float4), gNormalTexture)
DECL_SAMPLER(Persistent, SamplerState, gImageSampler)
END_SRT_SET(Persistent)
END_SRT(ModelTextureResources)

#endif
