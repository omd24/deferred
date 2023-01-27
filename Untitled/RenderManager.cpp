#include "RenderManager.hpp"
#include "ImguiHelper.hpp"
#include <pix3.h>
#include <algorithm>
#include "Common/Input.hpp"
#include "AppSettings.hpp"

//---------------------------------------------------------------------------//
// Internal variables
//---------------------------------------------------------------------------//
static POINT prevMousePos = {};
static constexpr uint32_t MaxSpotLights = 32;
static const float SpotLightIntensityFactor = 25.0f;
static uint32_t MaxLightClamp = 0;

//---------------------------------------------------------------------------//
// Internal structs
//---------------------------------------------------------------------------//
enum DeferredRootParams : uint32_t
{
  DeferredParams_StandardDescriptors, // textures
  DeferredParams_PSCBuffer,
  DeferredParams_DeferredCBuffer,
  DeferredParams_LightCBuffer,
  DeferredParams_SRVIndices,
  DeferredParams_UAVDescriptors,
  DeferredParams_AppSettings,

  NumDeferredRootParams
};

struct MeshVSConstants
{
  glm::mat4x4 World;
  glm::mat4x4 View;
  glm::mat4x4 WorldViewProjection;
  float NearClip = 0.0f;
  float FarClip = 0.0f;
};

struct DeferredConstants
{
  glm::mat4 InvViewProj;
  glm::mat4 Projection;
  glm::vec2 RTSize;
  uint32_t NumComputeTilesX = 0;
};

struct MaterialTextureIndices
{
  uint32_t Albedo;
  uint32_t Normal;
  uint32_t Roughness;
  uint32_t Metallic;
};
struct LightConstants
{
  SpotLight Lights[MaxSpotLights];
  glm::mat4x4 ShadowMatrices[MaxSpotLights];
};

