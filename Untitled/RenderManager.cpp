#include "RenderManager.hpp"
#include "ImguiHelper.hpp"
#include <pix3.h>
#include <algorithm>
#include "Common/Input.hpp"
#include "Common/Quaternion.hpp"
#include "Common/Spectrum.hpp"

#define ENABLE_PARTICLE_EXPERIMENTAL 0
#define ENABLE_GPU_BASED_VALIDATION 0

//---------------------------------------------------------------------------//
// Internal variables
//---------------------------------------------------------------------------//
static POINT prevMousePos = {};
static const float SpotLightIntensityFactor = 25.0f;
static const uint64_t SpotLightShadowMapSize = 1024;
static const uint64_t SunShadowMapSize = 2048;
static const uint64_t NumConeSides = 16;
static glm::mat4 prevViewProj = glm::mat4();
static glm::vec2 jitterOffsetXY = glm::vec2(0.0f, 0.0f);
static glm::vec2 jitterXY = glm::vec2(0.0f, 0.0f);
static glm::vec2 prevJitterXY = glm::vec2(0.0f, 0.0f);
static bool pauseRendering = false;

//---------------------------------------------------------------------------//
// Local helpers
//---------------------------------------------------------------------------//
// Returns true if a sphere intersects a capped cone defined by a direction, height, and angle
static bool _sphereConeIntersection(
    const glm::vec3& coneTip,
    const glm::vec3& coneDir,
    float coneHeight,
    float coneAngle,
    const glm::vec3& sphereCenter,
    float sphereRadius)
{
  if (glm::dot(sphereCenter - coneTip, coneDir) > coneHeight + sphereRadius)
    return false;

  float cosHalfAngle = std::cos(coneAngle * 0.5f);
  float sinHalfAngle = std::sin(coneAngle * 0.5f);

  glm::vec3 v = sphereCenter - coneTip;
  float a = glm::dot(v, coneDir);
  float b = a * sinHalfAngle / cosHalfAngle;
  float c = std::sqrt(glm::dot(v, v) - a * a);
  float d = c - b;
  float e = d * cosHalfAngle;

  return e < sphereRadius;
}
//---------------------------------------------------------------------------//
// Cone-sphere intersection test using Bart Wronski modified version:
// https://bartwronski.com/2017/04/13/cull-that-cone/
static bool _sphereConeIntersectionBartWronski(
    const glm::vec3& coneTip,
    const glm::vec3& coneDir,
    float coneHeight,
    float coneAngle,
    const glm::vec3& sphereCenter,
    float sphereRadius)
{
  const float angle = coneAngle * 0.5f;

  const glm::vec3 V = sphereCenter - coneTip;
  const float VlenSq = glm::dot(V, V);
  const float V1len = glm::dot(V, coneDir);
  const float distanceClosestPoint = cos(angle) * sqrt(VlenSq - V1len * V1len) - V1len * sin(angle);

  const bool angleCull = distanceClosestPoint > sphereRadius;
  const bool frontCull = V1len > sphereRadius + coneHeight;
  const bool backCull = V1len < -sphereRadius;
  return !(angleCull || frontCull || backCull);
}
//---------------------------------------------------------------------------//
// Numerical sequences
//---------------------------------------------------------------------------//
float _halton (int i, int b)
{
  // Creates a halton sequence of values between 0 and 1.
  // https://en.wikipedia.org/wiki/Halton_sequence
  // Used for jittering based on a constant set of 2D points.
  float f = 1.0f;
  float r = 0.0f;
  while (i > 0)
  {
    f = f / float(b);
    r = r + f * float(i % b);
    i = i / b;
  }
  return r;
}
//---------------------------------------------------------------------------//
// https://blog.demofox.org/2017/10/31/animating-noise-for-integration-over-time/
float _interleavedGradientNoise (glm::vec2 pixel, int index)
{
  pixel.x += float(index) * 5.588238f;
  pixel.y += float(index) * 5.588238f;
  const float noise =
      fmodf(52.9829189f * fmodf(0.06711056f * pixel.x + 0.00583715f * pixel.y, 1.0f), 1.0f);
  return noise;
}
//---------------------------------------------------------------------------//
glm::vec2 _halton23Sequence (int index) { return glm::vec2{_halton(index, 2), _halton(index, 3)}; }
//---------------------------------------------------------------------------//
// http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
glm::vec2 _martinRobertsR2Sequence (int index)
{
  const float g = 1.32471795724474602596f;
  const float a1 = 1.0f / g;
  const float a2 = 1.0f / (g * g);

  const float x = fmod(0.5f + a1 * index, 1.0f);
  const float y = fmod(0.5f + a2 * index, 1.0f);
  return glm::vec2{x, y};
}
//---------------------------------------------------------------------------//
glm::vec2 _interleavedGradientSequence (int index)
{
  return glm::vec2{
      _interleavedGradientNoise({1.f, 1.f}, index), _interleavedGradientNoise({1.f, 2.f}, index)};
}
//---------------------------------------------------------------------------//
// Computes a radical inverse with base 2 using crazy bit-twiddling from "Hacker's Delight"
inline float _radicalInverseBase2 (uint32_t bits)
{
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}
//---------------------------------------------------------------------------//
// Returns a single 2D point in a Hammersley sequence of length "numSamples", using base 1 and base 2
glm::vec2 _hammersleySequence (int index, int numSamples)
{
  return glm::vec2{index * 1.f / numSamples, _radicalInverseBase2(uint32_t(index))};
}

//---------------------------------------------------------------------------//
// Internal structs
//---------------------------------------------------------------------------//

namespace JitterType
{
enum Enum
{
  Halton = 0,
  R2,
  Hammersley,
  InterleavedGradients
};
const char* names[] = {"Halton", "Martin Roberts R2", "Hammersley", "Interleaved Gradients"};
} // namespace JitterType

enum ClusterRootParams : uint32_t
{
  ClusterParams_StandardDescriptors,
  ClusterParams_UAVDescriptors,
  ClusterParams_CBuffer,
  ClusterParams_AppSettings,

  NumClusterRootParams,
};

enum ClusterVisRootParams : uint32_t
{
  ClusterVisParams_StandardDescriptors,
  ClusterVisParams_CBuffer,
  ClusterVisParams_AppSettings,

  NumClusterVisRootParams,
};

enum DeferredRootParams : uint32_t
{
  DeferredParams_StandardDescriptors, // textures
  DeferredParams_PSCBuffer,
  DeferredParams_ShadowCBuffer,
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
  float nearClip = 0;
  float farClip = 0;
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
  SpotLight Lights[AppSettings::MaxSpotLights];
  glm::mat4x4 ShadowMatrices[AppSettings::MaxSpotLights];
};

struct ClusterBounds
{
  glm::vec3 Position;
  Quaternion Orientation;
  glm::vec3 Scale;
  glm::uvec2 ZBounds;
};

struct ClusterConstants
{
  glm::mat4 ViewProjection;
  glm::mat4 InvProjection;
  float NearClip = 0.0f;
  float FarClip = 0.0f;
  float InvClipRange = 0.0f;
  uint32_t NumXTiles = 0;
  uint32_t NumYTiles = 0;
  uint32_t NumXYTiles = 0;
  uint32_t ElementsPerCluster = 0;
  uint32_t InstanceOffset = 0;
  uint32_t NumLights = 0;
  uint32_t NumDecals = uint32_t(-1); // TODO!

  uint32_t BoundsBufferIdx = uint32_t(-1);
  uint32_t VertexBufferIdx = uint32_t(-1);
  uint32_t InstanceBufferIdx = uint32_t(-1);
};

struct ClusterVisConstants
{
  glm::mat4 Projection;
  glm::vec3 ViewMin;
  float NearClip = 0.0f;
  glm::vec3 ViewMax;
  float InvClipRange = 0.0f;
  glm::vec2 DisplaySize;
  uint32_t NumXTiles = 0;
  uint32_t NumXYTiles = 0;

  uint32_t DecalClusterBufferIdx = uint32_t(-1); // TODO!
  uint32_t SpotLightClusterBufferIdx = uint32_t(-1);
};

