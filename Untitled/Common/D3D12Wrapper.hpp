#pragma once

//=================================================================================================
//  All code licensed under the MIT license
//=================================================================================================

#include "Utility.hpp"

//---------------------------------------------------------------------------//
// D3D12 Globals:
//---------------------------------------------------------------------------//
#define RENDER_LATENCY 2

struct Texture;

static ID3D12Device* g_Device = nullptr;
static const uint64_t g_UploadBufferSize = 32 * 1024 * 1024;

// Total number of CPU frames completed
// (completed means all command buffers submitted to the GPU):
extern uint64_t g_CurrentCPUFrame;

extern uint64_t g_CurrentGPUFrame;

extern uint64_t g_CurrFrameIdx; // CurrentCPUFrame % RenderLatency
//---------------------------------------------------------------------------//
// Direct3D 12 helper functions
//---------------------------------------------------------------------------//
void initializeUpload(ID3D12Device* p_Dev);
void shutdownUpload();

void EndFrame_Upload(ID3D12CommandQueue* p_Queue);

inline void setViewport(
    ID3D12GraphicsCommandList* p_CmdList,
    uint64_t p_Width,
    uint64_t p_Height,
    float p_MinZ = 0.0f,
    float p_MaxZ = 1.0f)
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
inline void createRootSignature(
    ID3D12Device* dev,
    ID3D12RootSignature** rootSignature,
    const D3D12_ROOT_SIGNATURE_DESC1& desc)
{
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
  versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  versionedDesc.Desc_1_1 = desc;

  ID3DBlobPtr signature;
  ID3DBlobPtr error;
  HRESULT hr =
      D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error);
  if (FAILED(hr))
  {
    const char* errString =
        error ? reinterpret_cast<const char*>(error->GetBufferPointer()) : "";

    assert(false && "Failed to create root signature");
  }

  dev->CreateRootSignature(
      0,
      signature->GetBufferPointer(),
      signature->GetBufferSize(),
      IID_PPV_ARGS(rootSignature));
}
inline bool fileExists(const wchar_t* filePath)
{
  if (filePath == NULL)
    return false;

  DWORD fileAttr = GetFileAttributes(filePath);
  if (fileAttr == INVALID_FILE_ATTRIBUTES)
    return false;

  return true;
}
inline std::wstring getFileExtension(const wchar_t* filePath_)
{
  assert(filePath_);

  std::wstring filePath(filePath_);
  size_t idx = filePath.rfind(L'.');
  if (idx != std::wstring::npos)
    return filePath.substr(idx + 1, filePath.length() - idx - 1);
  else
    return std::wstring(L"");
}
// Returns the directory containing a file
inline std::wstring getDirectoryFromFilePath(const wchar_t* filePath_)
{
  assert(filePath_);

  std::wstring filePath(filePath_);
  size_t idx = filePath.rfind(L'\\');
  if (idx != std::wstring::npos)
    return filePath.substr(0, idx + 1);
  else
    return std::wstring(L"");
}
inline void writeLog(const char* format, ...)
{
  char buffer[1024] = {0};
  va_list args;
  va_start(args, format);
  vsprintf_s(buffer, arrayCount32(buffer), format, args);
  OutputDebugStringA(buffer);
  OutputDebugStringA("\n");
}
// Returns the name of the file given the path (extension included)
inline std::wstring getFileName(const wchar_t* filePath_)
{
  assert(filePath_);

  std::wstring filePath(filePath_);
  size_t idx = filePath.rfind(L'\\');
  if (idx != std::wstring::npos && idx < filePath.length() - 1)
    return filePath.substr(idx + 1);
  else
  {
    idx = filePath.rfind(L'/');
    if (idx != std::wstring::npos && idx < filePath.length() - 1)
      return filePath.substr(idx + 1);
    else
      return filePath;
  }
}

//---------------------------------------------------------------------------//
// various d3d helpers
//---------------------------------------------------------------------------//

