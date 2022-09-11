#pragma once

#include <Utility.hpp>
#include <Camera.hpp>
#include <Timer.hpp>
#include "d3dx12.h"

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

  // Uniforms
  struct TriangleParams
  {
    UINT m_Params[4];
    float m_ParamsFloat[4];
  };
  struct FrameParams
  {
    XMFLOAT4X4 m_Wvp;
    XMFLOAT4X4 m_InvView;

    // Constant buffers are 256-byte aligned in GPU memory
    float padding[32];
  };
  ID3D12ResourcePtr m_TriangleUniforms;
  ID3D12ResourcePtr m_FrameUniforms;
  UINT8* m_FrameUniformsDataPtr;

  // Pipeline objects.
  CD3DX12_VIEWPORT m_Viewport;
  CD3DX12_RECT m_ScissorRect;
  IDXGISwapChain3Ptr m_Swc;
  ID3D12ResourcePtr m_RenderTargets[FRAME_COUNT];
  UINT m_FrameIndex;
  ID3D12CommandAllocatorPtr m_CmdAllocs[FRAME_COUNT];
  ID3D12CommandQueuePtr m_CmdQue;
  ID3D12RootSignaturePtr m_RootSig;
  ID3D12DescriptorHeapPtr m_RtvHeap;
  ID3D12DescriptorHeapPtr m_SrvUavHeap;
  UINT m_RtvDescriptorSize;
  UINT m_SrvUavDescriptorSize;

  // Asset objects.
  ID3D12PipelineStatePtr m_Pso;
  ID3D12GraphicsCommandListPtr m_CmdList;
  ID3D12ResourcePtr m_VtxBuffer;
  ID3D12ResourcePtr m_VtxBufferUpload;
  D3D12_VERTEX_BUFFER_VIEW m_VtxBufferView;

  Camera m_Camera;
  Timer m_Timer;

  // Compute objects:

  // Synchronization objects.
  HANDLE m_SwapChainEvent;
  ID3D12FencePtr m_RenderContextFence;
  UINT64 m_RenderContextFenceValue;
  HANDLE m_RenderContextFenceEvent;
  UINT64 m_FrameFenceValues[FRAME_COUNT];
  UINT64 volatile m_RenderContextFenceValues[THREAD_COUNT];

  // Vertex data (color for now)
  struct Vertex
  {
    XMFLOAT3 m_Position;
    XMFLOAT4 m_Color;
  };

  // Indices of the root signature parameters:
  enum GraphicsRootParameters : UINT32
  {
    GraphicsRootCBV = 0,

    GraphicsRootParametersCount
  };

  // ComputeRootParameters:

  // Indices of shader resources in the descriptor heap:
  enum DescriptorHeapIndex : UINT32
  {
    // Dummy indices
    Uav0 = 0,
    Srv0 = Uav0 + THREAD_COUNT,
    DescriptorCount = Srv0 + THREAD_COUNT
  };

  std::wstring _getAssetPath(LPCWSTR p_AssetName);
  std::wstring _getShaderPath(LPCWSTR p_ShaderName);
  void _createVertexBuffer();
  bool _createPSOs();
  void _loadD3D12Pipeline();
  void _loadAssets();
  void _populateCommandList();
  void _waitForRenderContext();
  void _waitForGpu();
  void _moveToNextFrame();
  void _restoreD3DResources();
  void _releaseD3DResources();
};

//---------------------------------------------------------------------------//
