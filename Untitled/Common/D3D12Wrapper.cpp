//=================================================================================================
//  All code licensed under the MIT license
//=================================================================================================

#include "D3D12Wrapper.hpp"
#include "d3dx12.h"
#include <string> 

uint64_t g_CurrentCPUFrame = 0;
uint64_t g_CurrentGPUFrame = 0;
uint64_t g_CurrFrameIdx = 0; // CurrentCPUFrame % RenderLatency

ID3D12Device* g_Device = nullptr;

static const uint64_t g_UploadBufferSize = 32 * 1024 * 1024;

size_t bitsPerPixel(DXGI_FORMAT fmt)
{
  switch (static_cast<int>(fmt))
  {
  case DXGI_FORMAT_R32G32B32A32_TYPELESS:
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
  case DXGI_FORMAT_R32G32B32A32_UINT:
  case DXGI_FORMAT_R32G32B32A32_SINT:
    return 128;

  case DXGI_FORMAT_R32G32B32_TYPELESS:
  case DXGI_FORMAT_R32G32B32_FLOAT:
  case DXGI_FORMAT_R32G32B32_UINT:
  case DXGI_FORMAT_R32G32B32_SINT:
    return 96;

  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R16G16B16A16_UNORM:
  case DXGI_FORMAT_R16G16B16A16_UINT:
  case DXGI_FORMAT_R16G16B16A16_SNORM:
  case DXGI_FORMAT_R16G16B16A16_SINT:
  case DXGI_FORMAT_R32G32_TYPELESS:
  case DXGI_FORMAT_R32G32_FLOAT:
  case DXGI_FORMAT_R32G32_UINT:
  case DXGI_FORMAT_R32G32_SINT:
  case DXGI_FORMAT_R32G8X24_TYPELESS:
  case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
  case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
  case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
  case DXGI_FORMAT_Y416:
  case DXGI_FORMAT_Y210:
  case DXGI_FORMAT_Y216:
    return 64;

  case DXGI_FORMAT_R10G10B10A2_TYPELESS:
  case DXGI_FORMAT_R10G10B10A2_UNORM:
  case DXGI_FORMAT_R10G10B10A2_UINT:
  case DXGI_FORMAT_R11G11B10_FLOAT:
  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_R8G8B8A8_UINT:
  case DXGI_FORMAT_R8G8B8A8_SNORM:
  case DXGI_FORMAT_R8G8B8A8_SINT:
  case DXGI_FORMAT_R16G16_TYPELESS:
  case DXGI_FORMAT_R16G16_FLOAT:
  case DXGI_FORMAT_R16G16_UNORM:
  case DXGI_FORMAT_R16G16_UINT:
  case DXGI_FORMAT_R16G16_SNORM:
  case DXGI_FORMAT_R16G16_SINT:
  case DXGI_FORMAT_R32_TYPELESS:
  case DXGI_FORMAT_D32_FLOAT:
  case DXGI_FORMAT_R32_FLOAT:
  case DXGI_FORMAT_R32_UINT:
  case DXGI_FORMAT_R32_SINT:
  case DXGI_FORMAT_R24G8_TYPELESS:
  case DXGI_FORMAT_D24_UNORM_S8_UINT:
  case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
  case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
  case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
  case DXGI_FORMAT_R8G8_B8G8_UNORM:
  case DXGI_FORMAT_G8R8_G8B8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8X8_UNORM:
  case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8X8_TYPELESS:
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
  case DXGI_FORMAT_AYUV:
  case DXGI_FORMAT_Y410:
  case DXGI_FORMAT_YUY2:
    return 32;

  case DXGI_FORMAT_P010:
  case DXGI_FORMAT_P016:
    return 24;

  case DXGI_FORMAT_R8G8_TYPELESS:
  case DXGI_FORMAT_R8G8_UNORM:
  case DXGI_FORMAT_R8G8_UINT:
  case DXGI_FORMAT_R8G8_SNORM:
  case DXGI_FORMAT_R8G8_SINT:
  case DXGI_FORMAT_R16_TYPELESS:
  case DXGI_FORMAT_R16_FLOAT:
  case DXGI_FORMAT_D16_UNORM:
  case DXGI_FORMAT_R16_UNORM:
  case DXGI_FORMAT_R16_UINT:
  case DXGI_FORMAT_R16_SNORM:
  case DXGI_FORMAT_R16_SINT:
  case DXGI_FORMAT_B5G6R5_UNORM:
  case DXGI_FORMAT_B5G5R5A1_UNORM:
  case DXGI_FORMAT_A8P8:
  case DXGI_FORMAT_B4G4R4A4_UNORM:
    return 16;

  case DXGI_FORMAT_NV12:
  case DXGI_FORMAT_420_OPAQUE:
  case DXGI_FORMAT_NV11:
    return 12;

  case DXGI_FORMAT_R8_TYPELESS:
  case DXGI_FORMAT_R8_UNORM:
  case DXGI_FORMAT_R8_UINT:
  case DXGI_FORMAT_R8_SNORM:
  case DXGI_FORMAT_R8_SINT:
  case DXGI_FORMAT_A8_UNORM:
  case DXGI_FORMAT_BC2_TYPELESS:
  case DXGI_FORMAT_BC2_UNORM:
  case DXGI_FORMAT_BC2_UNORM_SRGB:
  case DXGI_FORMAT_BC3_TYPELESS:
  case DXGI_FORMAT_BC3_UNORM:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
  case DXGI_FORMAT_BC5_TYPELESS:
  case DXGI_FORMAT_BC5_UNORM:
  case DXGI_FORMAT_BC5_SNORM:
  case DXGI_FORMAT_BC6H_TYPELESS:
  case DXGI_FORMAT_BC6H_UF16:
  case DXGI_FORMAT_BC6H_SF16:
  case DXGI_FORMAT_BC7_TYPELESS:
  case DXGI_FORMAT_BC7_UNORM:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
  case DXGI_FORMAT_AI44:
  case DXGI_FORMAT_IA44:
  case DXGI_FORMAT_P8:
    return 8;

  case DXGI_FORMAT_R1_UNORM:
    return 1;

  case DXGI_FORMAT_BC1_TYPELESS:
  case DXGI_FORMAT_BC1_UNORM:
  case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC4_TYPELESS:
  case DXGI_FORMAT_BC4_UNORM:
  case DXGI_FORMAT_BC4_SNORM:
    return 4;

  default:
    return 0;
  }
}

//---------------------------------------------------------------------------//
// internal helper structs
//---------------------------------------------------------------------------//
enum ConvertRootParams : uint32_t
{
  ConvertParams_StandardDescriptors = 0,
  ConvertParams_UAV,
  ConvertParams_SRVIndex,

  NumConvertRootParams
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
    D3D_EXEC_CHECKED(
        g_Device->CreateFence(p_InitialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3DFence)));
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
      D3D_EXEC_CHECKED(m_D3DFence->SetEventOnCompletion(p_FenceValue, m_FenceEvent));
      WaitForSingleObject(m_FenceEvent, INFINITE);
    }
  }
  bool signaled(uint64_t p_FenceValue) { return m_D3DFence->GetCompletedValue() >= p_FenceValue; }
  void clear(uint64_t p_FenceValue) { m_D3DFence->Signal(p_FenceValue); }
};
//---------------------------------------------------------------------------//
// static/internal variables
//---------------------------------------------------------------------------//
static ID3D12GraphicsCommandList1* convertCmdList = nullptr;
static ID3D12CommandQueue* convertCmdQueue = nullptr;
static ID3D12CommandAllocator* convertCmdAllocator = nullptr;
static ID3D12RootSignature* convertRootSignature = nullptr;
static ID3D12PipelineState* convertPSO = nullptr;
static ID3D12PipelineState* convertArrayPSO = nullptr;
static ID3DBlobPtr convertCS;
static ID3DBlobPtr convertArrayCS;
static uint32_t convertTGSize = 16;
static Fence convertFence;
static ID3D12GraphicsCommandList1* readbackCmdList = nullptr;
static ID3D12CommandAllocator* readbackCmdAllocator = nullptr;
static Fence readbackFence;

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

// These are protected by UploadSubmissionLock
static constexpr int MaxUploadSubmissions = 1;
static uint64_t UploadBufferStart = 0;
static uint64_t UploadBufferUsed = 0;
static UploadSubmission UploadSubmissions[MaxUploadSubmissions];
static uint64_t UploadSubmissionStart = 0;
static uint64_t UploadSubmissionUsed = 0;

static const uint64_t TempBufferSize = 2 * 1024 * 1024;
static ID3D12Resource* TempFrameBuffers[RENDER_LATENCY] = {};
static uint8_t* TempFrameCPUMem[RENDER_LATENCY] = {};
static uint64_t TempFrameGPUMem[RENDER_LATENCY] = {};
static volatile int64_t TempFrameUsed = 0;
//---------------------------------------------------------------------------//
// various d3d helpers
//---------------------------------------------------------------------------//
uint32_t RTVDescriptorSize = 0;
uint32_t SRVDescriptorSize = 0;
uint32_t UAVDescriptorSize = 0;
uint32_t CBVDescriptorSize = 0;
uint32_t DSVDescriptorSize = 0;

DescriptorHeap RTVDescriptorHeap;
DescriptorHeap SRVDescriptorHeap;
DescriptorHeap DSVDescriptorHeap;
DescriptorHeap UAVDescriptorHeap;

uint32_t NullTexture2DSRV = uint32_t(-1);

static const uint64_t NumBlendStates = uint64_t(BlendState::NumValues);
static const uint64_t NumRasterizerStates = uint64_t(RasterizerState::NumValues);
static const uint64_t NumDepthStates = uint64_t(DepthState::NumValues);
static const uint64_t NumSamplerStates = uint64_t(SamplerState::NumValues);

static D3D12_BLEND_DESC BlendStateDescs[NumBlendStates] = {};
static D3D12_RASTERIZER_DESC RasterizerStateDescs[NumRasterizerStates] = {};
static D3D12_DEPTH_STENCIL_DESC DepthStateDescs[NumBlendStates] = {};
static D3D12_SAMPLER_DESC SamplerStateDescs[NumSamplerStates] = {};

static D3D12_DESCRIPTOR_RANGE1 StandardDescriptorRangeDescs[NumStandardDescriptorRanges] = {};

static void ClearFinishedUploads(uint64_t flushCount)
{
  const uint64_t start = UploadSubmissionStart;
  const uint64_t used = UploadSubmissionUsed;
  for (uint64_t i = 0; i < used; ++i)
  {
    const uint64_t idx = (start + i) % MaxUploadSubmissions;
    UploadSubmission& submission = UploadSubmissions[idx];
    assert(submission.Size > 0);
    assert(UploadBufferUsed >= submission.Size);

    // If the submission hasn't been sent to the GPU yet we can't wait for it
    if (submission.FenceValue == uint64_t(-1))
      return;

    if (i < flushCount)
      s_UploadFence.wait(submission.FenceValue);

    if (s_UploadFence.signaled(submission.FenceValue))
    {
      UploadSubmissionStart = (UploadSubmissionStart + 1) % MaxUploadSubmissions;
      UploadSubmissionUsed -= 1;
      UploadBufferStart = (UploadBufferStart + submission.Padding) % s_UploadBufferSize;
      assert(submission.Offset == UploadBufferStart);
      assert(UploadBufferStart + submission.Size <= s_UploadBufferSize);
      UploadBufferStart = (UploadBufferStart + submission.Size) % s_UploadBufferSize;
      UploadBufferUsed -= (submission.Size + submission.Padding);
      submission.reset();

      if (UploadBufferUsed == 0)
        UploadBufferStart = 0;
    }
    else
    {
      return;
    }
  }
}

