//=================================================================================================
// Includes
//=================================================================================================
#include "GlobalResources.hlsl"
#include "AppSettings.hlsl"
#include "VolumetricFogHelpers.hlsl"

//=================================================================================================
// Constant buffers
//=================================================================================================
struct UniformConstants
{
  row_major float4x4 ProjMat;
  row_major float4x4 InvViewProj;

  uint sceneTexIdx;
  uint fogTexIdx;
  uint depthMapIdx;
  float near;

  float far;
  float2 Resolution;
  float pad0;

  float3 FogGridDimensions;
  float pad1;

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
// Test compute shader
//=================================================================================================
[numthreads(8, 8, 1)]
void TestCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    const uint2 pixelPos = DispatchID.xy;

  // transform to world space

    Texture2D depthMap = Tex2DTable[CBuffer.depthMapIdx];
    float z = depthMap[pixelPos].x;
    float2 invRTSize = 1.0f / float2(CBuffer.Resolution.x, CBuffer.Resolution.y);
    float2 screenUV = (pixelPos + 0.5f) * invRTSize;

    // Sample scene texture
    Texture2D sceneTexture = Tex2DTable[CBuffer.sceneTexIdx];
    float2 inputSize = 0.0f;
    sceneTexture.GetDimensions(inputSize.x, inputSize.y);
    float2 texCoord = float2(pixelPos.x, pixelPos.y) / inputSize;
    float4 sceneColor = sceneTexture.SampleLevel(PointSampler, texCoord, 0);

    const float near = CBuffer.near;
    const float far = CBuffer.far;
    Texture3D fogVolume = Tex3DTable[CBuffer.fogTexIdx];
    float3 output = applyVolumetricFog(
        screenUV, z, near, far, CBuffer.FogGridDimensions.z, fogVolume, LinearClampSampler, sceneColor.rgb);

    // float t = 1;
    // float4 blend2 = lerp(sceneColor, float4(fogSample3, 1), t);
    OutputTexture[pixelPos] = float4(output, 1.0f);
}
