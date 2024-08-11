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

struct GpuMeshDrawCommand
{
    uint drawId;
    uint2 cbvAddress;
    uint4 drawArguments;
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
#if (Gbuffer_Meshlet > 0)
  RWStructuredBuffer<GpuMeshDrawCommand> m_MeshTaskIndirectEarlyCommands : register(u0);
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
