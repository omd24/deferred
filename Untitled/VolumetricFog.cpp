
#include "VolumetricFog.hpp"
#include "d3dx12.h"
#include "pix3.h"
#include "../AppSettings.hpp"

static const uint32_t MaxInputs = 8;

// TODOs:
// - Fix jitter code to properly address artifacts (maybe try Halton instead of blue noise for z-jitter ¯\_(ツ)_/¯)
// 
// - Add spatial filter
// - Add 3D noise baking pass
// - Add 3D noise for froxelization
// - Add volumetric shadows
// - Add froxel debug view
// - Add a global uniform for Fog, taa and motion vectors
// - Add local lights lut for fog sampling
// 
// Others:
// - Add GI
// - Add sky rendering
// - Add sun shadow
// - Add postfx shaft
// - Add dxc (and unify shader compilations)

// TODOs (known issues):
// - Investigate temporal pass as it might be related to the froxelization issues
// - Investigate froxel depth distribution bug (the depth/slice computation seems inaccurate):
//   https://www.desmos.com/calculator/myr1vu75cu
// - Investigate tricubic filter and compare with the other implementation:
//   https://www.desmos.com/calculator/gy9chfclsv
// - Investigate jitter and dither and alternative methods
// - investigate the issue with gamepad and imgui

namespace AppSettings
{
const extern uint32_t CBufferRegister;
void bindCBufferCompute(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter);
} // namespace AppSettings

struct FogConstants
{
  glm::mat4x4 ProjMat = glm::mat4();
  glm::mat4x4 InvViewProj = glm::mat4();
  glm::mat4x4 PrevViewProj = glm::mat4();

  glm::vec2 Resolution;
  float NearClip = 0.0f;
  float FarClip = 0.0f;

  uint32_t MaterialIDMapIdx = uint32_t(-1);
  uint32_t TangentFrameMapIndex = uint32_t(-1);
  uint32_t ClusterBufferIdx = uint32_t(-1);
  uint32_t DepthBufferIdx = uint32_t(-1);

  uint32_t SpotLightShadowIdx = uint32_t(-1);
  uint32_t DataVolumeIdx = uint32_t(-1);
  uint32_t UVMapIdx = uint32_t(-1);
  uint32_t NoiseTextureIdx = uint32_t(-1);

  glm::uvec3 Dimensions;
  uint32_t CurrFrame = uint32_t(-1);

  uint32_t ScatteringVolumeIdx = uint32_t(-1);
  uint32_t PreviousScatteringVolumeIdx = uint32_t(-1);
  uint32_t FinalIntegrationVolumeIdx = uint32_t(-1);
  uint32_t unused2 = uint32_t(-1);

  glm::vec3 CameraPos;
  uint32_t unused3 = uint32_t(-1);

  uint32_t NumXTiles;
  uint32_t NumXYTiles;
  glm::vec2 HaltonXY;
};

enum RootParams : uint32_t
{
  RootParam_StandardDescriptors,
  RootParam_Cbuffer,
  RootParam_LightsCbuffer,
  RootParam_UAVDescriptors,
  RootParam_AppSettings,

  NumRootParams,
};

enum RenderPass : uint32_t
{
  RenderPass_DataInjection,
  RenderPass_LightContribution,
  RenderPass_TemporalFilter,
  RenderPass_FinalIntegration,

  NumRenderPasses,
};

int VolumetricFog::m_CurrLightScatteringTextureIndex = 1;
int VolumetricFog::m_PrevLightScatteringTextureIndex = 0;
bool VolumetricFog::m_BuffersInitialized = false;


glm::vec3 getVolumetricFogGridZParams(float nearPlane, float farPlane, int gridSizeZ, float exponent, float offset)
{
  // S = distribution scale
  // B, O are solved for given the z distances of the first+last slice, and the # of slices.
  //
  // slice = log2(z*B + O) * S

  float S = exponent;
  float N = nearPlane + offset;
  float F = farPlane;

  float O = (F - N * pow(2.0f, (gridSizeZ - 1.0f) / S)) / (F - N);
  float B = (1.0f - O) / N;

  return glm::vec3(B, O, S);
}

