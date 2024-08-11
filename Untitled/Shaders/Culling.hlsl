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

//=================================================================================================
// Resources
//=================================================================================================

Texture2D<uint> MaterialIDMaps[] : register(t0, space104);

// Samplers:
SamplerState PointSampler : register(s0);
SamplerState LinearClampSampler : register(s1);
SamplerState LinearWrapSampler : register(s2);
SamplerState LinearBorderSampler : register(s3);
SamplerComparisonState ShadowMapSampler : register(s4);

// Render targets / UAVs
#if (GPU_CULLING > 0)
  RWStructuredBuffer<GpuMeshDrawCounts> MeshTaskIndirectCountEarly : register(u0);
#endif

//=================================================================================================
// Gpu Culling
//=================================================================================================
#if (GPU_CULLING > 0)
[numthreads(1, 1, 1)]
void CullingCS(in uint3 DispatchID : SV_DispatchThreadID,
                     in uint3 GroupID : SV_GroupID) 
{   
    const uint2 coord = DispatchID.xy;
    GpuMeshDrawCounts test = (GpuMeshDrawCounts)0;
    MeshTaskIndirectCountEarly[0] = test;
    MeshTaskIndirectCountEarly[1] = test;
}
#endif //(GPU_CULLING > 0)
