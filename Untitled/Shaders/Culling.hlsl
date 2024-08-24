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
  float Unused0;
  float Unused1;

  uint meshBufferSrv;
};
ConstantBuffer<UniformConstants> CBuffer : register(b0);

//=================================================================================================
// Shader Types
//=================================================================================================

struct MeshDrawCommand
{
    uint        drawId;
    uint        firstTask;

    // D3D12_DRAW_INDEXED_ARGUMENTS
    uint        indexCount;
    uint        instanceCount;
    uint        firstIndex;
    uint        vertexOffset;
    uint        firstInstance;

    // D3D12_DISPATCH_MESH_ARGUMENTS
    uint        threadGroupCountX;
    uint        threadGroupCountY;
    uint        threadGroupCountZ;
};

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

struct MeshDraw {

    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    // Occlusion and roughness are encoded in the same texture
    uint4       textures;
    float4      emissive;
    float4      baseColorFactor;
    float4      metallicRoughnessOcclusionFactor;

    uint        flags;
    float       alphaCutoff;
    //uint        vertexOffset; // == meshes[meshIndex].vertexOffset, helps data locality in mesh shader
    uint        unused;
    uint        meshIndex;

    uint        meshletOffset;
    uint        meshletCount;
    uint        meshletIndexCount;
    uint        pad001;

    // uint        positionBuffer;
    // uint        uvBuffer;
    // uint        indexBuffer;
    // uint        normalsBuffer;

    uint        materialIndex;
    uint        indexCount;
    uint        vertexBufferOffset;
    uint        indexBufferOffset;
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

#if (GPU_CULLING > 0)
  Texture2D<uint> MaterialIDMaps[] : register(t0, space104);
  StructuredBuffer<MeshDraw> MeshBuffers[] : register(t0, space100);
  
  RWStructuredBuffer<GpuMeshDrawCounts> MeshCountBuffer : register(u0);
  RWStructuredBuffer<MeshDrawCommand> MeshTaskIndirectCountEarly : register(u1);
#endif

//=================================================================================================
// Gpu Culling
//=================================================================================================
#if (GPU_CULLING > 0)
[numthreads(64, 1, 1)]
void CullingCS(in uint3 dispatchID : SV_DispatchThreadID,
                     in uint3 groupID : SV_GroupID) 
{   
    // TODO: Add actual instancing. Right now mesh instance index is same as mesh draw index
    // which is not necessarily true for actual instanced drawing
    const uint meshInstanceIndex = dispatchID.x;

    // TODO: get total_count from a cbuffer or root constant like ExecIndirect sample
    // meshDraw buffer in vk sample is meshes_sb on c++ side and is the buffer with all mesh data 


    //uint meshDrawIndex = meshInstanceDraws[meshInstanceIndex].mesh_draw_index;

    StructuredBuffer<MeshDraw> meshDraws = MeshBuffers[CBuffer.meshBufferSrv];
    MeshDraw meshDraw = meshDraws[meshInstanceIndex];

    uint drawIndex; // previous value
    InterlockedAdd(MeshCountBuffer[0].opaqueMeshVisibleCount, 1 , drawIndex);


    // Write draw commands
    MeshTaskIndirectCountEarly[drawIndex].drawId = meshInstanceIndex;
    MeshTaskIndirectCountEarly[drawIndex].indexCount = 0; // meshDraw.indexCount;
    MeshTaskIndirectCountEarly[drawIndex].instanceCount = 1;
    MeshTaskIndirectCountEarly[drawIndex].firstIndex = 0; //meshDraw.indexBufferOffset;
    MeshTaskIndirectCountEarly[drawIndex].vertexOffset = meshDraw.vertexBufferOffset;
    MeshTaskIndirectCountEarly[drawIndex].firstInstance = 0;

    uint taskCount = (meshDraw.meshletCount + 31) / 32;
    MeshTaskIndirectCountEarly[drawIndex].threadGroupCountX = taskCount;
    MeshTaskIndirectCountEarly[drawIndex].firstTask = meshDraw.meshletOffset / 32;

    MeshTaskIndirectCountEarly[drawIndex].indexCount = meshDraw.meshletIndexCount;
}
#endif //(GPU_CULLING > 0)