//---------------------------------------------------------------------------//
// Internal private methods
//---------------------------------------------------------------------------//
bool RenderManager::compileShaders()
{
  bool ret = true;
  ID3DBlobPtr tempVS;
  ID3DBlobPtr tempPS;
  ID3DBlobPtr tempCS;

  // Gbuffer shaders
  {
    ID3DBlobPtr errorBlob;
#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif
    HRESULT hr = D3DCompileFromFile(
        getShaderPath(L"Mesh.hlsl").c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS",
        "vs_5_1",
        compileFlags,
        0,
        &tempVS,
        &errorBlob);
    if (nullptr == tempVS || FAILED(hr))
    {
      OutputDebugStringA("Failed to load gbuffer vertex shader.\n");
      if (errorBlob != nullptr)
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
      ret = false;
    }
    errorBlob = nullptr;
    hr = D3DCompileFromFile(
        getShaderPath(L"Mesh.hlsl").c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS",
        "ps_5_1",
        compileFlags,
        0,
        &tempPS,
        &errorBlob);
    if (nullptr == tempPS || FAILED(hr))
    {
      OutputDebugStringA("Failed to load gbuffer pixel shader.\n");
      if (errorBlob != nullptr)
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
      ret = false;
    }
  }

  // Deferred shader
  {
    ID3DBlobPtr csErrorBlob;
#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif
    compileFlags |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
    HRESULT hr = D3DCompileFromFile(
        getShaderPath(L"Deferred.hlsl").c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "CS",
        "cs_5_1",
        compileFlags,
        0,
        &tempCS,
        &csErrorBlob);
    if (nullptr == tempCS || FAILED(hr))
    {
      OutputDebugStringA("Failed to load deferred compute shader.\n");
      if (csErrorBlob != nullptr)
        OutputDebugStringA((char*)csErrorBlob->GetBufferPointer());
      ret = false;
    }
  }

  // Don't update shaders if there was any issue:
  if (ret)
  {
    m_GBufferVS = tempVS;
    m_GBufferPS = tempPS;
    m_DeferredCS = tempCS;
  }

  return ret;
}
std::wstring RenderManager::getShaderPath(LPCWSTR p_ShaderName)
{
  return m_Info.m_AssetsPath + L"Shaders\\" + p_ShaderName;
}
std::wstring RenderManager::getAssetPath(LPCWSTR p_AssetName)
{
  return m_Info.m_AssetsPath + p_AssetName;
}
//---------------------------------------------------------------------------//
void RenderManager::loadD3D12Pipeline()
{
  UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
  // Enable the debug layer (requires the Graphics Tools "optional feature").
  // NOTE: Enabling the debug layer after device creation will invalidate the
  // active device.
  {
    ID3D12DebugPtr debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
      debugController->EnableDebugLayer();

      // Enable additional debug layers.
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  IDXGIFactory4Ptr factory;
  D3D_EXEC_CHECKED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  if (m_Info.m_UseWarpDevice)
  {
    IDXGIAdapterPtr warpAdapter;
    D3D_EXEC_CHECKED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    D3D_EXEC_CHECKED(D3D12CreateDevice(
        warpAdapter.GetInterfacePtr(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Dev)));
  }
  else
  {
    IDXGIAdapter1Ptr hardwareAdapter;
    getHardwareAdapter(factory.GetInterfacePtr(), &hardwareAdapter, true);

    D3D_EXEC_CHECKED(D3D12CreateDevice(
        hardwareAdapter.GetInterfacePtr(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Dev)));
  }

  // Describe and create the command queue.
  D3D12_COMMAND_QUEUE_DESC queueDesc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT, .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE};

  D3D_EXEC_CHECKED(m_Dev->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CmdQue)));
  D3D_NAME_OBJECT(m_CmdQue);

  // Describe and create the swap chain.
  DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
  swapChainDesc.OutputWindow = g_WinHandle;
  swapChainDesc.BufferCount = FRAME_COUNT;
  swapChainDesc.BufferDesc.Width = m_Info.m_Width;
  swapChainDesc.BufferDesc.Height = m_Info.m_Height;
  swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.Windowed = true;
  swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH |
                        DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
                        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

  IDXGISwapChain* tempSwapChain;
  D3D_EXEC_CHECKED(factory->CreateSwapChain(
      m_CmdQue.GetInterfacePtr(), // Swc needs the queue to force a flush on it.
      &swapChainDesc,
      &tempSwapChain));

  // This sample does not support fullscreen transitions.
  D3D_EXEC_CHECKED(factory->MakeWindowAssociation(g_WinHandle, DXGI_MWA_NO_ALT_ENTER));

  D3D_EXEC_CHECKED(tempSwapChain->QueryInterface(IID_PPV_ARGS(&m_Swc)));
  tempSwapChain->Release();

  m_FrameIndex = m_Swc->GetCurrentBackBufferIndex();
  m_SwapChainEvent = m_Swc->GetFrameLatencyWaitableObject();

  // Create descriptor heaps.
  {
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    D3D_EXEC_CHECKED(m_Dev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap)));

    m_RtvDescriptorSize = m_Dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }

  // Init uploads and other helpers
  initializeUpload(m_Dev);
  initializeHelpers();

  // Create frame resources.
  {
    // Create a RTV and a command allocator for each frame.
    for (UINT n = 0; n < FRAME_COUNT; n++)
    {
      m_RenderTargets[n].m_RTV = RTVDescriptorHeap.AllocatePersistent().Handles[0];
      D3D_EXEC_CHECKED(m_Swc->GetBuffer(n, IID_PPV_ARGS(&m_RenderTargets[n].m_Texture.Resource)));

      D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
      rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
      rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      rtvDesc.Texture2D.MipSlice = 0;
      rtvDesc.Texture2D.PlaneSlice = 0;
      m_Dev->CreateRenderTargetView(
          m_RenderTargets[n].m_Texture.Resource, &rtvDesc, m_RenderTargets[n].m_RTV);

      std::wstring rtName = L"Swapchain backbuffer #";
      rtName += std::to_wstring(n);
      m_RenderTargets[n].m_Texture.Resource->SetName(rtName.c_str());

      m_RenderTargets[n].m_Texture.Width = m_Info.m_Width;
      m_RenderTargets[n].m_Texture.Height = m_Info.m_Height;
      m_RenderTargets[n].m_Texture.ArraySize = 1;
      m_RenderTargets[n].m_Texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      m_RenderTargets[n].m_Texture.NumMips = 1;
      m_RenderTargets[n].m_MSAASamples = 1;

      D3D_EXEC_CHECKED(m_Dev->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CmdAllocs[n])));
    }
  }
}
//---------------------------------------------------------------------------//
bool RenderManager::createPSOs()
{
  bool ret = true;
  HRESULT hr = E_FAIL;

  // Release previous resources:
  if (gbufferPSO != nullptr)
    gbufferPSO->Release();
  if (deferredPSO != nullptr)
    deferredPSO->Release();

  // Load and compile shaders:
  ret = compileShaders();
  assert(m_GBufferPS != nullptr && m_GBufferVS != nullptr && m_DeferredCS != nullptr);

  // 1. Gbuffer pso:
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

    DXGI_FORMAT gbufferFormats[] = {
        tangentFrameTarget.format(),
        uvTarget.format(),
        materialIDTarget.format(),
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = gbufferRootSignature;
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_GBufferVS.GetInterfacePtr());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_GBufferPS.GetInterfacePtr());
    psoDesc.RasterizerState = GetRasterizerState(RasterizerState::BackFaceCull);
    psoDesc.BlendState = GetBlendState(BlendState::Disabled);
    psoDesc.DepthStencilState = GetDepthState(DepthState::WritesEnabled);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = arrayCount32(gbufferFormats);
    for (uint64_t i = 0; i < arrayCount32(gbufferFormats); ++i)
      psoDesc.RTVFormats[i] = gbufferFormats[i];
    psoDesc.DSVFormat = depthBuffer.DSVFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.InputLayout.NumElements = arrayCount32(standardInputElements);
    psoDesc.InputLayout.pInputElementDescs = standardInputElements;

    hr = m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&gbufferPSO));
    gbufferPSO->SetName(L"Gbuffer PSO");
    if (FAILED(hr))
      ret = false;
  }

  // 2. Deferred pso
  {
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.CS = CD3DX12_SHADER_BYTECODE(m_DeferredCS.GetInterfacePtr());
    psoDesc.pRootSignature = deferredRootSig;
    m_Dev->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&deferredPSO));
    deferredPSO->SetName(L"Deferred PSO");
    if (FAILED(hr))
      ret = false;
  }

  return ret;
}
//---------------------------------------------------------------------------//
void RenderManager::createRenderTargets()
{
  // Depth buffer
  {
    DepthBufferInit dbInit;
    dbInit.Width = m_Info.m_Width;
    dbInit.Height = m_Info.m_Height;
    dbInit.Format = DXGI_FORMAT_D32_FLOAT;
    dbInit.MSAASamples = 1;
    dbInit.InitialState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_READ;
    dbInit.Name = L"Main Depth Buffer";
    depthBuffer.init(dbInit);
  }

  // Create gbuffers:
  {
    RenderTextureInit rtInit;
    rtInit.Width = m_Info.m_Width;
    rtInit.Height = m_Info.m_Height;
    rtInit.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    rtInit.MSAASamples = 1;
    rtInit.ArraySize = 1;
    rtInit.CreateUAV = false;
    rtInit.InitialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rtInit.Name = L"Tangent Frame Target";
    tangentFrameTarget.init(rtInit);
  }
  {
    RenderTextureInit rtInit;
    rtInit.Width = m_Info.m_Width;
    rtInit.Height = m_Info.m_Height;
    rtInit.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
    rtInit.MSAASamples = 1;
    rtInit.ArraySize = 1;
    rtInit.CreateUAV = false;
    rtInit.InitialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rtInit.Name = L"UV Target";
    uvTarget.init(rtInit);
  }
  {
    RenderTextureInit rtInit;
    rtInit.Width = m_Info.m_Width;
    rtInit.Height = m_Info.m_Height;
    rtInit.Format = DXGI_FORMAT_R8_UINT;
    rtInit.MSAASamples = 1;
    rtInit.ArraySize = 1;
    rtInit.CreateUAV = false;
    rtInit.InitialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rtInit.Name = L"Material ID Target";
    materialIDTarget.init(rtInit);
  }

  // Create deferred target:
  {
    RenderTextureInit rtInit;
    rtInit.Width = m_Info.m_Width;
    rtInit.Height = m_Info.m_Height;
    rtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtInit.MSAASamples = 1;
    rtInit.ArraySize = 1;
    rtInit.CreateUAV = true;
    rtInit.InitialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    rtInit.Name = L"Main Target";
    deferredTarget.init(rtInit);
  }
}
//---------------------------------------------------------------------------//
void RenderManager::loadAssets()
{
  // set up camera
  float aspect = float(m_Info.m_Width) / m_Info.m_Height;
  camera.Initialize(aspect, glm::quarter_pi<float>(), 0.1f, 35.0f, float(m_Info.m_Width));
  camera.SetPosition(glm::vec3(-11.5f, 1.85f, -0.45f));
  camera.SetXRotation(0.0f);
  camera.SetYRotation(1.544f);

  // Model filename
  static const wchar_t* ScenePath = L"..\\Content\\Models\\Sponza\\Sponza.fbx";

  // Load scene
  static const float SceneScale = 0.01f;
  ModelLoadSettings settings;
  settings.FilePath = ScenePath;
  settings.ForceSRGB = true;
  settings.SceneScale = SceneScale;
  settings.MergeMeshes = false;
  sceneModel.CreateWithAssimp(m_Dev, settings);

  {
    // Initialize the spotlight data used for rendering
    const uint64_t numSpotLights =
        std::min<uint64_t>(MaxSpotLights, sceneModel.SpotLights().size());
    spotLights.resize(numSpotLights);

    for (uint64_t i = 0; i < numSpotLights; ++i)
    {
      const ModelSpotLight& srcLight = sceneModel.SpotLights()[i];

      SpotLight& spotLight = spotLights[i];
      spotLight.Position = srcLight.Position;
      spotLight.Direction = -srcLight.Direction;
      spotLight.Intensity = srcLight.Intensity * SpotLightIntensityFactor;
      spotLight.AngularAttenuationX = std::cos(srcLight.AngularAttenuation.x * 0.5f);
      spotLight.AngularAttenuationY = std::cos(srcLight.AngularAttenuation.y * 0.5f);
      spotLight.Range = 7.5f;
    }

    MaxLightClamp = static_cast<uint32_t>(numSpotLights);
  }

  // Init post fx
  {
    m_PostFx.init();
  }

  // Create a structured buffer containing texture indices per-material:
  // 1. get the array of materials
  // 2. loop through that and store the SRV indices (of the material textures)
  const std::vector<MeshMaterial>& materials = sceneModel.Materials();
  const uint64_t numMaterials = materials.size();
  std::vector<MaterialTextureIndices> textureIndices(numMaterials);
  for (uint64_t i = 0; i < numMaterials; ++i)
  {
    MaterialTextureIndices& matIndices = textureIndices[i];
    const MeshMaterial& material = materials[i];

    matIndices.Albedo = material.Textures[uint64_t(MaterialTextures::Albedo)]->SRV;
    matIndices.Normal = material.Textures[uint64_t(MaterialTextures::Normal)]->SRV;
    matIndices.Roughness = material.Textures[uint64_t(MaterialTextures::Roughness)]->SRV;
    matIndices.Metallic = material.Textures[uint64_t(MaterialTextures::Metallic)]->SRV;
  }
  StructuredBufferInit sbInit;
  sbInit.Stride = sizeof(MaterialTextureIndices);
  sbInit.NumElements = numMaterials;
  sbInit.Dynamic = false;
  sbInit.InitData = textureIndices.data();
  materialTextureIndices.init(sbInit);
  materialTextureIndices.resource()->SetName(L"Material Texture Indices");

  // Create render targets:
  {
    createRenderTargets();
  }

  {
    // Spot light and shadow bounds buffer
    ConstantBufferInit cbInit;
    cbInit.Size = sizeof(LightConstants);
    cbInit.Dynamic = true;
    cbInit.CPUAccessible = false;
    cbInit.InitialState = D3D12_RESOURCE_STATE_COMMON;
    cbInit.Name = L"Spot Light Buffer";

    spotLightBuffer.init(cbInit);
  }

  // Gbuffer Root Sig
  {
    D3D12_ROOT_PARAMETER1 rootParameters[2] = {};

    // VSCBuffer
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    // MatIndexCBuffer
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

    createRootSignature(m_Dev, &gbufferRootSignature, rootSignatureDesc);
    gbufferRootSignature->SetName(L"Gbuffer Root Sig");
  }

  // Deferred root signature
  {
    D3D12_DESCRIPTOR_RANGE1 descriptorRanges[1] = {};
    descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptorRanges[0].NumDescriptors = 1;
    descriptorRanges[0].BaseShaderRegister = 0;
    descriptorRanges[0].RegisterSpace = 0;
    descriptorRanges[0].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 rootParameters[NumDeferredRootParams] = {};

    rootParameters[DeferredParams_StandardDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[DeferredParams_StandardDescriptors].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[DeferredParams_StandardDescriptors].DescriptorTable.pDescriptorRanges =
        StandardDescriptorRanges();
    rootParameters[DeferredParams_StandardDescriptors].DescriptorTable.NumDescriptorRanges =
        NumStandardDescriptorRanges;

    // PSCBuffer
    rootParameters[DeferredParams_PSCBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[DeferredParams_PSCBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[DeferredParams_PSCBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[DeferredParams_PSCBuffer].Descriptor.ShaderRegister = 0;
    rootParameters[DeferredParams_PSCBuffer].Descriptor.Flags =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    // DeferredCBuffer
    rootParameters[DeferredParams_DeferredCBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[DeferredParams_DeferredCBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[DeferredParams_DeferredCBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[DeferredParams_DeferredCBuffer].Descriptor.ShaderRegister = 2;
    rootParameters[DeferredParams_DeferredCBuffer].Descriptor.Flags =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    // LightCBuffer
    rootParameters[DeferredParams_LightCBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[DeferredParams_LightCBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[DeferredParams_LightCBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[DeferredParams_LightCBuffer].Descriptor.ShaderRegister = 3;
    rootParameters[DeferredParams_LightCBuffer].Descriptor.Flags =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;

    // SRV Indices
    rootParameters[DeferredParams_SRVIndices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[DeferredParams_SRVIndices].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[DeferredParams_SRVIndices].Descriptor.RegisterSpace = 0;
    rootParameters[DeferredParams_SRVIndices].Descriptor.ShaderRegister = 4;
    rootParameters[DeferredParams_SRVIndices].Descriptor.Flags =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    // UAV's
    rootParameters[DeferredParams_UAVDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[DeferredParams_UAVDescriptors].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[DeferredParams_UAVDescriptors].DescriptorTable.pDescriptorRanges =
        descriptorRanges;
    rootParameters[DeferredParams_UAVDescriptors].DescriptorTable.NumDescriptorRanges =
        arrayCount32(descriptorRanges);

    // AppSettings
    rootParameters[DeferredParams_AppSettings].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[DeferredParams_AppSettings].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[DeferredParams_AppSettings].Descriptor.RegisterSpace = 0;
    rootParameters[DeferredParams_AppSettings].Descriptor.ShaderRegister =
        AppSettings::CBufferRegister;

    // AppSettings

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0] =
        GetStaticSamplerState(SamplerState::Anisotropic, 0, 0, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = arrayCount32(staticSamplers);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    createRootSignature(m_Dev, &deferredRootSig, rootSignatureDesc);
    deferredRootSig->SetName(L"Deferred Root Sig");
  }

  createPSOs();

  // Create the command list.
  D3D_EXEC_CHECKED(m_Dev->CreateCommandList(
      0,
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      m_CmdAllocs[m_FrameIndex].GetInterfacePtr(),
      gbufferPSO,
      IID_PPV_ARGS(&m_CmdList)));
  D3D_NAME_OBJECT(m_CmdList);

  // Close the command list and execute it to begin the initial GPU setup.
  D3D_EXEC_CHECKED(m_CmdList->Close());
  ID3D12CommandList* ppCommandLists[] = {m_CmdList.GetInterfacePtr()};
  m_CmdQue->ExecuteCommandLists(arrayCount32(ppCommandLists), ppCommandLists);

  // Create synchronization objects and wait until assets have been uploaded
  // to the GPU.
  {
    D3D_EXEC_CHECKED(m_Dev->CreateFence(
        m_RenderContextFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_RenderContextFence)));
    m_RenderContextFenceValue++;

    m_RenderContextFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_RenderContextFenceEvent == nullptr)
    {
      D3D_EXEC_CHECKED(HRESULT_FROM_WIN32(GetLastError()));
    }

    waitForRenderContext();
  }
}
void RenderManager::renderForward()
{
  // TODO:
  assert(false);
}
void RenderManager::renderDeferred()
{
  // Draw to Gbuffers
#pragma region Gbuffer pass
  PIXBeginEvent(m_CmdList.GetInterfacePtr(), 0, "Render Gbuffers");

  {
    // Transition our G-Buffer targets to a writable state
    D3D12_RESOURCE_BARRIER barriers[4] = {};

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = depthBuffer.getResource();
    barriers[0].Transition.StateBefore =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_READ;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[0].Transition.Subresource = 0;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[1].Transition.pResource = tangentFrameTarget.resource();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource = 0;

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[2].Transition.pResource = uvTarget.resource();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[2].Transition.Subresource = 0;

    barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[3].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[3].Transition.pResource = materialIDTarget.resource();
    barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[3].Transition.Subresource = 0;

    m_CmdList->ResourceBarrier(arrayCount32(barriers), barriers);
  }

  // Set the G-Buffer render targets and clear them
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[] = {
      tangentFrameTarget.m_RTV,
      uvTarget.m_RTV,
      materialIDTarget.m_RTV,
  };
  m_CmdList->OMSetRenderTargets(arrayCount32(rtvHandles), rtvHandles, false, &depthBuffer.DSV);
  const float clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
  for (uint64_t i = 0; i < arrayCount(rtvHandles); ++i)
    m_CmdList->ClearRenderTargetView(rtvHandles[i], clearColor, 0, nullptr);
  m_CmdList->ClearDepthStencilView(
      depthBuffer.DSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
  setViewport(m_CmdList, m_Info.m_Width, m_Info.m_Height);

  //
  // Render the Gbuffer!
  //

  m_CmdList->SetGraphicsRootSignature(gbufferRootSignature);
  m_CmdList->SetPipelineState(gbufferPSO);

  // Set constant buffers:
  {
    glm::mat4 world = glm::identity<glm::mat4>();
    MeshVSConstants vsConstants;
    vsConstants.World = world;
    vsConstants.View = glm::transpose(camera.ViewMatrix());
    vsConstants.WorldViewProjection = world * glm::transpose(camera.ViewProjectionMatrix());
    vsConstants.NearClip = camera.NearClip();
    vsConstants.FarClip = camera.FarClip();
    BindTempConstantBuffer(m_CmdList, vsConstants, 0, CmdListMode::Graphics);
  }

  // Bind vb and ib
  D3D12_VERTEX_BUFFER_VIEW vbView = sceneModel.VertexBuffer().vbView();
  D3D12_INDEX_BUFFER_VIEW ibView = sceneModel.IndexBuffer().IBView();
  m_CmdList->IASetVertexBuffers(0, 1, &vbView);
  m_CmdList->IASetIndexBuffer(&ibView);

  m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  //
  // Draw geometries:

  // TODO:
  // Frustum culling
  const uint64_t numVisible = sceneModel.Meshes().size();

  // Draw all visible meshes
  uint32_t currMaterial = uint32_t(-1);
  for (uint64_t i = 0; i < numVisible; ++i)
  {
    uint64_t meshIdx = i; // this should be just the visible mesh
    const Mesh& mesh = sceneModel.Meshes()[meshIdx];

    // Draw all parts
    for (uint64_t partIdx = 0; partIdx < mesh.NumMeshParts(); ++partIdx)
    {
      const MeshPart& part = mesh.MeshParts()[partIdx];
      if (part.MaterialIdx != currMaterial)
      {
        m_CmdList->SetGraphicsRoot32BitConstant(1, part.MaterialIdx, 0);
        currMaterial = part.MaterialIdx;
      }
      m_CmdList->DrawIndexedInstanced(
          part.IndexCount, 1, mesh.IndexOffset() + part.IndexStart, mesh.VertexOffset(), 0);
    }
  }

  // Transition back G-Buffer stuff:
  {
    D3D12_RESOURCE_BARRIER barriers[4] = {};

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = depthBuffer.getResource();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[0].Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_READ;
    barriers[0].Transition.Subresource = 0;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[1].Transition.pResource = tangentFrameTarget.resource();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.Subresource = 0;

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[2].Transition.pResource = uvTarget.resource();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[2].Transition.Subresource = 0;

    barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[3].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[3].Transition.pResource = materialIDTarget.resource();
    barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[3].Transition.Subresource = 0;

    m_CmdList->ResourceBarrier(arrayCount32(barriers), barriers);
  }

  PIXEndEvent(m_CmdList.GetInterfacePtr()); // Render Gbuffers
#pragma endregion

  //
  // Render fullscreen deferred pass!
  //
  PIXBeginEvent(m_CmdList.GetInterfacePtr(), 0, "Render Deferred");

  const uint32_t numComputeTilesX = alignUp<uint32_t>(uint32_t(deferredTarget.width()), 8) / 8;
  const uint32_t numComputeTilesY = alignUp<uint32_t>(uint32_t(deferredTarget.height()), 8) / 8;

  static bool firstAccess = true;
  if (firstAccess)
    deferredTarget.transition(
        m_CmdList,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  firstAccess = false;

  m_CmdList->SetComputeRootSignature(deferredRootSig);
  m_CmdList->SetPipelineState(deferredPSO);

  BindStandardDescriptorTable(m_CmdList, DeferredParams_StandardDescriptors, CmdListMode::Compute);

  // Set constant buffers
  {
    DeferredConstants deferredConstants;
    deferredConstants.InvViewProj = glm::transpose(glm::inverse(camera.ViewProjectionMatrix()));
    deferredConstants.Projection = camera.ProjectionMatrix();
    deferredConstants.RTSize =
        glm::vec2(float(deferredTarget.width()), float(deferredTarget.height()));
    deferredConstants.NumComputeTilesX = numComputeTilesX;
    BindTempConstantBuffer(
        m_CmdList, deferredConstants, DeferredParams_DeferredCBuffer, CmdListMode::Compute);

    uint32_t srvIndices[] = {
        materialTextureIndices.m_SrvIndex,
        materialIDTarget.srv(),
        uvTarget.srv(),
        depthBuffer.getSrv(),
        tangentFrameTarget.srv()};
    BindTempConstantBuffer(m_CmdList, srvIndices, DeferredParams_SRVIndices, CmdListMode::Compute);
  }

  {
    ShadingConstants shadingConstants;
    shadingConstants.CameraPosWS = camera.Position();
    shadingConstants.NumXTiles = numComputeTilesX;
    shadingConstants.NumXYTiles = numComputeTilesY;
    shadingConstants.NearClip = camera.NearClip();
    shadingConstants.FarClip = camera.FarClip();

    BindTempConstantBuffer(
        m_CmdList, shadingConstants, DeferredParams_PSCBuffer, CmdListMode::Compute);
  }

  spotLightBuffer.setAsComputeRootParameter(m_CmdList, DeferredParams_LightCBuffer);

  AppSettings::bindCBufferCompute(m_CmdList, DeferredParams_AppSettings);

  D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {deferredTarget.m_UAV};
  BindTempDescriptorTable(
      m_CmdList, uavs, arrayCount(uavs), DeferredParams_UAVDescriptors, CmdListMode::Compute);

  m_CmdList->Dispatch(numComputeTilesX, numComputeTilesY, 1);

  PIXEndEvent(m_CmdList.GetInterfacePtr()); // Render Deferred

  // Copy deferred target to backbuffer:
#if 0
  PIXBeginEvent(m_CmdList.GetInterfacePtr(), 0, "Copy to Backbuffer");

  deferredTarget.transition(
      m_CmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_CmdList->ResourceBarrier(
      1,
      &CD3DX12_RESOURCE_BARRIER::Transition(
          m_RenderTargets[m_FrameIndex].m_Texture.Resource,
          D3D12_RESOURCE_STATE_RENDER_TARGET,
          D3D12_RESOURCE_STATE_COPY_DEST));

  m_CmdList->CopyResource(
      m_RenderTargets[m_FrameIndex].m_Texture.Resource, deferredTarget.resource());

  m_CmdList->ResourceBarrier(
      1,
      &CD3DX12_RESOURCE_BARRIER::Transition(
          m_RenderTargets[m_FrameIndex].m_Texture.Resource,
          D3D12_RESOURCE_STATE_COPY_DEST,
          D3D12_RESOURCE_STATE_RENDER_TARGET));

  PIXEndEvent(m_CmdList.GetInterfacePtr()); // Copy to Backbuffer
#endif // 0
}
//---------------------------------------------------------------------------//
void RenderManager::populateCommandList()
{
  SetDescriptorHeaps(m_CmdList);

  renderDeferred();

  m_PostFx.render(m_CmdList, deferredTarget, m_RenderTargets[m_FrameIndex]);
}
//---------------------------------------------------------------------------//
void RenderManager::waitForRenderContext()
{
  // Add a signal command to the queue.
  D3D_EXEC_CHECKED(
      m_CmdQue->Signal(m_RenderContextFence.GetInterfacePtr(), m_RenderContextFenceValue));

  // Instruct the fence to set the event obj when the signal command
  // completes.
  D3D_EXEC_CHECKED(m_RenderContextFence->SetEventOnCompletion(
      m_RenderContextFenceValue, m_RenderContextFenceEvent));
  m_RenderContextFenceValue++;

  // Wait until the signal command has been processed.
  WaitForSingleObject(m_RenderContextFenceEvent, INFINITE);
}
//---------------------------------------------------------------------------//
void RenderManager::moveToNextFrame()
{
  // Assign the current fence value to the current frame.
  m_FrameFenceValues[m_FrameIndex] = m_RenderContextFenceValue;

  // Signal and increment the fence value.
  D3D_EXEC_CHECKED(
      m_CmdQue->Signal(m_RenderContextFence.GetInterfacePtr(), m_RenderContextFenceValue));
  m_RenderContextFenceValue++;

  // Update the frame index.
  m_FrameIndex = m_Swc->GetCurrentBackBufferIndex();

  // If the next frame is not ready to be rendered yet, wait until it is
  // ready.
  if (m_RenderContextFence->GetCompletedValue() < m_FrameFenceValues[m_FrameIndex])
  {
    D3D_EXEC_CHECKED(m_RenderContextFence->SetEventOnCompletion(
        m_FrameFenceValues[m_FrameIndex], m_RenderContextFenceEvent));
    WaitForSingleObject(m_RenderContextFenceEvent, INFINITE);
  }
}
//---------------------------------------------------------------------------//
void RenderManager::restoreD3DResources()
{
  // Give GPU a chance to finish its execution in progress.
  try
  {
    waitForRenderContext();
  }
  catch (std::exception)
  {
    // Do nothing, currently attached adapter is unresponsive.
  }
  releaseD3DResources();
  onLoad();
}
//---------------------------------------------------------------------------//
void RenderManager::releaseD3DResources()
{
  m_RenderContextFence = nullptr;
  for (UINT n = 0; n < FRAME_COUNT; n++)
    m_RenderTargets[n].m_Texture.Resource->Release();
  m_CmdQue = nullptr;
  m_Swc = nullptr;
  m_Dev = nullptr;
}
//---------------------------------------------------------------------------//
// Main public methods
//---------------------------------------------------------------------------//
void RenderManager::OnInit(UINT p_Width, UINT p_Height, std::wstring p_Name)
{
  m_Info.m_Width = p_Width;
  m_Info.m_Height = p_Height;
  m_Info.m_Title = p_Name;
  m_Info.m_UseWarpDevice = false;

  WCHAR assetsPath[512];
  getAssetsPath(assetsPath, _countof(assetsPath));
  m_Info.m_AssetsPath = assetsPath;

  m_Info.m_AspectRatio = static_cast<float>(p_Width) / static_cast<float>(p_Height);

  m_Info.m_IsInitialized = true;
}
//---------------------------------------------------------------------------//
void RenderManager::onLoad()
{
  DEBUG_BREAK(m_Info.m_IsInitialized);

  UINT width = m_Info.m_Width;
  UINT height = m_Info.m_Height;

  setArrayToZero(m_FrameFenceValues);

  D3D_EXEC_CHECKED(DXGIDeclareAdapterRemovalSupport());

  m_Timer.init();

  // Load pipeline
  loadD3D12Pipeline();

  // Initialize COM for DirectX Texture Loader
  CoInitializeEx(NULL, COINIT_MULTITHREADED);

  // Load assets
  loadAssets();

  // Init imgui
  ImGuiHelper::init(g_WinHandle, m_Dev);

  // General appsettings
  AppSettings::init();
}
//---------------------------------------------------------------------------//
void RenderManager::onDestroy()
{
  m_Timer.deinit();

  // Ensure that the GPU is no longer referencing resources that are about to
  // be cleaned up by the destructor.
  waitForRenderContext();

  // Close handles to fence events and threads.
  CloseHandle(m_RenderContextFenceEvent);

  sceneModel.Shutdown();

  // TODO Release these in mesh renderer
  gbufferRootSignature->Release();
  gbufferPSO->Release();
  deferredRootSig->Release();
  deferredPSO->Release();

  depthBuffer.deinit();

  // Shutdown render target(s):
  uvTarget.deinit();
  tangentFrameTarget.deinit();
  materialIDTarget.deinit();
  materialTextureIndices.deinit();
  deferredTarget.deinit();
  spotLightBuffer.deinit();

  // Release swc backbuffers
  for (UINT n = 0; n < FRAME_COUNT; n++)
  {
    m_RenderTargets[n].m_Texture.Resource->Release();
    RTVDescriptorHeap.FreePersistent(m_RenderTargets[n].m_RTV);
  }

  m_PostFx.deinit();
  AppSettings::deinit();
  ImGuiHelper::deinit();

  // Shudown uploads and other helpers
  shutdownHelpers();
  shutdownUpload();
}
//---------------------------------------------------------------------------//
void RenderManager::onUpdate()
{
  m_Timer.update();

  // Wait for the previous Present to complete.
  WaitForSingleObjectEx(m_SwapChainEvent, 100, FALSE);

  // Rotate the camera with the mouse
  {
    // static float CamRotSpeed = 0.0005f * m_Timer.m_DeltaSecondsF;
    static float CamRotSpeed = 0.001f;
    POINT pos;
    GetCursorPos(&pos);
    if (g_WinHandle)
      ScreenToClient(g_WinHandle, &pos);
    float dx = static_cast<float>(pos.x - prevMousePos.x);
    float dy = static_cast<float>(pos.y - prevMousePos.y);
    bool rbPressed = (GetKeyState(VK_RBUTTON) & 0x8000) > 0;
    // bool lbPressed = (GetKeyState(VK_LBUTTON) & 0x8000) > 0;
    if (rbPressed && isMouseOverWindow(pos))
    {
      float xRot = camera.XRotation();
      float yRot = camera.YRotation();
      xRot += dy * CamRotSpeed;
      yRot += dx * CamRotSpeed;
      camera.SetXRotation(xRot);
      camera.SetYRotation(yRot);
    }
    prevMousePos = pos;
  }

  // Keyboard input handling
  {
    float CamMoveSpeed = 5.0f * m_Timer.m_DeltaSecondsF;
    KeyboardState kbState = KeyboardState::GetKeyboardState(g_WinHandle);
    // Move the camera with keyboard input
    if (kbState.IsKeyDown(KeyboardState::LeftShift))
      CamMoveSpeed *= 0.25f;

    glm::vec3 camPos = camera.Position();
    if (kbState.IsKeyDown(KeyboardState::W))
      camPos += camera.Forward() * CamMoveSpeed;
    else if (kbState.IsKeyDown(KeyboardState::S))
      camPos += camera.Back() * CamMoveSpeed;
    if (kbState.IsKeyDown(KeyboardState::A))
      camPos += camera.Left() * CamMoveSpeed;
    else if (kbState.IsKeyDown(KeyboardState::D))
      camPos += camera.Right() * CamMoveSpeed;
    if (kbState.IsKeyDown(KeyboardState::Q))
      camPos += camera.Up() * CamMoveSpeed;
    else if (kbState.IsKeyDown(KeyboardState::E))
      camPos += camera.Down() * CamMoveSpeed;
    camera.SetPosition(camPos);
  }

  // Update light uniforms
  {
    const void* srcData[1] = {spotLights.data()};
    uint64_t sizes[1] = {spotLights.size() * sizeof(SpotLight)};
    uint64_t offsets[1] = {0};
    spotLightBuffer.multiUpdateData(srcData, sizes, offsets, arrayCount(srcData));
  }

  // Update application settings
  AppSettings::updateCBuffer();

  // Imgui begin frame:
  // TODO: add correct delta time
  ImGuiHelper::beginFrame(m_Info.m_Width, m_Info.m_Height, 1.0f / 60.0f, m_Dev);
}
//---------------------------------------------------------------------------//
void RenderManager::onRender()
{
  if (m_Info.m_IsInitialized)
  {
    try
    {
      // Wait for frame:
      waitForRenderContext();

      // Prepare for [re]-recording commands:
      D3D_EXEC_CHECKED(m_CmdAllocs[m_FrameIndex]->Reset());
      D3D_EXEC_CHECKED(m_CmdList->Reset(m_CmdAllocs[m_FrameIndex].GetInterfacePtr(), gbufferPSO));

      // Swc begin-frame backbuffer transition:
      m_CmdList->ResourceBarrier(
          1,
          &CD3DX12_RESOURCE_BARRIER::Transition(
              m_RenderTargets[m_FrameIndex].m_Texture.Resource,
              D3D12_RESOURCE_STATE_PRESENT,
              D3D12_RESOURCE_STATE_RENDER_TARGET));

      // TODO: move this stuff to the correct location!
      endFrameHelpers();
      EndFrame_Upload(m_CmdQue);

      PIXBeginEvent(m_CmdQue.GetInterfacePtr(), 0, L"Render");

      // Internal rendering code:
      populateCommandList();

      // Imgui rendering:
      {
        PIXBeginEvent(m_CmdList.GetInterfacePtr(), 0, "Render Imgui");
        ImGuiHelper::endFrame(
            m_CmdList, m_RenderTargets[m_FrameIndex].m_RTV, m_Info.m_Width, m_Info.m_Height);
        PIXEndEvent(m_CmdQue.GetInterfacePtr()); // Render Imgui
      }

      // Swc end-frame backbuffer transition:
      m_CmdList->ResourceBarrier(
          1,
          &CD3DX12_RESOURCE_BARRIER::Transition(
              m_RenderTargets[m_FrameIndex].m_Texture.Resource,
              D3D12_RESOURCE_STATE_RENDER_TARGET,
              D3D12_RESOURCE_STATE_PRESENT));

      // Execute the command list.
      D3D_EXEC_CHECKED(m_CmdList->Close());
      ID3D12CommandList* ppCommandLists[] = {m_CmdList.GetInterfacePtr()};
      m_CmdQue->ExecuteCommandLists(arrayCount32(ppCommandLists), ppCommandLists);

      PIXEndEvent(m_CmdQue.GetInterfacePtr()); // Render

      // Present the frame.
      D3D_EXEC_CHECKED(m_Swc->Present(1, 0));

      ++g_CurrentCPUFrame;

      moveToNextFrame();
      // waitForRenderContext();
    }
    catch (HrException& e)
    {
      if (e.Error() == DXGI_ERROR_DEVICE_REMOVED || e.Error() == DXGI_ERROR_DEVICE_RESET)
      {
        restoreD3DResources();
      }
      else
      {
        throw;
      }
    }
  }
}
//---------------------------------------------------------------------------//
void RenderManager::onKeyDown(UINT8 p_Key)
{
  // float CamMoveSpeed = 0.1f;
  float CamMoveSpeed = 5.5f * m_Timer.m_DeltaSecondsF;
  glm::vec3 camPos = camera.Position();

  switch (p_Key)
  {
  case 'W': {
    camPos += camera.Forward() * CamMoveSpeed;
  }
  break;
  case 'A': {
    camPos += camera.Left() * CamMoveSpeed;
  }
  break;
  case 'S': {
    camPos += camera.Back() * CamMoveSpeed;
  }
  break;
  case 'D': {
    camPos += camera.Right() * CamMoveSpeed;
  }
  break;
  case VK_LEFT: {
    camPos += camera.Left() * CamMoveSpeed;
  }
  break;
  case VK_RIGHT: {
    camPos += camera.Right() * CamMoveSpeed;
  }
  break;
  case VK_UP: {
    camPos += camera.Up() * CamMoveSpeed;
  }
  break;
  case VK_DOWN: {
    camPos += camera.Down() * CamMoveSpeed;
  }
  break;
  }

  camera.SetPosition(camPos);
}
//---------------------------------------------------------------------------//
void RenderManager::onKeyUp(UINT8 p_Key) {}
//---------------------------------------------------------------------------//
void RenderManager::onResize()
{
  RECT clientRect;
  ::GetClientRect(g_WinHandle, &clientRect);
  m_Info.m_Width = clientRect.right;
  m_Info.m_Height = clientRect.bottom;

  // Flush gpu before resizing swapchain
  waitForRenderContext();

  // Release all references
  for (UINT i = 0; i < FRAME_COUNT; ++i)
  {
    m_RenderTargets[i].m_Texture.Resource->Release();
    RTVDescriptorHeap.FreePersistent(m_RenderTargets[i].m_RTV);
  }

  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  DXGI_RATIONAL refreshRate = {};
  refreshRate.Numerator = 60;
  refreshRate.Denominator = 1;

  m_Swc->ResizeBuffers(
      FRAME_COUNT,
      m_Info.m_Width,
      m_Info.m_Height,
      format,
      DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
          DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

  // Re-create an RTV for each back buffer
  for (UINT i = 0; i < FRAME_COUNT; i++)
  {
    m_RenderTargets[i].m_RTV = RTVDescriptorHeap.AllocatePersistent().Handles[0];
    D3D_EXEC_CHECKED(
        m_Swc->GetBuffer(uint32_t(i), IID_PPV_ARGS(&m_RenderTargets[i].m_Texture.Resource)));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = format;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    m_Dev->CreateRenderTargetView(
        m_RenderTargets[i].m_Texture.Resource, &rtvDesc, m_RenderTargets[i].m_RTV);

    m_RenderTargets[i].m_Texture.Resource->SetName(MakeString(L"Back Buffer %u", i).c_str());

    m_RenderTargets[i].m_Texture.Width = m_Info.m_Width;
    m_RenderTargets[i].m_Texture.Height = m_Info.m_Height;
    m_RenderTargets[i].m_Texture.ArraySize = 1;
    m_RenderTargets[i].m_Texture.Format = format;
    m_RenderTargets[i].m_Texture.NumMips = 1;
    m_RenderTargets[i].m_MSAASamples = 1;
  }
  m_FrameIndex = m_Swc->GetCurrentBackBufferIndex();

  float aspect = float(m_Info.m_Width) / m_Info.m_Height;
  camera.SetAspectRatio(aspect);

  createRenderTargets();

  // Re-create psos:
  createPSOs();
  // m_PostFx.init();

  waitForRenderContext();
}
//---------------------------------------------------------------------------//
void RenderManager::onCodeChange() {}
//---------------------------------------------------------------------------//
void RenderManager::onShaderChange()
{
  OutputDebugStringA("[RenderManager] Starting shader reload...\n");
  Sleep(1000);
  waitForRenderContext();

  if (compileShaders())
  {
    OutputDebugStringA("[RenderManager] Shaders loaded\n");
    createPSOs();
    m_PostFx.deinit();
    m_PostFx.init();
    return;
  }
  else
  {
    OutputDebugStringA("[RenderManager] Failed to reload the shaders\n");
  }
}
//---------------------------------------------------------------------------//
