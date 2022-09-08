#pragma once

#include "Utility.hpp"

//---------------------------------------------------------------------------//
// D3D12 Globals:
//---------------------------------------------------------------------------//
ID3D12Device2* g_Device = nullptr;
static const uint64_t g_UploadBufferSize = 32 * 1024 * 1024;

//---------------------------------------------------------------------------//
// Direct3D 12 helper functions
//---------------------------------------------------------------------------//
inline void setViewport(
    ID3D12GraphicsCommandList* p_CmdList,
    uint64_t p_Width,
    uint64_t p_Height,
    float p_MinZ,
    float p_MaxZ)
{
  D3D12_VIEWPORT viewport = {};
  viewport.Width = float(p_Width);
  viewport.Height = float(p_Height);
  viewport.MinDepth = p_MinZ;
  viewport.MaxDepth = p_MaxZ;
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;

  D3D12_RECT scissorRect = {};
  scissorRect.left = 0;
  scissorRect.top = 0;
  scissorRect.right = uint32_t(p_Width);
  scissorRect.bottom = uint32_t(p_Height);

  p_CmdList->RSSetViewports(1, &viewport);
  p_CmdList->RSSetScissorRects(1, &scissorRect);
}
inline const D3D12_HEAP_PROPERTIES* getDefaultHeapProps()
{
  static D3D12_HEAP_PROPERTIES heapProps = {
      D3D12_HEAP_TYPE_DEFAULT,
      D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
      D3D12_MEMORY_POOL_UNKNOWN,
      0,
      0,
  };
  return &heapProps;
}
inline const D3D12_HEAP_PROPERTIES* getUploadHeapProps()
{
  static D3D12_HEAP_PROPERTIES heapProps = {
      D3D12_HEAP_TYPE_UPLOAD,
      D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
      D3D12_MEMORY_POOL_UNKNOWN,
      0,
      0,
  };
  return &heapProps;
}
inline void transitionResource(
    ID3D12GraphicsCommandList* p_CmdList,
    ID3D12Resource* p_Resource,
    D3D12_RESOURCE_STATES p_Before,
    D3D12_RESOURCE_STATES p_After,
    uint32_t p_SubResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
{
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = p_Resource;
  barrier.Transition.StateBefore = p_Before;
  barrier.Transition.StateAfter = p_After;
  barrier.Transition.Subresource = p_SubResource;
  p_CmdList->ResourceBarrier(1, &barrier);
}
//---------------------------------------------------------------------------//
// D3D12 helper structs
//---------------------------------------------------------------------------//
struct MapResult
{
  void* CpuAddress = nullptr;
  uint64_t GpuAddress = 0;
  uint64_t ResourceOffset = 0;
  ID3D12Resource* Resource = nullptr;
};
struct UploadContext
{
  ID3D12GraphicsCommandList* CmdList;
  void* CpuAddress = nullptr;
  uint64_t ResourceOffset = 0;
  ID3D12Resource* Resource = nullptr;
  void* Submission = nullptr;
};
struct UploadSubmission
{
  ID3D12CommandAllocator* CmdAllocator = nullptr;
  ID3D12GraphicsCommandList1* CmdList = nullptr;
  uint64_t Offset = 0;
  uint64_t Size = 0;
  uint64_t FenceValue = 0;
  uint64_t Padding = 0;

  void reset()
  {
    Offset = 0;
    Size = 0;
    FenceValue = 0;
    Padding = 0;
  }
};
//---------------------------------------------------------------------------//
// Buffers
//---------------------------------------------------------------------------//
struct Buffer
{
  ID3D12Resource* m_Resource = nullptr;
  uint64_t m_CurrBuffer = 0;
  uint8_t* m_CpuAddress = 0;
  uint64_t m_GpuAddress = 0;
  uint64_t m_Alignment = 0;
  uint64_t m_Size = 0;
  bool m_Dynamic = false;
  bool m_CpuAccessible = false;
  ID3D12Heap* m_Heap = nullptr;
  uint64_t m_HeapOffset = 0;
  uint64_t m_UploadFrame = uint64_t(-1);

  Buffer()
  {
    // Do nothing
  }
  ~Buffer()
  {
    // Do nothing
  }

  void init(
      uint64_t p_Size,
      uint64_t p_Alignment,
      bool p_Dynamic,
      bool p_CpuAccessible,
      bool p_AllowUav,
      const void* p_InitData,
      D3D12_RESOURCE_STATES p_InitialState,
      ID3D12Heap* p_Heap,
      uint64_t p_HeapOffset,
      const wchar_t* p_Name);
  void deinit();

  MapResult map();
  MapResult mapAndSetData(const void* p_Data, uint64_t p_DataSize);
  template <typename T> MapResult mapAndSetData(const T& p_Data)
  {
    return mapAndSetData(&p_Data, sizeof(T));
  }
  uint64_t
  updateData(const void* p_SrcData, uint64_t p_SrcSize, uint64_t p_DstOffset);
  uint64_t multiUpdateData(
      const void* p_SrcData[],
      uint64_t p_SrcSize[],
      uint64_t p_DstOffset[],
      uint64_t p_NumUpdates);

  void transition(
      ID3D12GraphicsCommandList* p_CmdList,
      D3D12_RESOURCE_STATES p_Before,
      D3D12_RESOURCE_STATES p_After) const;
  void makeReadable(ID3D12GraphicsCommandList* p_CmdList) const;
  void makeWritable(ID3D12GraphicsCommandList* p_CmdList) const;
  void uavBarrier(ID3D12GraphicsCommandList* p_CmdList) const;

  bool initialized() const
  {
    return m_Size > 0;
  }
};
//---------------------------------------------------------------------------//
// Textures
//---------------------------------------------------------------------------//