//---------------------------------------------------------------------------//
// Internal private methods
//---------------------------------------------------------------------------//
bool RenderManager::compileShaders()
{
  bool ret = true;

  #if defined(_DEBUG)
  UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  UINT compileFlags = 0;
#endif
  compileFlags |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;

  // Gbuffer shaders
  {
    const D3D_SHADER_MACRO defines[] = {{"GBUFFER_VS_DBG", "1"}, {NULL, NULL}};
    compileShader(
        "gbuffer vertex",
        getShaderPath(L"Mesh.hlsl").c_str(),
        arrayCountU8(defines),
        defines,
        compileFlags,
        ShaderType::Vertex,
        "VS",
        m_GBufferVS);
  }

  {
    const D3D_SHADER_MACRO defines[] = {{"GBUFFER_PS_DBG", "1"}, {NULL, NULL}};
    compileShader(
        "gbuffer fragment",
        getShaderPath(L"Mesh.hlsl").c_str(),
        arrayCountU8(defines),
        defines,
        compileFlags,
        ShaderType::Pixel,
        "PS",
        m_GBufferPS);
  }

  // Deferred shader
  {
    const D3D_SHADER_MACRO defines[] = {{"DEFERRED_DBG", "1"}, {NULL, NULL}};
    compileShader(
        "deferred compute",
        getShaderPath(L"Deferred.hlsl").c_str(),
        arrayCountU8(defines),
        defines,
        compileFlags,
        ShaderType::Compute,
        "CS",
        m_DeferredCS);
  }

  // Clustering shaders
  {
    {
      const D3D_SHADER_MACRO defines[] = {
          {"FrontFace_", "1"}, {"BackFace_", "0"}, {"Intersecting_", "0"}, {NULL, NULL}};
      compileShader(
          "cluster vertex",
          getShaderPath(L"Clusters.hlsl").c_str(),
          arrayCountU8(defines),
          defines,
          compileFlags,
          ShaderType::Vertex,
          "ClusterVS",
          clusterVS);
    }

    {
      const D3D_SHADER_MACRO defines[] = {
          {"FrontFace_", "1"}, {"BackFace_", "0"}, {"Intersecting_", "0"}, {NULL, NULL}};
      compileShader(
          "cluster frontface",
          getShaderPath(L"Clusters.hlsl").c_str(),
          arrayCountU8(defines),
          defines,
          compileFlags,
          ShaderType::Pixel,
          "ClusterPS",
          clusterFrontFacePS);
    }

    {
      const D3D_SHADER_MACRO defines[] = {
          {"FrontFace_", "0"}, {"BackFace_", "1"}, {"Intersecting_", "0"}, {NULL, NULL}};
      compileShader(
          "cluster backface",
          getShaderPath(L"Clusters.hlsl").c_str(),
          arrayCountU8(defines),
          defines,
          compileFlags,
          ShaderType::Pixel,
          "ClusterPS",
          clusterBackFacePS);
    }

    {
      const D3D_SHADER_MACRO defines[] = {
          {"FrontFace_", "0"}, {"BackFace_", "0"}, {"Intersecting_", "1"}, {NULL, NULL}};
      compileShader(
          "cluster intersecting",
          getShaderPath(L"Clusters.hlsl").c_str(),
          arrayCountU8(defines),
          defines,
          compileFlags,
          ShaderType::Pixel,
          "ClusterPS",
          clusterIntersectingPS);
    }

    {
      compileShader(
          "cluster visualizer",
          getShaderPath(L"ClusterVisualizer.hlsl").c_str(),
          0,
          nullptr,
          compileFlags,
          ShaderType::Pixel,
          "ClusterVisualizerPS",
          clusterVisPS);
    }

    {
      compileShader(
          "cluster visualizer",
          getShaderPath(L"ClusterVisualizer.hlsl").c_str(),
          0,
          nullptr,
          compileFlags,
          ShaderType::Pixel,
          "ClusterVisualizerPS",
          clusterVisPS);
    }
  }

  // Fullscreen triangle
  {
    compileShader(
        "fullscreen triangle",
        getShaderPath(L"FullScreenTriangle.hlsl").c_str(),
        0,
        nullptr,
        compileFlags,
        ShaderType::Vertex,
        "VS",
        fullScreenTriVS);
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
      // d3d12 validation layer
      debugController->EnableDebugLayer();

      // enable synchronization layer
#  if (ENABLE_GPU_BASED_VALIDATION > 0)

      Microsoft::WRL::ComPtr<ID3D12Debug1> debugController1;
      D3D_EXEC_CHECKED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1)));

      debugController1->EnableDebugLayer();
      debugController1->SetEnableGPUBasedValidation(true);
      debugController1->SetEnableSynchronizedCommandQueueValidation(true);
#  endif // (ENABLE_GPU_BASED_VALIDATION > 0)

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

#if defined(_DEBUG)
  // Break on d3d12 validation errors
  ID3D12InfoQueue* infoQueue = nullptr;
  HRESULT result = m_Dev->QueryInterface(IID_PPV_ARGS(&infoQueue));
  infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
  D3D_EXEC_CHECKED(result);
