
#include "VolumetricFog.hpp"
#include "d3dx12.h"
#include "pix3.h"
#include "../AppSettings.hpp"

static const uint32_t MaxInputs = 8;

namespace AppSettings
{
const extern uint32_t CBufferRegister;
void bindCBufferCompute(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter);
} // namespace AppSettings

struct FogConstants
{
  uint32_t Variable1 = 0;
  uint32_t Variable2 = 0;
  float NearClip = 0.0f;
  float FarClip = 0.0f;

  uint32_t ClusterBufferIdx = uint32_t(-1);
  uint32_t DepthBufferIdx = uint32_t(-1);
  uint32_t SpotLightShadowIdx = uint32_t(-1);
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
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
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
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
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
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
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
      m_LightContributionShader = tempFinalIntegralShader;
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
    staticSamplers[0] = GetStaticSamplerState(SamplerState::Point, 0);
    staticSamplers[1] = GetStaticSamplerState(SamplerState::LinearClamp, 1);
    staticSamplers[2] = GetStaticSamplerState(SamplerState::Linear, 2);
    staticSamplers[3] = GetStaticSamplerState(SamplerState::LinearBorder, 3);

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
  vtInit.Width = 128;
  vtInit.Height = 128;
  vtInit.Depth = 128;
  vtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  vtInit.InitialState =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  vtInit.Name = L"Data Volume Texture";
  m_DataVolume.init(vtInit);
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

  m_DataVolume.deinit();
}
//---------------------------------------------------------------------------//
void VolumetricFog::render(
    ID3D12GraphicsCommandList* p_CmdList,
    uint32_t p_ClusterBufferSrv,
    uint32_t p_DepthBufferSrv,
    uint32_t p_SpotLightShadowSrv
)
{
  assert(p_CmdList != nullptr);

  const uint32_t dispatchGroupX = std::ceilf(m_DataVolume.getWidth() / 8.0f);
  const uint32_t dispatchGroupY = std::ceilf(m_DataVolume.getHeight() / 8.0f);

  // 1. Data injection
  {
    PIXBeginEvent(p_CmdList, 0, "VolumetricFog-DataInjection");

    // Prepare volume buffer for write
    m_DataVolume.makeWritable(p_CmdList);

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_DataInjection]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers

    {
      FogConstants uniforms;
      uniforms.Variable1 = 0;
      uniforms.Variable2 = 0;
      uniforms.NearClip = 0;
      uniforms.FarClip = 0;

      uniforms.ClusterBufferIdx = p_ClusterBufferSrv;
      uniforms.DepthBufferIdx = p_DepthBufferSrv;
      uniforms.SpotLightShadowIdx = p_SpotLightShadowSrv;

      BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);
    }

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {m_DataVolume.UAV};
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    p_CmdList->Dispatch(8, 8, 1);

    // Sync back volume buffer to be read
    m_DataVolume.makeReadable(p_CmdList);

    PIXEndEvent(p_CmdList);
  }

  // 2. Light contribution
  {
    PIXBeginEvent(p_CmdList, 0, "VolumetricFog-LightContribution");

    PIXEndEvent(p_CmdList);
  }

  // 3. Final integration
  {
    PIXBeginEvent(p_CmdList, 0, "VolumetricFog-LightContribution");

    PIXEndEvent(p_CmdList);
  }
}
//---------------------------------------------------------------------------//
