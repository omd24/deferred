
#include "VolumetricFog.hpp"
#include "d3dx12.h"
#include "pix3.h"
#include "../AppSettings.hpp"

static const uint32_t MaxInputs = 8;

// TODOs:
// - Add correct scattering-transmittance calculation logic (Beer's law)
// - Add imgui support for box density
// - Add constant fog and exponential height fog (with imgui support)
// - Add spatial filter
// - Add imgui support for box position and size
// - Add noise baking pass
// - Add noise (several possibilities)
// - Add temporal filter
// - Add spot light contribution
// - Add shadows contribution
// - Add GI (separate system) and its contribution here
// - 


namespace AppSettings
{
const extern uint32_t CBufferRegister;
void bindCBufferCompute(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter);
} // namespace AppSettings

struct FogConstants
{
  glm::mat4x4 ProjMat;
  glm::mat4x4 InvViewProj;

  glm::vec2 Resolution;
  float NearClip = 0.0f;
  float FarClip = 0.0f;

  uint32_t ClusterBufferIdx = uint32_t(-1);
  uint32_t DepthBufferIdx = uint32_t(-1);
  uint32_t SpotLightShadowIdx = uint32_t(-1);
  uint32_t DataVolumeIdx = uint32_t(-1);

  uint32_t ScatteringVolumeIdx = uint32_t(-1);
  glm::uvec3 Dimensions;

  float PhaseAnisotropy01;
  glm::vec3 CameraPos;
};

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
  RenderPass_DataInjection,
  RenderPass_LightContribution,
  RenderPass_FinalIntegration,

  NumRenderPasses,
};

void VolumetricFog::init(ID3D12Device * p_Device)
{
  // Load and compile shaders:
  {
    bool res = true;
    ID3DBlobPtr errorBlob;
    ID3DBlobPtr tempDataInjShader = nullptr;
    ID3DBlobPtr tempLightContributionShader = nullptr;
    ID3DBlobPtr tempFinalIntegralShader = nullptr;

#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif
    // Unbounded size descriptor tables
    compileFlags |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
    WCHAR assetsPath[512];
    getAssetsPath(assetsPath, _countof(assetsPath));
    std::wstring shaderPath = assetsPath;
    shaderPath += L"Shaders\\VolumetricFog.hlsl";

    // Data injection shader
    {
      const D3D_SHADER_MACRO definesVS[] = {{"DATA_INJECTION", "1"}, {NULL, NULL}};
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          definesVS,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "DataInjectionCS",
          "cs_5_1",
          compileFlags,
          0,
          &tempDataInjShader,
          &errorBlob);
      if (nullptr == tempDataInjShader || FAILED(hr))
      {
        OutputDebugStringA("Failed to load data injection shader.\n");
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        res = false;
      }
      errorBlob = nullptr;
    }

    // Light contribution shader
    {
      const D3D_SHADER_MACRO definesVS[] = {{"LIGHT_SCATTERING", "1"}, {NULL, NULL}};
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          definesVS,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "LightContributionCS",
          "cs_5_1",
          compileFlags,
          0,
          &tempLightContributionShader,
          &errorBlob);
      if (nullptr == tempLightContributionShader || FAILED(hr))
      {
        OutputDebugStringA("Failed to load light contribution shader.\n");
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        res = false;
      }
      errorBlob = nullptr;
    }

    // Final integration shader
    {
      const D3D_SHADER_MACRO definesVS[] = {{"FINAL_INTEGRATION", "1"}, {NULL, NULL}};
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          definesVS,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "FinalIntegrationCS",
          "cs_5_1",
          compileFlags,
          0,
          &tempFinalIntegralShader,
          &errorBlob);
      if (nullptr == tempFinalIntegralShader || FAILED(hr))
      {
        OutputDebugStringA("Failed to load final integration shader.\n");
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        res = false;
      }
      errorBlob = nullptr;
    }

    // Only update the shaders if there was no issue:
    if (res)
    {
      m_DataInjectionShader = tempDataInjShader;
      m_LightContributionShader = tempLightContributionShader;
      m_FinalIntegralShader = tempFinalIntegralShader;
    }
    assert(m_DataInjectionShader && m_LightContributionShader && m_FinalIntegralShader);
  }


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
    rootParameters[RootParam_AppSettings].Descriptor.ShaderRegister =
        AppSettings::CBufferRegister;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[4] = {};
    staticSamplers[0] = GetStaticSamplerState(SamplerState::Point, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[1] =
        GetStaticSamplerState(SamplerState::LinearClamp, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[2] =
        GetStaticSamplerState(SamplerState::Linear, 2, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[3] =
        GetStaticSamplerState(SamplerState::LinearBorder, 3, 0, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = arrayCount32(staticSamplers);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    CreateRootSignature(&m_RootSig, rootSignatureDesc);
    m_RootSig->SetName(L"VolumetricFog-RootSig");
}

// Create psos
{
  m_PSOs.resize(NumRenderPasses);

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = m_RootSig;

  psoDesc.CS = CD3DX12_SHADER_BYTECODE(m_DataInjectionShader.GetInterfacePtr());
  p_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_PSOs[RenderPass_DataInjection]));
  m_PSOs[RenderPass_DataInjection]->SetName(L"Data Injection PSO");

  psoDesc.CS = CD3DX12_SHADER_BYTECODE(m_LightContributionShader.GetInterfacePtr());
  p_Device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&m_PSOs[RenderPass_LightContribution]));
  m_PSOs[RenderPass_LightContribution]->SetName(L"Light contribution PSO");

  psoDesc.CS = CD3DX12_SHADER_BYTECODE(m_FinalIntegralShader.GetInterfacePtr());
  p_Device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&m_PSOs[RenderPass_FinalIntegration]));
  m_PSOs[RenderPass_FinalIntegration]->SetName(L"Final integration PSO");
}

