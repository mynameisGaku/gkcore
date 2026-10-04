#include <gkcore/Shader.hlsl>

/**
 * Applies a per-draw tint to the incoming vertex color.
 */
float4 main(GkcorePixelInput input) : SV_Target0
{
    return input.color * gkcoreUserData[0];
}
