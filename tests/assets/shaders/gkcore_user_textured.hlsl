#include <gkcore/Shader.hlsl>

float4 main(GkcorePixelInput input) : SV_Target0
{
    return gkcoreTexture.Sample(gkcoreSampler, input.uv) * input.color * gkcoreUserData[0];
}
