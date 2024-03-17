//=================================================================================================
// Includes
//=================================================================================================
#include "GlobalResources.hlsl"
#include "AppSettings.hlsl"
#include "LightingHelpers.hlsl"

//=================================================================================================
// Constant buffers
//=================================================================================================
struct UniformConstants
{
  row_major float4x4 PrevViewProj;
  row_major float4x4 InvViewProj;

  float2 JitterXY;
  float2 PreviousJitterXY;

  float2 Resolution;
  uint DepthMapIdx;
  float pad0;
};
ConstantBuffer<UniformConstants> CBuffer : register(b0);

//=================================================================================================
// Resources
//=================================================================================================

// Render targets / UAVs
RWTexture2D<float4> OutputTexture : register(u0);

// Sampler(s)
SamplerState PointSampler : register(s0);
SamplerState LinearSampler : register(s1);
SamplerState LinearBorderSampler : register(s2);
SamplerState LinearClampSampler : register(s3);

//=================================================================================================
// Utility methods
//=================================================================================================
float3 ndcFromUVRawDepth (float2 uv, float rawDepth)
{
    return float3(uv.x * 2 - 1, (1 - uv.y) * 2 - 1, rawDepth);
}
//=================================================================================================
float3 worldPositionFromDepth (float2 uv, float rawDepth, float4x4 invViewProj)
{

    float4 H = float4(ndcFromUVRawDepth (uv, rawDepth), 1.0);
    float4 D = mul(H, invViewProj);

    return D.xyz / D.w;
}

//=================================================================================================
// Motion vectors composition shader
//=================================================================================================
[numthreads(8, 8, 1)]
void MotionVectorsCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    const uint2 pixelPos = DispatchID.xy;
    float2 invRTSize = 1.0f / float2(CBuffer.Resolution.x, CBuffer.Resolution.y);
    float2 screenUV = (pixelPos + 0.5f) * invRTSize;

    Texture2D depthMap = Tex2DTable[CBuffer.DepthMapIdx];
    const float rawDepth = depthMap[pixelPos].x;
    const float3 posWS = worldPositionFromDepth(screenUV, rawDepth, CBuffer.InvViewProj);

    float4 currPosNDC = float4(ndcFromUVRawDepth(screenUV, rawDepth), 1.0f);
    float4 prevPosNDC = mul(float4(posWS, 1.0f), CBuffer.PrevViewProj);
    prevPosNDC.xyz /= prevPosNDC.w;

    float2 jitterDifference = (CBuffer.JitterXY - CBuffer.PreviousJitterXY) * 0.5f;
    float2 velocity = currPosNDC.xy - prevPosNDC.xy;
    velocity -= jitterDifference;

    OutputTexture[pixelPos.xy] = float4(velocity, 0, 0);
}
