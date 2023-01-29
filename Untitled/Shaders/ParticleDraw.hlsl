struct VSConstants
{
  row_major float4x4 World;
  row_major float4x4 View;
  row_major float4x4 WorldViewProjection;
};

ConstantBuffer<VSConstants> VSCBuffer : register(b0);

struct VSInput
{
  float3 PositionOS : POSITION;
  float3 NormalOS : NORMAL;
  float2 UV : UV;
  float3 TangentOS : TANGENT;
  float3 BitangentOS : BITANGENT;
};

struct PSInput
{
  float4 PositionSS : SV_Position;
  float3 PositionWS : POSITIONWS;
};

PSInput VS(VSInput p_Input)
{
  PSInput output;

  float3 positionOS = p_Input.PositionOS;

  // Calc the world-space position
  output.PositionWS = mul(float4(positionOS, 1.0f), VSCBuffer.World).xyz;

  // Calc the clip-space position
  output.PositionSS = mul(float4(positionOS, 1.0f), VSCBuffer.WorldViewProjection);

  return output;
}

float4 PS(PSInput p_Input) : SV_TARGET
{
  float4 ret = float4(1.0, 0.0f, 0.0f, 1.0f);
  return ret;
}