struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSTriangleDraw(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = position;
    result.color = color;

    return result;
}

float4 PSTriangleDraw(PSInput input) : SV_TARGET
{
    return input.color * 2.0f;
}