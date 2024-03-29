#pragma once

#include <Utility.hpp>
#include <Camera.hpp>
#include <Timer.hpp>
#include "d3dx12.h"
#include "D3D12Wrapper.hpp"
#include "Model.hpp"
#include "PostProcessor.hpp"
#include "SimpleParticle.hpp"
#include "AppSettings.hpp"
#include "VolumetricFog.hpp"
#include "TestPass.hpp"
#include "TAA.hpp"
#include "MotionVector.hpp"

#define FRAME_COUNT 2
#define THREAD_COUNT 1

//---------------------------------------------------------------------------//
// General Renderer Settings:
//---------------------------------------------------------------------------//
struct RendererSettings
{
  // Viewport dimensions
  UINT m_Width;
  UINT m_Height;
  float m_AspectRatio;

  // Adapter info
  bool m_UseWarpDevice;

  // Root assets path
  std::wstring m_AssetsPath;

  // Window title
  std::wstring m_Title;

  // State info
  bool m_IsInitialized;

  // Additional data goes here:
  //
};
//---------------------------------------------------------------------------//
// General Structs:
//---------------------------------------------------------------------------//
struct SpotLight
{
  glm::vec3 Position;
  float AngularAttenuationX;

  glm::vec3 Direction;
  float AngularAttenuationY;

  glm::vec3 Intensity;
  float Range;
};
struct ShadingConstants
{
  Float4Align glm::vec3 CameraPosWS;

  uint32_t NumXTiles = 0;
  uint32_t NumXYTiles = 0;
  float NearClip = 0.0f;
  float FarClip = 0.0f;
  uint32_t NumFroxelGridSlices;
};
static_assert(sizeof(ShadingConstants) == 32);
//---------------------------------------------------------------------------//
// RenderManager Manager:
//---------------------------------------------------------------------------//
struct RenderManager
{
  ID3D12DevicePtr m_Dev;

  RenderManager()
  {
    // Do nothing
  }
  ~RenderManager()
  {
    // dtor needed to release ComPtrs
  }

  RendererSettings m_Info;

  //---------------------------------------------------------------------------//
  void OnInit(UINT p_Width, UINT p_Height, std::wstring p_Name);

  void onLoad();
  void onDestroy();
  void onUpdate();
  void onRender();
  void onKeyDown(UINT8);
  void onKeyUp(UINT8);
  void onResize();
  void onCodeChange();
  void onShaderChange();

  //---------------------------------------------------------------------------//
  void parseCmdArgs(_In_reads_(p_Argc) WCHAR* p_Argv[], int p_Argc)
  {
    for (int i = 1; i < p_Argc; ++i)
    {
      if (_wcsnicmp(p_Argv[i], L"-warp", wcslen(p_Argv[i])) == 0 ||
          _wcsnicmp(p_Argv[i], L"/warp", wcslen(p_Argv[i])) == 0)
      {
        m_Info.m_UseWarpDevice = true;
        m_Info.m_Title = m_Info.m_Title + L" (WARP)";
      }
    }
  }

  //---------------------------------------------------------------------------//
private:
  // Model loading
  Model sceneModel;
  DepthBuffer depthBuffer;

  // Gbuffer stuff
  RenderTexture gbufferTestTarget;
  ID3D12RootSignature* gbufferRootSignature = nullptr;
  ID3D12PipelineState* gbufferPSO = nullptr;

  RenderTexture tangentFrameTarget;
  RenderTexture uvTarget;
  RenderTexture materialIDTarget;
  StructuredBuffer materialTextureIndices;

  // Light stuff
  std::vector<SpotLight> spotLights;
  ConstantBuffer spotLightBuffer;
  glm::mat4 spotLightShadowMatrices[AppSettings::MaxSpotLights];
  DepthBuffer spotLightShadowMap;

  // Clustering stuff
  StructuredBuffer spotLightBoundsBuffer;
  StructuredBuffer spotLightInstanceBuffer;
  RawBuffer spotLightClusterBuffer;
  uint64_t numIntersectingSpotLights = 0;

