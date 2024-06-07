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
struct GpuDrivenRenderer
{
  void init(ID3D12Device* p_Device);
  void deinit();

  void render(ID3D12GraphicsCommandList* p_CmdList);

  void addMesh(Mesh& p_Mesh);
  void createResources();

  ID3DBlobPtr m_DataShader = nullptr;

  std::vector<ID3D12PipelineState*> m_PSOs;
  ID3D12RootSignature* m_RootSig = nullptr;

  std::vector<GpuMeshlet> m_Meshlets{};
  std::vector<GpuMeshletVertexPosition> m_MeshletsVertexPositions{};
  std::vector<GpuMeshletVertexData> m_MeshletsVertexData{};
  std::vector<uint32_t> m_MeshletsData;
  uint32_t m_MeshletsIndexCount;

  // copy of meshes for gpu driven rendering
  std::vector<Mesh> m_Meshes{};

  // Gpu resources
  StructuredBuffer m_MeshletsDataBuffer;
  StructuredBuffer m_MeshletsVertexPosBuffer;
  StructuredBuffer m_MeshletsVertexDataBuffer;
  StructuredBuffer m_MeshletsBuffer;
  StructuredBuffer m_MeshesBuffer;
  StructuredBuffer m_MeshInstancesBuffer;

  // command args buffers
  StructuredBuffer m_EarlyDrawCommands;
  StructuredBuffer m_CulledDrawCommands;
  StructuredBuffer m_LateDrawCommands;
  StructuredBuffer m_LateDrawCommands;
  StructuredBuffer m_MeshTaskIndirectLateCommands;
  StructuredBuffer m_MeshTaskIndirectEarlyCommands;


  // meshlets_instances_buffer
  // meshlets_index_buffer

};



