#include "GpuDrivenRenderer.hpp"
#include "meshoptimizer/meshoptimizer.h"
#include "d3dx12.h"
#include "pix3.h"
#include "../AppSettings.hpp"

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

enum RootParams : uint32_t
{
  RootParam_StandardDescriptors,
  RootParam_Cbuffer,
  RootParam_UAVDescriptors,
  RootParam_AppSettings,

  NumRootParams,
};

enum RenderPass : uint32_t
{
  RenderPass_GpuCulling,
  RenderPass_GbufferEarly,
  
  NumRenderPasses,
};

namespace AppSettings
{
const extern uint32_t CBufferRegister;
void bindCBufferCompute(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter);
} // namespace AppSettings

struct Uniforms
{
  float NearClip = 0.0f;
  float FarClip = 0.0f;
};

void GpuDrivenRenderer::init(ID3D12Device* p_Device)
{
  // Create root signature:
  {
    D3D12_DESCRIPTOR_RANGE1 uavRanges[1] = {};
    uavRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRanges[0].NumDescriptors = 1;
    uavRanges[0].BaseShaderRegister = 0;
    uavRanges[0].RegisterSpace = 0;
    uavRanges[0].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 rootParameters[NumRootParams] = {};

    rootParameters[RootParam_StandardDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[RootParam_StandardDescriptors].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[RootParam_StandardDescriptors].DescriptorTable.pDescriptorRanges =
        StandardDescriptorRanges();
    rootParameters[RootParam_StandardDescriptors].DescriptorTable.NumDescriptorRanges =
        NumStandardDescriptorRanges;

    // UAV's
    rootParameters[RootParam_UAVDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[RootParam_UAVDescriptors].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[RootParam_UAVDescriptors].DescriptorTable.pDescriptorRanges = uavRanges;
    rootParameters[RootParam_UAVDescriptors].DescriptorTable.NumDescriptorRanges =
        arrayCount32(uavRanges);

    // Uniforms
    rootParameters[RootParam_Cbuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[RootParam_Cbuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[RootParam_Cbuffer].Descriptor.RegisterSpace = 0;
    rootParameters[RootParam_Cbuffer].Descriptor.ShaderRegister = 0;
    rootParameters[RootParam_Cbuffer].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    // AppSettings
    rootParameters[RootParam_AppSettings].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[RootParam_AppSettings].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[RootParam_AppSettings].Descriptor.RegisterSpace = 0;
    rootParameters[RootParam_AppSettings].Descriptor.ShaderRegister = AppSettings::CBufferRegister;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[5] = {};
    staticSamplers[0] =
        GetStaticSamplerState(SamplerState::Point, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[1] =
        GetStaticSamplerState(SamplerState::LinearClamp, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[2] =
        GetStaticSamplerState(SamplerState::Linear, 2, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[3] =
        GetStaticSamplerState(SamplerState::LinearBorder, 3, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[4] =
        GetStaticSamplerState(SamplerState::ShadowMapPCF, 4, 0, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = arrayCount32(staticSamplers);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    CreateRootSignature(&m_RootSig, rootSignatureDesc);
    m_RootSig->SetName(L"GpuDrivenRenderer-RootSig");
  }

  // Create the command signature used for indirect drawing.
  {
    // Each command consists of an amplification/mesh shader call.
    D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[1] = {};
    argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
    commandSignatureDesc.pArgumentDescs = argumentDescs;
    commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
    commandSignatureDesc.ByteStride = sizeof(IndirectCommand);

#if 0 // if changing the root signature
    p_Device->CreateCommandSignature(
        &commandSignatureDesc, m_RootSig, IID_PPV_ARGS(&m_CommandSignature));
    m_CommandSignature->SetName(L"GpuDrivenRenderer-CommandSignature");
#endif
    p_Device->CreateCommandSignature(
        &commandSignatureDesc, nullptr, IID_PPV_ARGS(&m_CommandSignature));
    m_CommandSignature->SetName(L"GpuDrivenRenderer-CommandSignature");
  }
}

void GpuDrivenRenderer::deinit()
{
  // TODO:
  // Release all created resources and whatnot
  //
}

void GpuDrivenRenderer::addMeshes(std::vector<Mesh>& meshes)
{
  assert(0 == m_MeshletsVertexPositions.size());
  assert(0 == m_MeshletsVertexData.size());
  assert(0 == m_MeshletsData.size());
  assert(0 == m_Meshlets.size());
  assert(0 == m_Meshes.size());
  assert(0 == m_MeshInstances.size());
  
  uint32_t totalMeshlets = 0;
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


    // mesh instances
    {
      MeshInstance meshInstance{};
      // Assign scene graph node index
      meshInstance.sceneGraphNodeIndex = 0;

      //glTF::MeshPrimitive& mesh_primitive = gltf_mesh.primitives[primitive_index];

      // Cache parent mesh
      meshInstance.mesh = &meshes[p];

      // TODO(OM): Handle material per instance

      // Cache gpu mesh instance index, used to retrieve data on gpu.
      meshInstance.gpuMeshInstanceIndex = m_MeshInstances.size();

      // TODO(OM): Skinning

      totalMeshlets += meshInstance.mesh->m_MeshletCount;

      printf("Current total meshlet instances %u\n", totalMeshlets);

      m_MeshInstances.push_back(meshInstance);
    }
  }
}

void GpuDrivenRenderer::createResources(ID3D12Device* p_Device)
{
#if defined(_DEBUG)
  UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  UINT compileFlags = 0;
#endif
  // Unbounded size descriptor tables
  compileFlags |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
  WCHAR assetsPath[512];
  getAssetsPath(assetsPath, _countof(assetsPath));
  const std::wstring ShaderPath = assetsPath;

  // Gpu culling shader
  {
    std::wstring cullingShaderPath = ShaderPath + L"Shaders\\Culling.hlsl";
    const D3D_SHADER_MACRO defines[] = {{"GPU_CULLING", "1"}, {NULL, NULL}};
    compileShader(
        "gpu culling",
        cullingShaderPath.c_str(),
        defines,
        compileFlags,
        ShaderType::Compute,
        "CullingCS",
        m_CullingShader);
  }

  // Gbuffer shader
  {
    std::wstring meshletShaderPath = ShaderPath + L"Shaders\\Meshlet.hlsl";
    const D3D_SHADER_MACRO defines[] = {{"Gbuffer_Meshlet", "1"}, {NULL, NULL}};
    compileShader(
        "gbuffer meshlet",
        meshletShaderPath.c_str(),
        defines,
        compileFlags,
        ShaderType::Compute,
        "MeshletCS",
        m_GbufferShader);
  }

  assert(m_CullingShader && m_GbufferShader);

  // Create psos
  {
    m_PSOs.resize(NumRenderPasses);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_RootSig;

    psoDesc.CS = CD3DX12_SHADER_BYTECODE(m_CullingShader.GetInterfacePtr());
    p_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_PSOs[RenderPass_GpuCulling]));
    m_PSOs[RenderPass_GpuCulling]->SetName(L"Gpu Culling PSO");

    psoDesc.CS = CD3DX12_SHADER_BYTECODE(m_GbufferShader.GetInterfacePtr());
    p_Device->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&m_PSOs[RenderPass_GbufferEarly]));
    m_PSOs[RenderPass_GbufferEarly]->SetName(L"Gbuffer Meshlet PSO");
  }

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
    sbInit.Dynamic = false;
    sbInit.CPUAccessible = false; // does it need to be host visible ?
    m_MeshletsBuffer.init(sbInit);
    m_MeshletsBuffer.resource()->SetName(L"meshes_sb");
  }

  // mesh bound
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(glm::vec4);
    sbInit.NumElements = m_Meshes.size();
    sbInit.Dynamic = false;
    sbInit.CPUAccessible = false; // does it need to be host visible ?
    m_MeshBoundsBuffer.init(sbInit);
    m_MeshBoundsBuffer.resource()->SetName(L"mesh_bound_sb");
  }

  // mesh instance
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(GpuMeshInstanceData);
    sbInit.NumElements = m_MeshInstances.size();
    sbInit.Dynamic = false;
    sbInit.CPUAccessible = false; // does it need to be host visible ?
    m_MeshBoundsBuffer.init(sbInit);
    m_MeshBoundsBuffer.resource()->SetName(L"mesh_instances_sb");
  }

  // Create indirect buffers, dynamic (i.e., need multiple buffering)
  {
   //{
   //   RenderTextureInit rtInit;
   //   rtInit.Width = m_Info.m_Width;
   //   rtInit.Height = m_Info.m_Height;
   //   rtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
   //   rtInit.MSAASamples = 1;
   //   rtInit.ArraySize = 1;
   //   rtInit.CreateUAV = true;
   //   rtInit.InitialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
   //   rtInit.Name = L"Main Target";
   //   m_MeshTaskIndirectEarlyCommands.init(rtInit);
   // }


    // This buffer contains both opaque and transparent commands, thus is multiplied by two. 
    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(GpuMeshDrawCommand);
      sbInit.NumElements = m_MeshInstances.size() * 2;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      sbInit.CPUAccessible = false;
      m_MeshTaskIndirectEarlyCommands.init(sbInit);
      m_MeshTaskIndirectEarlyCommands.resource()->SetName(L"early_draw_commands_sb");
    }

    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(GpuMeshDrawCommand);
      sbInit.NumElements = m_MeshInstances.size() * 2;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      sbInit.CPUAccessible = false;
      m_MeshTaskIndirectCulledCommands.init(sbInit);
      m_MeshTaskIndirectCulledCommands.resource()->SetName(L"culled_draw_commands_sb");
    }

    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(GpuMeshDrawCommand);
      sbInit.NumElements = m_MeshInstances.size() * 2;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      sbInit.CPUAccessible = false;
      m_MeshTaskIndirectLateCommands.init(sbInit);
      m_MeshTaskIndirectLateCommands.resource()->SetName(L"late_draw_commands_sb");
    }

    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(GpuMeshDrawCounts);
      sbInit.NumElements = m_MeshInstances.size() * 2;
      sbInit.Dynamic = false;
      sbInit.InitialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      sbInit.CPUAccessible = false;
      sbInit.CreateUAV = true;
      m_MeshTaskIndirectCountEarly.init(sbInit);
      m_MeshTaskIndirectCountEarly.resource()->SetName(L"early_mesh_count_sb");
    }

    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(GpuMeshDrawCounts);
      sbInit.NumElements = m_MeshInstances.size() * 2;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_COPY_SOURCE;
      sbInit.CPUAccessible = true;
      sbInit.CreateUAV = false;
      m_MeshTaskIndirectCountCpuVisible.init(sbInit);
      m_MeshTaskIndirectCountCpuVisible.resource()->SetName(L"mesh_count_cpu_visible");
    }

    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(GpuMeshDrawCounts);
      sbInit.NumElements = 1;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      sbInit.CPUAccessible = false;
      m_MeshTaskIndirectCountLate.init(sbInit);
      m_MeshTaskIndirectCountLate.resource()->SetName(L"late_mesh_count_sb");
    }

    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(uint32_t);
      sbInit.NumElements = 4;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      sbInit.CPUAccessible = false;
      m_MeshletInstancesIndirectCount.init(sbInit);
      m_MeshletInstancesIndirectCount.resource()->SetName(L"meshlet_instances_indirect_sb");
    }
  }


  // Debug draw buffers
  {
    static constexpr uint32_t MaxLines = 64000 + 64000; // 3D + 2D lines in the same buffer
    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(glm::vec4);
      sbInit.NumElements = MaxLines * 2;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      sbInit.CPUAccessible = false;
      m_DebugLineStructureBuffer.init(sbInit);
      m_DebugLineStructureBuffer.resource()->SetName(L"debug_line_sb");
    }

    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(glm::vec4);
      sbInit.NumElements = 1;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      sbInit.CPUAccessible = false;
      m_DebugLineCount.init(sbInit);
      m_DebugLineCount.resource()->SetName(L"debug_line_count_sb");
    }

    // Gather 3D and 2D gpu drawing commands
    {
      StructuredBufferInit sbInit;
      sbInit.Stride = sizeof(D3D12_DRAW_ARGUMENTS);
      sbInit.NumElements = 2;
      sbInit.Dynamic = true;
      sbInit.InitialState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      sbInit.CPUAccessible = false;
      m_DebugLineCommands.init(sbInit);
      m_DebugLineCommands.resource()->SetName(L"debug_line_commands_sb");
    }
  }


  // Create per mesh material buffer
  for(Mesh & mesh : m_Meshes)
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(GpuMaterialData);
    sbInit.NumElements = 1;
    sbInit.Dynamic = true;
    sbInit.CPUAccessible = false;
    mesh.m_MaterialBuffer.init(sbInit);
    mesh.m_MaterialBuffer.resource()->SetName(L"material_buffer_");
  }

  // meshlets_index_buffer (only needed if mesh shader is not supported)
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(uint32_t) * 8;
    sbInit.NumElements = m_MeshletsIndexCount;
    sbInit.Dynamic = true;
    sbInit.CPUAccessible = false;
    m_MeshletsIndexBuffer.init(sbInit);
    m_MeshletsIndexBuffer.resource()->SetName(L"meshlets_index_buffer");
  }
  
  // meshlets_instances_buffer
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(uint32_t) * 2;
    sbInit.NumElements = m_Meshlets.size();
    sbInit.Dynamic = true;
    sbInit.CPUAccessible = false;
    m_MeshletsInstances.init(sbInit);
    m_MeshletsInstances.resource()->SetName(L"meshlets_instances_buffer");
  }
  
  // meshlets_visible_instances_sb
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(uint32_t) * 2;
    sbInit.NumElements = m_Meshlets.size();
    sbInit.Dynamic = true;
    sbInit.CPUAccessible = false;
    m_MeshletsVisibleInstances.init(sbInit);
    m_MeshletsVisibleInstances.resource()->SetName(L"meshlets_visible_instances_sb");
  }
}

