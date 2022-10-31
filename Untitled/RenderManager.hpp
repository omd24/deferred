#pragma once

#include <Utility.hpp>
#include <Camera.hpp>
#include <Timer.hpp>
#include "d3dx12.h"
#include "D3D12Wrapper.hpp"
#include "Model.hpp"

#define FRAME_COUNT 3
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
    // Just to release ComPtrs
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
  RenderTexture albedoTarget;
  ID3D12RootSignature* gbufferRootSignature = nullptr;
  ID3D12PipelineState* gbufferPSO = nullptr;

  RenderTexture materialIDTarget;
  StructuredBuffer materialTextureIndices;

  // Deferred Stuff
  RenderTexture deferredTarget;
  ID3D12RootSignature* deferredRootSig = nullptr;
  ID3D12PipelineState* deferredPSO = nullptr;

  // Pipeline objects.
  CD3DX12_VIEWPORT m_Viewport;
  CD3DX12_RECT m_ScissorRect;
  IDXGISwapChain3Ptr m_Swc;
  ID3D12ResourcePtr m_RenderTargets[FRAME_COUNT];
  UINT m_FrameIndex;
  ID3D12CommandAllocatorPtr m_CmdAllocs[FRAME_COUNT];
  ID3D12CommandQueuePtr m_CmdQue;
  ID3D12DescriptorHeapPtr m_RtvHeap;
  UINT m_RtvDescriptorSize;

  // Asset objects.
  ID3D12GraphicsCommandListPtr m_CmdList;

  Camera m_Camera;
  Timer m_Timer;

  // Synchronization objects.
  HANDLE m_SwapChainEvent;
  ID3D12FencePtr m_RenderContextFence;
  UINT64 m_RenderContextFenceValue;
  HANDLE m_RenderContextFenceEvent;
  UINT64 m_FrameFenceValues[FRAME_COUNT];
  UINT64 volatile m_RenderContextFenceValues[THREAD_COUNT];

  struct ModelVertex
  {
    XMFLOAT3 m_Position;
    XMFLOAT3 m_Normal;
    XMFLOAT2 m_UV;
    XMFLOAT3 m_Tangent;
    XMFLOAT3 m_Bitangent;
  };
  static_assert(sizeof(ModelVertex) == 56);

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
};

//---------------------------------------------------------------------------//
