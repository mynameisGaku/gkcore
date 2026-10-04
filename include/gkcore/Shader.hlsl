#ifndef GKCORE_SHADER_HLSL
#define GKCORE_SHADER_HLSL

/**
 * User-provided pixel constants, visible at register b0 in space 3.
 */
cbuffer GkcoreUserConstants : register(b0, space3)
{
    float4 gkcoreUserData[64];
};

// Optional pixel texture at register t0 in space 0.
Texture2D<float4> gkcoreTexture : register(t0, space0);

// Optional pixel sampler at register s0 in space 0.
SamplerState gkcoreSampler : register(s0, space0);

/**
 * Input values supplied to a user pixel shader's main entry point.
 */
struct GkcorePixelInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

#endif
