#include "D3D12Wrapper.hpp"

#define RENDER_LATENCY 2

//---------------------------------------------------------------------------//
// global helper variables
//---------------------------------------------------------------------------//
uint64_t g_CurrentCPUFrame = 0;

//---------------------------------------------------------------------------//
// internal helper structs
//---------------------------------------------------------------------------//
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
struct Fence
{
  ID3D12Fence* m_D3DFence = nullptr;
  HANDLE m_FenceEvent = INVALID_HANDLE_VALUE;

  ~Fence()
  {
    if (m_D3DFence)
      m_D3DFence->Release();
  }

  void init(uint64_t p_InitialValue = 0)
  {
    D3D_EXEC_CHECKED(g_Device->CreateFence(
        p_InitialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3DFence)));
    m_FenceEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
    DEBUG_BREAK(m_FenceEvent != 0);
  }

  void signal(ID3D12CommandQueue* p_Queue, uint64_t p_FenceValue)
  {
    p_Queue->Signal(m_D3DFence, p_FenceValue);
  }
  void wait(uint64_t p_FenceValue)
  {
    if (m_D3DFence->GetCompletedValue() < p_FenceValue)
    {
      D3D_EXEC_CHECKED(
          m_D3DFence->SetEventOnCompletion(p_FenceValue, m_FenceEvent));
      WaitForSingleObject(m_FenceEvent, INFINITE);
    }
  }
  bool signaled(uint64_t p_FenceValue)
  {
    return m_D3DFence->GetCompletedValue() >= p_FenceValue;
  }
  void clear(uint64_t p_FenceValue)
  {
    m_D3DFence->Signal(p_FenceValue);
  }
};
//---------------------------------------------------------------------------//
// static/internal variables
//---------------------------------------------------------------------------//
static const uint64_t s_UploadBufferSize = 32 * 1024 * 1024;
static ID3D12Resource* s_UploadBuffer = nullptr;
static uint8_t* s_UploadBufferCPUAddr = nullptr;

static UploadSubmission s_UploadSubmission; // TODO: make an array of this
static SRWLOCK s_UploadSubmissionLock = SRWLOCK_INIT;
static SRWLOCK s_UploadQueueLock = SRWLOCK_INIT;

// These are protected by UploadQueueLock
static ID3D12CommandQueue* s_UploadCmdQueue = nullptr;
static Fence s_UploadFence;
static uint64_t s_UploadFenceValue = 0;
//---------------------------------------------------------------------------//
// internal local functions
//---------------------------------------------------------------------------//

static void _clearFinishedUploads()
{
  UploadSubmission& submission = s_UploadSubmission;
  if (s_UploadFence.signaled(submission.FenceValue))
    submission.reset();
}
static UploadSubmission* _allocUploadSubmission(uint64_t p_Size)
{
  // TODO: Properly allocate the submission

  UploadSubmission* submission = &s_UploadSubmission;
  submission->Offset = 0;
  submission->Size = p_Size;
  submission->FenceValue = uint64_t(-1);
  submission->Padding = 0;

  return submission;
}
void initializeUpload(ID3D12Device* dev)
{
  g_Device = dev;
  DEBUG_BREAK(g_Device != nullptr);

  UploadSubmission& submission = s_UploadSubmission;
  D3D_EXEC_CHECKED(g_Device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&submission.CmdAllocator)));
  D3D_EXEC_CHECKED(g_Device->CreateCommandList(
      0,
      D3D12_COMMAND_LIST_TYPE_COPY,
      submission.CmdAllocator,
      nullptr,
      IID_PPV_ARGS(&submission.CmdList)));
  D3D_EXEC_CHECKED(submission.CmdList->Close());

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  D3D_EXEC_CHECKED(g_Device->CreateCommandQueue(
      &queueDesc, IID_PPV_ARGS(&s_UploadCmdQueue)));

  s_UploadFence.init(0);

  D3D12_RESOURCE_DESC resourceDesc = {};
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resourceDesc.Width = uint32_t(s_UploadBufferSize);
  resourceDesc.Height = 1;
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.MipLevels = 1;
  resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
  resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.SampleDesc.Quality = 0;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  resourceDesc.Alignment = 0;

  D3D_EXEC_CHECKED(g_Device->CreateCommittedResource(
      getUploadHeapProps(),
      D3D12_HEAP_FLAG_NONE,
      &resourceDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&s_UploadBuffer)));

  D3D12_RANGE readRange = {};
  D3D_EXEC_CHECKED(s_UploadBuffer->Map(
      0, &readRange, reinterpret_cast<void**>(&s_UploadBufferCPUAddr)));

  // TODO: temporary buffer memory that swaps every frame

  // TODO: readback resources
}
void shutdownUpload()
{
  s_UploadBuffer->Release();
  s_UploadCmdQueue->Release();
  s_UploadSubmission.CmdAllocator->Release();
  s_UploadSubmission.CmdList->Release();
}
UploadContext _resourceUploadBegin(uint64_t p_Size)
{
  DEBUG_BREAK(g_Device != nullptr);

  p_Size = alignUp<uint64_t>(p_Size, 512);
  DEBUG_BREAK(p_Size <= g_UploadBufferSize);
  DEBUG_BREAK(p_Size > 0);

  UploadSubmission* submission = nullptr;
  {
    AcquireSRWLockExclusive(&s_UploadSubmissionLock);

    _clearFinishedUploads();
    submission = _allocUploadSubmission(p_Size);

    ReleaseSRWLockExclusive(&s_UploadSubmissionLock);
  }

  D3D_EXEC_CHECKED(submission->CmdAllocator->Reset());
  D3D_EXEC_CHECKED(
      submission->CmdList->Reset(submission->CmdAllocator, nullptr));

  UploadContext context;
  context.CmdList = submission->CmdList;
  context.Resource = s_UploadBuffer;
  context.CpuAddress = s_UploadBufferCPUAddr + submission->Offset;
  context.ResourceOffset = submission->Offset;
  context.Submission = submission;

  return context;
}