#endif

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
  initializeHelpers(m_Dev);
  initializeUpload(m_Dev);

  // Create frame resources.
  {
    // NOTE: We cannot directly use SRGB format on swapchain
    // instead we use srgb rendertargets as described here:
    // https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/bb173064(v=vs.85)?redirectedfrom=MSDN

    // Create a RTV and a command allocator for each frame.
    for (UINT n = 0; n < FRAME_COUNT; n++)
    {
      m_RenderTargets[n].m_RTV = RTVDescriptorHeap.AllocatePersistent().Handles[0];
      D3D_EXEC_CHECKED(m_Swc->GetBuffer(n, IID_PPV_ARGS(&m_RenderTargets[n].m_Texture.Resource)));

      D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
      rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
      rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
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
      m_RenderTargets[n].m_Texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
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
  if (spotLightShadowPSO != nullptr)
    spotLightShadowPSO->Release();

  if (clusterFrontFacePSO != nullptr)
    clusterFrontFacePSO->Release();
  if (clusterBackFacePSO != nullptr)
    clusterBackFacePSO->Release();
  if (clusterIntersectingPSO != nullptr)
    clusterIntersectingPSO->Release();
  if (clusterVisPSO != nullptr)
    clusterVisPSO->Release();

  // Load and compile shaders:
  ret = compileShaders();
  assert(
      m_GBufferPS != nullptr && m_GBufferVS != nullptr && m_DeferredCS != nullptr &&
      clusterVS != nullptr && clusterFrontFacePS != nullptr && clusterBackFacePS != nullptr &&
      clusterIntersectingPS != nullptr && clusterVisPS != nullptr && fullScreenTriVS != nullptr);

  // Standard input elements
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

  // 1. Gbuffer pso:
  {
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

  // 3. depth only psos
  {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = depthRootSignature;
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_GBufferVS.GetInterfacePtr());
    psoDesc.RasterizerState = GetRasterizerState(RasterizerState::BackFaceCull);
    psoDesc.BlendState = GetBlendState(BlendState::Disabled);
    psoDesc.DepthStencilState = GetDepthState(DepthState::WritesEnabled);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat = depthBuffer.DSVFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.InputLayout.NumElements = arrayCount32(standardInputElements);
    psoDesc.InputLayout.pInputElementDescs = standardInputElements;
    hr = m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&depthPSO));
    depthPSO->SetName(L"Depth-only PSO");
    if (FAILED(hr))
      ret = false;

    // Spotlight shadow depth PSO
    psoDesc.DSVFormat = spotLightShadowMap.DSVFormat;
    hr = m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&spotLightShadowPSO));
    spotLightShadowPSO->SetName(L"Spotlight shadow PSO");
    if (FAILED(hr))
      ret = false;

    // sun shadow map pso
    psoDesc.DSVFormat = sunShadowMap.DSVFormat;
    psoDesc.SampleDesc.Count = sunShadowMap.MSAASamples;
    psoDesc.SampleDesc.Quality = sunShadowMap.MSAASamples > 1 ? StandardMSAAPattern : 0;
    psoDesc.RasterizerState = GetRasterizerState(RasterizerState::BackFaceCullNoZClip);
    hr = m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&sunShadowPSO));
    if (FAILED(hr))
      ret = false;
  }

  // 4. clustering psos
  {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = clusterRS;
    psoDesc.BlendState = GetBlendState(BlendState::Disabled);
    psoDesc.DepthStencilState = GetDepthState(DepthState::Disabled);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(clusterVS.GetInterfacePtr());
    psoDesc.SampleDesc.Count = 1;

    // TODO: toggle conservative mode
    D3D12_CONSERVATIVE_RASTERIZATION_MODE crMode = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;

    psoDesc.PS = CD3DX12_SHADER_BYTECODE(clusterFrontFacePS.GetInterfacePtr());
    psoDesc.RasterizerState = GetRasterizerState(RasterizerState::BackFaceCull);
    psoDesc.RasterizerState.ConservativeRaster = crMode;
    hr = m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&clusterFrontFacePSO));
    if (FAILED(hr))
      ret = false;

    psoDesc.PS = CD3DX12_SHADER_BYTECODE(clusterBackFacePS.GetInterfacePtr());
    psoDesc.RasterizerState = GetRasterizerState(RasterizerState::FrontFaceCull);
    psoDesc.RasterizerState.ConservativeRaster = crMode;
    (hr = m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&clusterBackFacePSO)));
    if (FAILED(hr))
      ret = false;

    psoDesc.PS = CD3DX12_SHADER_BYTECODE(clusterIntersectingPS.GetInterfacePtr());
    psoDesc.RasterizerState = GetRasterizerState(RasterizerState::FrontFaceCull);
    psoDesc.RasterizerState.ConservativeRaster = crMode;
    hr = m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&clusterIntersectingPSO));
    if (FAILED(hr))
      ret = false;

    clusterFrontFacePSO->SetName(L"Cluster Front-Face PSO");
    clusterBackFacePSO->SetName(L"Cluster Back-Face PSO");
    clusterIntersectingPSO->SetName(L"Cluster Intersecting PSO");
  }

  // 5. cluster visualizer pso
  {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = clusterVisRootSignature;
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(fullScreenTriVS.GetInterfacePtr());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(clusterVisPS.GetInterfacePtr());
    psoDesc.RasterizerState = GetRasterizerState(RasterizerState::NoCull);
    psoDesc.BlendState = GetBlendState(BlendState::AlphaBlend);
    psoDesc.DepthStencilState = GetDepthState(DepthState::Disabled);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&clusterVisPSO));
  }

  // create sky psos
  skybox.CreatePSOs(
      deferredTarget.m_Texture.Format, depthBuffer.DSVFormat, 1);

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
    rtInit.InitialState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    rtInit.Name = L"Main Target";
    deferredTarget.init(rtInit);
  }

  // Clustered rendering
  AppSettings::NumXTiles =
      (m_Info.m_Width + (AppSettings::ClusterTileSize - 1)) / AppSettings::ClusterTileSize;
  AppSettings::NumYTiles =
      (m_Info.m_Height + (AppSettings::ClusterTileSize - 1)) / AppSettings::ClusterTileSize;
  const uint64_t numXYZTiles =
      AppSettings::NumXTiles * AppSettings::NumYTiles * AppSettings::NumZTiles;

  // Spotlight cluster bitmask buffer
  {
    RawBufferInit rbInit;
    rbInit.NumElements = numXYZTiles * AppSettings::SpotLightElementsPerCluster;
    rbInit.CreateUAV = true;
    spotLightClusterBuffer.init(rbInit);
    spotLightClusterBuffer.InternalBuffer.m_Resource->SetName(L"Spot Light Cluster Buffer");
  }
}
//---------------------------------------------------------------------------//
void RenderManager::loadAssets()
{
  ShadowHelper::init();

  // set up camera
  float aspect = float(m_Info.m_Width) / m_Info.m_Height;
  camera.Initialize(aspect, glm::quarter_pi<float>(), 0.1f, 100.0f, float(m_Info.m_Width));
  camera.SetPosition(glm::vec3(-11.5f, 1.85f, -0.45f));
  camera.SetXRotation(0.0f);
  camera.SetYRotation(1.544f);

  // camera.SetPosition(glm::vec3(-1.25f, 1.31f, 0.2f));
   camera.SetPosition(glm::vec3(-5.25f, 4.31f, 0.2f));
   camera.SetXRotation(0.5f);

  // test shadows
   //camera.SetXRotation(0.0f);
   //camera.SetYRotation(1.544f);

   //camera.SetPosition(glm::vec3(-1.25f, 1.31f, 0.2f));

  // debugging the clustering bug
  /*
  {
    camera.SetPosition(glm::vec3(3.49f, 1.76f, 0.49f));
    camera.SetXRotation(-3.14f / 12.0f);
    camera.SetYRotation(1.5f * 3.14f);
  }
  */

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
        std::min<uint64_t>(AppSettings::MaxSpotLights, sceneModel.SpotLights().size());
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

    AppSettings::MaxLightClamp = static_cast<uint32_t>(numSpotLights);
  }

  // Load blue noise texture
  static const wchar_t* noiseTexPath = L"..\\Content\\Textures\\blueNoiseTex128.png"; 
  loadTexture(m_Dev, m_BlueNoiseTexture, noiseTexPath, false);

  // Init analytical sky
  skybox.Initialize();

  // Init post fx
  {
    m_PostFx.init();
  }

#pragma region Set up clustered rendering stuff
  // Spot light bounds and instance buffers
  {
    StructuredBufferInit sbInit;
    sbInit.Stride = sizeof(ClusterBounds);
    sbInit.NumElements = AppSettings::MaxSpotLights;
    sbInit.Dynamic = true;
    sbInit.CPUAccessible = true;
    spotLightBoundsBuffer.init(sbInit);

    sbInit.Stride = sizeof(uint32_t);
    spotLightInstanceBuffer.init(sbInit);
  }

  makeConeGeometry(
      NumConeSides, spotLightClusterVtxBuffer, spotLightClusterIdxBuffer, coneVertices);

