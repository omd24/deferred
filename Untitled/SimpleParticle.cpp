#include "SimpleParticle.hpp"
#include "d3dx12.h"
#include "pix3.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

static constexpr int ParticleCount = 2;

//---------------------------------------------------------------------------//
enum RootParams : uint32_t
{
  RootParam_StandardDescriptors,
  RootParam_VSCBuffer,
  RootParam_SRVIndicesCB,

  NumRootParams
};
//---------------------------------------------------------------------------//
struct SpriteConstants
{
  glm::mat4x4 World;
  glm::mat4x4 WorldView;
  glm::mat4x4 Projection;
  glm::mat4x4 ViewProj;
  glm::mat4x4 View;
  glm::mat4x4 InvView;
  glm::vec4 Params0; // .x = scale
  glm::quat QuatCamera;
  glm::vec4 CamUp;
  glm::vec4 CamRight;
  glm::mat3x3 InvView3;
};
//---------------------------------------------------------------------------//
void SimpleParticle::init(DXGI_FORMAT p_Fmt, DXGI_FORMAT p_DepthFmt, const glm::vec3& p_Position)
{
  assert(g_Device != nullptr);

  m_OutputFormat = p_Fmt;
  m_DepthFormat = p_DepthFmt;

  // init transofrms:
  m_Transforms.resize(ParticleCount);
  glm::mat4 worldMat = glm::identity<glm::mat4>();
  // worldMat[0][3] = p_Position.x;
  // worldMat[1][3] = p_Position.y;
  // worldMat[2][3] = p_Position.z;

  worldMat[1][3] = 2.0f; // shift a little up

  // rotate 90 degree
  // worldMat = glm::rotate(worldMat, 1.0f, glm::vec3(0, 1, 0));

  m_Transforms[0] = glm::transpose(worldMat);

  // init index data
  {
    // Create the index buffer
    uint16_t indices[] = {0, 1, 2, 3, 0, 2};
    FormattedBufferInit ibInit;
    ibInit.Format = DXGI_FORMAT_R16_UINT;
    ibInit.NumElements = arrayCount32(indices);
    ibInit.InitialState = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    ibInit.InitData = indices;
    ibInit.Name = L"Quad Index Buffer";
    m_IndexBuffer.init(ibInit);
  }

  // load main texture
  // loadTexture(g_Device, m_SpriteTexture, L"..\\Content\\Textures\\ring.dds");
  loadTexture(g_Device, m_SpriteTexture, L"..\\Content\\Textures\\checkerboard.dds");

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
  m_SpriteTexture.Shutdown();
  m_IndexBuffer.deinit();
}
//---------------------------------------------------------------------------//
void SimpleParticle::render(
    ID3D12GraphicsCommandList* p_CmdList,
    const glm::mat4& p_ViewMat,
    const glm::mat4& p_ProjMat,
    const glm::mat4& p_ViewProj,
    const glm::vec3& p_Dir,
    const glm::quat& p_CameraOrientation,
    const glm::vec4& p_CameraUp,
    const glm::vec4& p_CameraRight,
    const float p_DeltaTime)
{
  PIXBeginEvent(p_CmdList, 0, "Particle Draw");

  p_CmdList->SetGraphicsRootSignature(m_DrawRootSig);
  p_CmdList->SetPipelineState(m_DrawPSO);

  // Calculate particle position
  glm::mat4 world = m_Transforms[0];
  // Animate particle position:
  {
    // const float particleIndex = 1.0f;
    // float multiplier = std::cos(p_DeltaTime * pi / 500 + particleIndex * 0.1f);
    // float yFactor = multiplier;
    // world[3][1] += yFactor;
  }

#pragma region Billboarding
  glm::mat4 worldView = world * p_ViewMat;
  // N.B. For billboarding just set the upper left 3x3 of worldView to identity:
  // https://stackoverflow.com/a/15325758/4623650
  // worldView[0][0] = 1;
  // worldView[0][1] = 0;
  // worldView[0][2] = 0;
  // worldView[1][0] = 0;
  // worldView[1][1] = 1;
  // worldView[1][2] = 0;
  // worldView[2][0] = 0;
  // worldView[2][1] = 0;
  // worldView[2][2] = 1;
#pragma endregion

  glm::mat3 invViewRot = glm::inverse(glm::mat3(p_ViewMat));

  // Set vs cbuffer:
  {
    SpriteConstants spriteConstants;
    spriteConstants.Projection = p_ProjMat;
    spriteConstants.World = world;
    spriteConstants.WorldView = world * p_ViewMat;
    spriteConstants.ViewProj = p_ViewProj;
    spriteConstants.View = p_ViewMat;
    glm::mat4 rotMat = glm::mat4(1);
    glm::vec3 new_y = glm::normalize(p_Dir);
    glm::vec3 new_z = -glm::normalize(glm::cross(new_y, glm::vec3(0, 1, 0)));
    glm::vec3 new_x = glm::normalize(glm::cross(new_y, new_z));
    // spriteConstants.RotationMatrix = glm::transpose(glm::mat3(new_x, new_y, new_z));
    spriteConstants.InvView = glm::inverse(p_ViewMat);
    spriteConstants.Params0.x = std::cos(p_DeltaTime * Pi / 50.0f); // scale factor
    spriteConstants.InvView3 = invViewRot;
    spriteConstants.QuatCamera = p_CameraOrientation;
    spriteConstants.CamUp = p_CameraUp;
    spriteConstants.CamRight = p_CameraRight;

    BindTempConstantBuffer(p_CmdList, spriteConstants, RootParam_VSCBuffer, CmdListMode::Graphics);
  }

  // Set srv indices:
  {
    uint32_t srvIndices[] = {m_SpriteTexture.SRV};
    BindTempConstantBuffer(p_CmdList, srvIndices, RootParam_SRVIndicesCB, CmdListMode::Graphics);
  }

  // Bind vb and ib
  D3D12_INDEX_BUFFER_VIEW ibView = m_IndexBuffer.IBView();
  p_CmdList->IASetIndexBuffer(&ibView);
  p_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  BindStandardDescriptorTable(p_CmdList, RootParam_StandardDescriptors, CmdListMode::Graphics);

  //
  // Draw particles:
  {
    p_CmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);
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
