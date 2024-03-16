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
  row_major float4x4 ProjMat;
  row_major float4x4 InvViewProj;

  uint SceneColorIdx;
  float2 Resolution;
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
// TAA compute shader
//=================================================================================================
[numthreads(8, 8, 1)]
void TaaCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    const uint2 pixelPos = DispatchID.xy;
    float2 invRTSize = 1.0f / float2(CBuffer.Resolution.x, CBuffer.Resolution.y);
    float2 screenUV = (pixelPos + 0.5f) * invRTSize;

    Texture2D inputTex = Tex2DTable[CBuffer.SceneColorIdx];
    float4 sceneColor = inputTex.SampleLevel(LinearClampSampler, screenUV, 0);

    OutputTexture[pixelPos] = sceneColor;
}
