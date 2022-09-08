#include "D3D12Wrapper.hpp"

#define RENDER_LATENCY 2

//---------------------------------------------------------------------------//
// internal local functions
//---------------------------------------------------------------------------//
void _initializeUpload()
{
  // TODO
  // create upload submissions
}
UploadContext _resourceUploadBegin(uint64_t p_Size)
{
  DEBUG_BREAK(g_Device != nullptr);

  p_Size = alignUp<uint64_t>(p_Size, 512);
  DEBUG_BREAK(p_Size <= g_UploadBufferSize);
  DEBUG_BREAK(p_Size > 0);

  UploadSubmission* submission = nullptr;

  {
    // TODO
    // Sync with other upload submissions
  }

  UploadContext context;

  // TODO
  //

  return context;
}

void _resourceUploadEnd(UploadContext& context)
{
  DEBUG_BREAK(context.CmdList != nullptr);
  DEBUG_BREAK(context.Submission != nullptr);
  UploadSubmission* submission =
      reinterpret_cast<UploadSubmission*>(context.Submission);

  {
    // TODO
    // Sync with other upload submissions
  }

  context = UploadContext();
}
//---------------------------------------------------------------------------//
// Buffers
//---------------------------------------------------------------------------//
void Buffer::init(
    uint64_t p_Size,
    uint64_t p_Alignment,
    bool p_Dynamic,
    bool p_CpuAccessible,
    bool p_AllowUav,
    const void* p_InitData,
    D3D12_RESOURCE_STATES p_InitialState,
    ID3D12Heap* p_Heap,
    uint64_t p_HeapOffset,
    const wchar_t* p_Name)
{
  DEBUG_BREAK(g_Device);

  m_Size = alignUp(p_Size, p_Alignment);
  m_Alignment = p_Alignment;
  m_Dynamic = p_Dynamic;
  m_CpuAccessible = p_CpuAccessible;
  m_CurrBuffer = 0;
  m_CpuAddress = nullptr;
  m_GpuAddress = 0;
  m_Heap = nullptr;
  m_HeapOffset = 0;

  DEBUG_BREAK(p_AllowUav == false || p_Dynamic == false);
  DEBUG_BREAK(p_Dynamic || p_CpuAccessible == false);

  D3D12_RESOURCE_DESC resourceDesc = {};
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resourceDesc.Width = p_Dynamic ? m_Size * RENDER_LATENCY : m_Size;
  resourceDesc.Height = 1;
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.MipLevels = 1;
  resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
  resourceDesc.Flags = p_AllowUav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                  : D3D12_RESOURCE_FLAG_NONE;
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.SampleDesc.Quality = 0;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  resourceDesc.Alignment = 0;

  const D3D12_HEAP_PROPERTIES* heapProps =
      p_CpuAccessible ? getUploadHeapProps() : getDefaultHeapProps();
  D3D12_RESOURCE_STATES resourceState = p_InitialState;
  if (p_CpuAccessible)
    resourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
  else if (p_InitData)
    resourceState = D3D12_RESOURCE_STATE_COMMON;

  if (p_Heap)
  {
    m_Heap = p_Heap;
    m_HeapOffset = p_HeapOffset;
    D3D_EXEC_CHECKED(g_Device->CreatePlacedResource(
        p_Heap,
        p_HeapOffset,
        &resourceDesc,
        resourceState,
        nullptr,
        IID_PPV_ARGS(&m_Resource)));
  }
  else
  {
    D3D_EXEC_CHECKED(g_Device->CreateCommittedResource(
        heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        resourceState,
        nullptr,
        IID_PPV_ARGS(&m_Resource)));
  }

  m_GpuAddress = m_Resource->GetGPUVirtualAddress();

  if (p_CpuAccessible)
  {
    D3D12_RANGE readRange = {};
    D3D_EXEC_CHECKED(m_Resource->Map(
        0, &readRange, reinterpret_cast<void**>(&m_CpuAddress)));
  }

  if (p_InitData && p_CpuAccessible)
  {
    for (uint64_t i = 0; i < RENDER_LATENCY; ++i)
    {
      uint8_t* dstMem = m_CpuAddress + m_Size * i;
      memcpy(dstMem, p_InitData, p_Size);
    }
  }
  else if (p_InitData)
  {
    UploadContext uploadContext = _resourceUploadBegin(resourceDesc.Width);

    // TODO
    // implement submission setup
    DEBUG_BREAK(false);

    memcpy(uploadContext.CpuAddress, p_InitData, p_Size);
    if (p_Dynamic)
      memcpy((uint8_t*)uploadContext.CpuAddress + p_Size, p_InitData, p_Size);

    uploadContext.CmdList->CopyBufferRegion(
        m_Resource,
        0,
        uploadContext.Resource,
        uploadContext.ResourceOffset,
        p_Size);

    _resourceUploadEnd(uploadContext);
  }
}
void Buffer::deinit()
{
  // release the buffer resource:
  m_Resource->Release();
}
MapResult Buffer::mapAndSetData(const void* p_Data, uint64_t p_DataSize)
{
  DEBUG_BREAK(p_DataSize <= m_Size);
  MapResult result;

  // TODO
  // do the actual map
  DEBUG_BREAK(false);

  memcpy(result.CpuAddress, p_Data, p_DataSize);
  return result;
}
uint64_t Buffer::updateData(
    const void* p_SrcData, uint64_t p_SrcSize, uint64_t p_DstOffset)
{
  // TODO
  // do the actual updating
  DEBUG_BREAK(false);
}
uint64_t Buffer::multiUpdateData(
    const void* p_SrcData[],
    uint64_t p_SrcSize[],
    uint64_t p_DstOffset[],
    uint64_t p_NumUpdates)
{
  // TODO
  // do the actual updating
  DEBUG_BREAK(false);
}

void Buffer::transition(
    ID3D12GraphicsCommandList* p_CmdList,
    D3D12_RESOURCE_STATES p_Before,
    D3D12_RESOURCE_STATES p_After) const
{
  DEBUG_BREAK(m_Resource != nullptr);
  transitionResource(p_CmdList, m_Resource, p_Before, p_After);
}
void Buffer::makeReadable(ID3D12GraphicsCommandList* p_CmdList) const
{
  DEBUG_BREAK(m_Resource != nullptr);
  transitionResource(
      p_CmdList,
      m_Resource,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_GENERIC_READ);
}
void Buffer::makeWritable(ID3D12GraphicsCommandList* p_CmdList) const
{
  DEBUG_BREAK(m_Resource != nullptr);
  transitionResource(
      p_CmdList,
      m_Resource,
      D3D12_RESOURCE_STATE_GENERIC_READ,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
void Buffer::uavBarrier(ID3D12GraphicsCommandList* p_CmdList) const
{
  DEBUG_BREAK(m_Resource != nullptr);
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.UAV.pResource = m_Resource;
  p_CmdList->ResourceBarrier(1, &barrier);
}
//---------------------------------------------------------------------------//
// Textures
//---------------------------------------------------------------------------//