#pragma endregion

  {
    DepthBufferInit dbInit;
    dbInit.Width = SunShadowMapSize;
    dbInit.Height = SunShadowMapSize;
    dbInit.Format = DXGI_FORMAT_D32_FLOAT;
    dbInit.MSAASamples = 1;
    dbInit.ArraySize = NumCascades;
    dbInit.InitialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    dbInit.Name = L"Sun Shadow Map";
    sunShadowMap.init(dbInit);
  }

  {
    DepthBufferInit dbInit;
    dbInit.Width = SpotLightShadowMapSize;
    dbInit.Height = SpotLightShadowMapSize;
    dbInit.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dbInit.MSAASamples = 1;
    dbInit.ArraySize = sceneModel.SpotLights().size();
    dbInit.InitialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    dbInit.Name = L"Spot Light Shadow Map";
    spotLightShadowMap.init(dbInit);
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

  // Init particles:
#if (ENABLE_PARTICLE_EXPERIMENTAL > 0)
  {
    glm::vec3 initPos = glm::vec3(1.20f, 0.42f, -1.38f);
    m_Particle.init(deferredTarget.format(), depthBuffer.DSVFormat, initPos);
  }
#endif

  const uint64_t numVisible = sceneModel.Meshes().size();
  for (uint64_t meshIdx = 0; meshIdx < numVisible; ++meshIdx)
  {
    Mesh& mesh = sceneModel.Meshes()[meshIdx];
    for (uint64_t partIdx = 0; partIdx < mesh.NumMeshParts(); ++partIdx)
    {
      const MeshPart& part = mesh.MeshParts()[partIdx];
      //m_GpuDrivenRenderer.addMesh(part.IndexCount);
      // Just in case dealing with mesh parts shenanigan
      assert(part.IndexCount == mesh.NumIndices());
    }
  }

  // Gpu Driven Renderer
  {
    // Initialize gpu driven renderer
    m_GpuDrivenRenderer.init(m_Dev, m_Info.m_Width, m_Info.m_Height);

    // Add meshes for gpu driven rendering
    m_GpuDrivenRenderer.addMeshes(sceneModel.Meshes());

    // Create resources
    m_GpuDrivenRenderer.createResources(m_Dev);
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

  // Depth only root signature
  {
    D3D12_ROOT_PARAMETER1 rootParameters[1] = {};

    // VSCBuffer
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    createRootSignature(m_Dev, &depthRootSignature, rootSignatureDesc);
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

    // ShadowCBuffer
    rootParameters[DeferredParams_ShadowCBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[DeferredParams_ShadowCBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[DeferredParams_ShadowCBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[DeferredParams_ShadowCBuffer].Descriptor.ShaderRegister = 1;
    rootParameters[DeferredParams_ShadowCBuffer].Descriptor.Flags =
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

    D3D12_STATIC_SAMPLER_DESC staticSamplers[4] = {};
    staticSamplers[0] =
        GetStaticSamplerState(SamplerState::Anisotropic, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[1] =
        GetStaticSamplerState(SamplerState::ShadowMapPCF, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[2] =
        GetStaticSamplerState(SamplerState::LinearClamp, 2, 0, D3D12_SHADER_VISIBILITY_ALL);
    staticSamplers[3] =
        GetStaticSamplerState(SamplerState::Linear, 3, 0, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = arrayCount32(staticSamplers);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    createRootSignature(m_Dev, &deferredRootSig, rootSignatureDesc);
    deferredRootSig->SetName(L"Deferred Root Sig");
  }

  // Clustering root signature
  {
    D3D12_DESCRIPTOR_RANGE1 uavRanges[1] = {};
    uavRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRanges[0].NumDescriptors = 1;
    uavRanges[0].BaseShaderRegister = 0;
    uavRanges[0].RegisterSpace = 0;
    uavRanges[0].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 rootParameters[NumClusterRootParams] = {};
    rootParameters[ClusterParams_StandardDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[ClusterParams_StandardDescriptors].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[ClusterParams_StandardDescriptors].DescriptorTable.pDescriptorRanges =
        StandardDescriptorRanges();
    rootParameters[ClusterParams_StandardDescriptors].DescriptorTable.NumDescriptorRanges =
        NumStandardDescriptorRanges;

    rootParameters[ClusterParams_UAVDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[ClusterParams_UAVDescriptors].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[ClusterParams_UAVDescriptors].DescriptorTable.pDescriptorRanges = uavRanges;
    rootParameters[ClusterParams_UAVDescriptors].DescriptorTable.NumDescriptorRanges =
        arrayCount32(uavRanges);

    rootParameters[ClusterParams_CBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[ClusterParams_CBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[ClusterParams_CBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[ClusterParams_CBuffer].Descriptor.ShaderRegister = 0;
    rootParameters[ClusterParams_CBuffer].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    rootParameters[ClusterParams_AppSettings].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[ClusterParams_AppSettings].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[ClusterParams_AppSettings].Descriptor.RegisterSpace = 0;
    rootParameters[ClusterParams_AppSettings].Descriptor.ShaderRegister =
        AppSettings::CBufferRegister;
    rootParameters[ClusterParams_AppSettings].Descriptor.Flags =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    createRootSignature(m_Dev, &clusterRS, rootSignatureDesc);
  }

  // Cluster visualization root signature
  {
    D3D12_ROOT_PARAMETER1 rootParameters[NumClusterVisRootParams] = {};

    // Standard SRV descriptors
    rootParameters[ClusterVisParams_StandardDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[ClusterVisParams_StandardDescriptors].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[ClusterVisParams_StandardDescriptors].DescriptorTable.pDescriptorRanges =
        StandardDescriptorRanges();
    rootParameters[ClusterVisParams_StandardDescriptors].DescriptorTable.NumDescriptorRanges =
        NumStandardDescriptorRanges;

    // CBuffer
    rootParameters[ClusterVisParams_CBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[ClusterVisParams_CBuffer].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[ClusterVisParams_CBuffer].Descriptor.RegisterSpace = 0;
    rootParameters[ClusterVisParams_CBuffer].Descriptor.ShaderRegister = 0;
    rootParameters[ClusterVisParams_CBuffer].Descriptor.Flags =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    // AppSettings
    rootParameters[ClusterVisParams_AppSettings].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[ClusterVisParams_AppSettings].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[ClusterVisParams_AppSettings].Descriptor.RegisterSpace = 0;
    rootParameters[ClusterVisParams_AppSettings].Descriptor.ShaderRegister =
        AppSettings::CBufferRegister;
    rootParameters[ClusterVisParams_AppSettings].Descriptor.Flags =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    CreateRootSignature(&clusterVisRootSignature, rootSignatureDesc);
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
}
void RenderManager::renderForward()
{
  // TODO:
  assert(false);
}
void RenderManager::renderDeferred()
{
#if 1
  // Gpu driven renderer
  GpuDrivenRenderer::RenderDesc desc = {
      //
  };
  m_GpuDrivenRenderer.render(m_CmdList, desc);
#endif

  // Draw to Gbuffers
#pragma region Gbuffer pass
  PIXBeginEvent(m_CmdList.GetInterfacePtr(), 0, "Render Gbuffers");

  {
    // Transition our G-Buffer targets to a writable state and sync on shadowmap
    D3D12_RESOURCE_BARRIER barriers[5] = {};

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

    barriers[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[4].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[4].Transition.pResource = sunShadowMap.getResource();
    barriers[4].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[4].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[4].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

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
      // to be tested
      assert(partIdx == 0);

      const MeshPart& part = mesh.MeshParts()[partIdx];
      if (part.MaterialIdx != currMaterial)
      {
        m_CmdList->SetGraphicsRoot32BitConstant(1, part.MaterialIdx, 0);
        currMaterial = part.MaterialIdx;
      }
      assert(part.IndexStart == 0); // just testing
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

  PIXEndEvent(m_CmdList.GetInterfacePtr()); // End Render Gbuffers
#pragma endregion

  //
  // Render fullscreen deferred pass!
  //
  PIXBeginEvent(m_CmdList.GetInterfacePtr(), 0, "Render Deferred");

  const uint32_t numComputeTilesX = alignUp<uint32_t>(uint32_t(deferredTarget.width()), 8) / 8;
  const uint32_t numComputeTilesY = alignUp<uint32_t>(uint32_t(deferredTarget.height()), 8) / 8;

  // prepare main render target as a UAV
  deferredTarget.transition(
      m_CmdList,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  m_CmdList->SetComputeRootSignature(deferredRootSig);
  m_CmdList->SetPipelineState(deferredPSO);

  BindStandardDescriptorTable(m_CmdList, DeferredParams_StandardDescriptors, CmdListMode::Compute);

  // Set constant buffers
  {
    DeferredConstants deferredConstants;
    deferredConstants.InvViewProj = glm::transpose(glm::inverse(camera.ViewProjectionMatrix()));
    deferredConstants.Projection = glm::transpose(camera.ProjectionMatrix());
    deferredConstants.RTSize =
        glm::vec2(float(deferredTarget.width()), float(deferredTarget.height()));
    deferredConstants.NumComputeTilesX = numComputeTilesX;
    deferredConstants.nearClip = camera.NearClip();
    deferredConstants.farClip = camera.FarClip();
    BindTempConstantBuffer(
        m_CmdList, deferredConstants, DeferredParams_DeferredCBuffer, CmdListMode::Compute);

    uint32_t skyTargetSRV = NullTexture2DSRV;
    uint32_t srvIndices[] = {
        //todo...meshRenderer.SunShadowMap().SRV(),
        spotLightShadowMap.getSrv(),
        materialTextureIndices.m_SrvIndex,
        spotLightClusterBuffer.SRV,
        materialIDTarget.srv(),
        uvTarget.srv(),
        depthBuffer.getSrv(),
        tangentFrameTarget.srv(),
        m_Fog.m_FinalVolume.getSRV(),
        m_BlueNoiseTexture.SRV,
        skyTargetSRV
    };
    BindTempConstantBuffer(m_CmdList, srvIndices, DeferredParams_SRVIndices, CmdListMode::Compute);
  }

  {
    ShadingConstants shadingConstants;
    shadingConstants.SunDirectionWS = AppSettings::SunDirection;
    shadingConstants.SunIrradiance = skyCache.SunIrradiance;
    shadingConstants.CosSunAngularRadius = std::cos(degToRad(AppSettings::SunSize));
    shadingConstants.SinSunAngularRadius = std::sin(degToRad(AppSettings::SunSize));
    shadingConstants.CameraPosWS = camera.Position();
    shadingConstants.NumXTiles = uint32_t(AppSettings::NumXTiles);
    shadingConstants.NumXYTiles = uint32_t(AppSettings::NumXTiles * AppSettings::NumYTiles);
    shadingConstants.NearClip = camera.NearClip();
    shadingConstants.FarClip = camera.FarClip();
    shadingConstants.NumFroxelGridSlices = m_Fog.m_Dimensions.z;
    shadingConstants.SkySH = skyCache.sh;

    BindTempConstantBuffer(
        m_CmdList, shadingConstants, DeferredParams_PSCBuffer, CmdListMode::Compute);
  }

  spotLightBuffer.setAsComputeRootParameter(m_CmdList, DeferredParams_LightCBuffer);

  AppSettings::bindCBufferCompute(m_CmdList, DeferredParams_AppSettings);

  D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {deferredTarget.m_UAV};
  BindTempDescriptorTable(
      m_CmdList, uavs, arrayCount(uavs), DeferredParams_UAVDescriptors, CmdListMode::Compute);

  m_CmdList->Dispatch(numComputeTilesX, numComputeTilesY, 1);

  if (AppSettings::EnableSky)
  {
    // Render the sky in the empty areas
    deferredTarget.transition(
        m_CmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[1] = {deferredTarget.m_RTV};
    m_CmdList->OMSetRenderTargets(1, rtvHandles, false, &depthBuffer.DSV);

    const bool enableSun = true;
    skybox.RenderSky(
        m_CmdList,
        glm::transpose(camera.ViewMatrix()),
        glm::transpose(camera.ProjectionMatrix()),
        skyCache,
        enableSun);

    deferredTarget.transition(
        m_CmdList, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }

  // transition back shadow map
  {
    D3D12_RESOURCE_BARRIER barriers[2] = {};

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = sunShadowMap.getResource();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[1].Transition.pResource = spotLightShadowMap.getResource();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_CmdList->ResourceBarrier(arrayCount32(barriers), barriers);
  }

  PIXEndEvent(m_CmdList.GetInterfacePtr()); // End Render Deferred
}
//---------------------------------------------------------------------------//
void RenderManager::renderParticles()
{
#if (ENABLE_PARTICLE_EXPERIMENTAL > 0)
  deferredTarget.transition(
      m_CmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_CmdList->OMSetRenderTargets(1, &deferredTarget.m_RTV, false, &depthBuffer.DSV);

  const glm::mat4 view = glm::transpose(camera.ViewMatrix());
  const glm::mat4 proj = glm::transpose(camera.ProjectionMatrix()); // Already transposed
  const glm::mat4 viewproj = glm::transpose(camera.ViewProjectionMatrix());
  const glm::mat4 wvp = glm::identity<glm::mat4>() * glm::transpose(camera.ViewProjectionMatrix());
  const glm::vec2 viewportSize = glm::vec2(m_Info.m_Width, m_Info.m_Height);
  m_Particle.render(
      m_CmdList,
      view,
      proj,
      viewproj,
      camera.Forward(),
      camera.Orientation(),
      glm::vec4(camera.Up(), 0.0f),
      glm::vec4(camera.Right(), 0.0f),
      m_Timer.m_ElapsedSecondsF);

  deferredTarget.transition(
      m_CmdList, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
#endif
}
//---------------------------------------------------------------------------//
// Renders all meshes using depth-only rendering
void RenderManager::renderDepth(
    ID3D12GraphicsCommandList* p_CmdList,
    const CameraBase& p_Camera,
    ID3D12PipelineState* p_PSO,
    uint64_t p_NumVisible)
{
  // TODO:
  // Frustum culling
  p_NumVisible = sceneModel.Meshes().size();

  p_CmdList->SetGraphicsRootSignature(depthRootSignature);
  p_CmdList->SetPipelineState(p_PSO);
  p_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  glm::mat4 world = glm::identity<glm::mat4>();

  // Set constant buffers
  MeshVSConstants vsConstants;
  vsConstants.World = world;
  vsConstants.View = glm::transpose(p_Camera.ViewMatrix());
  vsConstants.WorldViewProjection = world * glm::transpose(p_Camera.ViewProjectionMatrix());
  BindTempConstantBuffer(p_CmdList, vsConstants, 0, CmdListMode::Graphics);

  // Bind vertices and indices
  D3D12_VERTEX_BUFFER_VIEW vbView = sceneModel.VertexBuffer().vbView();
  D3D12_INDEX_BUFFER_VIEW ibView = sceneModel.IndexBuffer().IBView();
  p_CmdList->IASetVertexBuffers(0, 1, &vbView);
  p_CmdList->IASetIndexBuffer(&ibView);

  // Draw all meshes
  for (uint64_t i = 0; i < p_NumVisible; ++i)
  {
    uint64_t meshIdx = i;
    const Mesh& mesh = sceneModel.Meshes()[meshIdx];

    // Draw the whole mesh
    p_CmdList->DrawIndexedInstanced(
        mesh.NumIndices(), 1, mesh.IndexOffset(), mesh.VertexOffset(), 0);
  }
}
//---------------------------------------------------------------------------//
// Renders all meshes using depth-only rendering for spotlight shadowmap
void RenderManager::renderSpotLightShadowDepth(
    ID3D12GraphicsCommandList* p_CmdList, const CameraBase& p_Camera)
{
  // TODO:
  // Frustum culling
  const uint64_t numVisible = sceneModel.Meshes().size();
  renderDepth(p_CmdList, p_Camera, spotLightShadowPSO, numVisible);
}
//---------------------------------------------------------------------------//
// Render shadows for all spot lights
void RenderManager::renderSpotLightShadowMap(
    ID3D12GraphicsCommandList* p_CmdList, const CameraBase& p_Camera)
{
  PIXBeginEvent(p_CmdList, 0, "Spot Light Shadow Map Rendering");

  const std::vector<ModelSpotLight>& spotLights = sceneModel.SpotLights();
  const uint64_t numSpotLights = std::min<uint64_t>(spotLights.size(), AppSettings::MaxLightClamp);
  for (uint64_t i = 0; i < numSpotLights; ++i)
  {
    PIXBeginEvent(p_CmdList, 0, "Rendering Spot Light Shadow  %u", i);

    // Set the viewport
    SetViewport(p_CmdList, SpotLightShadowMapSize, SpotLightShadowMapSize);

    // Set the shadow map as the depth target
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = spotLightShadowMap.ArrayDSVs[i];
    p_CmdList->OMSetRenderTargets(0, nullptr, false, &dsv);
    p_CmdList->ClearDepthStencilView(
        dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    const ModelSpotLight& light = spotLights[i];

    // Draw the mesh with depth only, using the new shadow camera
    PerspectiveCamera shadowCamera;
    shadowCamera.Initialize(
        1.0f,
        light.AngularAttenuation.y,
        AppSettings::SpotShadowNearClip,
        AppSettings::SpotLightRange,
        float(SpotLightShadowMapSize));
    shadowCamera.SetPosition(light.Position);
    shadowCamera.SetOrientation(light.Orientation);
    renderSpotLightShadowDepth(p_CmdList, shadowCamera);

    // TODO: should the result get transposed?
    glm::mat4 shadowMatrix =
        shadowCamera.ViewProjectionMatrix() * ShadowHelper::ShadowScaleOffsetMatrix;
    spotLightShadowMatrices[i] = shadowMatrix;

    PIXEndEvent(p_CmdList); // End spotlight shadow
  }

  PIXEndEvent(p_CmdList); // End spotlight shadowmap
}
//---------------------------------------------------------------------------//
// Renders all meshes using depth-only rendering for a sun shadow map
void RenderManager::renderSunShadowDepth(
    ID3D12GraphicsCommandList* cmdList, const OrthographicCamera& camera)
{
  // TODO: Frustum culls meshes for an orthographic projection

  const uint64_t numVisible = sceneModel.Meshes().size();
  renderDepth(cmdList, camera, sunShadowPSO, numVisible);
}
//---------------------------------------------------------------------------//
void RenderManager::renderSunShadowMap (
  ID3D12GraphicsCommandList* p_CmdList, const CameraBase& p_Camera)
{
  PIXBeginEvent(p_CmdList, 0, "Sun Shadow Map Rendering");

  OrthographicCamera cascadeCameras[NumCascades];
  ShadowHelper::prepareCascades(
      AppSettings::SunDirection,
      SunShadowMapSize,
      true,
      camera,
      sunShadowConstants.Base,
      cascadeCameras);

  // Render the meshes to each cascade
  for (uint64_t cascadeIdx = 0; cascadeIdx < NumCascades; ++cascadeIdx)
  {
    PIXBeginEvent(p_CmdList, 0, "Rendering Shadow Map Cascade %u", cascadeIdx);

    // Set the viewport
    SetViewport(p_CmdList, SunShadowMapSize, SunShadowMapSize);

    // Set the shadow map as the depth target
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = sunShadowMap.ArrayDSVs[cascadeIdx];
    p_CmdList->OMSetRenderTargets(0, nullptr, false, &dsv);
    p_CmdList->ClearDepthStencilView(
        dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Draw the mesh with depth only, using the new shadow camera
    renderSunShadowDepth(p_CmdList, cascadeCameras[cascadeIdx]);

    PIXEndEvent(p_CmdList); // End cascade shadowmap
  }

  PIXEndEvent(p_CmdList); // End sun shadowmap
}
//---------------------------------------------------------------------------//

void RenderManager::populateCommandList()
{
  SetDescriptorHeaps(m_CmdList);

  renderClusters();

  if (AppSettings::EnableSky)
    renderSunShadowMap(m_CmdList, camera);

  renderSpotLightShadowMap(m_CmdList, camera);

  // Volumetric fog
  {
    // Volumetrics sample shadow map
    {
      D3D12_RESOURCE_BARRIER barriers[1] = {};

      barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barriers[0].Transition.pResource = spotLightShadowMap.getResource();
      barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

      m_CmdList->ResourceBarrier(arrayCount32(barriers), barriers);
    }

    VolumetricFog::RenderDesc desc =
    {
        .ClusterBufferSrv = spotLightClusterBuffer.SRV,
        .DepthBufferSrv = depthBuffer.getSrv(),
        .SpotLightShadowSrv = spotLightShadowMap.getSrv(),

        .UVMapSrv = uvTarget.srv(),
        .TangentFrameSrv = tangentFrameTarget.srv(),
        .MaterialIdMapSrv = materialIDTarget.srv(),
        .NoiseTexSrv = m_BlueNoiseTexture.SRV,

        .Near = camera.NearClip(),
        .Far = camera.FarClip(),
        .ScreenWidth = static_cast<float>(m_Info.m_Width),
        .ScreenHeight = static_cast<float>(m_Info.m_Height),
        .CurrentFrame = g_CurrentCPUFrame,

        .Camera = camera,
        .PrevViewProj = prevViewProj,
        .LightsBuffer = spotLightBuffer,

        .HaltonXY = jitterOffsetXY
    };
    m_Fog.render(m_CmdList, desc);
  }

  renderDeferred();

  renderParticles();

  // prepare main render target for post processing
  deferredTarget.transition(
      m_CmdList,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  // Composite motion vectors
  {
    prevJitterXY = jitterXY;
    jitterXY = glm::vec2(jitterOffsetXY.x / m_Info.m_Width, jitterOffsetXY.y / m_Info.m_Height);
    MotionVector::RenderDesc desc =
    { 
      .DepthMapIdx = depthBuffer.getSrv(),
      .JitterXY = jitterXY,
      .PreviousJitterXY = prevJitterXY,
      .Camera = camera,
      .PrevViewProj = prevViewProj
    };
    m_MotionVectors.render(m_CmdList, desc);
  }

    // Toggle TAA pass
  if (AppSettings::EnableTAA)
  {
    m_TAA.render(m_CmdList, camera, deferredTarget.srv(), m_MotionVectors.m_uavTarget.srv());
    m_PostFx.render(
        m_CmdList,
        m_TAA.m_uavTargets[TAARenderPass::ms_CurrOutputTextureIndex],
        m_RenderTargets[m_FrameIndex]);
  }
  else
  {
    m_PostFx.render(m_CmdList, deferredTarget, m_RenderTargets[m_FrameIndex]);
  }

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

  // init sampled spectrum
  SampledSpectrum::Init();

  // Init volumetric fog
  m_Fog.init(m_Dev);

  // Init motion vectors
  m_MotionVectors.init(m_Dev, m_Info.m_Width, m_Info.m_Height);

  // Init test compute
  m_TestCompute.init(m_Dev, m_Info.m_Width, m_Info.m_Height);

  // Init taa
  m_TAA.init(m_Dev, m_Info.m_Width, m_Info.m_Height);

  // Init imgui
  ImGuiHelper::init(g_WinHandle, m_Dev);

  // General appsettings
  AppSettings::init();

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
  spotLightBoundsBuffer.deinit();
  spotLightClusterBuffer.deinit();
  spotLightInstanceBuffer.deinit();

  spotLightClusterVtxBuffer.deinit();
  spotLightClusterIdxBuffer.deinit();

  clusterRS->Release();
  clusterVisRootSignature->Release();

  depthRootSignature->Release();
  depthPSO->Release();
  spotLightShadowPSO->Release();
  sunShadowPSO->Release();

  sunShadowMap.deinit();
  spotLightShadowMap.deinit();

  clusterFrontFacePSO->Release();
  clusterBackFacePSO->Release();
  clusterIntersectingPSO->Release();
  clusterVisPSO->Release();

  skybox.DestroyPSOs();

  // Release swc backbuffers
  for (UINT n = 0; n < FRAME_COUNT; n++)
  {
    m_RenderTargets[n].m_Texture.Resource->Release();
    RTVDescriptorHeap.FreePersistent(m_RenderTargets[n].m_RTV);
  }

#if (ENABLE_PARTICLE_EXPERIMENTAL > 0)
  m_Particle.deinit();
#endif

  ShadowHelper::deinit();

  skybox.Shutdown();
  skyCache.Shutdown();

  m_PostFx.deinit();
  AppSettings::deinit();
  ImGuiHelper::deinit();

  m_Fog.deinit();
  m_TestCompute.deinit(true);
  m_MotionVectors.deinit(true);
  m_TAA.deinit(true);

  m_BlueNoiseTexture.Shutdown();

  m_GpuDrivenRenderer.deinit();

  // Shutdown uploads and other helpers
  shutdownHelpers();
  shutdownUpload();
}
//---------------------------------------------------------------------------//
void RenderManager::onUpdate()
{
  if (pauseRendering)
    return;

  m_Timer.update();

  // Wait for the previous Present to complete.
  WaitForSingleObjectEx(m_SwapChainEvent, 100, FALSE);

  // Jittering update
  {
    static uint32_t jitterIndex = 0;
    static JitterType::Enum jitterType = JitterType::Halton; // TODO: imgui control
    static uint32_t jitterPeriod = 2; // TODO:
    glm::vec2 jitterValues = glm::vec2{0.0f, 0.0f};

    switch (jitterType)
    {
    case JitterType::Halton:
      jitterValues = _halton23Sequence(jitterIndex);
      break;

    case JitterType::R2:
      jitterValues = _martinRobertsR2Sequence(jitterIndex);
      break;

    case JitterType::InterleavedGradients:
      jitterValues = _interleavedGradientSequence(jitterIndex);
      break;

    case JitterType::Hammersley:
      jitterValues = _hammersleySequence(jitterIndex, jitterPeriod);
      break;
    }
    jitterIndex = (jitterIndex + 1) % jitterPeriod;

    jitterOffsetXY = glm::vec2{jitterValues.x * 2 - 1.0f, jitterValues.y * 2 - 1.0f};
    static float jitterScale = 1.f; // TODO: imgui control

    jitterOffsetXY.x *= jitterScale;
    jitterOffsetXY.y *= jitterScale;
  }

  // Cache previous view projection before updating camera
  prevViewProj = glm::transpose(camera.ViewMatrix() * camera.ProjectionMatrix());

  // Update Camera
  // Rotate the camera with the mouse
  {
    // static float CamRotSpeed = 0.0005f * m_Timer.m_DeltaSecondsF;
    static float CamRotSpeed = 0.0023f;
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
    float CamMoveSpeed = AppSettings::CameraSpeed * m_Timer.m_DeltaSecondsF;
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

    // TODO: move this to proper place
    AppSettings::CameraPosition = camera.Position();
  }

  // Apply camera jitter if TAA is on
  if (AppSettings::EnableTAA)
  {
    camera.ApplyJittering(jitterOffsetXY.x / float(m_Info.m_Width), jitterOffsetXY.y / float(m_Info.m_Height));
  }

  // update light bound buffer for clustering
  updateLights();

  // update sky cache
  skyCache.Init(
      AppSettings::SunDirection,
      AppSettings::SunSize,
      AppSettings::GroundAlbedo,
      AppSettings::Turbidity,
      true);

  // Update light uniforms
  {
    const void* srcData[2] = {spotLights.data(), spotLightShadowMatrices};
    uint64_t sizes[2] = {
        spotLights.size() * sizeof(SpotLight), spotLights.size() * sizeof(glm::mat4)};
    uint64_t offsets[2] = {0, AppSettings::MaxSpotLights * sizeof(SpotLight)};
    spotLightBuffer.multiUpdateData(srcData, sizes, offsets, arrayCount(srcData));
  }

  // Update gpu mesh data
  m_GpuDrivenRenderer.uploadGpuData();

  // Update application settings
  AppSettings::CurrentFrame = static_cast<uint32_t>(g_CurrentCPUFrame);
  AppSettings::updateCBuffer();

  // Imgui begin frame:
  // TODO: add correct delta time
  ImGuiHelper::beginFrame(m_Info.m_Width, m_Info.m_Height, 1.0f / 60.0f, m_Dev);
}
//---------------------------------------------------------------------------//
void RenderManager::onRender()
{
  if (pauseRendering)
    return;

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

      // Cluster visualizer
      renderClusterVisualizer();

      // Imgui rendering (NOTE: here descriptor heap changes!):
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
  if (pauseRendering)
    return;

  RECT clientRect;
  ::GetClientRect(g_WinHandle, &clientRect);
  if (clientRect.right > 0.0f && clientRect.bottom > 0.0f)
  {
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
}
//---------------------------------------------------------------------------//
void RenderManager::onCodeChange() {}
//---------------------------------------------------------------------------//
void RenderManager::onShaderChange()
{
  pauseRendering = true;

  OutputDebugStringA("[RenderManager] Starting shader reload...\n");
  Sleep(1000);
  waitForRenderContext();

  if (compileShaders())
  {
    createPSOs();

    m_PostFx.deinit();
    m_PostFx.init();

    m_Fog.deinit();
    m_Fog.init(m_Dev);

    m_MotionVectors.deinit(false);
    m_MotionVectors.init(m_Dev, m_Info.m_Width, m_Info.m_Height);

    m_TestCompute.deinit(false);
    m_TestCompute.init(m_Dev, m_Info.m_Width, m_Info.m_Height);

    m_TAA.deinit(false);
    m_TAA.init(m_Dev, m_Info.m_Width, m_Info.m_Height);

#if (ENABLE_PARTICLE_EXPERIMENTAL > 0)
    m_Particle.createPSOs();
#endif

    pauseRendering = false;
    OutputDebugStringA("[RenderManager] Shaders load completed.\n");
    
    return;
  }
  else
  {
    OutputDebugStringA("[RenderManager] Failed to reload the shaders\n");
  }

  pauseRendering = false;
}
//---------------------------------------------------------------------------//
void RenderManager::updateLights()
{
  const uint64_t numSpotLights = std::min<uint64_t>(spotLights.size(), AppSettings::MaxLightClamp);

  // An additional scale factor that is needed to make sure that our polygonal bounding cone fully
  // encloses the actual cone representing the light's area of influence
  const float inRadius = std::cos(Pi / NumConeSides);
  float scaleCorrection = 1.0f / inRadius;

  // NOTE(OM): disabling scale correction for a visual bug 
  /*
    the bug can be reproduced by putting camera at the following position:

    camera.SetPosition(glm::vec3(3.49f, 1.76f, 0.49f));
    camera.SetXRotation(-3.14f / 12.0f);
    camera.SetYRotation(1.5f * 3.14f);
  */
  scaleCorrection = 1.0f;

  const glm::mat4 viewMatrix = camera.ViewMatrix();
  const float nearClip = camera.NearClip();
  const float farClip = camera.FarClip();
  const float zRange = farClip - nearClip;
  const glm::vec3 cameraPos = camera.Position();
  const uint64_t numConeVerts = coneVertices.size();

  // Come up with a bounding sphere that surrounds the near clipping plane. We'll test this sphere
  // for intersection with the spotlight's bounding cone and use that to over-estimate if the
  // bounding geometry will end up getting clipped by the camera's near clipping plane
  glm::vec3 nearClipCenter = cameraPos + nearClip * camera.Forward();
  glm::mat4 invViewProjection = glm::inverse(camera.ViewProjectionMatrix());
  glm::vec3 nearTopRight = _transformVec3Mat4(glm::vec3(1.0f, 1.0f, 0.0f), invViewProjection);
  float nearClipRadius = glm::length(nearTopRight - nearClipCenter);

  ClusterBounds* boundsData = spotLightBoundsBuffer.map<ClusterBounds>();
  bool intersectsCamera[AppSettings::MaxSpotLights] = {};

  // Update the light bounds buffer
  for (uint64_t spotLightIdx = 0; spotLightIdx < numSpotLights; ++spotLightIdx)
  {
    const SpotLight& spotLight = spotLights[spotLightIdx];
    const ModelSpotLight& srcSpotLight = sceneModel.SpotLights()[spotLightIdx];
    ClusterBounds bounds;
    bounds.Position = spotLight.Position;
    bounds.Orientation = srcSpotLight.Orientation;
    bounds.Scale.x = bounds.Scale.y =
        std::tan(srcSpotLight.AngularAttenuation.y / 2.0f) * spotLight.Range * scaleCorrection;
    bounds.Scale.z = spotLight.Range;

    // Compute conservative Z bounds for the light based on vertices of the bounding geometry
    constexpr float FloatMax = std::numeric_limits<float>::max();
    float minZ = FloatMax;
    float maxZ = -FloatMax;
    for (uint64_t i = 0; i < numConeVerts; ++i)
    {
      glm::vec3 coneVert = coneVertices[i] * bounds.Scale;
      // coneVert = coneVert * bounds.Orientation.ToMat3(); // glm so mat * vec
      coneVert = bounds.Orientation.ToMat3() * coneVert; // glm so mat * vec
      coneVert += bounds.Position;

      float vertZ = _transformVec3Mat4(coneVert, viewMatrix).z;
      minZ = std::min(minZ, vertZ);
      maxZ = std::max(maxZ, vertZ);
    }

    minZ = saturate((minZ - nearClip) / zRange);
    maxZ = saturate((maxZ - nearClip) / zRange);

    bounds.ZBounds.x = uint32_t(minZ * AppSettings::NumZTiles);
    bounds.ZBounds.y =
        std::min(uint32_t(maxZ * AppSettings::NumZTiles), uint32_t(AppSettings::NumZTiles - 1));

    // Estimate if the light's bounding geometry intersects with the camera's near clip plane
    boundsData[spotLightIdx] = bounds;
    intersectsCamera[spotLightIdx] = _sphereConeIntersection(
        spotLight.Position,
        srcSpotLight.Direction,
        spotLight.Range,
        srcSpotLight.AngularAttenuation.y,
        nearClipCenter,
        nearClipRadius);

    // there is a random cluster flickering bug with spotlight #17
    //if (spotLightIdx == 17)
    //  intersectsCamera[spotLightIdx] = true;

    spotLights[spotLightIdx].Intensity = srcSpotLight.Intensity * SpotLightIntensityFactor;

    //if (spotLightIdx == 17)
    //  {
    //    if (intersectsCamera[spotLightIdx] == true)
    //    {
    //        char msg[256]{};
    //        sprintf_s<256>(msg, "spotlight %d intersected\n", uint32_t(spotLightIdx));
    //        OutputDebugStringA(msg);
    //    }
    //    if (intersectsCamera[spotLightIdx] == false)
    //    {
    //      char msg[256]{};
    //      sprintf_s<256>(msg, "spotlight %d not intersected\n", uint32_t(spotLightIdx));
    //      OutputDebugStringA(msg);
    //    }
    //}


    //if (spotLightIdx == 17)
    //  spotLights[spotLightIdx].Intensity = glm::vec3(0.0f);
  }

  numIntersectingSpotLights = 0;
  uint32_t* instanceData = spotLightInstanceBuffer.map<uint32_t>();

  for (uint64_t spotLightIdx = 0; spotLightIdx < numSpotLights; ++spotLightIdx)
    if (intersectsCamera[spotLightIdx])
    {
      instanceData[numIntersectingSpotLights++] = uint32_t(spotLightIdx);

      char msg[256]{};
      sprintf_s<256>(msg, "active index = %d\n", uint32_t(spotLightIdx));
      //OutputDebugStringA(msg);
    }

  uint64_t offset = numIntersectingSpotLights;
  for (uint64_t spotLightIdx = 0; spotLightIdx < numSpotLights; ++spotLightIdx)
    if (intersectsCamera[spotLightIdx] == false)
      instanceData[offset++] = uint32_t(spotLightIdx);
}
//---------------------------------------------------------------------------//
void RenderManager::renderClusters()
{
  PIXBeginEvent(m_CmdList.GetInterfacePtr(), 0, "Cluster Update");

  spotLightClusterBuffer.makeWritable(m_CmdList);

  // Clear spot light clusters
  {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptrs[1] = {spotLightClusterBuffer.UAV};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
        TempDescriptorTable(cpuDescriptrs, arrayCount32(cpuDescriptrs));

    uint32_t values[4] = {};
    m_CmdList->ClearUnorderedAccessViewUint(
        gpuHandle,
        cpuDescriptrs[0],
        spotLightClusterBuffer.InternalBuffer.m_Resource,
        values,
        0,
        nullptr);
  }

  ClusterConstants clusterConstants = {};
  clusterConstants.ViewProjection = glm::transpose(camera.ViewProjectionMatrix());
  clusterConstants.InvProjection = glm::transpose(glm::inverse(camera.ProjectionMatrix()));
  clusterConstants.NearClip = camera.NearClip();
  clusterConstants.FarClip = camera.FarClip();
  clusterConstants.InvClipRange = 1.0f / (camera.FarClip() - camera.NearClip());
  clusterConstants.NumXTiles = uint32_t(AppSettings::NumXTiles);
  clusterConstants.NumYTiles = uint32_t(AppSettings::NumYTiles);
  clusterConstants.NumXYTiles = uint32_t(AppSettings::NumXTiles * AppSettings::NumYTiles);
  clusterConstants.InstanceOffset = 0;
  clusterConstants.NumLights =
      std::min<uint32_t>(uint32_t(spotLights.size()), uint32_t(AppSettings::MaxLightClamp));
  clusterConstants.NumDecals = -1; // TODO: Decals

  m_CmdList->OMSetRenderTargets(0, nullptr, false, nullptr);

  SetViewport(m_CmdList, AppSettings::NumXTiles, AppSettings::NumYTiles);
  m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  m_CmdList->SetGraphicsRootSignature(clusterRS);

  BindStandardDescriptorTable(m_CmdList, ClusterParams_StandardDescriptors, CmdListMode::Graphics);

  if (AppSettings::RenderLights)
  {
    // Update spotlight clusters
    spotLightClusterBuffer.uavBarrier(m_CmdList);

    D3D12_INDEX_BUFFER_VIEW ibView = spotLightClusterIdxBuffer.IBView();
    m_CmdList->IASetIndexBuffer(&ibView);

    clusterConstants.ElementsPerCluster = uint32_t(AppSettings::SpotLightElementsPerCluster);
    clusterConstants.InstanceOffset = 0;
    clusterConstants.BoundsBufferIdx = spotLightBoundsBuffer.m_SrvIndex;
    clusterConstants.VertexBufferIdx = spotLightClusterVtxBuffer.m_SrvIndex;
    clusterConstants.InstanceBufferIdx = spotLightInstanceBuffer.m_SrvIndex;
    BindTempConstantBuffer(
        m_CmdList, clusterConstants, ClusterParams_CBuffer, CmdListMode::Graphics);

    AppSettings::bindCBufferGfx(m_CmdList, ClusterParams_AppSettings);

    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {spotLightClusterBuffer.UAV};
    BindTempDescriptorTable(
        m_CmdList, uavs, arrayCount32(uavs), ClusterParams_UAVDescriptors, CmdListMode::Graphics);

    const uint64_t numLightsToRender =
        std::min<uint64_t>(spotLights.size(), AppSettings::MaxLightClamp);
    assert(numIntersectingSpotLights <= numLightsToRender);
    const uint64_t numNonIntersecting = numLightsToRender - numIntersectingSpotLights;

    // Render back faces for spotlights that intersect with the camera
    m_CmdList->SetPipelineState(clusterIntersectingPSO);

    m_CmdList->DrawIndexedInstanced(
        uint32_t(spotLightClusterIdxBuffer.NumElements),
        uint32_t(numIntersectingSpotLights),
        0,
        0,
        0);

    // Now for all other lights, render the back faces followed by the front faces
    m_CmdList->SetPipelineState(clusterBackFacePSO);

    clusterConstants.InstanceOffset = uint32_t(numIntersectingSpotLights);
    BindTempConstantBuffer(
        m_CmdList, clusterConstants, ClusterParams_CBuffer, CmdListMode::Graphics);

    m_CmdList->DrawIndexedInstanced(
        uint32_t(spotLightClusterIdxBuffer.NumElements), uint32_t(numNonIntersecting), 0, 0, 0);

    spotLightClusterBuffer.uavBarrier(m_CmdList);

    m_CmdList->SetPipelineState(clusterFrontFacePSO);

    m_CmdList->DrawIndexedInstanced(
        uint32_t(spotLightClusterIdxBuffer.NumElements), uint32_t(numNonIntersecting), 0, 0, 0);
  }

  // Sync back cluster buffer to be read
  spotLightClusterBuffer.makeReadable(m_CmdList);

  PIXEndEvent(m_CmdList.GetInterfacePtr()); // End Cluster Update
}
//---------------------------------------------------------------------------//
// Renders the 2D "overhead" visualizer that shows per-cluster light counts
void RenderManager::renderClusterVisualizer()
{
  if (false == AppSettings::ShowClusterVisualizer)
    return;

  PIXBeginEvent(m_CmdList.GetInterfacePtr(), 0, "Cluster Visualizer");

  glm::vec2 displaySize = glm::vec2(float(m_Info.m_Width), float(m_Info.m_Height));
  glm::vec2 drawSize = displaySize * 0.375f;
  glm::vec2 drawPos = displaySize * (0.5f + (0.5f - 0.375f) / 2.0f);

  D3D12_VIEWPORT viewport = {};
  viewport.Width = drawSize.x;
  viewport.Height = drawSize.y;
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  viewport.TopLeftX = drawPos.x;
  viewport.TopLeftY = drawPos.y;

  D3D12_RECT scissorRect = {};
  scissorRect.left = 0;
  scissorRect.top = 0;
  scissorRect.right = uint32_t(m_Info.m_Width);
  scissorRect.bottom = uint32_t(m_Info.m_Height);

  m_CmdList->RSSetViewports(1, &viewport);
  m_CmdList->RSSetScissorRects(1, &scissorRect);

  m_CmdList->SetGraphicsRootSignature(clusterVisRootSignature);
  m_CmdList->SetPipelineState(clusterVisPSO);

  BindStandardDescriptorTable(
      m_CmdList, ClusterVisParams_StandardDescriptors, CmdListMode::Graphics);

  // no need to transpose proj mat here as we use it temporarily here in a glm style vector-mat
  // multiplication!
  glm::mat4 invProjection = glm::inverse(camera.ProjectionMatrix());
  glm::vec3 farTopRight = _transformVec3Mat4(glm::vec3(1.0f, 1.0f, 1.0f), invProjection);
  glm::vec3 farBottomLeft = _transformVec3Mat4(glm::vec3(-1.0f, -1.0f, 1.0f), invProjection);

  ClusterVisConstants clusterVisConstants;
  clusterVisConstants.Projection = glm::transpose(camera.ProjectionMatrix());
  clusterVisConstants.ViewMin = glm::vec3(farBottomLeft.x, farBottomLeft.y, camera.NearClip());
  clusterVisConstants.NearClip = camera.NearClip();
  clusterVisConstants.ViewMax = glm::vec3(farTopRight.x, farTopRight.y, camera.FarClip());
  clusterVisConstants.InvClipRange = 1.0f / (camera.FarClip() - camera.NearClip());
  clusterVisConstants.DisplaySize = displaySize;
  clusterVisConstants.NumXTiles = uint32_t(AppSettings::NumXTiles);
  clusterVisConstants.NumXYTiles = uint32_t(AppSettings::NumXTiles * AppSettings::NumYTiles);
  clusterVisConstants.DecalClusterBufferIdx = -1; // TODO: Decals
  clusterVisConstants.SpotLightClusterBufferIdx = spotLightClusterBuffer.SRV;
  BindTempConstantBuffer(
      m_CmdList, clusterVisConstants, ClusterVisParams_CBuffer, CmdListMode::Graphics);

  AppSettings::bindCBufferGfx(m_CmdList, ClusterVisParams_AppSettings);

  m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_CmdList->IASetIndexBuffer(nullptr);
  m_CmdList->IASetVertexBuffers(0, 0, nullptr);

  m_CmdList->DrawInstanced(3, 1, 0, 0);

  PIXEndEvent(m_CmdList.GetInterfacePtr()); // End Cluster Visualizer
}
//---------------------------------------------------------------------------//
