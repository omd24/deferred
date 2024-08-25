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

struct MeshDraw
{
  uint4 textures; // diffuse, roughness, normal, occlusion
  // PBR
  float4 emissive; // emissiveColorFactor + emissive texture index
  float4 baseColorFactor;
  float4 metallicRoughnessOcclusionFactor; // metallic, roughness, occlusion

  uint flags;
  float alphaCutoff;
  //uint vertexOffset;
  uint unused;
  uint meshIndex;

  uint meshletOffset;
  uint meshletCount;
  uint meshletIndexCount;
  uint padding1_;

  // Gpu addresses (unused ?)
  //uint64_t positionBuffer; 
  //uint64_t uvBuffer;
  //uint64_t indexBuffer;
  //uint64_t normalsBuffer;

  uint materialIndex;
  uint indexCount;
  uint vertexBufferOffset;
  uint indexBufferOffset;
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

// SRVs / UAVs
#if (Gbuffer_Meshlet_FRAGMENT > 0)
  Texture2D<uint> MaterialIDMaps[] : register(t0, space100);
  StructuredBuffer<MeshDraw> MeshBuffers[] : register(t0, space101);
  StructuredBuffer<GpuMeshDrawCounts> MeshCountBuffer : register(t0, space102);

  RWStructuredBuffer<GpuMeshDrawCommand> MeshTaskIndirectCountEarly : register(u1);
#endif

// Payload data to export to dispatched mesh shader from task shaders
struct Payload
{
    uint MeshletIndices[32];
};

struct VertexOut
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS   : POSITION0;
    float3 Normal       : NORMAL0;
    uint   MeshletIndex : COLOR0;
};

//=================================================================================================
// Meshlets Gbuffer Task Shader
//=================================================================================================
#if (Gbuffer_Meshlet_TASK > 0)


// The groupshared payload data to export to dispatched mesh shader threadgroups
groupshared Payload s_Payload;

[NumThreads(32, 1, 1)]
void GbufferMeshletTS(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{

#if 0
    bool visible = false;

    // Check bounds of meshlet cull data resource
    if (dtid < MeshInfo.MeshletCount)
    {
        // Do visibility testing for this thread
        visible = IsVisible(MeshletCullData[dtid], Instance.World, Instance.Scale, Constants.CullViewPosition);
    }

    // Compact visible meshlets into the export payload array
    if (visible)
    {
        uint index = WavePrefixCountBits(visible);
        s_Payload.MeshletIndices[index] = dtid;
    }

    // Dispatch the required number of MS threadgroups to render the visible meshlets
    uint visibleCount = WaveActiveCountBits(visible);
    DispatchMesh(visibleCount, 1, 1, s_Payload);
#endif

  DispatchMesh(1, 1, 1, s_Payload);

}

#endif //(Gbuffer_Meshlet_TASK > 0)

//=================================================================================================
// Meshlets Gbuffer Pixel Shader
//=================================================================================================
#if (Gbuffer_Meshlet_MESH > 0)
[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void GbufferMeshletMS(
    uint dtid : SV_DispatchThreadID,
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    in payload Payload payload,
    out vertices VertexOut verts[64],
    out indices uint3 tris[126])
{

#if 0
    // // Load the meshlet from the AS payload data
    // uint meshletIndex = payload.MeshletIndices[gid];

    // // Catch any out-of-range indices (in case too many MS threadgroups were dispatched from AS)
    // if (meshletIndex >= MeshInfo.MeshletCount)
    //     return;

    // // Load the meshlet
    // Meshlet m = Meshlets[meshletIndex];

    // // Our vertex and primitive counts come directly from the meshlet
    // SetMeshOutputCounts(m.VertCount, m.PrimCount);

    // //--------------------------------------------------------------------
    // // Export Primitive & Vertex Data

    // if (gtid < m.VertCount)
    // {
    //     uint vertexIndex = GetVertexIndex(m, gtid);
    //     verts[gtid] = GetVertexAttributes(meshletIndex, vertexIndex);
    // }

    // if (gtid < m.PrimCount)
    // {
    //     tris[gtid] = GetPrimitive(m, gtid);
    // }
#endif

    uint VertCount = 1;
    uint PrimCount = 1;
    SetMeshOutputCounts(VertCount, PrimCount);

    uint meshletIndex = payload.MeshletIndices[gid];

    VertexOut vout;
    vout.PositionVS   = (float3)0;
    vout.PositionHS   = (float4)0;
    vout.Normal       = (float3)0;
    vout.MeshletIndex = meshletIndex;

    verts[gtid] = vout;

}

#endif //(Gbuffer_Meshlet_MESH > 0)


//=================================================================================================
// Meshlets Gbuffer Pixel Shader
//=================================================================================================
#if (Gbuffer_Meshlet_FRAGMENT > 0)
float4 GbufferMeshletPS(VertexOut pin) : SV_TARGET
{   

  return float4(0, 0, 0, pin.MeshletIndex);
}
#endif //(Gbuffer_Meshlet_FRAGMENT > 0)