void initializeHelpers(ID3D12Device* dev)
{
  g_Device = dev;

  RTVDescriptorHeap.Init(256, 0, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);
  SRVDescriptorHeap.Init(1024, 1024, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
  DSVDescriptorHeap.Init(256, 0, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);
  UAVDescriptorHeap.Init(256, 0, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);

  RTVDescriptorSize = RTVDescriptorHeap.DescriptorSize;
  SRVDescriptorSize = UAVDescriptorSize = CBVDescriptorSize = SRVDescriptorHeap.DescriptorSize;
  DSVDescriptorSize = DSVDescriptorHeap.DescriptorSize;

  // Standard descriptor ranges for binding to the arrays in
  // DescriptorTables.hlsl
  InsertStandardDescriptorRanges(StandardDescriptorRangeDescs);

  // Blend state initialization
  {
    D3D12_BLEND_DESC& blendDesc = BlendStateDescs[uint64_t(BlendState::Disabled)];
    blendDesc.RenderTarget[0].BlendEnable = false;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  }

  {
    D3D12_BLEND_DESC& blendDesc = BlendStateDescs[uint64_t(BlendState::Additive)];
    blendDesc.RenderTarget[0].BlendEnable = true;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  }

  {
    D3D12_BLEND_DESC& blendDesc = BlendStateDescs[uint64_t(BlendState::AlphaBlend)];
    blendDesc.RenderTarget[0].BlendEnable = true;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  }

  {
    D3D12_BLEND_DESC& blendDesc = BlendStateDescs[uint64_t(BlendState::PreMultiplied)];
    blendDesc.RenderTarget[0].BlendEnable = false;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  }

  {
    D3D12_BLEND_DESC& blendDesc = BlendStateDescs[uint64_t(BlendState::NoColorWrites)];
    blendDesc.RenderTarget[0].BlendEnable = false;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  }

  {
    D3D12_BLEND_DESC& blendDesc = BlendStateDescs[uint64_t(BlendState::PreMultipliedRGB)];
    blendDesc.RenderTarget[0].BlendEnable = true;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC1_COLOR;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  }

  // Rasterizer state initialization
  {
    D3D12_RASTERIZER_DESC& rastDesc = RasterizerStateDescs[uint64_t(RasterizerState::NoCull)];
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;
    rastDesc.DepthClipEnable = true;
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.MultisampleEnable = true;
  }

  {
    D3D12_RASTERIZER_DESC& rastDesc =
        RasterizerStateDescs[uint64_t(RasterizerState::FrontFaceCull)];
    rastDesc.CullMode = D3D12_CULL_MODE_FRONT;
    rastDesc.DepthClipEnable = true;
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.MultisampleEnable = true;
  }

  {
    D3D12_RASTERIZER_DESC& rastDesc = RasterizerStateDescs[uint64_t(RasterizerState::BackFaceCull)];
    rastDesc.CullMode = D3D12_CULL_MODE_BACK;
    rastDesc.DepthClipEnable = true;
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.MultisampleEnable = true;
  }

  {
    D3D12_RASTERIZER_DESC& rastDesc =
        RasterizerStateDescs[uint64_t(RasterizerState::BackFaceCullNoZClip)];
    rastDesc.CullMode = D3D12_CULL_MODE_BACK;
    rastDesc.DepthClipEnable = false;
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.MultisampleEnable = true;
  }

  {
    D3D12_RASTERIZER_DESC& rastDesc = RasterizerStateDescs[uint64_t(RasterizerState::NoCullNoMS)];
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;
    rastDesc.DepthClipEnable = true;
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.MultisampleEnable = false;
  }

  {
    D3D12_RASTERIZER_DESC& rastDesc = RasterizerStateDescs[uint64_t(RasterizerState::Wireframe)];
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;
    rastDesc.DepthClipEnable = true;
    rastDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
    rastDesc.MultisampleEnable = true;
  }

  // Depth state initialization
  {
    D3D12_DEPTH_STENCIL_DESC& dsDesc = DepthStateDescs[uint64_t(DepthState::Disabled)];
    dsDesc.DepthEnable = false;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  }

  {
    D3D12_DEPTH_STENCIL_DESC& dsDesc = DepthStateDescs[uint64_t(DepthState::Enabled)];
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  }

  {
    D3D12_DEPTH_STENCIL_DESC& dsDesc = DepthStateDescs[uint64_t(DepthState::Reversed)];
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  }

  {
    D3D12_DEPTH_STENCIL_DESC& dsDesc = DepthStateDescs[uint64_t(DepthState::WritesEnabled)];
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  }

  {
    D3D12_DEPTH_STENCIL_DESC& dsDesc = DepthStateDescs[uint64_t(DepthState::ReversedWritesEnabled)];
    dsDesc.DepthEnable = true;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  }

  // Sampler state initialization
  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::Linear)];

    sampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::LinearClamp)];

    sampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::LinearBorder)];

    sampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::Point)];

    sampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::Anisotropic)];

    sampDesc.Filter = D3D12_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::ShadowMap)];

    sampDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::ShadowMapPCF)];

    sampDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::ReversedShadowMap)];

    sampDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SAMPLER_DESC& sampDesc = SamplerStateDescs[uint64_t(SamplerState::ReversedShadowMapPCF)];

    sampDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] =
        sampDesc.BorderColor[3] = 0;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
  }

  {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    PersistentDescriptorAlloc srvAlloc = SRVDescriptorHeap.AllocatePersistent();
    for (uint32_t i = 0; i < SRVDescriptorHeap.NumHeaps; ++i)
      g_Device->CreateShaderResourceView(nullptr, &srvDesc, srvAlloc.Handles[i]);
    NullTexture2DSRV = srvAlloc.Index;
  }
}

void shutdownHelpers()
{
  SRVDescriptorHeap.FreePersistent(NullTexture2DSRV);

  RTVDescriptorHeap.Shutdown();
  SRVDescriptorHeap.Shutdown();
  DSVDescriptorHeap.Shutdown();
  UAVDescriptorHeap.Shutdown();
}

void endFrameHelpers()
{
  RTVDescriptorHeap.EndFrame();
  SRVDescriptorHeap.EndFrame();
  DSVDescriptorHeap.EndFrame();
  UAVDescriptorHeap.EndFrame();
}

void TransitionResource(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after,
    uint32_t subResource)
{
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = subResource;
  cmdList->ResourceBarrier(1, &barrier);
}

uint64_t GetResourceSize(
    const D3D12_RESOURCE_DESC& desc, uint32_t firstSubResource, uint32_t numSubResources)
{
  uint64_t size = 0;
  g_Device->GetCopyableFootprints(
      &desc, firstSubResource, numSubResources, 0, nullptr, nullptr, nullptr, &size);
  return size;
}

uint64_t
GetResourceSize(ID3D12Resource* resource, uint32_t firstSubResource, uint32_t numSubResources)
{
  D3D12_RESOURCE_DESC desc = resource->GetDesc();

  return GetResourceSize(desc, firstSubResource, numSubResources);
}

const D3D12_HEAP_PROPERTIES* GetDefaultHeapProps()
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

const D3D12_HEAP_PROPERTIES* GetUploadHeapProps()
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

const D3D12_HEAP_PROPERTIES* GetReadbackHeapProps()
{
  static D3D12_HEAP_PROPERTIES heapProps = {
      D3D12_HEAP_TYPE_READBACK,
      D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
      D3D12_MEMORY_POOL_UNKNOWN,
      0,
      0,
  };

  return &heapProps;
}

D3D12_BLEND_DESC GetBlendState(BlendState blendState)
{
  DEBUG_BREAK(uint64_t(blendState) < arrayCount(BlendStateDescs));
  return BlendStateDescs[uint64_t(blendState)];
}

D3D12_RASTERIZER_DESC GetRasterizerState(RasterizerState rasterizerState)
{
  DEBUG_BREAK(uint64_t(rasterizerState) < arrayCount(RasterizerStateDescs));
  return RasterizerStateDescs[uint64_t(rasterizerState)];
}

D3D12_DEPTH_STENCIL_DESC GetDepthState(DepthState depthState)
{
  DEBUG_BREAK(uint64_t(depthState) < arrayCount(DepthStateDescs));
  return DepthStateDescs[uint64_t(depthState)];
}

D3D12_SAMPLER_DESC GetSamplerState(SamplerState samplerState)
{
  DEBUG_BREAK(uint64_t(samplerState) < arrayCount(SamplerStateDescs));
  return SamplerStateDescs[uint64_t(samplerState)];
}

D3D12_STATIC_SAMPLER_DESC GetStaticSamplerState(
    SamplerState samplerState,
    uint32_t shaderRegister,
    uint32_t registerSpace,
    D3D12_SHADER_VISIBILITY visibility)
{
  DEBUG_BREAK(uint64_t(samplerState) < arrayCount(SamplerStateDescs));
  return ConvertToStaticSampler(
      SamplerStateDescs[uint64_t(samplerState)], shaderRegister, registerSpace, visibility);
}

D3D12_STATIC_SAMPLER_DESC ConvertToStaticSampler(
    const D3D12_SAMPLER_DESC& samplerDesc,
    uint32_t shaderRegister,
    uint32_t registerSpace,
    D3D12_SHADER_VISIBILITY visibility)
{
  D3D12_STATIC_SAMPLER_DESC staticDesc = {};
  staticDesc.Filter = samplerDesc.Filter;
  staticDesc.AddressU = samplerDesc.AddressU;
  staticDesc.AddressV = samplerDesc.AddressV;
  staticDesc.AddressW = samplerDesc.AddressW;
  staticDesc.MipLODBias = samplerDesc.MipLODBias;
  staticDesc.MaxAnisotropy = samplerDesc.MaxAnisotropy;
  staticDesc.ComparisonFunc = samplerDesc.ComparisonFunc;
  staticDesc.MinLOD = samplerDesc.MinLOD;
  staticDesc.MaxLOD = samplerDesc.MaxLOD;
  staticDesc.ShaderRegister = shaderRegister;
  staticDesc.RegisterSpace = registerSpace;
  staticDesc.ShaderVisibility = visibility;

  float borderColor[] = {
      samplerDesc.BorderColor[0],
      samplerDesc.BorderColor[1],
      samplerDesc.BorderColor[2],
      samplerDesc.BorderColor[3]};
  if (borderColor[0] == 1.0f && borderColor[1] == 1.0f && borderColor[2] == 1.0f &&
      borderColor[3] == 1.0f)
    staticDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
  else if (
      borderColor[0] == 0.0f && borderColor[1] == 0.0f && borderColor[2] == 0.0f &&
      borderColor[3] == 0.0f)
    staticDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  else
    staticDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;

  return staticDesc;
}