void GpuDrivenRenderer::render(ID3D12GraphicsCommandList* p_CmdList, const RenderDesc& p_RenderDesc)
{

  // TODOS:
  /*
  * 
  * CullingEarlyPass 
  * for now just pass through
  * 
  * GBufferPass::prepare_draws aka gbuffer_pass_early
  * GBufferPass::render
  * LateGBufferPass::prepare_draws aka gbuffer_pass_late
  * LateGBufferPass::render
  * 
  * 
  * both use: draw_mesh_task_indirect_count
  * 
  * 
  * 
  * 
  * 
  * CullingEarlyPass
  * CullingEarlyPass::prepare_draws aka mesh_occlusion_early_pass
  * CullingEarlyPass::render and frustum culling
  * 
  * CullingLatePass
  * CullingLatePass::prepare_draws aka mesh_occlusion_late_pass
  * CullingLatePass::render
  * 
  * 
  * meshlet_pointshadows_
  * PointlightShadowPass::prepare_draws
  * PointlightShadowPass::render
  * 
  * 
  * handle resize
  * RenderScene::on_resize
  * 
  * 
  */


  // first gpu culling pass
  // Frustum cull meshes
  GpuMeshDrawCounts& meshDrawCounts = m_MeshDrawCounts;
  meshDrawCounts.opaqueMeshVisibleCount = 0;
  meshDrawCounts.opaqueMeshCulledCount = 0;
  meshDrawCounts.transparentMeshVisibleCount = 0;
  meshDrawCounts.transparentMeshCulledCount = 0;

  meshDrawCounts.totalCount = m_MeshInstances.size();

  // TODO: pass HiZ level aka depth_pyramid_texture_index, 
  meshDrawCounts.depthPyramidTextureIndex = p_RenderDesc.DepthBufferSrv;
  meshDrawCounts.lateFlag = 0;
  meshDrawCounts.meshletIndexCount = 0;
  meshDrawCounts.dispatchTaskX = 0;
  meshDrawCounts.dispatchTaskY = 1;
  meshDrawCounts.dispatchTaskZ = 1;

  // Reset mesh draw counts
  m_MeshTaskIndirectCountCpuVisible.mapAndSetData(&meshDrawCounts, m_MeshInstances.size() * 2);

  // Prepare count buffer for copy
  {
    D3D12_RESOURCE_BARRIER barriers[1] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = m_MeshTaskIndirectCountEarly.resource();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.Subresource = 0;

    p_CmdList->ResourceBarrier(_countof(barriers), barriers);
  }

  p_CmdList->CopyBufferRegion(
      m_MeshTaskIndirectCountEarly.resource(),
      0,
      m_MeshTaskIndirectCountCpuVisible.resource(),
      0,
      m_MeshInstances.size() * 2 * sizeof(GpuMeshDrawCounts));

  // Prepare count and command buffer for write
  {
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = m_MeshTaskIndirectCountEarly.resource();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[0].Transition.Subresource = 0;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[1].Transition.pResource = m_MeshTaskIndirectEarlyCommands.resource();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = 0;

    p_CmdList->ResourceBarrier(_countof(barriers), barriers);
  }

  p_CmdList->SetComputeRootSignature(m_RootSig);
  p_CmdList->SetPipelineState(m_PSOs[RenderPass_GpuCulling]);

  BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

  // Set constant buffers
  Uniforms cdata = {};
  BindTempConstantBuffer(p_CmdList, cdata, RootParam_Cbuffer, CmdListMode::Compute);

  AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

  D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
      m_MeshTaskIndirectCountEarly.m_Uav, };
  BindTempDescriptorTable(
      p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

  uint32_t groupX = static_cast<uint32_t>(ceil(m_MeshInstances.size() / 64.0f));
  p_CmdList->Dispatch(groupX, 1, 1);

  // Sync back command buffer
  {
    D3D12_RESOURCE_BARRIER barriers[1] = {};
    //barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    //barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    //barriers[0].Transition.pResource = m_MeshTaskIndirectCountEarly.resource();
    //barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    //barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    //barriers[0].Transition.Subresource = 0;

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = m_MeshTaskIndirectEarlyCommands.resource();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    barriers[0].Transition.Subresource = 0;

    p_CmdList->ResourceBarrier(_countof(barriers), barriers);
  }

//... continue culling shader (a pass through for now)

#if 0
  // Then main draw call
  if (true)
  {
    PIXBeginEvent(p_CmdList, 0, "gbuffer_pass_early");

    p_CmdList->ExecuteIndirect(
        m_CommandSignature,
        int worstCaseScenarioTotalTriangleOrMeshCount,
        ID3D12Resource * someBuffer,
        0 or ifDoubleBuffer_FrameIndexByTotalSizeOfIndirectCommand,
        nullptr or ID3D12Resource * sameOrSomeOtherBuffer,
        0 or ifSameBuffer_Offset);

    PIXEndEvent(p_CmdList);
  }
  else
  {
    PIXBeginEvent(p_CmdList, 0, "Draw all triangles");

    int maxCount = 1;
    p_CmdList->ExecuteIndirect(
        m_CommandSignature,
        maxCount,
        m_EarlyDrawCommands.resource(),
        0,
        nullptr,
        0);

    PIXEndEvent(p_CmdList);
  }
  #endif
}
