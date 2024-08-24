//=================================================================================================
// Includes
//=================================================================================================
#include "GlobalResources.hlsl"
#include "AppSettings.hlsl"
#include "Shadows.hlsl"
#include "LightingHelpers.hlsl"
#include "Quaternion.hlsl"

//=================================================================================================
// Uniforms
//=================================================================================================
struct UniformConstants
{
  float Near;
  float Far;

};
ConstantBuffer<UniformConstants> CBuffer : register(b0);

//=================================================================================================
// Shader Types
//=================================================================================================

struct GpuMeshDrawCounts
{
  uint opaqueMeshVisibleCount;
  uint opaqueMeshCulledCount;
  uint transparentMeshVisibleCount;
  uint transparentMeshCulledCount;

  uint totalCount;
  uint depthPyramidTextureIndex;
  uint lateFlag;
  uint meshletIndexCount;

  uint dispatchTaskX;
  uint dispatchTaskY;
  uint dispatchTaskZ;
  uint pad001;
};

struct GpuMeshDrawCommand
{
    uint drawId;
    uint2 cbvAddress;
    uint4 drawArguments;
};

//=================================================================================================
// Resources
//=================================================================================================


// Samplers:
SamplerState PointSampler : register(s0);
SamplerState LinearClampSampler : register(s1);
SamplerState LinearWrapSampler : register(s2);
SamplerState LinearBorderSampler : register(s3);
SamplerComparisonState ShadowMapSampler : register(s4);

#if (Gbuffer_Meshlet > 0)
  Texture2D<uint> MaterialIDMaps[] : register(t0, space100);
  StructuredBuffer<MeshDraw> MeshBuffers[] : register(t0, space101);
  StructuredBuffer<GpuMeshDrawCounts> MeshCountBuffer : register(t0, space102);

  RWStructuredBuffer<MeshDrawCommand> MeshTaskIndirectCountEarly : register(u1);
#endif

//=================================================================================================
// Meshlets Gbuffer
//=================================================================================================
#if (Gbuffer_Meshlet > 0)
[numthreads(1, 1, 1)]
void MeshletCS(in uint3 DispatchID : SV_DispatchThreadID,
                     in uint3 GroupID : SV_GroupID) 
{   
    const uint2 coord = DispatchID.xy;
    GpuMeshDrawCommand test = (GpuMeshDrawCommand)0;
    m_MeshTaskIndirectEarlyCommands[0] = test;
    m_MeshTaskIndirectEarlyCommands[1] = test;
}
#endif //(Gbuffer_Meshlet > 0)