void SetViewport(
    ID3D12GraphicsCommandList* cmdList, uint64_t width, uint64_t height, float zMin, float zMax)
{
  D3D12_VIEWPORT viewport = {};
  viewport.Width = float(width);
  viewport.Height = float(height);
  viewport.MinDepth = zMin;
  viewport.MaxDepth = zMax;
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;

  D3D12_RECT scissorRect = {};
  scissorRect.left = 0;
  scissorRect.top = 0;
  scissorRect.right = uint32_t(width);
  scissorRect.bottom = uint32_t(height);

  cmdList->RSSetViewports(1, &viewport);
  cmdList->RSSetScissorRects(1, &scissorRect);
}

void CreateRootSignature(
    ID3D12RootSignature** rootSignature, const D3D12_ROOT_SIGNATURE_DESC1& desc)
{
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
  versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  versionedDesc.Desc_1_1 = desc;

  ID3DBlobPtr signature;
  ID3DBlobPtr error;
  HRESULT hr = D3D12SerializeVersionedRootSignature(&versionedDesc, &signature, &error);
  if (FAILED(hr))
  {
    DEBUG_BREAK(false); // TODO: print out the error
  }

  D3D_EXEC_CHECKED(g_Device->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(rootSignature)));
}

uint32_t DispatchSize(uint64_t numElements, uint64_t groupSize)
{
  DEBUG_BREAK(groupSize > 0);
  return uint32_t((numElements + (groupSize - 1)) / groupSize);
}

static const uint64_t MaxBindCount = 16;
static const uint32_t DescriptorCopyRanges[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static_assert(arrayCount(DescriptorCopyRanges) == MaxBindCount);

void SetDescriptorHeaps(ID3D12GraphicsCommandList* cmdList)
{
  ID3D12DescriptorHeap* heaps[] = {
      SRVDescriptorHeap.CurrentHeap(),
  };

  cmdList->SetDescriptorHeaps(UINT(arrayCount(heaps)), heaps);
}

D3D12_GPU_DESCRIPTOR_HANDLE
TempDescriptorTable(const D3D12_CPU_DESCRIPTOR_HANDLE* handles, uint64_t count)
{
  DEBUG_BREAK(count <= MaxBindCount);
  DEBUG_BREAK(count > 0);

  TempDescriptorAlloc tempAlloc = SRVDescriptorHeap.AllocateTemporary(uint32_t(count));

  uint32_t destRanges[1] = {uint32_t(count)};
  g_Device->CopyDescriptors(
      1,
      &tempAlloc.StartCPUHandle,
      destRanges,
      uint32_t(count),
      handles,
      DescriptorCopyRanges,
      SRVDescriptorHeap.HeapType);

  return tempAlloc.StartGPUHandle;
}

void BindTempDescriptorTable(
    ID3D12GraphicsCommandList* cmdList,
    const D3D12_CPU_DESCRIPTOR_HANDLE* handles,
    uint64_t count,
    uint32_t rootParameter,
    CmdListMode cmdListMode)
{
  D3D12_GPU_DESCRIPTOR_HANDLE tempTable = TempDescriptorTable(handles, count);

  if (cmdListMode == CmdListMode::Graphics)
    cmdList->SetGraphicsRootDescriptorTable(rootParameter, tempTable);
  else
    cmdList->SetComputeRootDescriptorTable(rootParameter, tempTable);
}
const D3D12_DESCRIPTOR_RANGE1* StandardDescriptorRanges()
{
  DEBUG_BREAK(SRVDescriptorSize != 0);
  return StandardDescriptorRangeDescs;
}
void InsertStandardDescriptorRanges(D3D12_DESCRIPTOR_RANGE1* ranges)
{
  uint32_t userStart = NumStandardDescriptorRanges - NumUserDescriptorRanges;
  for (uint32_t i = 0; i < NumStandardDescriptorRanges; ++i)
  {
    StandardDescriptorRangeDescs[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    StandardDescriptorRangeDescs[i].NumDescriptors = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    StandardDescriptorRangeDescs[i].BaseShaderRegister = 0;
    StandardDescriptorRangeDescs[i].RegisterSpace = i;
    StandardDescriptorRangeDescs[i].OffsetInDescriptorsFromTableStart = 0;
    StandardDescriptorRangeDescs[i].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    if (i >= userStart)
      StandardDescriptorRangeDescs[i].RegisterSpace = (i - userStart) + 100;
  }
}

void BindAsDescriptorTable(
    ID3D12GraphicsCommandList* cmdList,
    uint32_t descriptorIdx,
    uint32_t rootParameter,
    CmdListMode cmdListMode)
{
  DEBUG_BREAK(descriptorIdx != uint32_t(-1));
  D3D12_GPU_DESCRIPTOR_HANDLE handle = SRVDescriptorHeap.GPUHandleFromIndex(descriptorIdx);
  if (cmdListMode == CmdListMode::Compute)
    cmdList->SetComputeRootDescriptorTable(rootParameter, handle);
  else
    cmdList->SetGraphicsRootDescriptorTable(rootParameter, handle);
}

void BindStandardDescriptorTable(
    ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter, CmdListMode cmdListMode)
{
  D3D12_GPU_DESCRIPTOR_HANDLE handle = SRVDescriptorHeap.GPUStart[SRVDescriptorHeap.HeapIndex];
  if (cmdListMode == CmdListMode::Compute)
    cmdList->SetComputeRootDescriptorTable(rootParameter, handle);
  else
    cmdList->SetGraphicsRootDescriptorTable(rootParameter, handle);
}

MapResult AcquireTempBufferMem(uint64_t size, uint64_t alignment)
{
  uint64_t allocSize = size + alignment;
  uint64_t offset = InterlockedAdd64(&TempFrameUsed, allocSize) - allocSize;
  if (alignment > 0)
    offset = alignUp(offset, alignment);
  assert(offset + size <= TempBufferSize);

  MapResult result;
  result.CpuAddress = TempFrameCPUMem[g_CurrFrameIdx] + offset;
  result.GpuAddress = TempFrameGPUMem[g_CurrFrameIdx] + offset;
  result.ResourceOffset = offset;
  result.Resource = TempFrameBuffers[g_CurrFrameIdx];

  return result;
}

TempBuffer TempConstantBuffer(uint64_t cbSize, bool makeDescriptor)
{
  assert(cbSize > 0);
  MapResult tempMem = AcquireTempBufferMem(cbSize, ConstantBufferAlignment);
  TempBuffer tempBuffer;
  tempBuffer.CPUAddress = tempMem.CpuAddress;
  tempBuffer.GPUAddress = tempMem.GpuAddress;
  if (makeDescriptor)
  {
    TempDescriptorAlloc cbvAlloc = SRVDescriptorHeap.AllocateTemporary(1);
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = tempMem.GpuAddress;
    cbvDesc.SizeInBytes = alignUp<uint32_t>(uint32_t(cbSize), ConstantBufferAlignment);
    g_Device->CreateConstantBufferView(&cbvDesc, cbvAlloc.StartCPUHandle);
    tempBuffer.DescriptorIndex = cbvAlloc.StartIndex;
  }

  return tempBuffer;
}

void BindTempConstantBuffer(
    ID3D12GraphicsCommandList* cmdList,
    const void* cbData,
    uint64_t cbSize,
    uint32_t rootParameter,
    CmdListMode cmdListMode)
{
  TempBuffer tempBuffer = TempConstantBuffer(cbSize, false);
  memcpy(tempBuffer.CPUAddress, cbData, cbSize);

  if (cmdListMode == CmdListMode::Graphics)
    cmdList->SetGraphicsRootConstantBufferView(rootParameter, tempBuffer.GPUAddress);
  else
    cmdList->SetComputeRootConstantBufferView(rootParameter, tempBuffer.GPUAddress);
}

DescriptorHeap::~DescriptorHeap() { DEBUG_BREAK(Heaps[0] == nullptr); }

void DescriptorHeap::Init(
    uint32_t numPersistent,
    uint32_t numTemporary,
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    bool shaderVisible)
{
  Shutdown();

  uint32_t totalNumDescriptors = numPersistent + numTemporary;
  DEBUG_BREAK(totalNumDescriptors > 0);

  NumPersistent = numPersistent;
  NumTemporary = numTemporary;
  HeapType = heapType;
  ShaderVisible = shaderVisible;
  if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || heapType == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    ShaderVisible = false;

  NumHeaps = ShaderVisible ? 2 : 1;

  DeadList.resize(numPersistent);
  for (uint32_t i = 0; i < numPersistent; ++i)
    DeadList[i] = uint32_t(i);

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = uint32_t(totalNumDescriptors);
  heapDesc.Type = heapType;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (ShaderVisible)
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  for (uint32_t i = 0; i < NumHeaps; ++i)
  {
    D3D_EXEC_CHECKED(g_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&Heaps[i])));
    CPUStart[i] = Heaps[i]->GetCPUDescriptorHandleForHeapStart();
    if (ShaderVisible)
      GPUStart[i] = Heaps[i]->GetGPUDescriptorHandleForHeapStart();
  }

  DescriptorSize = g_Device->GetDescriptorHandleIncrementSize(heapType);
}

void DescriptorHeap::Shutdown()
{
  DEBUG_BREAK(PersistentAllocated == 0);
  for (uint64_t i = 0; i < arrayCount(Heaps); ++i)
  {
    if (Heaps[i] != nullptr)
    {
      Heaps[i]->Release();
      Heaps[i] = nullptr;
    }
  }
}

PersistentDescriptorAlloc DescriptorHeap::AllocatePersistent()
{
  DEBUG_BREAK(Heaps[0] != nullptr);

  AcquireSRWLockExclusive(&Lock);

  DEBUG_BREAK(PersistentAllocated < NumPersistent);
  uint32_t idx = DeadList[PersistentAllocated];
  ++PersistentAllocated;

  ReleaseSRWLockExclusive(&Lock);

  PersistentDescriptorAlloc alloc;
  alloc.Index = idx;
  for (uint32_t i = 0; i < NumHeaps; ++i)
  {
    alloc.Handles[i] = CPUStart[i];
    alloc.Handles[i].ptr += idx * DescriptorSize;
  }

  return alloc;
}

void DescriptorHeap::FreePersistent(uint32_t& idx)
{
  if (idx == uint32_t(-1))
    return;

  DEBUG_BREAK(idx < NumPersistent);
  DEBUG_BREAK(Heaps[0] != nullptr);

  AcquireSRWLockExclusive(&Lock);

  DEBUG_BREAK(PersistentAllocated > 0);
  DeadList[PersistentAllocated - 1] = idx;
  --PersistentAllocated;

  ReleaseSRWLockExclusive(&Lock);

  idx = uint32_t(-1);
}

void DescriptorHeap::FreePersistent(D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
  DEBUG_BREAK(NumHeaps == 1);
  if (handle.ptr != 0)
  {
    uint32_t idx = IndexFromHandle(handle);
    FreePersistent(idx);
    handle = {};
  }
}

void DescriptorHeap::FreePersistent(D3D12_GPU_DESCRIPTOR_HANDLE& handle)
{
  DEBUG_BREAK(NumHeaps == 1);
  if (handle.ptr != 0)
  {
    uint32_t idx = IndexFromHandle(handle);
    FreePersistent(idx);
    handle = {};
  }
}

