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

// Important NOTE: the scale of positions and Z direction is different from vulkan!
// compare the first boundign sphere for an example


void GpuDrivenRenderer::init(ID3D12Device* p_Device)
{

}

void GpuDrivenRenderer::deinit()
{
  // Release all created resources
  //
}

void GpuDrivenRenderer::addMeshes(std::vector<Mesh>& meshes)
{
  for (uint32_t p = 0; p < meshes.size(); ++p)
  {
    Mesh mesh_ = meshes[p];

    // Bounding sphere:
    {
      glm::vec3 center = (mesh_.AABBMax() + mesh_.AABBMin());
      center *= 0.5f;
      float radius =
          glm::max(glm::distance(mesh_.AABBMax(), center), glm::distance(mesh_.AABBMin(), center));
      mesh_.m_BoundingSphere = glm::vec4(center, radius);
    }

  // 1. Determine the maximum number of meshlets that could be generated for the mesh
    const size_t maxVertices = 64;
    const size_t maxTriangles = 124;
    const float coneWeight = 0.0f;
    const size_t maxMeshlets =
        meshopt_buildMeshletsBound(mesh_.NumIndices(), maxVertices, maxTriangles);

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
    const uint32_t numVertices = mesh_.NumVertices();
    std::vector<float> flattenedVerts;
    flattenedVerts.resize(numVertices * 3);
    for (uint32_t i = 0; i < numVertices; ++i)
    {
      const MeshVertex* vertPtr = mesh_.Vertices();
      flattenedVerts.at(i * 3) = vertPtr[i].Position.x;
      flattenedVerts.at(i * 3 + 1) = vertPtr[i].Position.y;
      flattenedVerts.at(i * 3 + 2) = vertPtr[i].Position.z;
    }

    // 3. Generate meshlets
    const size_t indexCount = mesh_.NumIndices();
    const uint16_t* indices = mesh_.Indices();
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

    mesh_.m_MeshletCount = static_cast<uint32_t>(meshletCount);

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

      const MeshVertex* vertPtr = mesh_.Vertices();
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
        // meshletVertexData.tangent[3] = (vertPtr[v].Tangent.w + 1.0f) * 127.0f;
      }

      meshletVertexData.uvCoords[0] = meshopt_quantizeHalf(vertPtr[v].UV.x);
      meshletVertexData.uvCoords[1] = meshopt_quantizeHalf(vertPtr[v].UV.y);

      m_MeshletsVertexData.push_back(meshletVertexData);
    }

    // Cache meshlet offset
    mesh_.m_MeshletOffset = m_Meshlets.size();
    mesh_.m_MeshletCount = meshletCount;
    mesh_.m_MeshletIndexCount = 0;

    // 5. Extract additional data (bounding sphere and cone) for each meshlet:
    // Append meshlet data
    for (uint32_t m = 0; m < meshletCount; ++m)
    {
      meshopt_Meshlet& localMeshlet = localMeshlets[m];

      meshopt_Bounds meshlet_bounds = meshopt_computeMeshletBounds(
          meshletVertexIndices.data() + localMeshlet.vertex_offset,
          meshletTriangles.data() + localMeshlet.triangle_offset,
          localMeshlet.triangle_count,
          flattenedVerts.data(),
          numVertices,
          sizeof(glm::vec3));

      GpuMeshlet meshlet{};
      meshlet.dataOffset = m_MeshletsData.size();
      meshlet.vertexCount = localMeshlet.vertex_count;
      meshlet.triangleCount = localMeshlet.triangle_count;

      meshlet.center =
          glm::vec3{meshlet_bounds.center[0], meshlet_bounds.center[1], meshlet_bounds.center[2]};
      meshlet.radius = meshlet_bounds.radius;

      meshlet.coneAxis[0] = meshlet_bounds.cone_axis_s8[0];
      meshlet.coneAxis[1] = meshlet_bounds.cone_axis_s8[1];
      meshlet.coneAxis[2] = meshlet_bounds.cone_axis_s8[2];

      meshlet.coneCutoff = meshlet_bounds.cone_cutoff_s8;
      meshlet.meshIndex = m_Meshes.size();

      // Resize data array
      const uint32_t indexGroupCount = (localMeshlet.triangle_count * 3 + 3) / 4;
      m_MeshletsData.reserve(m_MeshletsData.size() + localMeshlet.vertex_count + indexGroupCount);

      for (uint32_t i = 0; i < meshlet.vertexCount; ++i)
      {
        const uint32_t vertexIndex =
            meshletVertexOffset + meshletVertexIndices[localMeshlet.vertex_offset + i];
        m_MeshletsData.push_back(vertexIndex);
      }

      // Store indices as uint32
      // NOTE(marco): we write 4 indices at at time, it will come in handy in the mesh shader
      const uint32_t* indexGroups =
          reinterpret_cast<const uint32_t*>(meshletTriangles.data() + localMeshlet.triangle_offset);
      for (uint32_t i = 0; i < indexGroupCount; ++i)
      {
        const uint32_t index_group = indexGroups[i];
        m_MeshletsData.push_back(index_group);
      }

      // Writing in group of fours can be problematic, if there are non multiple of 3
      // indices a triangle can be shared between meshlets.
      // We need to add some padding for that.
      // This is visible only if we emulate meshlets (not using actual mesh shaders),
      // so probably there are controls at driver level that avoid this problems when using mesh
      // shaders. Check for the last 3 indices: if last one are two are zero, then add one or two
      // groups of empty triangles.
      uint32_t lastIndexGroup = indexGroups[indexGroupCount - 1];
      uint32_t lastIndex = (lastIndexGroup >> 8) & 0xff;
      uint32_t secondLastIndex = (lastIndexGroup >> 16) & 0xff;
      uint32_t thirdLastIndex = (lastIndexGroup >> 24) & 0xff;
      if (lastIndex != 0 && thirdLastIndex == 0)
      {

        if (secondLastIndex != 0)
        {
          // Add a single index group of zeroes
          m_MeshletsData.push_back(0);
          meshlet.triangleCount++;
        }

        meshlet.triangleCount++;
        // Add another index group of zeroes
        m_MeshletsData.push_back(0);
      }

      mesh_.m_MeshletIndexCount += meshlet.triangleCount * 3;

      m_Meshlets.push_back(meshlet);

      m_MeshletsIndexCount += indexGroupCount;
    }

    // Add mesh with all data
    m_Meshes.push_back(mesh_);

    while (m_Meshlets.size() % 32)
      m_Meshlets.push_back(GpuMeshlet());

  }
}

