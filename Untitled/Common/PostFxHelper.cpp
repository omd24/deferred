#include "PostFxHelper.hpp"
#include "d3dx12.h"
#include "pix3.h"
#include "../AppSettings.hpp"

static const uint32_t MaxInputs = 8;

namespace AppSettings
{
const extern uint32_t CBufferRegister;
void bindCBufferGfx(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter);
} // namespace AppSettings

enum RootParams : uint32_t
{
  RootParam_StandardDescriptors,
  RootParam_SRVIndices,
  RootParam_AppSettings,

  NumRootParams,
};

PostFxHelper::PostFxHelper() {}
PostFxHelper::~PostFxHelper() {}

void PostFxHelper::init()
{
  // Load and compile triangle shader:

  {
    WCHAR assetsPath[512];
    getAssetsPath(assetsPath, _countof(assetsPath));
    std::wstring shaderPath = assetsPath;
    shaderPath += L"Shaders\\FullscreenTriangle.hlsl";


#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    compileShader(
        "fullscreen triangle",
        shaderPath.c_str(),
        0,
        nullptr,
        compileFlags,
        ShaderType::Vertex,
        "VS",
        m_FullscreenTriangleVS);
  }

  // Create root signature:
  {
    {
      D3D12_ROOT_PARAMETER1 rootParameters[NumRootParams] = {};

      rootParameters[RootParam_StandardDescriptors].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      rootParameters[RootParam_StandardDescriptors].ShaderVisibility =
          D3D12_SHADER_VISIBILITY_PIXEL;
      rootParameters[RootParam_StandardDescriptors].DescriptorTable.pDescriptorRanges =
          StandardDescriptorRanges();
      rootParameters[RootParam_StandardDescriptors].DescriptorTable.NumDescriptorRanges =
          NumStandardDescriptorRanges;

      rootParameters[RootParam_SRVIndices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      rootParameters[RootParam_SRVIndices].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
      rootParameters[RootParam_SRVIndices].Descriptor.RegisterSpace = 0;
      rootParameters[RootParam_SRVIndices].Descriptor.ShaderRegister = 0;

      rootParameters[RootParam_AppSettings].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      rootParameters[RootParam_AppSettings].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
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
      m_RootSig->SetName(L"PostFxHelper-RootSig");
    }
  }
}
void PostFxHelper::deinit()
{
  for (uint64_t i = 0; i < m_TempRenderTargets.size(); ++i)
  {
    TempRenderTarget* tempRT = m_TempRenderTargets[i];
    tempRT->m_RenderTarget.deinit();
  }

  m_TempRenderTargets.clear();

  for (uint64_t i = 0; i < m_PSOs.size(); ++i)
    m_PSOs[i]->Release();

  m_PSOs.clear();

  m_RootSig->Release();
}

TempRenderTarget* PostFxHelper::getTempRenderTarget(
    uint64_t p_Width, uint64_t p_Height, DXGI_FORMAT p_Format, bool p_UseAsUAV)
{
  for (uint64_t i = 0; i < m_TempRenderTargets.size(); ++i)
  {
    TempRenderTarget* tempRT = m_TempRenderTargets[i];
    if (tempRT->m_InUse)
      continue;

    const RenderTexture& rt = tempRT->m_RenderTarget;
    if (rt.m_Texture.Width == p_Width && rt.m_Texture.Height == p_Height &&
        rt.m_Texture.Format == p_Format && p_UseAsUAV == (rt.m_UAV.ptr != 0))
    {
      tempRT->m_InUse = true;
      return tempRT;
    }
  }

  RenderTextureInit rtInit;
  rtInit.Width = p_Width;
  rtInit.Height = p_Height;
  rtInit.Format = p_Format;
  rtInit.CreateUAV = p_UseAsUAV;
  rtInit.InitialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  TempRenderTarget* tempRT = new TempRenderTarget();
  tempRT->m_RenderTarget.init(rtInit);
  tempRT->m_RenderTarget.m_Texture.Resource->SetName(L"PostFxHelper Temp Render Target");
  tempRT->m_InUse = true;
  m_TempRenderTargets.push_back(tempRT);

  return tempRT;
}

void PostFxHelper::begin(ID3D12GraphicsCommandList* p_CmdList)
{
  assert(nullptr == m_CmdList);
  m_CmdList = p_CmdList;
}
void PostFxHelper::end()
{
  assert(m_CmdList != nullptr);
  m_CmdList = nullptr;

  for (uint64_t i = 0; i < m_TempRenderTargets.size(); ++i)
    assert(m_TempRenderTargets[i]->m_InUse == false);
}