  ID3D12RootSignature* clusterRS = nullptr;
  ID3DBlobPtr clusterVS;
  ID3DBlobPtr clusterFrontFacePS;
  ID3DBlobPtr clusterBackFacePS;
  ID3DBlobPtr clusterIntersectingPS;
  ID3D12PipelineState* clusterFrontFacePSO = nullptr;
  ID3D12PipelineState* clusterBackFacePSO = nullptr;
  ID3D12PipelineState* clusterIntersectingPSO = nullptr;

  ID3DBlobPtr fullScreenTriVS;

  StructuredBuffer spotLightClusterVtxBuffer;
  FormattedBuffer spotLightClusterIdxBuffer;
  std::vector<glm::vec3> coneVertices;

  ID3DBlobPtr clusterVisPS;
  ID3D12RootSignature* clusterVisRootSignature = nullptr;
  ID3D12PipelineState* clusterVisPSO = nullptr;

  // Depth only stuff
  ID3D12PipelineState* depthPSO = nullptr;
  ID3D12RootSignature* depthRootSignature = nullptr;
  ID3D12PipelineState* spotLightShadowPSO = nullptr;

  // Deferred Stuff
  RenderTexture deferredTarget;
  ID3D12RootSignature* deferredRootSig = nullptr;
  ID3D12PipelineState* deferredPSO = nullptr;

  // Pipeline objects.
  IDXGISwapChain4Ptr m_Swc;
  RenderTexture m_RenderTargets[FRAME_COUNT]; // swc backbuffers
  UINT m_FrameIndex;
  ID3D12CommandAllocatorPtr m_CmdAllocs[FRAME_COUNT];
  ID3D12CommandQueuePtr m_CmdQue;
  ID3D12DescriptorHeapPtr m_RtvHeap;
  UINT m_RtvDescriptorSize;

  // Asset objects.
  ID3D12GraphicsCommandListPtr m_CmdList;

  Texture m_BlueNoiseTexture;
  VolumetricFog m_Fog;
  TestCompute m_TestCompute;
  TAARenderPass m_TAA;
  MotionVector m_MotionVectors;

  FirstPersonCamera camera;
  PostProcessor m_PostFx;
  SimpleParticle m_Particle;
  Timer m_Timer;

  // Synchronization objects.
  HANDLE m_SwapChainEvent;
  ID3D12FencePtr m_RenderContextFence;
  UINT64 m_RenderContextFenceValue;
  HANDLE m_RenderContextFenceEvent;
  UINT64 m_FrameFenceValues[FRAME_COUNT];
  UINT64 volatile m_RenderContextFenceValues[THREAD_COUNT];

  // Shaders blob
  ID3DBlobPtr m_GBufferVS = nullptr;
  ID3DBlobPtr m_GBufferPS = nullptr;
  ID3DBlobPtr m_DeferredCS = nullptr;

  bool compileShaders();
  std::wstring getAssetPath(LPCWSTR p_AssetName);
  std::wstring getShaderPath(LPCWSTR p_ShaderName);
  bool createPSOs();
  void loadD3D12Pipeline();
  void loadAssets();
  void populateCommandList();
  void waitForRenderContext();
  void moveToNextFrame();
  void restoreD3DResources();
  void releaseD3DResources();
  void renderForward();
  void renderDeferred();
  void renderParticles();
  void createRenderTargets();

  // Renders all meshes using depth-only rendering
  void renderDepth(
      ID3D12GraphicsCommandList* p_CmdList,
      const CameraBase& p_Camera,
      ID3D12PipelineState* p_PSO,
      uint64_t p_NumVisible);

  // Renders all meshes using depth-only rendering for spotlight shadowmap
  void renderSpotLightShadowDepth(ID3D12GraphicsCommandList* p_CmdList, const CameraBase& p_Camera);

  // Render shadows for all spot lights
  void renderSpotLightShadowMap(ID3D12GraphicsCommandList* p_CmdList, const CameraBase& p_Camera);

  // Clustered rendering
  void updateLights();
  void renderClusters();
  void renderClusterVisualizer();
};

//---------------------------------------------------------------------------//