void _resourceUploadEnd(UploadContext& context)
{
  DEBUG_BREAK(context.CmdList != nullptr);
  DEBUG_BREAK(context.Submission != nullptr);
  UploadSubmission* submission =
      reinterpret_cast<UploadSubmission*>(context.Submission);

  {
    AcquireSRWLockExclusive(&s_UploadQueueLock);

    // Finish off and execute the command list
    D3D_EXEC_CHECKED(submission->CmdList->Close());
    ID3D12CommandList* cmdLists[1] = {submission->CmdList};
    s_UploadCmdQueue->ExecuteCommandLists(1, cmdLists);

    ++s_UploadFenceValue;
    s_UploadFence.signal(s_UploadCmdQueue, s_UploadFenceValue);
    submission->FenceValue = s_UploadFenceValue;

    ReleaseSRWLockExclusive(&s_UploadQueueLock);
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
MapResult Buffer::map()
{
  DEBUG_BREAK(initialized());
  DEBUG_BREAK(m_Dynamic);
  DEBUG_BREAK(m_CpuAccessible);

  // Make sure that we do this at most once per-frame
  DEBUG_BREAK(m_UploadFrame != g_CurrentCPUFrame);
  m_UploadFrame = g_CurrentCPUFrame;

  // Cycle to the next buffer
  m_CurrBuffer = (g_CurrentCPUFrame + 1) % RENDER_LATENCY;

  MapResult result;
  result.ResourceOffset = m_CurrBuffer * m_Size;
  result.CpuAddress = m_CpuAddress + m_CurrBuffer * m_Size;
  result.GpuAddress = m_GpuAddress + m_CurrBuffer * m_Size;
  result.Resource = m_Resource;
  return result;
}
MapResult Buffer::mapAndSetData(const void* p_Data, uint64_t p_DataSize)
{
  DEBUG_BREAK(p_DataSize <= m_Size);
  MapResult result = map();
  memcpy(result.CpuAddress, p_Data, p_DataSize);
  return result;
}
uint64_t Buffer::updateData(
    const void* p_SrcData, uint64_t p_SrcSize, uint64_t p_DstOffset)
{
  return multiUpdateData(&p_SrcData, &p_SrcSize, &p_DstOffset, 1);
}
uint64_t Buffer::multiUpdateData(
    const void* p_SrcData[],
    uint64_t p_SrcSize[],
    uint64_t p_DstOffset[],
    uint64_t p_NumUpdates)
{
  DEBUG_BREAK(m_Dynamic);
  DEBUG_BREAK(m_CpuAccessible == false);
  DEBUG_BREAK(p_NumUpdates > 0);

  // Make sure that we do this at most once per-frame
  DEBUG_BREAK(m_UploadFrame != g_CurrentCPUFrame);
  m_UploadFrame = g_CurrentCPUFrame;

  // Cycle to the next buffer
  m_CurrBuffer = (g_CurrentCPUFrame + 1) % RENDER_LATENCY;

  uint64_t currOffset = m_CurrBuffer * m_Size;

  uint64_t totalUpdateSize = 0;
  for (uint64_t i = 0; i < p_NumUpdates; ++i)
    totalUpdateSize += p_SrcSize[i];

  UploadContext uploadContext = _resourceUploadBegin(totalUpdateSize);

  uint64_t uploadOffset = 0;
  for (uint64_t i = 0; i < p_NumUpdates; ++i)
  {
    DEBUG_BREAK(p_DstOffset[i] + p_SrcSize[i] <= m_Size);
    DEBUG_BREAK(uploadOffset + p_SrcSize[i] <= totalUpdateSize);
    memcpy(
        reinterpret_cast<uint8_t*>(uploadContext.CpuAddress) + uploadOffset,
        p_SrcData[i],
        p_SrcSize[i]);
    uploadContext.CmdList->CopyBufferRegion(
        m_Resource,
        currOffset + p_DstOffset[i],
        uploadContext.Resource,
        uploadContext.ResourceOffset + uploadOffset,
        p_SrcSize[i]);

    uploadOffset += p_SrcSize[i];
  }

  _resourceUploadEnd(uploadContext);

  return m_GpuAddress + currOffset;
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
// StructuredBuffer
//---------------------------------------------------------------------------//

//---------------------------------------------------------------------------//
// Textures
//---------------------------------------------------------------------------//
