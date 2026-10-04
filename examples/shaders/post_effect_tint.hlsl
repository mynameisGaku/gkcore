#include <gkcore/Shader.hlsl>

/**
 * Applies a user tint to the linear scene color while preserving its alpha.
 */
float4 main(GkcorePixelInput input) : SV_Target0
{
    float4 tint = gkcoreUserData[0];
    tint.a = 1.0;
    return gkcoreTexture.Sample(gkcoreSampler, input.uv) * tint;
}
