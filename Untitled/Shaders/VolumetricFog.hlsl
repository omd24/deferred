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

  uint X;
  uint Y;
  float NearClip;
  float FarClip;

  uint ClusterBufferIdx;
  uint DepthBufferIdx;
  uint SpotLightShadowIdx;
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
RWTexture3D<float4> DataVolumeTexture : register(u0);

//=================================================================================================
// 1. Data injection
//=================================================================================================
[numthreads(8, 8, 1)]
void DataInjectionCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
  const uint3 froxelCoord = DispatchID;

  // transform froxel to world space
  float2 uv = (froxelCoord.xy + 0.5f) / 128.0f;
  float linearZ = (froxelCoord.z + 0.5f) / 128.0f;
  float rawDepth = linearZ * CBuffer.ProjMat._33 + CBuffer.ProjMat._43 / linearZ;

  uv = 2.0f * uv - 1.0f;
  uv.y *= -1.0f;

  float4 worldPos = mul(float4(uv, rawDepth, 1.0f), CBuffer.InvViewProj);
  worldPos /= worldPos.w;

  float4 scatteringExtinction = float4(1, 1, 1, 1);

  // Add density from box
  float3 boxSize = float3(2.0, 2.0, 2.0);
  float3 boxPos = float3(0, 0, 0);
  float3 boxDist = abs(worldPos - boxPos);
  if (all(boxDist <= boxSize)) {
    scatteringExtinction = (float4)0.1;
  }

  DataVolumeTexture[froxelCoord] = scatteringExtinction;
  //DataVolumeTexture[froxelCoord] = float4(DispatchID, 1.0f);
}

//=================================================================================================
// 2. Light contribution
//=================================================================================================
[numthreads(8, 8, 1)]
void LightContributionCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
  const uint3 froxelCoord = DispatchID;

  DataVolumeTexture[froxelCoord] = float4(1.0f, 0.0f, 0.0f, 1.0f);
}

//=================================================================================================
// 3. Light contribution
//=================================================================================================
[numthreads(8, 8, 1)]
void FinalIntegrationCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
  const uint3 froxelCoord = DispatchID;

  DataVolumeTexture[froxelCoord] = float4(1.0f, 0.0f, 0.0f, 1.0f);
}