// Create shader UAVs
{
  VolumeTextureInit vtInit;
  vtInit.Width = m_Dimensions.x;
  vtInit.Height = m_Dimensions.y;
  vtInit.Depth = m_Dimensions.z;
  vtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  vtInit.InitialState =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  vtInit.Name = L"Data Volume Texture";
  m_DataVolume.init(vtInit);
}

{
  VolumeTextureInit vtInit;
  vtInit.Width = m_Dimensions.x;
  vtInit.Height = m_Dimensions.y;
  vtInit.Depth = m_Dimensions.z;
  vtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  vtInit.InitialState =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  vtInit.Name = L"Scattering Volume Texture";
  m_ScatteringVolume.init(vtInit);
}

{
  VolumeTextureInit vtInit;
  vtInit.Width = m_Dimensions.x;
  vtInit.Height = m_Dimensions.y;
  vtInit.Depth = m_Dimensions.z;
  vtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  vtInit.InitialState =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  vtInit.Name = L"Final Volume Texture";
  m_FinalVolume.init(vtInit);
}
}
//---------------------------------------------------------------------------//
void VolumetricFog::deinit()
{
  for (unsigned i = 0; i < NumRenderPasses; ++i)
  {
    m_PSOs[i]->Release();
  }

  m_RootSig->Release();

  m_DataInjectionShader = nullptr;
  m_LightContributionShader = nullptr;
  m_FinalIntegralShader = nullptr;

  m_FinalVolume.deinit();
  m_ScatteringVolume.deinit();
  m_DataVolume.deinit();
}
//---------------------------------------------------------------------------//
void VolumetricFog::render(
    ID3D12GraphicsCommandList* p_CmdList,
    uint32_t p_ClusterBufferSrv,
    uint32_t p_DepthBufferSrv,
    uint32_t p_SpotLightShadowSrv,
    float p_Near, float p_Far,
    float p_ScreenWidth, float p_ScreenHeight,
    FirstPersonCamera const& p_Camera)
{
  assert(p_CmdList != nullptr);

  PIXBeginEvent(p_CmdList, 0, "Volumetric Fog");
  
  const uint32_t dispatchGroupX = alignUp<uint32_t>(m_Dimensions.x, 8) / 8;
  const uint32_t dispatchGroupY = alignUp<uint32_t>(m_Dimensions.y, 8) / 8;

  // 1. Data injection
  {
    PIXBeginEvent(p_CmdList, 0, "Data Injection");

    // Prepare volume buffer for write
    m_DataVolume.makeWritable(p_CmdList);

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_DataInjection]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers

    {
      FogConstants uniforms;
      // The transpose of a matrix is same as transpose of the inverse of that matrix.

      uniforms.InvViewProj = glm::transpose(glm::inverse(p_Camera.ViewMatrix() * p_Camera.ProjectionMatrix()));
      uniforms.ProjMat = glm::transpose(p_Camera.ProjectionMatrix());
      uniforms.Resolution = glm::vec2(p_ScreenWidth, p_ScreenHeight);
      uniforms.NearClip = p_Near;
      uniforms.FarClip = p_Far;

      uniforms.ClusterBufferIdx = p_ClusterBufferSrv;
      uniforms.DepthBufferIdx = p_DepthBufferSrv;
      uniforms.SpotLightShadowIdx = p_SpotLightShadowSrv;

      uniforms.Dimensions = m_Dimensions;
      uniforms.CameraPos = p_Camera.Position();

      BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);
    }

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {m_DataVolume.UAV};
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    p_CmdList->Dispatch(dispatchGroupX, dispatchGroupY, m_Dimensions.z);

    // Sync back volume buffer to be read
    m_DataVolume.makeReadable(p_CmdList);

    PIXEndEvent(p_CmdList);
  }

  // 2. Light contribution
  {
    PIXBeginEvent(p_CmdList, 0, " Light Scattering");

    m_ScatteringVolume.makeWritable(p_CmdList);

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_LightContribution]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers
    {
      FogConstants uniforms;
      uniforms.InvViewProj = glm::mat4();
      uniforms.ProjMat = glm::mat4();
      uniforms.Resolution = glm::vec2(p_ScreenWidth, p_ScreenHeight);
      uniforms.NearClip = p_Near;
      uniforms.FarClip = p_Far;

      uniforms.ClusterBufferIdx = p_ClusterBufferSrv;
      uniforms.DepthBufferIdx = p_DepthBufferSrv;
      uniforms.SpotLightShadowIdx = p_SpotLightShadowSrv;
      uniforms.DataVolumeIdx = m_DataVolume.getSRV();

      uniforms.Dimensions = m_Dimensions;

      uniforms.CameraPos = p_Camera.Position();

      BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);
    }

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {m_ScatteringVolume.UAV};
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    p_CmdList->Dispatch(dispatchGroupX, dispatchGroupY, m_Dimensions.z);

    // Sync back scatter volume to be read
    m_ScatteringVolume.makeReadable(p_CmdList);

    PIXEndEvent(p_CmdList);
  }

  // 3. Final integration
  {
    PIXBeginEvent(p_CmdList, 0, " Final Integration");
    
    m_FinalVolume.makeWritable(p_CmdList);

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_FinalIntegration]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers
    {
      FogConstants uniforms;
      uniforms.InvViewProj = glm::mat4();
      uniforms.ProjMat = glm::mat4();
      uniforms.Resolution = glm::vec2(p_ScreenWidth, p_ScreenHeight);
      uniforms.NearClip = p_Near;
      uniforms.FarClip = p_Far;

      uniforms.ClusterBufferIdx = p_ClusterBufferSrv;
      uniforms.DepthBufferIdx = p_DepthBufferSrv;
      uniforms.SpotLightShadowIdx = p_SpotLightShadowSrv;
      uniforms.ScatteringVolumeIdx = m_ScatteringVolume.getSRV();

      uniforms.Dimensions = m_Dimensions;

      uniforms.CameraPos = p_Camera.Position();

      BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);
    }

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {m_FinalVolume.UAV};
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    // NOTE: Z = 1 as we integrate inside the shader.
    p_CmdList->Dispatch(dispatchGroupX, dispatchGroupY, 1);

    // Sync back final volume to be read
    m_FinalVolume.makeReadable(p_CmdList);

    PIXEndEvent(p_CmdList);
  }

  PIXEndEvent(p_CmdList);
}
//---------------------------------------------------------------------------//
