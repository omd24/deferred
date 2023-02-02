#include "SimpleParticle.hpp"
#include "d3dx12.h"
#include "pix3.h"

struct VSConstants
{
  glm::mat4x4 World;
  glm::mat4x4 View;
  glm::mat4x4 Proj;
};
//---------------------------------------------------------------------------//
void SimpleParticle::init(DXGI_FORMAT p_Fmt, DXGI_FORMAT p_DepthFmt, const glm::vec3& p_CameraDir)
{
  assert(g_Device != nullptr);

  m_OutputFormat = p_Fmt;
  m_DepthFormat = p_DepthFmt;

  // init quad data
  {
    glm::quat rot90 = glm::angleAxis(glm::radians(90.f), glm::vec3(1.0f, 0.0f, 0.0f));
    m_QuadModel.GeneratePlaneScene(
        g_Device, glm::vec2(1.0f, 1.0f), glm::vec3(-1.0f, 2.0f, 0.0f), rot90);
  }

  // create root signatures:
  {
    D3D12_ROOT_PARAMETER1 rootParameters[2] = {};

    // vs cbuffer
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // place holder #1
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Constants.Num32BitValues = 1;
    rootParameters[1].Constants.RegisterSpace = 0;
    rootParameters[1].Constants.ShaderRegister = 2;

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    createRootSignature(g_Device, &m_DrawRootSig, rootSignatureDesc);
    m_DrawRootSig->SetName(L"Particles Draw Root Sig");
  }

  createPSOs();
}
//---------------------------------------------------------------------------//
void SimpleParticle::createPSOs()
{
  // release any dangling pso:
  destroyPSOs();

  // load and compile shaders
  compileShaders();

  // create PSOs:
  {
    static const D3D12_INPUT_ELEMENT_DESC standardInputElements[5] = {
        {"POSITION",
         0,
         DXGI_FORMAT_R32G32B32_FLOAT,
         0,
         0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"NORMAL",
         0,
         DXGI_FORMAT_R32G32B32_FLOAT,
         0,
         12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"UV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT",
         0,
         DXGI_FORMAT_R32G32B32_FLOAT,
         0,
         32,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"BITANGENT",
         0,
         DXGI_FORMAT_R32G32B32_FLOAT,
         0,
         44,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_DrawRootSig;
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_DrawVS.GetInterfacePtr());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_DrawPS.GetInterfacePtr());
    psoDesc.RasterizerState = GetRasterizerState(RasterizerState::NoCull);
    psoDesc.BlendState = GetBlendState(BlendState::AlphaBlend);
    psoDesc.DepthStencilState = GetDepthState(DepthState::Enabled);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_OutputFormat;
    psoDesc.DSVFormat = m_DepthFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.InputLayout.NumElements = arrayCount32(standardInputElements);
    psoDesc.InputLayout.pInputElementDescs = standardInputElements;

    HRESULT hr = g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_DrawPSO));
    m_DrawPSO->SetName(L"Particles Draw PSO");
    assert(SUCCEEDED(hr));
  }
}
//---------------------------------------------------------------------------//
void SimpleParticle::destroyPSOs()
{
  if (m_DrawPSO != nullptr)
    m_DrawPSO->Release();
}
//---------------------------------------------------------------------------//
void SimpleParticle::deinit()
{
  m_DrawPSO->Release();
  m_DrawRootSig->Release();
  m_QuadModel.Shutdown();
}
//---------------------------------------------------------------------------//
void SimpleParticle::render(
    ID3D12GraphicsCommandList* p_CmdList, const glm::mat4& p_ViewMat, const glm::mat4& p_ProjMat)
{
  PIXBeginEvent(p_CmdList, 0, "Particle Draw");

  p_CmdList->SetGraphicsRootSignature(m_DrawRootSig);
  p_CmdList->SetPipelineState(m_DrawPSO);

  // Set vs cbuffer:
  {
    glm::mat4 world = glm::identity<glm::mat4>();
    VSConstants vsConstants;
    vsConstants.World = world;
    vsConstants.View = p_ViewMat;
    vsConstants.Proj = p_ProjMat;

    BindTempConstantBuffer(p_CmdList, vsConstants, 0, CmdListMode::Graphics);
  }

  // Bind vb and ib
  D3D12_VERTEX_BUFFER_VIEW vbView = m_QuadModel.VertexBuffer().vbView();
  D3D12_INDEX_BUFFER_VIEW ibView = m_QuadModel.IndexBuffer().IBView();
  p_CmdList->IASetVertexBuffers(0, 1, &vbView);
  p_CmdList->IASetIndexBuffer(&ibView);
  p_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  //
  // Draw particles:
  {
    // There should only be one mesh and meshPart in the quad model:
    assert(m_QuadModel.NumMeshes() == 1 && m_QuadModel.Meshes()[0].NumMeshParts() == 1);
    const Mesh& quad = m_QuadModel.Meshes()[0];
    const MeshPart& part = quad.MeshParts()[0];
    p_CmdList->DrawIndexedInstanced(part.IndexCount, 1, 0, 0, 0);
  }

  PIXEndEvent(p_CmdList);
}
//---------------------------------------------------------------------------//
void SimpleParticle::compileShaders()
{
  // Load and compile shaders:
  {
    bool res = true;
    ID3DBlobPtr errorBlob;
    ID3DBlobPtr tempDrawVS = nullptr;
    ID3DBlobPtr tempDrawPS = nullptr;

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
    shaderPath += L"Shaders\\ParticleDraw.hlsl";

    // vertex shader
    {
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "VS",
          "vs_5_1",
          compileFlags,
          0,
          &tempDrawVS,
          &errorBlob);
      if (nullptr == tempDrawVS || FAILED(hr))
      {
        OutputDebugStringA("Failed to load particle vertex shader.\n");
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        res = false;
      }
      errorBlob = nullptr;
    }

    // pixel shader
    {
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "PS",
          "ps_5_1",
          compileFlags,
          0,
          &tempDrawPS,
          &errorBlob);
      if (nullptr == tempDrawPS || FAILED(hr))
      {
        OutputDebugStringA("Failed to load particle pixel shader.\n");
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        res = false;
      }
      errorBlob = nullptr;
    }

    // Only update the postfx shaders if there was no issue:
    if (res)
    {
      m_DrawVS = tempDrawVS;
      m_DrawPS = tempDrawPS;
    }
    assert(m_DrawVS && m_DrawPS);
  }
}
//---------------------------------------------------------------------------//