// Forward declarations
struct DescriptorHeap;
struct DescriptorHandle;
struct LinearDescriptorHeap;

enum class BlendState : uint64_t
{
  Disabled = 0,
  Additive,
  AlphaBlend,
  PreMultiplied,
  NoColorWrites,
  PreMultipliedRGB,

  NumValues
};

enum class RasterizerState : uint64_t
{
  NoCull = 0,
  BackFaceCull,
  BackFaceCullNoZClip,
  FrontFaceCull,
  NoCullNoMS,
  Wireframe,

  NumValues
};

enum class DepthState : uint64_t
{
  Disabled = 0,
  Enabled,
  Reversed,
  WritesEnabled,
  ReversedWritesEnabled,

  NumValues
};

enum class SamplerState : uint64_t
{
  Linear = 0,
  LinearClamp,
  LinearBorder,
  Point,
  Anisotropic,
  ShadowMap,
  ShadowMapPCF,
  ReversedShadowMap,
  ReversedShadowMapPCF,

  NumValues
};

enum class CmdListMode : uint64_t
{
  Graphics = 0,
  Compute,
};

struct TempDescriptorTable
{
  D3D12_CPU_DESCRIPTOR_HANDLE CPUStart;
  D3D12_GPU_DESCRIPTOR_HANDLE GPUStart;
};

struct TempBuffer
{
  void* CPUAddress = nullptr;
  uint64_t GPUAddress = 0;
  uint32_t DescriptorIndex = uint32_t(-1);
};

// Constants
const uint64_t ConstantBufferAlignment =
    D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
const uint64_t VertexBufferAlignment = 4;
const uint64_t IndexBufferAlignment = 4;
const uint32_t StandardMSAAPattern = 0xFFFFFFFF;

// Externals
extern uint32_t RTVDescriptorSize;
extern uint32_t SRVDescriptorSize;
extern uint32_t UAVDescriptorSize;
extern uint32_t CBVDescriptorSize;
extern uint32_t DSVDescriptorSize;

extern DescriptorHeap RTVDescriptorHeap;
extern DescriptorHeap SRVDescriptorHeap;
extern DescriptorHeap DSVDescriptorHeap;
extern DescriptorHeap UAVDescriptorHeap;

extern uint32_t NullTexture2DSRV;

const uint32_t NumUserDescriptorRanges = 16;
const uint32_t NumStandardDescriptorRanges = 7 + NumUserDescriptorRanges;

// Lifetime
void initializeHelpers();
void shutdownHelpers();

void endFrameHelpers();