void VolumetricFog::init(ID3D12Device * p_Device)
{
  // Load and compile shaders:
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
    std::wstring shaderPath = assetsPath;
    shaderPath += L"Shaders\\VolumetricFog.hlsl";

    // Data injection shader
    {
      const D3D_SHADER_MACRO defines[] = {{"DATA_INJECTION", "1"}, {NULL, NULL}};
      compileShader(
        "data injection",
        shaderPath.c_str(),
        arrayCountU8(defines),
        defines,
        compileFlags,
        ShaderType::Compute,
        "DataInjectionCS",
        m_DataInjectionShader
      );
    }

    // Light contribution shader
    {
      const D3D_SHADER_MACRO defines[] = {{"LIGHT_SCATTERING", "1"}, {NULL, NULL}};
      compileShader(
          "light contribution",
          shaderPath.c_str(),
          arrayCountU8(defines),
          defines,
          compileFlags,
          ShaderType::Compute,
          "LightContributionCS",
          m_LightContributionShader);
    }

    // Temporal filter shader
    {
      const D3D_SHADER_MACRO defines[] = {{"TEMPORAL_FILTERING", "1"}, {NULL, NULL}};
      compileShader(
          "temporal filter",
          shaderPath.c_str(),
          arrayCountU8(defines),
          defines,
          compileFlags,
          ShaderType::Compute,
          "TemporalFilterCS",
          m_TemporalFilterShader);
    }

    // Final integration shader
    {
      const D3D_SHADER_MACRO defines[] = {{"FINAL_INTEGRATION", "1"}, {NULL, NULL}};
      compileShader(
          "final integration",
          shaderPath.c_str(),
          arrayCountU8(defines),
          defines,
          compileFlags,
          ShaderType::Compute,
          "FinalIntegrationCS",
          m_FinalIntegralShader);
    }

    assert(
        m_DataInjectionShader && m_LightContributionShader && m_FinalIntegralShader &&
        m_TemporalFilterShader);
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

    // Lights buffer
    rootParameters[RootParam_LightsCbuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[RootParam_LightsCbuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[RootParam_LightsCbuffer].Descriptor.RegisterSpace = 0;
    rootParameters[RootParam_LightsCbuffer].Descriptor.ShaderRegister = 1;
    rootParameters[RootParam_LightsCbuffer].Descriptor.Flags =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;

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

    psoDesc.CS = CD3DX12_SHADER_BYTECODE(m_TemporalFilterShader.GetInterfacePtr());
    p_Device->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&m_PSOs[RenderPass_TemporalFilter]));
    m_PSOs[RenderPass_TemporalFilter]->SetName(L"Temporal filter PSO");

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
    m_PrevLightScatteringTextureIndex = 0;
    VolumeTextureInit vtInit;
    vtInit.Width = m_Dimensions.x;
    vtInit.Height = m_Dimensions.y;
    vtInit.Depth = m_Dimensions.z;
    vtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    vtInit.InitialState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    vtInit.Name = L"Scattering Volume Texture 0";
    m_ScatteringVolumes[m_PrevLightScatteringTextureIndex].init(vtInit);
  }

  {
    m_CurrLightScatteringTextureIndex = 1;
    VolumeTextureInit vtInit;
    vtInit.Width = m_Dimensions.x;
    vtInit.Height = m_Dimensions.y;
    vtInit.Depth = m_Dimensions.z;
    vtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    vtInit.InitialState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    vtInit.Name = L"Scattering Volume Texture 1";
    m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].init(vtInit);
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

  //m_DataInjectionShader = nullptr;
  //m_LightContributionShader = nullptr;
  //m_FinalIntegralShader = nullptr;

  m_FinalVolume.deinit();
  m_ScatteringVolumes[1].deinit();
  m_ScatteringVolumes[0].deinit();
  m_DataVolume.deinit();
}
//---------------------------------------------------------------------------//
void VolumetricFog::render(ID3D12GraphicsCommandList* p_CmdList, const RenderDesc& p_RenderDesc)
{
  assert(p_CmdList != nullptr);

  m_PrevLightScatteringTextureIndex = m_CurrLightScatteringTextureIndex;
  m_CurrLightScatteringTextureIndex = (m_CurrLightScatteringTextureIndex + 1) % 2;

  PIXBeginEvent(p_CmdList, 0, "Volumetric Fog");

  const uint32_t dispatchGroupX = alignUp<uint32_t>(m_Dimensions.x, 8) / 8;
  const uint32_t dispatchGroupY = alignUp<uint32_t>(m_Dimensions.y, 8) / 8;

  FogConstants uniforms;
  // Assign the uniforms used in different passes:
  {
    uniforms.PrevViewProj = p_RenderDesc.PrevViewProj;
    uniforms.InvViewProj = glm::transpose(
        glm::inverse(p_RenderDesc.Camera.ViewMatrix() * p_RenderDesc.Camera.ProjectionMatrix()));
    uniforms.ProjMat = glm::transpose(p_RenderDesc.Camera.ProjectionMatrix());
    uniforms.Resolution = glm::vec2(p_RenderDesc.ScreenWidth, p_RenderDesc.ScreenHeight);
    uniforms.NearClip = p_RenderDesc.Near;
    uniforms.FarClip = p_RenderDesc.Far;

    uniforms.ClusterBufferIdx = p_RenderDesc.ClusterBufferSrv;
    uniforms.DepthBufferIdx = p_RenderDesc.DepthBufferSrv;
    uniforms.SpotLightShadowIdx = p_RenderDesc.SpotLightShadowSrv;
    uniforms.UVMapIdx = p_RenderDesc.UVMapSrv;
    uniforms.TangentFrameMapIndex = p_RenderDesc.TangentFrameSrv;
    uniforms.MaterialIDMapIdx = p_RenderDesc.MaterialIdMapSrv;
    uniforms.NoiseTextureIdx = p_RenderDesc.NoiseTexSrv;

    uniforms.Dimensions = m_Dimensions;
    uniforms.CurrFrame = static_cast<uint32_t>(p_RenderDesc.CurrentFrame);

    uniforms.CameraPos = p_RenderDesc.Camera.Position();

    uniforms.NumXTiles = uint32_t(AppSettings::NumXTiles);
    uniforms.NumXYTiles = uint32_t(AppSettings::NumXTiles * AppSettings::NumYTiles);
    uniforms.HaltonXY = p_RenderDesc.HaltonXY;

    AppSettings::FOG_GridParams =
        getVolumetricFogGridZParams(p_RenderDesc.Near, p_RenderDesc.Far, m_Dimensions.z, AppSettings::FOG_Exponent, AppSettings::FOG_Offset);
  }

  // 1. Data injection
  {
    PIXBeginEvent(p_CmdList, 0, "Data Injection");

    // Prepare volume buffer for write
    m_DataVolume.makeWritable(p_CmdList);

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_DataInjection]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers
    BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {m_DataVolume.UAV};
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    p_RenderDesc.LightsBuffer.setAsComputeRootParameter(p_CmdList, RootParam_LightsCbuffer);

    p_CmdList->Dispatch(dispatchGroupX, dispatchGroupY, m_Dimensions.z);

    // Sync back volume buffer to be read
    m_DataVolume.makeReadable(p_CmdList);

    PIXEndEvent(p_CmdList); // End of froxelization
  }

  const bool temporalPassEnabled = m_BuffersInitialized && AppSettings::FOG_EnableTemporalFilter;

  // 2. Light contribution
  {
    PIXBeginEvent(p_CmdList, 0, "Light Scattering");

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_LightContribution]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers
    {
      uniforms.DataVolumeIdx = m_DataVolume.getSRV();
      BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);
    }

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    p_RenderDesc.LightsBuffer.setAsComputeRootParameter(p_CmdList, RootParam_LightsCbuffer);

    // NOTE: If temporal filter is active we must use another volume instead of
    // the default scattering volume as the rendertarget/UAV for this pass.
    // Reason being, the temporal pass samples current scattering volume but also
    // writes to it which causes invalid resource state in gpu-validation layer.
    D3D12_CPU_DESCRIPTOR_HANDLE uavs[1] = {};
    if (temporalPassEnabled)
    {
      m_FinalVolume.makeWritable(p_CmdList);
      uavs[0] = m_FinalVolume.UAV;

    }
    else
    {
      m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].makeWritable(p_CmdList);
      uavs[0] = m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].UAV;
    }
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    // Dispatch call with one thread per froxel:
    p_CmdList->Dispatch(dispatchGroupX, dispatchGroupY, m_Dimensions.z);

    // Sync back corresponding UAV
    if (temporalPassEnabled)
    {
      m_FinalVolume.makeReadable(p_CmdList);
    }
    else
    {
      m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].makeReadable(p_CmdList);
    }

    PIXEndEvent(p_CmdList); // End of light scattering pass
  }

  // 2.1. Temporal Filter
  if (m_BuffersInitialized && AppSettings::FOG_EnableTemporalFilter)
  {
    PIXBeginEvent(p_CmdList, 0, "Temporal Filter");

    m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].makeWritable(p_CmdList);

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_TemporalFilter]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers
    {
      uniforms.DataVolumeIdx = m_DataVolume.getSRV();
      uniforms.FinalIntegrationVolumeIdx = m_FinalVolume.getSRV();
      uniforms.ScatteringVolumeIdx =
          m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].getSRV();
      uniforms.PreviousScatteringVolumeIdx =
          m_ScatteringVolumes[m_PrevLightScatteringTextureIndex].getSRV();
      BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);
    }

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {
        m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].UAV};
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    p_RenderDesc.LightsBuffer.setAsComputeRootParameter(p_CmdList, RootParam_LightsCbuffer);

    p_CmdList->Dispatch(dispatchGroupX, dispatchGroupY, m_Dimensions.z);

    m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].makeReadable(p_CmdList);

    PIXEndEvent(p_CmdList); // End of temporal pass
  }

  // Regardless of filter pass,
  // Sync back scatter volume to be read
  //m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].makeReadable(p_CmdList);

  // 3. Final integration
  {
    PIXBeginEvent(p_CmdList, 0, "Final Integration");
    
    m_FinalVolume.makeWritable(p_CmdList);

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_FinalIntegration]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers
    {
      uniforms.ScatteringVolumeIdx =
          m_ScatteringVolumes[m_CurrLightScatteringTextureIndex].getSRV();
      BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);
    }

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {m_FinalVolume.UAV};
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    p_RenderDesc.LightsBuffer.setAsComputeRootParameter(p_CmdList, RootParam_LightsCbuffer);

    // NOTE: Z = 1 as we integrate inside the shader.
    p_CmdList->Dispatch(dispatchGroupX, dispatchGroupY, 1);

    // Sync back final volume to be read
    m_FinalVolume.makeReadable(p_CmdList);

    PIXEndEvent(p_CmdList); // End of integration pass
  }
  m_BuffersInitialized = true;

  PIXEndEvent(p_CmdList); // End of volumetric rendering
}
//---------------------------------------------------------------------------//
