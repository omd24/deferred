#pragma once

#include "Common/D3D12Wrapper.hpp"
#include <Camera.hpp>
#include <Model.hpp>

struct alignas(16) GpuMeshlet
{
  glm::vec3 center;
  float radius;

  int8_t coneAxis[3];
  int8_t coneCutoff;

  uint32_t dataOffset;
  uint32_t meshIndex;
  uint8_t vertexCount;
  uint8_t triangleCount;
};
//---------------------------------------------------------------------------//
struct GpuMeshletVertexPosition
{

  float position[3];
  float padding;
};
//---------------------------------------------------------------------------//
struct GpuMeshletVertexData
{

  uint8_t normal[4];
  uint8_t tangent[4];
  uint16_t uvCoords[2];
  float padding;
};
//---------------------------------------------------------------------------//
struct alignas(16) GpuMaterialData
{
  uint32_t textures[4]; // diffuse, roughness, normal, occlusion
  // PBR
  glm::vec4 emissive; // emissiveColorFactor + emissive texture index
  glm::vec4 baseColorFactor;
  glm::vec4 metallicRoughnessOcclusionFactor; // metallic, roughness, occlusion

  uint32_t flags;
  float alphaCutoff;
  uint32_t vertexOffset;
  uint32_t meshIndex;

  uint32_t meshletOffset;
  uint32_t meshletCount;
  uint32_t meshletIndexCount;
  uint32_t padding1_;

  // Gpu addresses (unused ?)
  uint64_t positionBuffer; 
  uint64_t uvBuffer;
  uint64_t indexBuffer;
  uint64_t normalsBuffer;

}; // struct GpuMaterialData

struct alignas(16) GpuMeshInstanceData
{
  glm::mat4 world;
  glm::mat4 inverseWorld;

  uint32_t meshIndex;
  uint32_t pad000;
  uint32_t pad001;
  uint32_t pad002;
};

// Data structure to match the command signature used for ExecuteIndirect.
struct IndirectCommand
{
  D3D12_GPU_VIRTUAL_ADDRESS cbv; // Needed?
  D3D12_DISPATCH_ARGUMENTS drawArguments;
};

struct alignas(16) GpuMeshDrawCommand
{
  uint32_t drawId;
  IndirectCommand indirect;
};

struct alignas(16) GpuMeshDrawCounts
{
  uint32_t opaqueMeshVisibleCount;
  uint32_t opaqueMeshCulledCount;
  uint32_t transparentMeshVisibleCount;
  uint32_t transparentMeshCulledCount;

  uint32_t totalCount;
  uint32_t depthPyramidTextureIndex;
  uint32_t lateFlag;
  uint32_t meshletIndexCount;

  uint32_t dispatchTaskX;
  uint32_t dispatchTaskY;
  uint32_t dispatchTaskZ;
  uint32_t pad001;
};

struct MeshInstance
{
  Mesh* mesh;

  uint32_t gpuMeshInstanceIndex = UINT32_MAX;
  uint32_t sceneGraphNodeIndex = UINT32_MAX;
};

//---------------------------------------------------------------------------//
struct GpuDrivenRenderer
{
  // Helper wrapper for rendering parameters
  struct RenderDesc
  {
    uint32_t DepthBufferSrv = UINT32_MAX;
  };

  void init(ID3D12Device* p_Device);
  void deinit();

  void render(ID3D12GraphicsCommandList* p_CmdList, const RenderDesc& p_RenderDesc);

  void addMeshes(std::vector<Mesh>&);
  void createResources(ID3D12Device* p_Device);

  ID3DBlobPtr m_DataShader = nullptr;

  std::vector<ID3D12PipelineState*> m_PSOs;
  ID3D12RootSignature* m_RootSig = nullptr;
  ID3D12CommandSignature* m_CommandSignature = nullptr;

  ID3DBlobPtr m_CullingShader = nullptr;
  ID3DBlobPtr m_GbufferShader = nullptr;

  std::vector<GpuMeshlet> m_Meshlets{};
  std::vector<GpuMeshletVertexPosition> m_MeshletsVertexPositions{};
  std::vector<GpuMeshletVertexData> m_MeshletsVertexData{};
  std::vector<uint32_t> m_MeshletsData;
  uint32_t m_MeshletsIndexCount = 0;

  // copy of meshes for gpu driven rendering
  std::vector<Mesh> m_Meshes{};
  std::vector<MeshInstance> m_MeshInstances{};
  std::vector<uint32_t> m_GltfMeshToMeshOffset;

  // Gpu resources
  StructuredBuffer m_MeshletsDataBuffer;
  StructuredBuffer m_MeshletsVertexPosBuffer;
  StructuredBuffer m_MeshletsVertexDataBuffer;
  StructuredBuffer m_MeshletsBuffer;
  StructuredBuffer m_MeshesBuffer;
  StructuredBuffer m_MeshBoundsBuffer;
  StructuredBuffer m_MeshInstancesBuffer;

  // Dynamic (multi frame resource)
  StructuredBuffer m_MeshTaskIndirectEarlyCommands;
  StructuredBuffer m_MeshTaskIndirectCulledCommands;
  StructuredBuffer m_MeshTaskIndirectLateCommands;
  StructuredBuffer m_MeshTaskIndirectCountEarly;
  StructuredBuffer m_MeshTaskIndirectCountCpuVisible;
  StructuredBuffer m_MeshTaskIndirectCountLate;
  StructuredBuffer m_MeshletInstancesIndirectCount;
  
  // Debug draw buffers
  StructuredBuffer m_DebugLineStructureBuffer;
  StructuredBuffer m_DebugLineCount;
  StructuredBuffer m_DebugLineCommands;

  // other command args buffers
  StructuredBuffer m_EarlyDrawCommands;
  StructuredBuffer m_CulledDrawCommands;
  StructuredBuffer m_LateDrawCommands;
  //StructuredBuffer m_LateDrawCommands;

  StructuredBuffer m_MeshletsIndexBuffer;
  StructuredBuffer m_MeshletsInstances;
  StructuredBuffer m_MeshletsVisibleInstances;

  GpuMeshDrawCounts m_MeshDrawCounts;

  // meshletsInstances_buffer
  // meshletsIndex_buffer
};