void GpuDrivenRenderer::createResources()
{
  // meshlets_data_sb
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(uint32_t);
    sbInit.NumElements = m_MeshletsData.size();
    sbInit.Dynamic = false;
    sbInit.CPUAccessible = false;
    sbInit.InitData = m_MeshletsData.data();
    m_MeshletsDataBuffer.init(sbInit);
    m_MeshletsDataBuffer.resource()->SetName(L"meshlets_data_sb");
  }

  // meshlets_vertex_pos_sb
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(GpuMeshletVertexPosition);
    sbInit.NumElements = m_MeshletsVertexPositions.size();
    sbInit.Dynamic = false;
    sbInit.CPUAccessible = false;
    sbInit.InitData = m_MeshletsVertexPositions.data();
    m_MeshletsVertexPosBuffer.init(sbInit);
    m_MeshletsVertexPosBuffer.resource()->SetName(L"meshlets_vertex_pos_sb");
  }

  // meshlets_vertex_data_sb
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(GpuMeshletVertexData);
    sbInit.NumElements = m_MeshletsVertexData.size();
    sbInit.Dynamic = false;
    sbInit.CPUAccessible = false;
    sbInit.InitData = m_MeshletsVertexData.data();
    m_MeshletsVertexDataBuffer.init(sbInit);
    m_MeshletsVertexDataBuffer.resource()->SetName(L"meshlets_vertex_data_sb");
  }

  // meshlets buffer
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(GpuMeshlet);
    sbInit.NumElements = m_Meshlets.size();
    sbInit.Dynamic = false;
    sbInit.CPUAccessible = false;
    sbInit.InitData = m_Meshlets.data();
    m_MeshletsBuffer.init(sbInit);
    m_MeshletsBuffer.resource()->SetName(L"meshlet_sb");
  }

  // meshes buffer
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(GpuMaterialData);
    sbInit.NumElements = m_Meshes.size();
    sbInit.Dynamic = true; // should be this dynamic
    sbInit.CPUAccessible = true;
    m_MeshletsBuffer.init(sbInit);
    m_MeshletsBuffer.resource()->SetName(L"meshes_sb");
  }

  // mesh bound
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(glm::vec4);
    sbInit.NumElements = m_Meshes.size();
    sbInit.Dynamic = true; // should be this dynamic
    sbInit.CPUAccessible = true;
    m_MeshBoundsBuffer.init(sbInit);
    m_MeshBoundsBuffer.resource()->SetName(L"mesh_bound_sb");
  }

  continuehere
  ...

      

  // mesh instances buffer ?
  // n.b., this is meshlet instances so use meshlets to fill it 

  // per frame resource

    // early_draw_commands_sb

    // culled_draw_commands_sb

    // late_draw_commands_sb

    // meshlets_instances_buffer

    // meshlets_index_buffer

    // mesh_task_indirect_late_commands_sb

    // mesh_task_indirect_early_commands_sb

  // debug draw buffers

  // material buffer ?

  // 

  // 
}

