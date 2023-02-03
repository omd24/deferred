#include "SimpleParticle.hpp"
#include "d3dx12.h"
#include "pix3.h"

//---------------------------------------------------------------------------//
enum RootParams : uint32_t
{
  RootParam_StandardDescriptors,
  RootParam_VSCBuffer,
  RootParam_SRVIndicesCB,

  NumRootParams
};
//---------------------------------------------------------------------------//
struct VSConstants
{
  glm::mat4x4 WorldViewIdentity;
  glm::mat4x4 World;
  glm::mat4x4 View;
  glm::mat4x4 Projection;
  glm::vec4 Params0;
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
        g_Device, glm::vec2(0.5f, 0.5f), glm::vec3(-1.0f, 2.0f, 0.0f), rot90);
  }

  // load main texture
  loadTexture(g_Device, m_SpriteTexture, L"..\\Content\\Textures\\smoke.dds");

  // create root signatures:
  {
    D3D12_ROOT_PARAMETER1 rootParameters[NumRootParams] = {};

    // textures
    rootParameters[RootParam_StandardDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[RootParam_StandardDescriptors].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[RootParam_StandardDescriptors].DescriptorTable.pDescriptorRanges =
        StandardDescriptorRanges();
    rootParameters[RootParam_StandardDescriptors].DescriptorTable.NumDescriptorRanges =
        NumStandardDescriptorRanges;

    // vs cbuffer
    rootParameters[RootParam_VSCBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[RootParam_VSCBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[RootParam_VSCBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[RootParam_VSCBuffer].Descriptor.ShaderRegister = 0;

    // srv indices
    rootParameters[RootParam_SRVIndicesCB].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[RootParam_SRVIndicesCB].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[RootParam_SRVIndicesCB].Descriptor.RegisterSpace = 0;
    rootParameters[RootParam_SRVIndicesCB].Descriptor.ShaderRegister = 1;

    // samplers
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    staticSamplers[0] = GetStaticSamplerState(SamplerState::Point, 0);
    staticSamplers[1] = GetStaticSamplerState(SamplerState::LinearClamp, 1);

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = arrayCount32(staticSamplers);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
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
  m_SpriteTexture.Shutdown();
}
//---------------------------------------------------------------------------//
void SimpleParticle::render(
    ID3D12GraphicsCommandList* p_CmdList,
    const glm::mat4& p_ViewMat,
    const glm::mat4& p_ProjMat,
    const float p_DeltaTime)
{
  PIXBeginEvent(p_CmdList, 0, "Particle Draw");

  p_CmdList->SetGraphicsRootSignature(m_DrawRootSig);
  p_CmdList->SetPipelineState(m_DrawPSO);

#pragma region Billboarding
  glm::mat4 world = glm::identity<glm::mat4>();
  glm::mat4 worldView = world * p_ViewMat;
  // N.B. For billboarding just set the upper left 3x3 of worldView to identity:
  // https://stackoverflow.com/a/15325758/4623650
  worldView[0][0] = 1;
  worldView[0][1] = 0;
  worldView[0][2] = 0;
  worldView[1][0] = 0;
  worldView[1][1] = 1;
  worldView[1][2] = 0;
  worldView[2][0] = 0;
  worldView[2][1] = 0;
  worldView[2][2] = 1;
#pragma endregion

  const float pi = 3.141592654f;
  static float maxMultiplier = -1;
  // Animate particle position:
  {
    const float particleIndex = 1.0f;
    int rndNumInt = std::rand() % 2;
    float rndNumF = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    rndNumF = (0 == rndNumInt) ? -rndNumF : rndNumF;
    float multiplier = std::cos(p_DeltaTime * pi / 10 + particleIndex * 0.1f);
    // if (multiplier > maxMultiplier)
    {
      maxMultiplier = multiplier;
      // float xFactor = multiplier;
      float yFactor = multiplier;
      // float zFactor = multiplier;
      // worldView[3][0] += xFactor;
      worldView[3][1] += yFactor;
      // worldView[3][2] += zFactor;
    }
  }

  // Animate particle scale:
  {
      // static int sign = 1;
      // sign *= -1;
      // float scale = std::sin(p_DeltaTime * pi);
      // worldView[0][0] *= (1 + scale * sign);
      // worldView[1][1] *= (1 + scale * -sign);
  }

  // Set vs cbuffer:
  {
    VSConstants vsConstants;
    vsConstants.Projection = p_ProjMat;
    vsConstants.WorldViewIdentity = worldView;
    vsConstants.View = p_ViewMat;
    vsConstants.World = world;
    vsConstants.Params0.x = std::cos(p_DeltaTime * pi / 10.0f);

    BindTempConstantBuffer(p_CmdList, vsConstants, RootParam_VSCBuffer, CmdListMode::Graphics);
  }

  // Set srv indices:
  {
    uint32_t srvIndices[] = {m_SpriteTexture.SRV};
    BindTempConstantBuffer(p_CmdList, srvIndices, RootParam_SRVIndicesCB, CmdListMode::Graphics);
  }

  // Bind vb and ib
  D3D12_VERTEX_BUFFER_VIEW vbView = m_QuadModel.VertexBuffer().vbView();
  D3D12_INDEX_BUFFER_VIEW ibView = m_QuadModel.IndexBuffer().IBView();
  p_CmdList->IASetVertexBuffers(0, 1, &vbView);
  p_CmdList->IASetIndexBuffer(&ibView);
  p_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Graphics);

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
