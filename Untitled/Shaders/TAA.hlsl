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
  float pad0;
  float pad1;
  float pad2;
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

    OutputTexture[pixelPos] = float4(1.0f, 0.0f, 0.0f, 1.0f);
}
