#include "GlobalResources.hlsl"
#include "Quaternion.hlsl"

static const float DeferredUVScale = 2.0f;

//=================================================================================================
// Uniforms
//=================================================================================================
struct VSConstants
{
  row_major float4x4 World;
  row_major float4x4 View;
  row_major float4x4 WorldViewProjection;
  float NearClip;
  float FarClip;
};

struct MatIndexConstants
{
  uint MatIndex;
};

ConstantBuffer<VSConstants> VSCBuffer : register(b0);
ConstantBuffer<MatIndexConstants> MatIndexCBuffer : register(b2);

//=================================================================================================
// VS and PS structs
//=================================================================================================
struct VSInput
{
  float3 PositionOS : POSITION;
  float3 NormalOS : NORMAL;
  float2 UV : UV;
  float3 TangentOS : TANGENT;
  float3 BitangentOS : BITANGENT;
};

struct VSOutput
{
  float4 PositionCS : SV_Position;

  float3 NormalWS : NORMALWS;
  float3 PositionWS : POSITIONWS;
  float DepthVS : DEPTHVS;
  float3 TangentWS : TANGENTWS;
  float3 BitangentWS : BITANGENTWS;
  float2 UV : UV;
};

struct PSInput
{
  float4 PositionSS : SV_Position;

  float3 NormalWS : NORMALWS;
  float3 PositionWS : POSITIONWS;
  float DepthVS : DEPTHVS;
  float3 TangentWS : TANGENTWS;
  float3 BitangentWS : BITANGENTWS;
  float2 UV : UV;
};

struct PSOutputGBuffer
{
  float4 TangentFrame : SV_Target0;
  float4 UV : SV_Target1;
  uint MaterialID : SV_Target2;
};

//=================================================================================================
// Gbuffer shaders entry-points
//=================================================================================================
VSOutput VS(in VSInput input, in uint VertexID : SV_VertexID)
{
  VSOutput output;

  float3 positionOS = input.PositionOS;

  // Calc the world-space position
  output.PositionWS = mul(float4(positionOS, 1.0f), VSCBuffer.World).xyz;

  // Calc the clip-space position
  output.PositionCS = mul(float4(positionOS, 1.0f), VSCBuffer.WorldViewProjection);
  output.DepthVS = output.PositionCS.w;

  // Rotate the normal into world space
  output.NormalWS = normalize(mul(float4(input.NormalOS, 0.0f), VSCBuffer.World)).xyz;

  // Rotate the rest of the tangent frame into world space
  output.TangentWS = normalize(mul(float4(input.TangentOS, 0.0f), VSCBuffer.World)).xyz;
  output.BitangentWS = normalize(mul(float4(input.BitangentOS, 0.0f), VSCBuffer.World)).xyz;

  // Pass along the texture coordinates
  output.UV = input.UV;

  return output;
}
PSOutputGBuffer PS(in PSInput input)
{
  PSOutputGBuffer result;
  float3 normalWS = normalize(input.NormalWS);
  float3 tangentWS = normalize(input.TangentWS);
  float3 bitangentWS = normalize(input.BitangentWS);

  // The tangent frame can have arbitrary handedness, so we force it to be left-handed and then
  // pack the handedness into the material ID
  float handedness = dot(bitangentWS, cross(normalWS, tangentWS)) > 0.0f ? 1.0f : -1.0f;
  bitangentWS *= handedness;

  Quaternion tangentFrame = QuatFrom3x3(float3x3(tangentWS, bitangentWS, normalWS));

  result.TangentFrame = PackQuaternion(tangentFrame);

  result.MaterialID = MatIndexCBuffer.MatIndex & 0x7F;
  if (handedness == -1.0f)
    result.MaterialID |= 0x80;

  result.UV.xy = frac(input.UV / DeferredUVScale);
  result.UV.zw = float2(ddx_fine(input.PositionSS.z), ddy_fine(input.PositionSS.z));
  result.UV.zw = sign(result.UV.zw) * pow(abs(result.UV.zw), 1 / 2.0f);

  return result;
}