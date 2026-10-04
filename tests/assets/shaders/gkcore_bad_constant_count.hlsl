cbuffer BadConstants : register(b0, space3)
{
    float4 badData[63];
};

float4 main(float4 position : SV_Position, float4 color : COLOR0) : SV_Target0
{
    return color * badData[0];
}
