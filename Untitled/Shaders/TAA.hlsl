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
  uint HistorySceneColorIdx;
  uint MotionVectorsIdx;
  uint unused0;

  float2 Resolution;
  uint2 unused1;
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
float4 sampleHistoryColor (float2 uv)
{
    Texture2D historyTex = Tex2DTable[CBuffer.HistorySceneColorIdx];
    const float4 color = historyTex.SampleLevel(LinearClampSampler, uv, 0);

    if (false) // TODO
    {
        return float4(rgbToYcocg(color.rgb), color.a);
    }

    return color;
}
//=================================================================================================
float4 sampleColor(float2 uv)
{
    Texture2D sceneTex = Tex2DTable[CBuffer.SceneColorIdx];
    const float4 color = sceneTex.SampleLevel(LinearClampSampler, uv, 0);

    if (false) // TODO
    {
        return float4(rgbToYcocg(color.rgb), color.a);
    }

    return color;
}


//=================================================================================================
// Simplest implementation of TAA
float3 taaSimplest(uint2 pos, float2 screenUV)
{
    // Sample Motion Vectors:
    Texture2D motionVectorsTex = Tex2DTable[CBuffer.MotionVectorsIdx];
    const float2 velocity = motionVectorsTex[pos].rg; // point sample

    const float2 reprojectedUV = screenUV - velocity;
    float3 historyColor = sampleHistoryColor(reprojectedUV).rgb;
    float3 currentColor = sampleColor(screenUV).rgb;

    return lerp(currentColor, historyColor, 0.9f);
}

//=================================================================================================
// TAA compute shader
//=================================================================================================
[numthreads(8, 8, 1)]
void TaaCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    const uint2 pixelPos = DispatchID.xy;
    float2 invRTSize = 1.0f / float2(CBuffer.Resolution.x, CBuffer.Resolution.y);
    float2 screenUV = (pixelPos + 0.5f) * invRTSize;

    float3 color = taaSimplest(pixelPos, screenUV);

    OutputTexture[pixelPos] = float4(color, 1);
}