TempDescriptorAlloc DescriptorHeap::AllocateTemporary(uint32_t count)
{
  DEBUG_BREAK(Heaps[0] != nullptr);
  DEBUG_BREAK(count > 0);

  uint32_t tempIdx = uint32_t(InterlockedAdd64(&TemporaryAllocated, count)) - count;
  DEBUG_BREAK(tempIdx < NumTemporary);

  uint32_t finalIdx = tempIdx + NumPersistent;

  TempDescriptorAlloc alloc;
  alloc.StartCPUHandle = CPUStart[HeapIndex];
  alloc.StartCPUHandle.ptr += finalIdx * DescriptorSize;
  alloc.StartGPUHandle = GPUStart[HeapIndex];
  alloc.StartGPUHandle.ptr += finalIdx * DescriptorSize;
  alloc.StartIndex = finalIdx;

  return alloc;
}

void DescriptorHeap::EndFrame()
{
  DEBUG_BREAK(Heaps[0] != nullptr);
  TemporaryAllocated = 0;
  HeapIndex = (HeapIndex + 1) % NumHeaps;
}

D3D12_CPU_DESCRIPTOR_HANDLE
DescriptorHeap::CPUHandleFromIndex(uint32_t descriptorIdx) const
{
  return CPUHandleFromIndex(descriptorIdx, HeapIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE
DescriptorHeap::GPUHandleFromIndex(uint32_t descriptorIdx) const
{
  return GPUHandleFromIndex(descriptorIdx, HeapIndex);
}

D3D12_CPU_DESCRIPTOR_HANDLE
DescriptorHeap::CPUHandleFromIndex(uint32_t descriptorIdx, uint64_t heapIdx) const
{
  DEBUG_BREAK(Heaps[0] != nullptr);
  DEBUG_BREAK(heapIdx < NumHeaps);
  DEBUG_BREAK(descriptorIdx < TotalNumDescriptors());
  D3D12_CPU_DESCRIPTOR_HANDLE handle = CPUStart[heapIdx];
  handle.ptr += descriptorIdx * DescriptorSize;
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE
DescriptorHeap::GPUHandleFromIndex(uint32_t descriptorIdx, uint64_t heapIdx) const
{
  DEBUG_BREAK(Heaps[0] != nullptr);
  DEBUG_BREAK(heapIdx < NumHeaps);
  DEBUG_BREAK(descriptorIdx < TotalNumDescriptors());
  DEBUG_BREAK(ShaderVisible);
  D3D12_GPU_DESCRIPTOR_HANDLE handle = GPUStart[heapIdx];
  handle.ptr += descriptorIdx * DescriptorSize;
  return handle;
}

uint32_t DescriptorHeap::IndexFromHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
  DEBUG_BREAK(Heaps[0] != nullptr);
  DEBUG_BREAK(handle.ptr >= CPUStart[HeapIndex].ptr);
  DEBUG_BREAK(handle.ptr < CPUStart[HeapIndex].ptr + DescriptorSize * TotalNumDescriptors());
  DEBUG_BREAK((handle.ptr - CPUStart[HeapIndex].ptr) % DescriptorSize == 0);
  return uint32_t(handle.ptr - CPUStart[HeapIndex].ptr) / DescriptorSize;
}

uint32_t DescriptorHeap::IndexFromHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
  DEBUG_BREAK(Heaps[0] != nullptr);
  DEBUG_BREAK(handle.ptr >= GPUStart[HeapIndex].ptr);
  DEBUG_BREAK(handle.ptr < GPUStart[HeapIndex].ptr + DescriptorSize * TotalNumDescriptors());
  DEBUG_BREAK((handle.ptr - GPUStart[HeapIndex].ptr) % DescriptorSize == 0);
  return uint32_t(handle.ptr - GPUStart[HeapIndex].ptr) / DescriptorSize;
}

ID3D12DescriptorHeap* DescriptorHeap::CurrentHeap() const
{
  DEBUG_BREAK(Heaps[0] != nullptr);
  return Heaps[HeapIndex];
}

//---------------------------------------------------------------------------//
// internal upload helpers
//---------------------------------------------------------------------------//

static UploadSubmission* _allocUploadSubmission(uint64_t p_Size)
{
  assert(UploadSubmissionUsed <= MaxUploadSubmissions);
  if (UploadSubmissionUsed == MaxUploadSubmissions)
    return nullptr;

  const uint64_t submissionIdx =
      (UploadSubmissionStart + UploadSubmissionUsed) % MaxUploadSubmissions;
  assert(UploadSubmissions[submissionIdx].Size == 0);

  assert(UploadBufferUsed <= s_UploadBufferSize);
  if (p_Size > (s_UploadBufferSize - UploadBufferUsed))
    return nullptr;

  const uint64_t start = UploadBufferStart;
  const uint64_t end = UploadBufferStart + UploadBufferUsed;
  uint64_t allocOffset = uint64_t(-1);
  uint64_t padding = 0;
  if (end < s_UploadBufferSize)
  {
    const uint64_t endAmt = s_UploadBufferSize - end;
    if (endAmt >= p_Size)
    {
      allocOffset = end;
    }
    else if (start >= p_Size)
    {
      // Wrap around to the beginning
      allocOffset = 0;
      UploadBufferUsed += endAmt;
      padding = endAmt;
    }
  }
  else
  {
    const uint64_t wrappedEnd = end % s_UploadBufferSize;
    if ((start - wrappedEnd) >= p_Size)
      allocOffset = wrappedEnd;
  }

  if (allocOffset == uint64_t(-1))
    return nullptr;

  UploadSubmissionUsed += 1;
  UploadBufferUsed += p_Size;

  UploadSubmission* submission = &UploadSubmissions[submissionIdx];
  submission->Offset = allocOffset;
  submission->Size = p_Size;
  submission->FenceValue = uint64_t(-1);
  submission->Padding = padding;

  return submission;
}
void initializeUpload(ID3D12Device* dev)
{
  g_Device = dev;
  DEBUG_BREAK(g_Device != nullptr);

  for (uint64_t i = 0; i < MaxUploadSubmissions; ++i)
  {
    UploadSubmission& submission = UploadSubmissions[i];
    D3D_EXEC_CHECKED(g_Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&submission.CmdAllocator)));
    D3D_EXEC_CHECKED(g_Device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_COPY,
        submission.CmdAllocator,
        nullptr,
        IID_PPV_ARGS(&submission.CmdList)));
    D3D_EXEC_CHECKED(submission.CmdList->Close());
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  D3D_EXEC_CHECKED(g_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&s_UploadCmdQueue)));
  s_UploadCmdQueue->SetName(L"Upload Copy Queue");

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
      GetUploadHeapProps(),
      D3D12_HEAP_FLAG_NONE,
      &resourceDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&s_UploadBuffer)));

  D3D12_RANGE readRange = {};
  D3D_EXEC_CHECKED(
      s_UploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&s_UploadBufferCPUAddr)));

  // Temporary buffer memory that swaps every frame
  resourceDesc.Width = uint32_t(TempBufferSize);

  for (uint64_t i = 0; i < RENDER_LATENCY; ++i)
  {
    D3D_EXEC_CHECKED(g_Device->CreateCommittedResource(
        GetUploadHeapProps(),
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&TempFrameBuffers[i])));

    D3D_EXEC_CHECKED(
        TempFrameBuffers[i]->Map(0, &readRange, reinterpret_cast<void**>(&TempFrameCPUMem[i])));
    TempFrameGPUMem[i] = TempFrameBuffers[i]->GetGPUVirtualAddress();
  }

  // Texture conversion resources

  // Texture conversion resources
  D3D_EXEC_CHECKED(g_Device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&convertCmdAllocator)));
  D3D_EXEC_CHECKED(g_Device->CreateCommandList(
      0,
      D3D12_COMMAND_LIST_TYPE_COMPUTE,
      convertCmdAllocator,
      nullptr,
      IID_PPV_ARGS(&convertCmdList)));
  D3D_EXEC_CHECKED(convertCmdList->Close());
  D3D_EXEC_CHECKED(convertCmdList->Reset(convertCmdAllocator, nullptr));

  D3D12_COMMAND_QUEUE_DESC convertQueueDesc = {};
  convertQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  convertQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  D3D_EXEC_CHECKED(g_Device->CreateCommandQueue(&convertQueueDesc, IID_PPV_ARGS(&convertCmdQueue)));

  // Decode texture shader
  {
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
    shaderPath += L"Shaders\\DecodeTextureCS.hlsl";

    std::string s = std::to_string(convertTGSize);
    char const* tgSize = s.c_str(); // use char const* as target type
    const D3D_SHADER_MACRO defines[] = {{"TGSize_", tgSize}, {NULL, NULL}};
    compileShader(
        "decode texture",
        shaderPath.c_str(),
        defines,
        compileFlags,
        ShaderType::Compute,
        "DecodeTextureCS",
        convertCS);
    compileShader(
        "decode texture array",
        shaderPath.c_str(),
        defines,
        compileFlags,
        ShaderType::Compute,
        "DecodeTextureArrayCS",
        convertArrayCS);
  }

  {
    D3D12_DESCRIPTOR_RANGE1 uavRanges[1] = {};
    uavRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRanges[0].NumDescriptors = 1;
    uavRanges[0].BaseShaderRegister = 0;
    uavRanges[0].RegisterSpace = 0;
    uavRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 rootParameters[NumConvertRootParams] = {};
    rootParameters[ConvertParams_StandardDescriptors].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[ConvertParams_StandardDescriptors].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[ConvertParams_StandardDescriptors].DescriptorTable.pDescriptorRanges =
        StandardDescriptorRanges();
    rootParameters[ConvertParams_StandardDescriptors].DescriptorTable.NumDescriptorRanges =
        NumStandardDescriptorRanges;

    rootParameters[ConvertParams_UAV].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[ConvertParams_UAV].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[ConvertParams_UAV].DescriptorTable.pDescriptorRanges = uavRanges;
    rootParameters[ConvertParams_UAV].DescriptorTable.NumDescriptorRanges = arrayCount32(uavRanges);

    rootParameters[ConvertParams_SRVIndex].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[ConvertParams_SRVIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[ConvertParams_SRVIndex].Constants.Num32BitValues = 1;
    rootParameters[ConvertParams_SRVIndex].Constants.RegisterSpace = 0;
    rootParameters[ConvertParams_SRVIndex].Constants.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = arrayCount32(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    CreateRootSignature(&convertRootSignature, rootSignatureDesc);
  }

  {
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.CS = CD3DX12_SHADER_BYTECODE(convertCS.GetInterfacePtr());
    psoDesc.pRootSignature = convertRootSignature;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&convertPSO));

    psoDesc.CS = CD3DX12_SHADER_BYTECODE(convertArrayCS.GetInterfacePtr());
    g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&convertArrayPSO));
  }

  convertFence.init(0);

  // Readback resources
  D3D_EXEC_CHECKED(g_Device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&readbackCmdAllocator)));
  D3D_EXEC_CHECKED(g_Device->CreateCommandList(
      0,
      D3D12_COMMAND_LIST_TYPE_COPY,
      readbackCmdAllocator,
      nullptr,
      IID_PPV_ARGS(&readbackCmdList)));
  D3D_EXEC_CHECKED(readbackCmdList->Close());
  D3D_EXEC_CHECKED(readbackCmdList->Reset(readbackCmdAllocator, nullptr));

  readbackFence.init(0);
}
void shutdownUpload()
{
  for (uint64_t i = 0; i < arrayCount(TempFrameBuffers); ++i)
    TempFrameBuffers[i]->Release();

  if (s_UploadBuffer != nullptr)
    s_UploadBuffer->Release();
  if (s_UploadCmdQueue != nullptr)
    s_UploadCmdQueue->Release();
  if (s_UploadSubmission.CmdAllocator != nullptr)
    s_UploadSubmission.CmdAllocator->Release();
  if (s_UploadSubmission.CmdList != nullptr)
    s_UploadSubmission.CmdList->Release();

  for (uint64_t i = 0; i < MaxUploadSubmissions; ++i)
  {
    UploadSubmissions[i].CmdAllocator->Release();
    UploadSubmissions[i].CmdList->Release();
  }

  convertCmdAllocator->Release();
  convertCmdList->Release();
  convertCmdQueue->Release();
  convertPSO->Release();
  convertArrayPSO->Release();
  convertRootSignature->Release();

  readbackCmdAllocator->Release();
  readbackCmdList->Release();
}
void EndFrame_Upload(ID3D12CommandQueue* p_GfxQueue)
{
  // If we can grab the lock, try to clear out any completed submissions
  if (TryAcquireSRWLockExclusive(&s_UploadSubmissionLock))
  {
    ClearFinishedUploads(0);

    ReleaseSRWLockExclusive(&s_UploadSubmissionLock);
  }

  {
    AcquireSRWLockExclusive(&s_UploadQueueLock);

    // Make sure to sync on any pending uploads
    ClearFinishedUploads(0);
    p_GfxQueue->Wait(s_UploadFence.m_D3DFence, s_UploadFenceValue);

    ReleaseSRWLockExclusive(&s_UploadQueueLock);
  }

  TempFrameUsed = 0;
}

