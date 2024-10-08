
#include "TAA.hpp"
#include "d3dx12.h"
#include "pix3.h"
#include "../AppSettings.hpp"

// TODOs
// 1. apply jitter to camera (cpp)
// 2. add motion vectors (a velocity texture)
// 3. add simplest TAA ever :)

namespace AppSettings
{
const extern uint32_t CBufferRegister;
void bindCBufferCompute(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter);
} // namespace AppSettings

struct ConstantData
{
  glm::mat4x4 ProjMat;
  glm::mat4x4 InvViewProj;

  uint32_t SceneColorIdx;
  uint32_t HistorySceneColorIdx;
  uint32_t MotionVectorsIdx;
  uint32_t unused0;

  glm::vec2 Resolution;
  glm::vec2 unused1;
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
  RenderPass_TAA,

  NumRenderPasses,
};

int TAARenderPass::ms_CurrOutputTextureIndex = 1;
int TAARenderPass::ms_PrevOutputTextureIndex = 0;

void TAARenderPass::init(ID3D12Device* p_Device, uint32_t w, uint32_t h)
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
    shaderPath += L"Shaders\\TAA.hlsl";

    // TAA compute shader
    {
      const D3D_SHADER_MACRO defines[] = {{"DEBUG_TAA", "1"}, {NULL, NULL}};
      compileShader(
          "taa main",
          shaderPath.c_str(),
          arrayCountU8(defines),
          defines,
          compileFlags,
          ShaderType::Compute,
          "TaaCS",
          m_TAAShader);
    }
    assert(m_TAAShader);
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
    rootParameters[RootParam_AppSettings].Descriptor.ShaderRegister = AppSettings::CBufferRegister;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[4] = {};
    staticSamplers[0] =
        GetStaticSamplerState(SamplerState::Point, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[1] =
        GetStaticSamplerState(SamplerState::Linear, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[2] =
        GetStaticSamplerState(SamplerState::LinearBorder, 2, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[3] =
        GetStaticSamplerState(SamplerState::LinearClamp, 3, 0, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = arrayCount32(staticSamplers);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    CreateRootSignature(&m_RootSig, rootSignatureDesc);
    m_RootSig->SetName(L"TAA-RootSig");
  }

  // Create psos
  {
    m_PSOs.resize(NumRenderPasses);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_RootSig;

    psoDesc.CS = CD3DX12_SHADER_BYTECODE(m_TAAShader.GetInterfacePtr());
    p_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_PSOs[RenderPass_TAA]));
    m_PSOs[RenderPass_TAA]->SetName(L"TAA PSO");
  }

  // Create uav targets:
  {
    ms_PrevOutputTextureIndex = 0;
    RenderTextureInit rtInit;
    rtInit.Width = w;
    rtInit.Height = h;
    rtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtInit.MSAASamples = 1;
    rtInit.ArraySize = 1;
    rtInit.CreateUAV = true;
    rtInit.InitialState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rtInit.Name = L"TAA Target 0";
    m_uavTargets[ms_PrevOutputTextureIndex].init(rtInit);
  }

  {
    ms_CurrOutputTextureIndex = 1;
    RenderTextureInit rtInit;
    rtInit.Width = w;
    rtInit.Height = h;
    rtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtInit.MSAASamples = 1;
    rtInit.ArraySize = 1;
    rtInit.CreateUAV = true;
    rtInit.InitialState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rtInit.Name = L"TAA Target 1";
    m_uavTargets[ms_CurrOutputTextureIndex].init(rtInit);
  }
}
//---------------------------------------------------------------------------//
void TAARenderPass::deinit(bool p_ReleaseResources)
{
  for (unsigned i = 0; i < NumRenderPasses; ++i)
  {
    m_PSOs[i]->Release();
  }

  m_RootSig->Release();

  if (p_ReleaseResources)
  {
    m_TAAShader = nullptr;
    m_uavTargets[1].deinit();
    m_uavTargets[0].deinit();
  }
}
//---------------------------------------------------------------------------//
void TAARenderPass::render(
    ID3D12GraphicsCommandList* p_CmdList,
    FirstPersonCamera const& p_Camera, 
    const uint32_t p_SceneTexSrv,
    const uint32_t p_MotionVectorsSrv)
{
  assert(p_CmdList != nullptr);


  ms_PrevOutputTextureIndex = ms_CurrOutputTextureIndex;
  ms_CurrOutputTextureIndex = (ms_CurrOutputTextureIndex + 1) % 2;

  PIXBeginEvent(p_CmdList, 0, "TAA");

  // TAA pass
  {
    // Prepare current output to be written onto
    transitionResource(
        p_CmdList,
        m_uavTargets[ms_CurrOutputTextureIndex].resource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    p_CmdList->SetComputeRootSignature(m_RootSig);
    p_CmdList->SetPipelineState(m_PSOs[RenderPass_TAA]);

    BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Compute);

    // Set constant buffers
    const uint64_t width = m_uavTargets[ms_CurrOutputTextureIndex].width();
    const uint64_t height = m_uavTargets[ms_CurrOutputTextureIndex].height();
    {
      ConstantData uniforms;
      uniforms.InvViewProj = glm::inverse(glm::transpose(p_Camera.ViewProjectionMatrix()));
      uniforms.ProjMat = glm::transpose(p_Camera.ProjectionMatrix());
      uniforms.SceneColorIdx = p_SceneTexSrv;
      uniforms.MotionVectorsIdx = p_MotionVectorsSrv;
      uniforms.HistorySceneColorIdx = m_uavTargets[ms_PrevOutputTextureIndex].srv();
      uniforms.Resolution = {width, height};

      BindTempConstantBuffer(p_CmdList, uniforms, RootParam_Cbuffer, CmdListMode::Compute);
    }

    AppSettings::bindCBufferCompute(p_CmdList, RootParam_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {m_uavTargets[ms_CurrOutputTextureIndex].m_UAV};
    BindTempDescriptorTable(
        p_CmdList, uavs, arrayCount(uavs), RootParam_UAVDescriptors, CmdListMode::Compute);

    const uint32_t numComputeTilesX = alignUp<uint32_t>(uint32_t(width), 8) / 8;
    const uint32_t numComputeTilesY = alignUp<uint32_t>(uint32_t(height), 8) / 8;

    p_CmdList->Dispatch(numComputeTilesX, numComputeTilesY, 1);

    // Sync back render target to be read
    transitionResource(
        p_CmdList,
        m_uavTargets[ms_CurrOutputTextureIndex].resource(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  }

  PIXEndEvent(p_CmdList);
}
//---------------------------------------------------------------------------//