// Resource Barriers
void TransitionResource(
    ID3D12GraphicsCommandList* p_CmdList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES p_Before,
    D3D12_RESOURCE_STATES p_After,
    uint32_t subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

// Resource management
uint64_t GetResourceSize(
    const D3D12_RESOURCE_DESC& desc,
    uint32_t firstSubResource = 0,
    uint32_t numSubResources = 1);
uint64_t GetResourceSize(
    ID3D12Resource* resource,
    uint32_t firstSubResource = 0,
    uint32_t numSubResources = 1);

// Heap helpers
const D3D12_HEAP_PROPERTIES* GetDefaultHeapProps();
const D3D12_HEAP_PROPERTIES* GetUploadHeapProps();
const D3D12_HEAP_PROPERTIES* GetReadbackHeapProps();

// Render states
D3D12_BLEND_DESC GetBlendState(BlendState blendState);
D3D12_RASTERIZER_DESC GetRasterizerState(RasterizerState rasterizerState);
D3D12_DEPTH_STENCIL_DESC GetDepthState(DepthState depthState);
D3D12_SAMPLER_DESC GetSamplerState(SamplerState samplerState);
D3D12_STATIC_SAMPLER_DESC GetStaticSamplerState(
    SamplerState samplerState,
    uint32_t shaderRegister = 0,
    uint32_t registerSpace = 0,
    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_PIXEL);
D3D12_STATIC_SAMPLER_DESC ConvertToStaticSampler(
    const D3D12_SAMPLER_DESC& samplerDesc,
    uint32_t shaderRegister = 0,
    uint32_t registerSpace = 0,
    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_PIXEL);

// Convenience functions
void SetViewport(
    ID3D12GraphicsCommandList* p_CmdList,
    uint64_t width,
    uint64_t height,
    float zMin = 0.0f,
    float zMax = 1.0f);
void CreateRootSignature(
    ID3D12RootSignature** rootSignature,
    const D3D12_ROOT_SIGNATURE_DESC1& desc);
uint32_t DispatchSize(uint64_t numElements, uint64_t groupSize);

// Resource binding
void SetDescriptorHeaps(ID3D12GraphicsCommandList* p_CmdList);
D3D12_GPU_DESCRIPTOR_HANDLE
TempDescriptorTable(const D3D12_CPU_DESCRIPTOR_HANDLE* handles, uint64_t count);
void BindTempDescriptorTable(
    ID3D12GraphicsCommandList* p_CmdList,
    const D3D12_CPU_DESCRIPTOR_HANDLE* handles,
    uint64_t count,
    uint32_t rootParameter,
    CmdListMode p_CmdListMode);

// Helpers for buffer types that use temporary buffer memory from the upload
// helper
// TODO
//

const D3D12_DESCRIPTOR_RANGE1* StandardDescriptorRanges();
void InsertStandardDescriptorRanges(D3D12_DESCRIPTOR_RANGE1* ranges);

void BindAsDescriptorTable(
    ID3D12GraphicsCommandList* p_CmdList,
    uint32_t descriptorIdx,
    uint32_t rootParameter,
    CmdListMode p_CmdListMode);
void BindStandardDescriptorTable(
    ID3D12GraphicsCommandList* p_CmdList,
    uint32_t rootParameter,
    CmdListMode p_CmdListMode);

// Helpers for buffer types that use temporary buffer memory from the upload
// helper
TempBuffer TempConstantBuffer(uint64_t cbSize, bool makeDescriptor = false);
void BindTempConstantBuffer(
    ID3D12GraphicsCommandList* cmdList,
    const void* cbData,
    uint64_t cbSize,
    uint32_t rootParameter,
    CmdListMode cmdListMode);

template <typename T>
void BindTempConstantBuffer(
    ID3D12GraphicsCommandList* cmdList,
    const T& cbData,
    uint32_t rootParameter,
    CmdListMode cmdListMode)
{
  BindTempConstantBuffer(
      cmdList, &cbData, sizeof(T), rootParameter, cmdListMode);
}

template <uint32_t N>
void BindTempConstantBuffer(
    ID3D12GraphicsCommandList* cmdList,
    const uint32_t (&cbData)[N],
    uint32_t rootParameter,
    CmdListMode cmdListMode)
{
  BindTempConstantBuffer(
      cmdList, cbData, N * sizeof(uint32_t), rootParameter, cmdListMode);
}

struct PersistentDescriptorAlloc
{
  D3D12_CPU_DESCRIPTOR_HANDLE Handles[RENDER_LATENCY] = {};
  uint32_t Index = uint32_t(-1);
};

struct TempDescriptorAlloc
{
  D3D12_CPU_DESCRIPTOR_HANDLE StartCPUHandle = {};
  D3D12_GPU_DESCRIPTOR_HANDLE StartGPUHandle = {};
  uint32_t StartIndex = uint32_t(-1);
};

// Wrapper for D3D12 descriptor heaps that supports persistent and temporary
// allocations
struct DescriptorHeap
{
  ID3D12DescriptorHeap* Heaps[RENDER_LATENCY] = {};
  uint32_t NumPersistent = 0;
  uint32_t PersistentAllocated = 0;
  std::vector<uint32_t> DeadList;
  uint32_t NumTemporary = 0;
  volatile int64_t TemporaryAllocated = 0;
  uint32_t HeapIndex = 0;
  uint32_t NumHeaps = 0;
  uint32_t DescriptorSize = 0;
  bool ShaderVisible = false;
  D3D12_DESCRIPTOR_HEAP_TYPE HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  D3D12_CPU_DESCRIPTOR_HANDLE CPUStart[RENDER_LATENCY] = {};
  D3D12_GPU_DESCRIPTOR_HANDLE GPUStart[RENDER_LATENCY] = {};
  SRWLOCK Lock = SRWLOCK_INIT;

  ~DescriptorHeap();

  void Init(
      uint32_t numPersistent,
      uint32_t numTemporary,
      D3D12_DESCRIPTOR_HEAP_TYPE heapType,
      bool shaderVisible);
  void Shutdown();

  PersistentDescriptorAlloc AllocatePersistent();
  void FreePersistent(uint32_t& idx);
  void FreePersistent(D3D12_CPU_DESCRIPTOR_HANDLE& handle);
  void FreePersistent(D3D12_GPU_DESCRIPTOR_HANDLE& handle);

  TempDescriptorAlloc AllocateTemporary(uint32_t count);
  void EndFrame();

  D3D12_CPU_DESCRIPTOR_HANDLE CPUHandleFromIndex(uint32_t descriptorIdx) const;
  D3D12_GPU_DESCRIPTOR_HANDLE GPUHandleFromIndex(uint32_t descriptorIdx) const;

  D3D12_CPU_DESCRIPTOR_HANDLE
  CPUHandleFromIndex(uint32_t descriptorIdx, uint64_t heapIdx) const;
  D3D12_GPU_DESCRIPTOR_HANDLE
  GPUHandleFromIndex(uint32_t descriptorIdx, uint64_t heapIdx) const;

  uint32_t IndexFromHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  uint32_t IndexFromHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle);

  ID3D12DescriptorHeap* CurrentHeap() const;
  uint32_t TotalNumDescriptors() const
  {
    return NumPersistent + NumTemporary;
  }
};