void convertAndReadbackTexture(
    const Texture& texture, DXGI_FORMAT outputFormat, ReadbackBuffer& readbackBuffer)
{
  assert(convertCmdList != nullptr);
  assert(texture.Valid());
  assert(texture.Depth == 1);

  // Create a buffer for the CS to write flattened, converted texture data into
  FormattedBufferInit initParams;
  initParams.Format = outputFormat;
  initParams.NumElements = texture.Width * texture.Height * texture.ArraySize;
  initParams.CreateUAV = true;

  FormattedBuffer convertBuffer;
  convertBuffer.init(initParams);

  // Run the conversion compute shader
  SetDescriptorHeaps(convertCmdList);
  convertCmdList->SetComputeRootSignature(convertRootSignature);
  convertCmdList->SetPipelineState(texture.ArraySize > 1 ? convertArrayPSO : convertPSO);

  BindStandardDescriptorTable(
      convertCmdList, ConvertParams_StandardDescriptors, CmdListMode::Compute);

  D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {convertBuffer.UAV};
  BindTempDescriptorTable(
      convertCmdList, uavs, arrayCount(uavs), ConvertParams_UAV, CmdListMode::Compute);

  convertCmdList->SetComputeRoot32BitConstant(ConvertParams_SRVIndex, texture.SRV, 0);

  uint32_t dispatchX = DispatchSize(texture.Width, convertTGSize);
  uint32_t dispatchY = DispatchSize(texture.Height, convertTGSize);
  uint32_t dispatchZ = texture.ArraySize;
  convertCmdList->Dispatch(dispatchX, dispatchY, dispatchZ);

  convertBuffer.transition(
      convertCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

  // Execute the conversion command list and signal a fence
  D3D_EXEC_CHECKED(convertCmdList->Close());
  ID3D12CommandList* cmdLists[1] = {convertCmdList};
  convertCmdQueue->ExecuteCommandLists(1, cmdLists);

  convertFence.signal(convertCmdQueue, 1);

  // Have the readback wait for conversion finish, and then have it copy the data to a readback
  // buffer
  ID3D12CommandQueue* readbackQueue = s_UploadCmdQueue;
  readbackQueue->Wait(convertFence.m_D3DFence, 1);

  readbackBuffer.deinit();
  readbackBuffer.init(convertBuffer.InternalBuffer.m_Size);

  readbackCmdList->CopyResource(readbackBuffer.Resource, convertBuffer.InternalBuffer.m_Resource);

  // Execute the readback command list and signal a fence
  D3D_EXEC_CHECKED(readbackCmdList->Close());
  cmdLists[0] = readbackCmdList;
  readbackQueue->ExecuteCommandLists(1, cmdLists);

  readbackFence.signal(readbackQueue, 1);

  readbackFence.wait(1);

  // Clean up
  convertFence.clear(0);
  readbackFence.clear(0);

  D3D_EXEC_CHECKED(convertCmdAllocator->Reset());
  D3D_EXEC_CHECKED(convertCmdList->Reset(convertCmdAllocator, nullptr));

  D3D_EXEC_CHECKED(readbackCmdAllocator->Reset());
  D3D_EXEC_CHECKED(readbackCmdList->Reset(readbackCmdAllocator, nullptr));

  convertBuffer.deinit();
}

UploadContext resourceUploadBegin(uint64_t p_Size)
{
  DEBUG_BREAK(g_Device != nullptr);

  p_Size = alignUp<uint64_t>(p_Size, 512);
  DEBUG_BREAK(p_Size <= g_UploadBufferSize);
  DEBUG_BREAK(p_Size > 0);

  UploadSubmission* submission = nullptr;
  {
    AcquireSRWLockExclusive(&s_UploadSubmissionLock);

    ClearFinishedUploads(0);

    submission = _allocUploadSubmission(p_Size);
    while (submission == nullptr)
    {
      ClearFinishedUploads(1);
      submission = _allocUploadSubmission(p_Size);
    }

    ReleaseSRWLockExclusive(&s_UploadSubmissionLock);
  }

  D3D_EXEC_CHECKED(submission->CmdAllocator->Reset());
  D3D_EXEC_CHECKED(submission->CmdList->Reset(submission->CmdAllocator, nullptr));

  UploadContext context;
  context.CmdList = submission->CmdList;
  context.Resource = s_UploadBuffer;
  context.CpuAddress = s_UploadBufferCPUAddr + submission->Offset;
  context.ResourceOffset = submission->Offset;
  context.Submission = submission;

  return context;
}

void resourceUploadEnd(UploadContext& context)
{
  DEBUG_BREAK(context.CmdList != nullptr);
  DEBUG_BREAK(context.Submission != nullptr);
  UploadSubmission* submission = reinterpret_cast<UploadSubmission*>(context.Submission);

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
  resourceDesc.Flags =
      p_AllowUav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
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
        p_Heap, p_HeapOffset, &resourceDesc, resourceState, nullptr, IID_PPV_ARGS(&m_Resource)));
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
    D3D_EXEC_CHECKED(m_Resource->Map(0, &readRange, reinterpret_cast<void**>(&m_CpuAddress)));
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
    UploadContext uploadContext = resourceUploadBegin(resourceDesc.Width);

    memcpy(uploadContext.CpuAddress, p_InitData, p_Size);
    if (p_Dynamic)
      memcpy((uint8_t*)uploadContext.CpuAddress + p_Size, p_InitData, p_Size);

    uploadContext.CmdList->CopyBufferRegion(
        m_Resource, 0, uploadContext.Resource, uploadContext.ResourceOffset, p_Size);

    resourceUploadEnd(uploadContext);
  }
}
void Buffer::deinit()
{

  if (m_Resource == nullptr)
    return;

  if (g_Device == nullptr)
    m_Resource->Release();

  // TODO: deferred release!
  // right now the following leads to some access violations on shutdown

  IUnknown* base = m_Resource;
  if (base != nullptr)
    base->Release();
  m_Resource = nullptr;
}
MapResult Buffer::map()
{
  DEBUG_BREAK(initialized());
  DEBUG_BREAK(m_Dynamic);
  DEBUG_BREAK(m_CpuAccessible);

  // Make sure that we do this at most once per-frame
  // because we're calling update on winproc callback in case there is any unforeseen issue, this
  // might be called more than necessary!
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
uint64_t Buffer::updateData(const void* p_SrcData, uint64_t p_SrcSize, uint64_t p_DstOffset)
{
  return multiUpdateData(&p_SrcData, &p_SrcSize, &p_DstOffset, 1);
}
uint64_t Buffer::multiUpdateData(
    const void* p_SrcData[], uint64_t p_SrcSize[], uint64_t p_DstOffset[], uint64_t p_NumUpdates)
{
  DEBUG_BREAK(m_Dynamic);
  DEBUG_BREAK(m_CpuAccessible == false);
  DEBUG_BREAK(p_NumUpdates > 0);

  // Make sure that we do this at most once per-frame
  // because we're calling update on winproc callback in case there is any unforeseen issue, this
  // might be called more than necessary!
  DEBUG_BREAK(m_UploadFrame != g_CurrentCPUFrame);
  m_UploadFrame = g_CurrentCPUFrame;

  // Cycle to the next buffer
  m_CurrBuffer = (g_CurrentCPUFrame + 1) % RENDER_LATENCY;

  uint64_t currOffset = m_CurrBuffer * m_Size;

  uint64_t totalUpdateSize = 0;
  for (uint64_t i = 0; i < p_NumUpdates; ++i)
    totalUpdateSize += p_SrcSize[i];

  UploadContext uploadContext = resourceUploadBegin(totalUpdateSize);

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

  resourceUploadEnd(uploadContext);

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
// FormattedBuffer
//---------------------------------------------------------------------------//
FormattedBuffer::FormattedBuffer() {}

FormattedBuffer::~FormattedBuffer()
{
  assert(NumElements == 0);
  // deinit();
}

void FormattedBuffer::init(const FormattedBufferInit& init)
{
  DEBUG_BREAK(g_Device != nullptr);

  deinit();

  assert(init.Format != DXGI_FORMAT_UNKNOWN);
  assert(init.NumElements > 0);
  Stride = bitsPerPixel(init.Format) / 8;
  NumElements = init.NumElements;
  Format = init.Format;

  InternalBuffer.init(
      Stride * NumElements,
      Stride,
      init.Dynamic,
      init.CPUAccessible,
      init.CreateUAV,
      init.InitData,
      init.InitialState,
      init.Heap,
      init.HeapOffset,
      init.Name);
  GPUAddress = InternalBuffer.m_GpuAddress;

  PersistentDescriptorAlloc srvAlloc = SRVDescriptorHeap.AllocatePersistent();
  SRV = srvAlloc.Index;

  // Start off all SRV's pointing to the first buffer
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = srvDesc(0);
  for (uint32_t i = 0; i < arrayCount32(srvAlloc.Handles); ++i)
    g_Device->CreateShaderResourceView(InternalBuffer.m_Resource, &desc, srvAlloc.Handles[i]);

  if (init.CreateUAV)
  {
    assert(init.Dynamic == false);

    UAV = UAVDescriptorHeap.AllocatePersistent().Handles[0];

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = Format;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uavDesc.Buffer.NumElements = uint32_t(NumElements);

    g_Device->CreateUnorderedAccessView(InternalBuffer.m_Resource, nullptr, &uavDesc, UAV);
  }
}

void FormattedBuffer::deinit()
{
  SRVDescriptorHeap.FreePersistent(SRV);
  UAVDescriptorHeap.FreePersistent(UAV);
  InternalBuffer.deinit();
  Stride = 0;
  NumElements = 0;
}

D3D12_INDEX_BUFFER_VIEW FormattedBuffer::IBView() const
{
  assert(Format == DXGI_FORMAT_R16_UINT || Format == DXGI_FORMAT_R32_UINT);
  D3D12_INDEX_BUFFER_VIEW ibView = {};
  ibView.BufferLocation = GPUAddress;
  ibView.Format = Format;
  ibView.SizeInBytes = uint32_t(InternalBuffer.m_Size);
  return ibView;
}

void* FormattedBuffer::map()
{
  MapResult mapResult = InternalBuffer.map();

  GPUAddress = mapResult.GpuAddress;

  updateDynamicSRV();

  return mapResult.CpuAddress;
}

void FormattedBuffer::mapAndSetData(const void* data, uint64_t numElements)
{
  assert(numElements <= NumElements);
  void* cpuAddr = map();
  memcpy(cpuAddr, data, numElements * Stride);
}

void FormattedBuffer::updateData(
    const void* srcData, uint64_t srcNumElements, uint64_t dstElemOffset)
{
  GPUAddress = InternalBuffer.updateData(srcData, srcNumElements * Stride, dstElemOffset * Stride);

  updateDynamicSRV();
}

void FormattedBuffer::multiUpdateData(
    const void* srcData[], uint64_t srcNumElements[], uint64_t dstElemOffset[], uint64_t numUpdates)
{
  uint64_t srcSizes[16];
  uint64_t dstOffsets[16];
  assert(numUpdates <= arrayCount<uint64_t>(srcSizes));
  for (uint64_t i = 0; i < numUpdates; ++i)
  {
    srcSizes[i] = srcNumElements[i] * Stride;
    dstOffsets[i] = dstElemOffset[i] * Stride;
  }

  GPUAddress = InternalBuffer.multiUpdateData(srcData, srcSizes, dstOffsets, numUpdates);

  updateDynamicSRV();
}

void FormattedBuffer::transition(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) const
{
  InternalBuffer.transition(cmdList, before, after);
}

void FormattedBuffer::makeReadable(ID3D12GraphicsCommandList* cmdList) const
{
  InternalBuffer.makeReadable(cmdList);
}

void FormattedBuffer::makeWritable(ID3D12GraphicsCommandList* cmdList) const
{
  InternalBuffer.makeWritable(cmdList);
}

void FormattedBuffer::uavBarrier(ID3D12GraphicsCommandList* cmdList) const
{
  InternalBuffer.uavBarrier(cmdList);
}

D3D12_SHADER_RESOURCE_VIEW_DESC
FormattedBuffer::srvDesc(uint64_t bufferIdx) const
{
  assert(bufferIdx == 0 || InternalBuffer.m_Dynamic);
  assert(bufferIdx < RENDER_LATENCY);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = Format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Buffer.FirstElement = uint32_t(NumElements * bufferIdx);
  srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  srvDesc.Buffer.NumElements = uint32_t(NumElements);
  return srvDesc;
}

void FormattedBuffer::updateDynamicSRV() const
{
  assert(InternalBuffer.m_Dynamic);
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = srvDesc(InternalBuffer.m_CurrBuffer);

  D3D12_CPU_DESCRIPTOR_HANDLE handle = SRVDescriptorHeap.CPUHandleFromIndex(SRV, g_CurrFrameIdx);
  g_Device->CreateShaderResourceView(InternalBuffer.m_Resource, &desc, handle);

  for (uint64_t i = 1; i < RENDER_LATENCY; ++i)
  {
    uint64_t frameIdx = (g_CurrentCPUFrame + i) % RENDER_LATENCY;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = SRVDescriptorHeap.CPUHandleFromIndex(SRV, frameIdx);
    g_Device->CreateShaderResourceView(InternalBuffer.m_Resource, &desc, handle);
  }
}
//---------------------------------------------------------------------------//
// Uniform buffer
//---------------------------------------------------------------------------//

ConstantBuffer::ConstantBuffer() {}

ConstantBuffer::~ConstantBuffer() {}

void ConstantBuffer::init(const ConstantBufferInit& init)
{
  InternalBuffer.init(
      init.Size,
      ConstantBufferAlignment,
      init.Dynamic,
      init.CPUAccessible,
      false,
      init.InitData,
      init.InitialState,
      init.Heap,
      init.HeapOffset,
      init.Name);
}

void ConstantBuffer::deinit() { InternalBuffer.deinit(); }

void ConstantBuffer::setAsGfxRootParameter(
    ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter) const
{
  assert(InternalBuffer.m_Size > 0);
  cmdList->SetGraphicsRootConstantBufferView(rootParameter, CurrentGPUAddress);
}

void ConstantBuffer::setAsComputeRootParameter(
    ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter) const
{
  assert(InternalBuffer.m_Size > 0);
  cmdList->SetComputeRootConstantBufferView(rootParameter, CurrentGPUAddress);
}

void* ConstantBuffer::map()
{
  MapResult mapResult = InternalBuffer.map();
  CurrentGPUAddress = mapResult.GpuAddress;
  return mapResult.CpuAddress;
}

void ConstantBuffer::mapAndSetData(const void* data, uint64_t dataSize)
{
  assert(dataSize <= InternalBuffer.m_Size);
  void* cpuAddr = map();
  memcpy(cpuAddr, data, dataSize);
}

void ConstantBuffer::updateData(const void* srcData, uint64_t srcSize, uint64_t dstOffset)
{
  CurrentGPUAddress = InternalBuffer.updateData(srcData, srcSize, dstOffset);
}

void ConstantBuffer::multiUpdateData(
    const void* srcData[], uint64_t srcSize[], uint64_t dstOffset[], uint64_t numUpdates)
{
  CurrentGPUAddress = InternalBuffer.multiUpdateData(srcData, srcSize, dstOffset, numUpdates);
}

//---------------------------------------------------------------------------//
// StructuredBuffer
//---------------------------------------------------------------------------//
void StructuredBuffer::init(const StructuredBufferInit& p_Init)
{
  deinit();

  m_Stride = p_Init.Stride;
  m_NumElements = p_Init.NumElements;
  m_InternalBuffer.init(
      m_Stride * m_NumElements,
      m_Stride,
      p_Init.Dynamic,
      p_Init.CPUAccessible,
      p_Init.CreateUAV,
      p_Init.InitData,
      p_Init.InitialState,
      p_Init.Heap,
      p_Init.HeapOffset,
      p_Init.Name);
  m_GpuAddress = m_InternalBuffer.m_GpuAddress;

  PersistentDescriptorAlloc srvAlloc = SRVDescriptorHeap.AllocatePersistent();
  m_SrvIndex = srvAlloc.Index;

  // Start off all SRV's pointing to the first buffer
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = srvDesc(0);
  for (uint32_t i = 0; i < arrayCount(srvAlloc.Handles); ++i)
    g_Device->CreateShaderResourceView(m_InternalBuffer.m_Resource, &desc, srvAlloc.Handles[i]);

  if (p_Init.CreateUAV)
  {
    ID3D12Resource* counterRes = nullptr;
    if (p_Init.UseCounter)
    {
      D3D12_RESOURCE_DESC resourceDesc = {};
      resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      resourceDesc.Width = sizeof(uint32_t);
      resourceDesc.Height = 1;
      resourceDesc.DepthOrArraySize = 1;
      resourceDesc.MipLevels = 1;
      resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
      resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      resourceDesc.SampleDesc.Count = 1;
      resourceDesc.SampleDesc.Quality = 0;
      resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      resourceDesc.Alignment = 0;
      D3D_EXEC_CHECKED(g_Device->CreateCommittedResource(
          GetDefaultHeapProps(),
          D3D12_HEAP_FLAG_NONE,
          &resourceDesc,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          nullptr,
          IID_PPV_ARGS(&m_CounterResource)));

      counterRes = m_CounterResource;

      m_CounterUAV = UAVDescriptorHeap.AllocatePersistent().Handles[0];

      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      uavDesc.Format = DXGI_FORMAT_UNKNOWN;
      uavDesc.Buffer.CounterOffsetInBytes = 0;
      uavDesc.Buffer.FirstElement = 0;
      uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
      uavDesc.Buffer.NumElements = 1;
      uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
      g_Device->CreateUnorderedAccessView(counterRes, nullptr, &uavDesc, m_CounterUAV);
    }

    m_Uav = UAVDescriptorHeap.AllocatePersistent().Handles[0];

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uavDesc.Buffer.NumElements = uint32_t(m_NumElements);
    uavDesc.Buffer.StructureByteStride = uint32_t(m_Stride);
    g_Device->CreateUnorderedAccessView(m_InternalBuffer.m_Resource, counterRes, &uavDesc, m_Uav);
  }
}
void StructuredBuffer::deinit()
{
  SRVDescriptorHeap.FreePersistent(m_SrvIndex);
  UAVDescriptorHeap.FreePersistent(m_Uav);
  UAVDescriptorHeap.FreePersistent(m_CounterUAV);
  m_InternalBuffer.deinit();
  if (m_CounterResource != nullptr)
    m_CounterResource->Release();
  m_Stride = 0;
  m_NumElements = 0;
}

D3D12_VERTEX_BUFFER_VIEW StructuredBuffer::vbView() const
{
  D3D12_VERTEX_BUFFER_VIEW vbView = {};
  vbView.BufferLocation = m_GpuAddress;
  vbView.StrideInBytes = uint32_t(m_Stride);
  vbView.SizeInBytes = uint32_t(m_InternalBuffer.m_Size);
  return vbView;
}

void* StructuredBuffer::map()
{
  MapResult mapResult = m_InternalBuffer.map();
  m_GpuAddress = mapResult.GpuAddress;

  updateDynamicSRV();

  return mapResult.CpuAddress;
}
void StructuredBuffer::mapAndSetData(const void* p_Data, uint64_t p_NumElements)
{
  void* cpuAddr = map();
  memcpy(cpuAddr, p_Data, p_NumElements * m_Stride);
}
void StructuredBuffer::updateData(
    const void* p_SrcData, uint64_t p_SrcNumElements, uint64_t p_DstElemOffset)
{
  m_GpuAddress = m_InternalBuffer.updateData(
      p_SrcData, p_SrcNumElements * m_Stride, p_DstElemOffset * m_Stride);

  updateDynamicSRV();
}
void StructuredBuffer::multiUpdateData(
    const void* p_SrcData[],
    uint64_t p_SrcNumElements[],
    uint64_t p_DstElemOffset[],
    uint64_t p_NumUpdates)
{
  uint64_t srcSizes[16];
  uint64_t dstOffsets[16];
  for (uint64_t i = 0; i < p_NumUpdates; ++i)
  {
    srcSizes[i] = p_SrcNumElements[i] * m_Stride;
    dstOffsets[i] = p_DstElemOffset[i] * m_Stride;
  }

  m_GpuAddress = m_InternalBuffer.multiUpdateData(p_SrcData, srcSizes, dstOffsets, p_NumUpdates);

  updateDynamicSRV();
}

void StructuredBuffer::transition(
    ID3D12GraphicsCommandList* p_CmdList,
    D3D12_RESOURCE_STATES p_Before,
    D3D12_RESOURCE_STATES p_After) const
{
  m_InternalBuffer.transition(p_CmdList, p_Before, p_After);
}
void StructuredBuffer::makeReadable(ID3D12GraphicsCommandList* p_CmdList) const
{
  m_InternalBuffer.makeReadable(p_CmdList);
}
void StructuredBuffer::makeWritable(ID3D12GraphicsCommandList* p_CmdList) const
{
  m_InternalBuffer.makeWritable(p_CmdList);
}
void StructuredBuffer::uavBarrier(ID3D12GraphicsCommandList* p_CmdList) const
{
  m_InternalBuffer.uavBarrier(p_CmdList);
}
D3D12_SHADER_RESOURCE_VIEW_DESC
StructuredBuffer::srvDesc(uint64_t p_BufferIdx) const
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_UNKNOWN;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Buffer.FirstElement = uint32_t(m_NumElements * p_BufferIdx);
  srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  srvDesc.Buffer.NumElements = uint32_t(m_NumElements);
  srvDesc.Buffer.StructureByteStride = uint32_t(m_Stride);
  return srvDesc;
}
void StructuredBuffer::updateDynamicSRV() const
{
  DEBUG_BREAK(m_InternalBuffer.m_Dynamic);
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = srvDesc(m_InternalBuffer.m_CurrBuffer);

  D3D12_CPU_DESCRIPTOR_HANDLE handle =
      SRVDescriptorHeap.CPUHandleFromIndex(m_SrvIndex, g_CurrFrameIdx);
  g_Device->CreateShaderResourceView(m_InternalBuffer.m_Resource, &desc, handle);
}
//---------------------------------------------------------------------------//
// RawBuffer
//---------------------------------------------------------------------------//
void RawBuffer::init(const RawBufferInit& p_Init)
{
  deinit();

  assert(p_Init.NumElements > 0);
  NumElements = p_Init.NumElements;

  InternalBuffer.init(
      Stride * NumElements,
      Stride,
      p_Init.Dynamic,
      p_Init.CPUAccessible,
      p_Init.CreateUAV,
      p_Init.InitData,
      p_Init.InitialState,
      p_Init.Heap,
      p_Init.HeapOffset,
      p_Init.Name);
  GPUAddress = InternalBuffer.m_GpuAddress;

  PersistentDescriptorAlloc srvAlloc = SRVDescriptorHeap.AllocatePersistent();
  SRV = srvAlloc.Index;

  // Start off all SRV's pointing to the first buffer
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = srvDesc(0);
  for (uint32_t i = 0; i < arrayCount32(srvAlloc.Handles); ++i)
    g_Device->CreateShaderResourceView(InternalBuffer.m_Resource, &desc, srvAlloc.Handles[i]);

  if (p_Init.CreateUAV)
  {
    assert(p_Init.Dynamic == false);

    UAV = UAVDescriptorHeap.AllocatePersistent().Handles[0];

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    uavDesc.Buffer.NumElements = uint32_t(NumElements);
    g_Device->CreateUnorderedAccessView(InternalBuffer.m_Resource, nullptr, &uavDesc, UAV);
  }
}
void RawBuffer::deinit()
{
  SRVDescriptorHeap.FreePersistent(SRV);
  UAVDescriptorHeap.FreePersistent(UAV);
  InternalBuffer.deinit();
  NumElements = 0;
}

void* RawBuffer::map()
{
  MapResult mapResult = InternalBuffer.map();
  GPUAddress = mapResult.GpuAddress;

  updateDynamicSRV();

  return mapResult.CpuAddress;
}
void RawBuffer::mapAndSetData(const void* data, uint64_t numElements)
{
  assert(numElements <= NumElements);
  void* cpuAddr = map();
  memcpy(cpuAddr, data, numElements * Stride);
}
void RawBuffer::updateData(const void* srcData, uint64_t srcNumElements, uint64_t dstElemOffset)
{
  GPUAddress = InternalBuffer.updateData(srcData, srcNumElements * Stride, dstElemOffset * Stride);

  updateDynamicSRV();
}
void RawBuffer::multiUpdateData(
    const void* srcData[], uint64_t srcNumElements[], uint64_t dstElemOffset[], uint64_t numUpdates)
{
  uint64_t srcSizes[16];
  uint64_t dstOffsets[16];
  assert(numUpdates <= arrayCount32(srcSizes));
  for (uint64_t i = 0; i < numUpdates; ++i)
  {
    srcSizes[i] = srcNumElements[i] * Stride;
    dstOffsets[i] = dstElemOffset[i] * Stride;
  }

  GPUAddress = InternalBuffer.multiUpdateData(srcData, srcSizes, dstOffsets, numUpdates);

  updateDynamicSRV();
}

void RawBuffer::transition(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) const
{
  InternalBuffer.transition(cmdList, before, after);
}
void RawBuffer::makeReadable(ID3D12GraphicsCommandList* cmdList) const
{
  InternalBuffer.makeReadable(cmdList);
}
void RawBuffer::makeWritable(ID3D12GraphicsCommandList* cmdList) const
{
  InternalBuffer.makeWritable(cmdList);
}
void RawBuffer::uavBarrier(ID3D12GraphicsCommandList* cmdList) const
{
  InternalBuffer.uavBarrier(cmdList);
}
D3D12_SHADER_RESOURCE_VIEW_DESC RawBuffer::srvDesc(uint64_t bufferIdx) const
{
  assert(bufferIdx == 0 || InternalBuffer.m_Dynamic);
  assert(bufferIdx < RENDER_LATENCY);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Buffer.FirstElement = uint32_t(NumElements * bufferIdx);
  srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  srvDesc.Buffer.NumElements = uint32_t(NumElements);
  return srvDesc;
}
void RawBuffer::updateDynamicSRV() const
{
  assert(InternalBuffer.m_Dynamic);
  D3D12_SHADER_RESOURCE_VIEW_DESC desc = srvDesc(InternalBuffer.m_CurrBuffer);

  D3D12_CPU_DESCRIPTOR_HANDLE handle = SRVDescriptorHeap.CPUHandleFromIndex(SRV, g_CurrFrameIdx);
  g_Device->CreateShaderResourceView(InternalBuffer.m_Resource, &desc, handle);
}
//---------------------------------------------------------------------------//
// ReadbackBuffer
//---------------------------------------------------------------------------//
ReadbackBuffer::ReadbackBuffer() {}
ReadbackBuffer::~ReadbackBuffer() { assert(Resource == nullptr); }
void ReadbackBuffer::init(uint64_t size)
{
  assert(size > 0);
  Size = size;

  D3D12_RESOURCE_DESC resourceDesc = {};
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resourceDesc.Width = uint32_t(size);
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
      GetReadbackHeapProps(),
      D3D12_HEAP_FLAG_NONE,
      &resourceDesc,
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&Resource)));
}
void ReadbackBuffer::deinit()
{
  Resource->Release();
  Size = 0;
}
void* ReadbackBuffer::map()
{
  assert(Resource != nullptr);
  void* data = nullptr;
  Resource->Map(0, nullptr, &data);
  return data;
}
void ReadbackBuffer::unmap()
{
  assert(Resource != nullptr);
  Resource->Unmap(0, nullptr);
}

