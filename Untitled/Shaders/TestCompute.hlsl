//=================================================================================================
// Includes
//=================================================================================================
#include "AppSettings.hlsl"

//=================================================================================================
// Constant buffers
//=================================================================================================
struct UniformConstants
{
  row_major float4x4 ProjMat;
  row_major float4x4 InvViewProj;

  uint sceneTexIdx;
  uint fogTexIdx;

  float X;
  float Y;

};
ConstantBuffer<UniformConstants> CBuffer : register(b0);

//=================================================================================================
// Resources
//=================================================================================================

// TODO: Put this in a common header
// This is the standard set of descriptor tables that shaders use for accessing the SRV's that they
// need. These must match "NumStandardDescriptorRanges" and "StandardDescriptorRanges()"
Texture2D Tex2DTable[] : register(t0, space0);
Texture2DArray Tex2DArrayTable[] : register(t0, space1);
TextureCube TexCubeTable[] : register(t0, space2);
Texture3D Tex3DTable[] : register(t0, space3);
Texture2DMS<float4> Tex2DMSTable[] : register(t0, space4);
ByteAddressBuffer RawBufferTable[] : register(t0, space5);
Buffer<uint> BufferUintTable[] : register(t0, space6);

// Render targets / UAVs
RWTexture2D<float4> OutputTexture : register(u0);

// Sampler(s)
SamplerState PointSampler : register(s0);

//=================================================================================================
// Test compute shader
//=================================================================================================
[numthreads(8, 8, 1)]
void TestCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    const uint2 pixelPos = DispatchID.xy;

  // transform to world space
  // TODO: read actual depth buffer
#if 0
  float2 uv = (pixelPos.xy + 0.5f) / 128.0f;
  float linearZ = (pixelPos.z + 0.5f) / 128.0f;
  float rawDepth = linearZ * CBuffer.ProjMat._33 + CBuffer.ProjMat._43 / linearZ;

  uv = 2.0f * uv - 1.0f;
  uv.y *= -1.0f;

  float4 worldPos = mul(float4(uv, rawDepth, 1.0f), CBuffer.InvViewProj);
  worldPos /= worldPos.w;
#endif

    // Sample scene texture
    Texture2D sceneTexture = Tex2DTable[CBuffer.sceneTexIdx];
    float2 inputSize = 0.0f;
    sceneTexture.GetDimensions(inputSize.x, inputSize.y);
    float2 texCoord = float2(pixelPos.x, pixelPos.y) / inputSize;
    float4 sceneColor = sceneTexture.SampleLevel(PointSampler, texCoord, 0);

    // Sample fog volume
    Texture3D fogVolume = Tex3DTable[CBuffer.fogTexIdx];
    float3 volCoord = float3(pixelPos.x, pixelPos.y, 0.5f);
    float4 fogSample = fogVolume.SampleLevel(PointSampler, volCoord, 0);
    
    float4 blend = lerp(sceneColor, fogSample, 0.5);
    OutputTexture[pixelPos] = blend;
    //OutputTexture[pixelPos] = float4(DispatchID, 1.0f);
}