//---------------------------------------------------------------------------//
// buffer resource helpers
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
UploadContext resourceUploadBegin(uint64_t p_Size);
void resourceUploadEnd(UploadContext& context);
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
struct FormattedBufferInit
{
  DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
  uint64_t NumElements = 0;
  bool CreateUAV = false;
  bool Dynamic = false;
  bool CPUAccessible = false;
  const void* InitData = nullptr;
  D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
  ID3D12Heap* Heap = nullptr;
  uint64_t HeapOffset = 0;
  const wchar_t* Name = nullptr;
};
struct FormattedBuffer
{
  Buffer InternalBuffer;
  uint64_t Stride = 0;
  uint64_t NumElements = 0;
  DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
  uint32_t SRV = uint32_t(-1);
  D3D12_CPU_DESCRIPTOR_HANDLE UAV = {};
  uint64_t GPUAddress = 0;

  FormattedBuffer();
  ~FormattedBuffer();

  void init(const FormattedBufferInit& init);
  void deinit();

  D3D12_INDEX_BUFFER_VIEW IBView() const;
  ID3D12Resource* Resource() const
  {
    return InternalBuffer.m_Resource;
  }

  void* map();
  template <typename T> T* Map()
  {
    return reinterpret_cast<T*>(Map());
  };
  void mapAndSetData(const void* data, uint64_t numElements);
  void updateData(
      const void* srcData, uint64_t srcNumElements, uint64_t dstElemOffset);
  void multiUpdateData(
      const void* srcData[],
      uint64_t srcNumElements[],
      uint64_t dstElemOffset[],
      uint64_t numUpdates);