//---------------------------------------------------------------------------//
// Textures
//---------------------------------------------------------------------------//
void RenderTexture::init(const RenderTextureInit& p_Init)
{
  deinit();

  D3D12_RESOURCE_DESC textureDesc = {};
  textureDesc.MipLevels = 1;
  textureDesc.Format = p_Init.Format;
  textureDesc.Width = uint32_t(p_Init.Width);
  textureDesc.Height = uint32_t(p_Init.Height);
  textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (p_Init.CreateUAV)
    textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  textureDesc.DepthOrArraySize = uint16_t(p_Init.ArraySize);
  textureDesc.SampleDesc.Count = 1;
  textureDesc.SampleDesc.Quality = 0; // TODO
  textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  textureDesc.Alignment = 0;

  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = p_Init.Format;
  D3D_EXEC_CHECKED(g_Device->CreateCommittedResource(
      GetDefaultHeapProps(),
      D3D12_HEAP_FLAG_NONE,
      &textureDesc,
      p_Init.InitialState,
      &clearValue,
      IID_PPV_ARGS(&m_Texture.Resource)));

  if (p_Init.Name != nullptr)
    m_Texture.Resource->SetName(p_Init.Name);

  PersistentDescriptorAlloc srvAlloc = SRVDescriptorHeap.AllocatePersistent();
  m_Texture.SRV = srvAlloc.Index;
  for (uint32_t i = 0; i < SRVDescriptorHeap.NumHeaps; ++i)
    g_Device->CreateShaderResourceView(m_Texture.Resource, nullptr, srvAlloc.Handles[i]);

  m_Texture.Width = uint32_t(p_Init.Width);
  m_Texture.Height = uint32_t(p_Init.Height);
  m_Texture.Depth = 1;
  m_Texture.NumMips = 1;
  m_Texture.ArraySize = uint32_t(p_Init.ArraySize);
  m_Texture.Format = p_Init.Format;
  m_Texture.Cubemap = false;
  m_MSAASamples = uint32_t(p_Init.MSAASamples);
  m_MSAAQuality = uint32_t(textureDesc.SampleDesc.Quality);

  m_RTV = RTVDescriptorHeap.AllocatePersistent().Handles[0];
  g_Device->CreateRenderTargetView(m_Texture.Resource, nullptr, m_RTV);

  if (p_Init.ArraySize > 1)
  {
    m_ArrayRTVs.resize(p_Init.ArraySize);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtvDesc.Format = p_Init.Format;
    rtvDesc.Texture2DArray.ArraySize = 1;

    for (uint64_t i = 0; i < p_Init.ArraySize; ++i)
    {

      rtvDesc.Texture2DArray.FirstArraySlice = uint32_t(i);

      m_ArrayRTVs[i] = RTVDescriptorHeap.AllocatePersistent().Handles[0];
      g_Device->CreateRenderTargetView(m_Texture.Resource, &rtvDesc, m_ArrayRTVs[i]);
    }
  }

  if (p_Init.CreateUAV)
  {
    m_UAV = UAVDescriptorHeap.AllocatePersistent().Handles[0];
    g_Device->CreateUnorderedAccessView(m_Texture.Resource, nullptr, nullptr, m_UAV);
  }
}
void RenderTexture::deinit()
{
  RTVDescriptorHeap.FreePersistent(m_RTV);
  UAVDescriptorHeap.FreePersistent(m_UAV);
  for (uint64_t i = 0; i < m_ArrayRTVs.size(); ++i)
    RTVDescriptorHeap.FreePersistent(m_ArrayRTVs[i]);
  m_ArrayRTVs.clear();
  m_Texture.deinit();
}

