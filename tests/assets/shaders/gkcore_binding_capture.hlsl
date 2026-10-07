#include <gkcore/Shader.hlsl>

/**
 * slot 0と63の定数、画像、描画色を掛け、登録時の値の保持を検査する。
 */
float4 main(GkcorePixelInput input) : SV_Target0
{
    return input.color * gkcoreTexture.Sample(gkcoreSampler, input.uv) * gkcoreUserData[0] * gkcoreUserData[63];
}