void PostFxHelper::postProcess(
    ID3DBlobPtr p_PixelShader,
    const char* p_Name,
    const RenderTexture& p_Input,
    const RenderTexture& p_Output)
{
  uint32_t inputs[1] = {p_Input.srv()};
  const RenderTexture* outputs[1] = {&p_Output};
  postProcess(p_PixelShader, p_Name, inputs, 1, outputs, 1);
}
void PostFxHelper::postProcess(
    ID3DBlobPtr p_PixelShader,
    const char* p_Name,
    const RenderTexture& p_Input,
    const TempRenderTarget* p_Output)
{
  uint32_t inputs[1] = {p_Input.srv()};
  const RenderTexture* outputs[1] = {&p_Output->m_RenderTarget};
  postProcess(p_PixelShader, p_Name, inputs, 1, outputs, 1);
}
void PostFxHelper::postProcess(
    ID3DBlobPtr p_PixelShader,
    const char* p_Name,
    const TempRenderTarget* p_Input,
    const RenderTexture& p_Output)
{
  uint32_t inputs[1] = {p_Input->m_RenderTarget.srv()};
  const RenderTexture* outputs[1] = {&p_Output};
  postProcess(p_PixelShader, p_Name, inputs, 1, outputs, 1);
}
void PostFxHelper::postProcess(
    ID3DBlobPtr p_PixelShader,
    const char* p_Name,
    const TempRenderTarget* p_Input,
    const TempRenderTarget* p_Output)
{
  uint32_t inputs[1] = {p_Input->m_RenderTarget.srv()};
  const RenderTexture* outputs[1] = {&p_Output->m_RenderTarget};
  postProcess(p_PixelShader, p_Name, inputs, 1, outputs, 1);
}
void PostFxHelper::postProcess(
    ID3DBlobPtr p_PixelShader,
    const char* p_Name,
    const uint32_t* p_Inputs,
    uint64_t p_NumInputs,
    const RenderTexture* const* p_Outputs,
    uint64_t p_NumOutputs)
{
  assert(m_CmdList != nullptr);
  assert(p_NumOutputs > 0);
  assert(p_Outputs != nullptr);
  assert(p_NumInputs == 0 || p_Inputs != nullptr);
  assert(p_NumInputs <= MaxInputs);

  PIXBeginEvent(m_CmdList, 0, p_Name);

  ID3D12PipelineState* pso = nullptr;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = m_RootSig;
  psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_FullscreenTriangleVS);
  psoDesc.PS = CD3DX12_SHADER_BYTECODE(p_PixelShader);
  psoDesc.RasterizerState = GetRasterizerState(RasterizerState::NoCull);
  psoDesc.BlendState = GetBlendState(BlendState::Disabled);
  psoDesc.DepthStencilState = GetDepthState(DepthState::Disabled);
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = uint32_t(p_NumOutputs);
  for (uint64_t i = 0; i < p_NumOutputs; ++i)
    psoDesc.RTVFormats[i] = p_Outputs[i]->m_Texture.Format;
  psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
  psoDesc.SampleDesc.Count = 1;
  D3D_EXEC_CHECKED(g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));
  m_PSOs.push_back(pso);

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
  for (uint64_t n = 0; n < p_NumOutputs; ++n)
    rtvHandles[n] = p_Outputs[n]->m_RTV;
  m_CmdList->OMSetRenderTargets(uint32_t(p_NumOutputs), rtvHandles, false, nullptr);

  m_CmdList->SetGraphicsRootSignature(m_RootSig);
  m_CmdList->SetPipelineState(pso);

  BindStandardDescriptorTable(m_CmdList, RootParam_StandardDescriptors, CmdListMode::Graphics);

  AppSettings::bindCBufferGfx(m_CmdList, RootParam_AppSettings);

  uint32_t srvIndices[MaxInputs] = {};
  for (uint64_t i = 0; i < p_NumInputs; ++i)
    srvIndices[i] = p_Inputs[i];
  for (uint64_t i = p_NumInputs; i < MaxInputs; ++i)
    srvIndices[i] = NullTexture2DSRV;

  BindTempConstantBuffer(m_CmdList, srvIndices, RootParam_SRVIndices, CmdListMode::Graphics);

  SetViewport(m_CmdList, p_Outputs[0]->m_Texture.Width, p_Outputs[0]->m_Texture.Height);

  m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_CmdList->DrawInstanced(3, 1, 0, 0);

  PIXEndEvent(m_CmdList);
}
