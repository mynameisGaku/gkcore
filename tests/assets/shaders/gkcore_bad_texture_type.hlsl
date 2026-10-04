Texture2D<uint4> integerTexture : register(t0, space0);

float4 main(float4 position : SV_Position, float4 color : COLOR0) : SV_Target0
{
    return color * float4(integerTexture.Load(int3(0, 0, 0)));
}
