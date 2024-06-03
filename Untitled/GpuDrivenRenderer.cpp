#include "GpuDrivenRenderer.hpp"
#include "meshoptimizer/meshoptimizer.h"

/*
1. first on cpu
prepare the meshlets and their bounds and send them to gpu
2. do gpu culling

resources (create/destroy)

mesh/meshlet resources:
StructuredBuffers:
   meshlet_data_sb 
   meshlets_vertex_data_sb
   meshlets_vertex_pos_sb
   meshlets_sb
   meshes_sb
   mesh_bounds_sb
   mesh_instances_sb

per frame resources
assuming k_max_frames = 2

   meshlets_instances_sb[ k_max_frames ];
   meshlets_index_buffer_sb[k_max_frames];
   meshlets_visible_instances_sb[k_max_frames];


geometry (gbuffer) pass prepare
Indirect draw commands
   mesh_task_indirect_count_early_sb[k_max_frames];
   mesh_task_indirect_early_commands_sb[k_max_frames];
   mesh_task_indirect_culled_commands_sb[k_max_frames];

   mesh_task_indirect_count_late_sb[k_max_frames];
   mesh_task_indirect_late_commands_sb[k_max_frames];

   meshlet_instances_indirect_count_sb[k_max_frames];


- Indirect dispatch calls

- Generate meshlet list

- Cull visible meshlets

- Write counts

- Generate index buffer

main gbuffer renderpass
- draw_mesh_task_indirect_count


gltf-scene

Mesh optimizer:
add/link the library

when importing gltf, implement "add_mesh" using library "meshopt_buildMeshlets"

fill the meshlet triangles, indices, vertices arrays

compute meshlet bounds using meshopt_computeMeshletBounds

store geometry_transform

*/

void GpuDrivenRenderer::init(ID3D12Device* p_Device)
{

}

void GpuDrivenRenderer::addMesh(Mesh& p_Mesh)
{
  // 1. Determine the maximum number of meshlets that could be generated for the mesh
  const size_t maxVertices = 64;
  const size_t maxTriangles = 124;
  const float coneWeight = 0.0f;
  const size_t maxMeshlets = meshopt_buildMeshletsBound(p_Mesh.NumIndices(), maxVertices, maxTriangles);

  // 2. Allocate memory for the vertices and indices arrays that describe the meshlets
  std::vector<meshopt_Meshlet> localMeshlets;
  localMeshlets.resize(maxMeshlets);

  // list of vertex indices (4 bytes)
  std::vector<uint32_t> meshletVertexIndices;
  meshletVertexIndices.resize(maxMeshlets * maxVertices);

  // list of triangle indices (1 byte)
  std::vector<uint8_t> meshletTriangles;
  meshletTriangles.resize(maxMeshlets * maxTriangles * 3);

  // flatten vertex positions as a float array
  const uint32_t numVertices = p_Mesh.NumVertices();
  std::vector<float> flattenedVerts;
  flattenedVerts.resize(numVertices * 3);
  for (uint32_t i = 0; i < numVertices; ++i)
  {
    const MeshVertex* vertPtr = p_Mesh.Vertices();
    flattenedVerts.at(i * 3) = vertPtr[i].Position.x;
    flattenedVerts.at(i * 3 + 1) = vertPtr[i].Position.y;
    flattenedVerts.at(i * 3 + 2) = vertPtr[i].Position.z;
  }

  // 3. Generate meshlets
  const size_t indexCount = p_Mesh.NumIndices();
  const uint16_t* indices = p_Mesh.Indices();
  size_t meshletCount = meshopt_buildMeshlets(
      localMeshlets.data(),
      meshletVertexIndices.data(),
      meshletTriangles.data(),
      indices,
      indexCount,
      flattenedVerts.data(),
      numVertices,
      sizeof(glm::vec3),
      maxVertices,
      maxTriangles,
      coneWeight);

  p_Mesh.m_MeshletCount = static_cast<uint32_t>(meshletCount);

  // Extract data
  uint32_t meshletVertexOffset = static_cast<uint32_t>(m_MeshletsVertexPositions.size());
  for (uint32_t v = 0; v < numVertices; ++v)
  {
    GpuMeshletVertexPosition meshletVertexPos{};

    float x = flattenedVerts[v * 3 + 0];
    float y = flattenedVerts[v * 3 + 1];
    float z = flattenedVerts[v * 3 + 2];

    meshletVertexPos.position[0] = x;
    meshletVertexPos.position[1] = y;
    meshletVertexPos.position[2] = z;

    m_MeshletsVertexPositions.push_back(meshletVertexPos);

    GpuMeshletVertexData meshletVertexData{};

    const MeshVertex* vertPtr = p_Mesh.Vertices();
    // Normals
    {
      meshletVertexData.normal[0] = (vertPtr[v].Normal.x + 1.0f) * 127.0f;
      meshletVertexData.normal[1] = (vertPtr[v].Normal.y + 1.0f) * 127.0f;
      meshletVertexData.normal[2] = (vertPtr[v].Normal.z + 1.0f) * 127.0f;
    }

    // Tangents
    {
      meshletVertexData.tangent[0] = (vertPtr[v].Tangent.x + 1.0f) * 127.0f;
      meshletVertexData.tangent[1] = (vertPtr[v].Tangent.y + 1.0f) * 127.0f;
      meshletVertexData.tangent[2] = (vertPtr[v].Tangent.z + 1.0f) * 127.0f;
      meshletVertexData.tangent[3] = 0;
      //meshletVertexData.tangent[3] = (vertPtr[v].Tangent.w + 1.0f) * 127.0f;
    }

    meshletVertexData.uvCoords[0] = meshopt_quantizeHalf(vertPtr[v].UV.x);
    meshletVertexData.uvCoords[1] = meshopt_quantizeHalf(vertPtr[v].UV.y);

    m_MeshletsVertexData.push_back(meshletVertexData);
  }

  // 5. Extract additional data (bounding sphere and cone) for each meshlet:



}