  void transition(
      ID3D12GraphicsCommandList* cmdList,
      D3D12_RESOURCE_STATES before,
      D3D12_RESOURCE_STATES after) const;
  void makeReadable(ID3D12GraphicsCommandList* cmdList) const;
  void makeWritable(ID3D12GraphicsCommandList* cmdList) const;
  void uavBarrier(ID3D12GraphicsCommandList* cmdList) const;

private:
  FormattedBuffer(const FormattedBuffer& other)
  {
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc(uint64_t bufferIdx) const;
  void updateDynamicSRV() const;
};
//---------------------------------------------------------------------------//
// StructuredBuffer
//---------------------------------------------------------------------------//
struct StructuredBufferInit
{
  uint64_t Stride = 0;
  uint64_t NumElements = 0;
  bool CreateUAV = false;
  bool UseCounter = false;
  bool Dynamic = false;
  bool CPUAccessible = false;
  const void* InitData = nullptr;
  D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
  ID3D12Heap* Heap = nullptr;
  uint64_t HeapOffset = 0;
  const wchar_t* Name = nullptr;
};
struct StructuredBuffer
{
  Buffer m_InternalBuffer;
  uint64_t m_Stride = 0;
  uint64_t m_NumElements = 0;
  uint32_t m_SrvIndex = uint32_t(-1);
  D3D12_CPU_DESCRIPTOR_HANDLE m_Uav = {};
  ID3D12Resource* m_CounterResource = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE m_CounterUAV = {};
  uint64_t m_GpuAddress = 0;

  StructuredBuffer()
  {
  }
  ~StructuredBuffer()
  {
  }

  void init(const StructuredBufferInit& p_Init);
  void deinit();

  D3D12_VERTEX_BUFFER_VIEW vbView() const;
  ID3D12Resource* resource() const
  {
    return m_InternalBuffer.m_Resource;
  }

  void* map();
  template <typename T> T* map()
  {
    return reinterpret_cast<T*>(map());
  }
  void mapAndSetData(const void* p_Data, uint64_t p_NumElements);
  void updateData(
      const void* p_SrcData,
      uint64_t p_SrcNumElements,
      uint64_t p_DstElemOffset);
  void multiUpdateData(
      const void* p_SrcData[],
      uint64_t p_SrcNumElements[],
      uint64_t p_DstElemOffset[],
      uint64_t p_NumUpdates);

  void transition(
      ID3D12GraphicsCommandList* p_CmdList,
      D3D12_RESOURCE_STATES p_Before,
      D3D12_RESOURCE_STATES p_After) const;
  void makeReadable(ID3D12GraphicsCommandList* p_CmdList) const;
  void makeWritable(ID3D12GraphicsCommandList* p_CmdList) const;
  void uavBarrier(ID3D12GraphicsCommandList* p_CmdList) const;

private:
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc(uint64_t p_BufferIdx) const;
  void updateDynamicSRV() const;
};
//---------------------------------------------------------------------------//
// Textures
//---------------------------------------------------------------------------//
struct Texture
{
  uint32_t SRV = uint32_t(-1);
  ID3D12Resource* Resource = nullptr;
  uint32_t Width = 0;
  uint32_t Height = 0;
  uint32_t Depth = 0;
  uint32_t NumMips = 0;
  uint32_t ArraySize = 0;
  DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
  bool Cubemap = false;

  Texture()
  {
  }
  ~Texture()
  {
    // assert(Resource == nullptr);
  }

  bool Valid() const
  {
    return Resource != nullptr;
  }

  void Shutdown()
  {
    SRVDescriptorHeap.FreePersistent(SRV);
    if (Resource != nullptr)
      Resource->Release();
  }

private:
  Texture(const Texture& other)
  {
  }
};
struct TextureBase
{
  uint32_t SRV = uint32_t(-1);
  ID3D12Resource* Resource = nullptr;
  uint32_t Width = 0;
  uint32_t Height = 0;
  uint32_t Depth = 0;
  uint32_t NumMips = 0;
  uint32_t ArraySize = 0;
  DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
  bool Cubemap = false;

  TextureBase()
  {
  }
  ~TextureBase()
  {
  }

  bool Valid() const
  {
    return Resource != nullptr;
  }