void RenderTexture::transition(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after,
    uint64_t mipLevel,
    uint64_t arraySlice) const
{
  uint32_t subResourceIdx = mipLevel == uint64_t(-1) || arraySlice == uint64_t(-1)
                                ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
                                : uint32_t(subResourceIndex(mipLevel, arraySlice));
  transitionResource(cmdList, m_Texture.Resource, before, after, subResourceIdx);
}
void RenderTexture::makeReadable(
    ID3D12GraphicsCommandList* cmdList, uint64_t mipLevel, uint64_t arraySlice) const
{
  uint32_t subResourceIdx = mipLevel == uint64_t(-1) || arraySlice == uint64_t(-1)
                                ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
                                : uint32_t(subResourceIndex(mipLevel, arraySlice));
  transitionResource(
      cmdList,
      m_Texture.Resource,
      D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      subResourceIdx);
}
void RenderTexture::makeWritable(
    ID3D12GraphicsCommandList* cmdList, uint64_t mipLevel, uint64_t arraySlice) const
{
  uint32_t subResourceIdx = mipLevel == uint64_t(-1) || arraySlice == uint64_t(-1)
                                ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
                                : uint32_t(subResourceIndex(mipLevel, arraySlice));
  transitionResource(
      cmdList,
      m_Texture.Resource,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      D3D12_RESOURCE_STATE_RENDER_TARGET,
      subResourceIdx);
}
void RenderTexture::uavBarrier(ID3D12GraphicsCommandList* cmdList) const
{

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.UAV.pResource = m_Texture.Resource;
  cmdList->ResourceBarrier(1, &barrier);
}