  void deinit()
  {
    SRVDescriptorHeap.FreePersistent(SRV);
    if (Resource != nullptr)
      Resource->Release();
  }
};
struct RenderTextureInit
{
  uint64_t Width = 0;
  uint64_t Height = 0;
  DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
  uint64_t MSAASamples = 1;
  uint64_t ArraySize = 1;
  bool CreateUAV = false;
  D3D12_RESOURCE_STATES InitialState =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  const wchar_t* Name = nullptr;
};
struct RenderTexture
{
  TextureBase m_Texture;
  D3D12_CPU_DESCRIPTOR_HANDLE m_RTV = {};
  D3D12_CPU_DESCRIPTOR_HANDLE m_UAV = {};
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_ArrayRTVs;
  uint32_t m_MSAASamples = 0;
  uint32_t m_MSAAQuality = 0;

  RenderTexture()
  {
  }
  ~RenderTexture()
  {
  }

  void init(const RenderTextureInit& p_Init);
  void deinit();

  void transition(
      ID3D12GraphicsCommandList* p_CmdList,
      D3D12_RESOURCE_STATES p_Before,
      D3D12_RESOURCE_STATES p_After,
      uint64_t p_MipLevel = uint64_t(-1),
      uint64_t p_ArraySlice = uint64_t(-1)) const;
  void makeReadable(
      ID3D12GraphicsCommandList* p_CmdList,
      uint64_t p_MipLevel = uint64_t(-1),
      uint64_t p_ArraySlice = uint64_t(-1)) const;
  void makeWritable(
      ID3D12GraphicsCommandList* p_CmdList,
      uint64_t p_MipLevel = uint64_t(-1),
      uint64_t p_ArraySlice = uint64_t(-1)) const;
  void uavBarrier(ID3D12GraphicsCommandList* p_CmdList) const;

  uint32_t srv() const
  {
    return m_Texture.SRV;
  }
  uint64_t width() const
  {
    return m_Texture.Width;
  }
  uint64_t height() const
  {
    return m_Texture.Height;
  }
  DXGI_FORMAT format() const
  {
    return m_Texture.Format;
  }
  ID3D12Resource* resource() const
  {
    return m_Texture.Resource;
  }
  uint64_t subResourceIndex(uint64_t p_MipLevel, uint64_t p_ArraySlice) const
  {
    return p_ArraySlice * m_Texture.NumMips + p_MipLevel;
  }
};
struct VolumeTexture
{
  // TODO
};

struct DepthBufferInit
{
    uint64_t Width = 0;
    uint64_t Height = 0;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    uint64_t MSAASamples = 1;
    uint64_t ArraySize = 1;
    D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    const wchar_t* Name = nullptr;
};
struct DepthBuffer
{
  Texture Texture;
  D3D12_CPU_DESCRIPTOR_HANDLE DSV = {};
  D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyDSV = {};
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> ArrayDSVs;
  uint32_t MSAASamples = 1;
  uint32_t MSAAQuality = 0;
  DXGI_FORMAT DSVFormat = DXGI_FORMAT_UNKNOWN;

  DepthBuffer();
  ~DepthBuffer();

  void init(const DepthBufferInit& init);
  void deinit();

  void transition(
      ID3D12GraphicsCommandList* cmdList,
      D3D12_RESOURCE_STATES before,
      D3D12_RESOURCE_STATES after,
      uint64_t arraySlice = uint64_t(-1)) const;
  void makeReadable(
      ID3D12GraphicsCommandList* cmdList, uint64_t arraySlice = uint64_t(-1)) const;
  void makeWritable(
      ID3D12GraphicsCommandList* cmdList, uint64_t arraySlice = uint64_t(-1)) const;

  uint32_t getSrv() const
  {
    return Texture.SRV;
  }
  uint64_t getWidth() const
  {
    return Texture.Width;
  }
  uint64_t getHeight() const
  {
    return Texture.Height;
  }
  ID3D12Resource* getResource() const
  {
    return Texture.Resource;
  }

private:
  DepthBuffer(const DepthBuffer& other)
  {
  }
};