//---------------------------------------------------------------------------//
// Volume texture
//---------------------------------------------------------------------------//
void VolumeTexture::init(const VolumeTextureInit& p_Init)
{
  deinit();

  assert(p_Init.Width > 0);
  assert(p_Init.Height > 0);
  assert(p_Init.Depth > 0);

  D3D12_RESOURCE_DESC textureDesc = {};
  textureDesc.MipLevels = 1;
  textureDesc.Format = p_Init.Format;
  textureDesc.Width = uint32_t(p_Init.Width);
  textureDesc.Height = uint32_t(p_Init.Height);
  textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  textureDesc.DepthOrArraySize = uint16_t(p_Init.Depth);
  textureDesc.SampleDesc.Count = 1;
  textureDesc.SampleDesc.Quality = 0;
  textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  textureDesc.Alignment = 0;

  D3D_EXEC_CHECKED(g_Device->CreateCommittedResource(
      GetDefaultHeapProps(),
      D3D12_HEAP_FLAG_NONE,
      &textureDesc,
      p_Init.InitialState,
      nullptr,
      IID_PPV_ARGS(&Texture.Resource)));

  if (p_Init.Name != nullptr)
    Texture.Resource->SetName(p_Init.Name);

  PersistentDescriptorAlloc srvAlloc = SRVDescriptorHeap.AllocatePersistent();
  Texture.SRV = srvAlloc.Index;
  for (uint32_t i = 0; i < SRVDescriptorHeap.NumHeaps; ++i)
    g_Device->CreateShaderResourceView(Texture.Resource, nullptr, srvAlloc.Handles[i]);

  Texture.Width = uint32_t(p_Init.Width);
  Texture.Height = uint32_t(p_Init.Height);
  Texture.Depth = uint32_t(p_Init.Depth);
  Texture.NumMips = 1;
  Texture.ArraySize = 1;
  Texture.Format = p_Init.Format;
  Texture.Cubemap = false;

  UAV = UAVDescriptorHeap.AllocatePersistent().Handles[0];
  g_Device->CreateUnorderedAccessView(Texture.Resource, nullptr, nullptr, UAV);
}
void VolumeTexture::deinit()
{
  UAVDescriptorHeap.FreePersistent(UAV);
  Texture.Shutdown();
}
void VolumeTexture::transition(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) const
{
  assert(Texture.Resource != nullptr);
  TransitionResource(cmdList, Texture.Resource, before, after, 0);
}
void VolumeTexture::makeReadable(ID3D12GraphicsCommandList* cmdList) const
{
  assert(Texture.Resource != nullptr);
  TransitionResource(
      cmdList,
      Texture.Resource,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
      0);
}
void VolumeTexture::makeWritable(ID3D12GraphicsCommandList* cmdList) const
{
  assert(Texture.Resource != nullptr);
  TransitionResource(
      cmdList,
      Texture.Resource,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      0);
}
void VolumeTexture::uavBarrier(ID3D12GraphicsCommandList* cmdList) const
{

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.UAV.pResource = Texture.Resource;
  cmdList->ResourceBarrier(1, &barrier);
}

//---------------------------------------------------------------------------//
// Depth Buffers
//---------------------------------------------------------------------------//
DepthBuffer::DepthBuffer() {}

DepthBuffer::~DepthBuffer()
{
  assert(DSVFormat == DXGI_FORMAT_UNKNOWN);

  // TODO: Check why this causes access violation?
  // Are we releasing this earlier?
  deinit();
}

void DepthBuffer::init(const DepthBufferInit& init)
{
  deinit();

  assert(init.Width > 0);
  assert(init.Height > 0);
  assert(init.MSAASamples > 0);

  DXGI_FORMAT texFormat = init.Format;
  DXGI_FORMAT srvFormat = init.Format;
  if (init.Format == DXGI_FORMAT_D16_UNORM)
  {
    texFormat = DXGI_FORMAT_R16_TYPELESS;
    srvFormat = DXGI_FORMAT_R16_UNORM;
  }
  else if (init.Format == DXGI_FORMAT_D24_UNORM_S8_UINT)
  {
    texFormat = DXGI_FORMAT_R24G8_TYPELESS;
    srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  }
  else if (init.Format == DXGI_FORMAT_D32_FLOAT)
  {
    texFormat = DXGI_FORMAT_R32_TYPELESS;
    srvFormat = DXGI_FORMAT_R32_FLOAT;
  }
  else if (init.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
  {
    texFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
    srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  }
  else
  {
    assert(false && "Invalid depth buffer format!");
  }

  D3D12_RESOURCE_DESC textureDesc = {};
  textureDesc.MipLevels = 1;
  textureDesc.Format = texFormat;
  textureDesc.Width = uint32_t(init.Width);
  textureDesc.Height = uint32_t(init.Height);
  textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  textureDesc.DepthOrArraySize = uint16_t(init.ArraySize);
  textureDesc.SampleDesc.Count = 1;
  textureDesc.SampleDesc.Quality = 0;
  textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  textureDesc.Alignment = 0;

  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.DepthStencil.Depth = 1.0f;
  clearValue.DepthStencil.Stencil = 0;
  clearValue.Format = init.Format;

  D3D_EXEC_CHECKED(g_Device->CreateCommittedResource(
      GetDefaultHeapProps(),
      D3D12_HEAP_FLAG_NONE,
      &textureDesc,
      init.InitialState,
      &clearValue,
      IID_PPV_ARGS(&Texture.Resource)));

  if (init.Name != nullptr)
    Texture.Resource->SetName(init.Name);

  PersistentDescriptorAlloc srvAlloc = SRVDescriptorHeap.AllocatePersistent();
  Texture.SRV = srvAlloc.Index;

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = srvFormat;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (init.MSAASamples == 1 && init.ArraySize == 1)
  {
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  }
  else if (init.MSAASamples == 1 && init.ArraySize > 1)
  {
    srvDesc.Texture2DArray.ArraySize = uint32_t(init.ArraySize);
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.PlaneSlice = 0;
    srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
  }
  else if (init.MSAASamples > 1 && init.ArraySize == 1)
  {
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
  }
  else if (init.MSAASamples > 1 && init.ArraySize > 1)
  {
    srvDesc.Texture2DMSArray.FirstArraySlice = 0;
    srvDesc.Texture2DMSArray.ArraySize = uint32_t(init.ArraySize);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
  }

  for (uint32_t i = 0; i < SRVDescriptorHeap.NumHeaps; ++i)
    g_Device->CreateShaderResourceView(Texture.Resource, &srvDesc, srvAlloc.Handles[i]);

  Texture.Width = uint32_t(init.Width);
  Texture.Height = uint32_t(init.Height);
  Texture.Depth = 1;
  Texture.NumMips = 1;
  Texture.ArraySize = uint32_t(init.ArraySize);
  Texture.Format = srvFormat;
  Texture.Cubemap = false;
  MSAASamples = uint32_t(init.MSAASamples);
  MSAAQuality = uint32_t(textureDesc.SampleDesc.Quality);

  DSV = DSVDescriptorHeap.AllocatePersistent().Handles[0];

  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
  dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
  dsvDesc.Format = init.Format;

  if (init.MSAASamples == 1 && init.ArraySize == 1)
  {
    dsvDesc.Texture2D.MipSlice = 0;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  }
  else if (init.MSAASamples == 1 && init.ArraySize > 1)
  {
    dsvDesc.Texture2DArray.ArraySize = uint32_t(init.ArraySize);
    dsvDesc.Texture2DArray.FirstArraySlice = 0;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
  }
  else if (init.MSAASamples > 1 && init.ArraySize == 1)
  {
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
  }
  else if (init.MSAASamples > 1 && init.ArraySize > 1)
  {
    dsvDesc.Texture2DMSArray.ArraySize = uint32_t(init.ArraySize);
    dsvDesc.Texture2DMSArray.FirstArraySlice = 0;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
  }

  g_Device->CreateDepthStencilView(Texture.Resource, &dsvDesc, DSV);

  bool hasStencil = init.Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
                    init.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

  ReadOnlyDSV = DSVDescriptorHeap.AllocatePersistent().Handles[0];
  dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
  if (hasStencil)
    dsvDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
  g_Device->CreateDepthStencilView(Texture.Resource, &dsvDesc, ReadOnlyDSV);

  if (init.ArraySize > 1)
  {
    ArrayDSVs.resize(init.ArraySize);

    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    if (init.MSAASamples > 1)
      dsvDesc.Texture2DMSArray.ArraySize = 1;
    else
      dsvDesc.Texture2DArray.ArraySize = 1;

    for (uint64_t i = 0; i < init.ArraySize; ++i)
    {
      if (init.MSAASamples > 1)
        dsvDesc.Texture2DMSArray.FirstArraySlice = uint32_t(i);
      else
        dsvDesc.Texture2DArray.FirstArraySlice = uint32_t(i);

      ArrayDSVs[i] = DSVDescriptorHeap.AllocatePersistent().Handles[0];
      g_Device->CreateDepthStencilView(Texture.Resource, &dsvDesc, ArrayDSVs[i]);
    }
  }

  DSVFormat = init.Format;
}

void DepthBuffer::deinit()
{
  DSVDescriptorHeap.FreePersistent(DSV);
  DSVDescriptorHeap.FreePersistent(ReadOnlyDSV);
  for (uint64_t i = 0; i < ArrayDSVs.size(); ++i)
    DSVDescriptorHeap.FreePersistent(ArrayDSVs[i]);
  ArrayDSVs.clear();
  Texture.Shutdown();
  DSVFormat = DXGI_FORMAT_UNKNOWN;
}

void DepthBuffer::transition(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after,
    uint64_t arraySlice) const
{
  uint32_t subResourceIdx =
      arraySlice == uint64_t(-1) ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : uint32_t(arraySlice);
  assert(Texture.Resource != nullptr);
  TransitionResource(cmdList, Texture.Resource, before, after, subResourceIdx);
}

void DepthBuffer::makeReadable(ID3D12GraphicsCommandList* cmdList, uint64_t arraySlice) const
{
  uint32_t subResourceIdx =
      arraySlice == uint64_t(-1) ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : uint32_t(arraySlice);
  assert(Texture.Resource != nullptr);
  TransitionResource(
      cmdList,
      Texture.Resource,
      D3D12_RESOURCE_STATE_DEPTH_WRITE,
      D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      subResourceIdx);
}

void DepthBuffer::makeWritable(ID3D12GraphicsCommandList* cmdList, uint64_t arraySlice) const
{
  uint32_t subResourceIdx =
      arraySlice == uint64_t(-1) ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : uint32_t(arraySlice);
  assert(Texture.Resource != nullptr);
  TransitionResource(
      cmdList,
      Texture.Resource,
      D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      D3D12_RESOURCE_STATE_DEPTH_WRITE,
      subResourceIdx);
}
