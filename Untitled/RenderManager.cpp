#include "RenderManager.hpp"
#include <pix3.h>

//---------------------------------------------------------------------------//
// Internal private methods
//---------------------------------------------------------------------------//
std::wstring RenderManager::_getShaderPath(LPCWSTR p_ShaderName)
{
  return m_Info.m_AssetsPath + L"Shaders\\" + p_ShaderName;
}
std::wstring RenderManager::_getAssetPath(LPCWSTR p_AssetName)
{
  return m_Info.m_AssetsPath + p_AssetName;
}
//---------------------------------------------------------------------------//
void RenderManager::_loadD3D12Pipeline()
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
  D3D_EXEC_CHECKED(
      CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  if (m_Info.m_UseWarpDevice)
  {
    IDXGIAdapterPtr warpAdapter;
    D3D_EXEC_CHECKED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    D3D_EXEC_CHECKED(D3D12CreateDevice(
        warpAdapter.GetInterfacePtr(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_Dev)));
  }
  else
  {
    IDXGIAdapter1Ptr hardwareAdapter;
    getHardwareAdapter(factory.GetInterfacePtr(), &hardwareAdapter, true);

    D3D_EXEC_CHECKED(D3D12CreateDevice(
        hardwareAdapter.GetInterfacePtr(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_Dev)));
  }

  // Describe and create the command queue.
  D3D12_COMMAND_QUEUE_DESC queueDesc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE};

  D3D_EXEC_CHECKED(
      m_Dev->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CmdQue)));
  D3D_NAME_OBJECT(m_CmdQue);

  // Describe and create the swap chain.
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = FRAME_COUNT;
  swapChainDesc.Width = m_Info.m_Width;
  swapChainDesc.Height = m_Info.m_Height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

  IDXGISwapChain1Ptr swapChain;
  D3D_EXEC_CHECKED(factory->CreateSwapChainForHwnd(
      m_CmdQue.GetInterfacePtr(), // Swc needs the queue to force a flush on it.
      g_WinHandle,
      &swapChainDesc,
      nullptr,
      nullptr,
      &swapChain));

  // This sample does not support fullscreen transitions.
  D3D_EXEC_CHECKED(
      factory->MakeWindowAssociation(g_WinHandle, DXGI_MWA_NO_ALT_ENTER));

  D3D_EXEC_CHECKED(swapChain->QueryInterface(IID_PPV_ARGS(&m_Swc)));

  m_FrameIndex = m_Swc->GetCurrentBackBufferIndex();
  m_SwapChainEvent = m_Swc->GetFrameLatencyWaitableObject();

  // Create descriptor heaps.
  {
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    D3D_EXEC_CHECKED(
        m_Dev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap)));

    m_RtvDescriptorSize =
        m_Dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_SrvUavDescriptorSize = m_Dev->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }

  // Create frame resources.
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        m_RtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV and a command allocator for each frame.
    for (UINT n = 0; n < FRAME_COUNT; n++)
    {
      D3D_EXEC_CHECKED(m_Swc->GetBuffer(n, IID_PPV_ARGS(&m_RenderTargets[n])));
      m_Dev->CreateRenderTargetView(
          m_RenderTargets[n].GetInterfacePtr(), nullptr, rtvHandle);
      rtvHandle.Offset(1, m_RtvDescriptorSize);

      D3D_NAME_OBJECT_INDEXED(m_RenderTargets, n);

      D3D_EXEC_CHECKED(m_Dev->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CmdAllocs[n])));
    }
  }
}
//---------------------------------------------------------------------------//
void RenderManager::_createVertexBuffer()
{
  Vertex vertices[] = {
      {{0.0f, 0.25f * m_Info.m_AspectRatio, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.25f, -0.25f * m_Info.m_AspectRatio, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      {{-0.25f, -0.25f * m_Info.m_AspectRatio, 0.0f},
       {0.0f, 0.0f, 1.0f, 1.0f}}};

  const UINT bufferSize = 3 * sizeof(Vertex);

  D3D_EXEC_CHECKED(m_Dev->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
      D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,
      IID_PPV_ARGS(&m_VtxBuffer)));

  D3D_EXEC_CHECKED(m_Dev->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&m_VtxBufferUpload)));

  D3D_NAME_OBJECT(m_VtxBuffer);

  D3D12_SUBRESOURCE_DATA vertexData = {};
  vertexData.pData = reinterpret_cast<UINT8*>(&vertices[0]);
  vertexData.RowPitch = bufferSize;
  vertexData.SlicePitch = vertexData.RowPitch;

  UpdateSubresources<1>(
      m_CmdList.GetInterfacePtr(),
      m_VtxBuffer.GetInterfacePtr(),
      m_VtxBufferUpload.GetInterfacePtr(),
      0,
      0,
      1,
      &vertexData);
  m_CmdList->ResourceBarrier(
      1,
      &CD3DX12_RESOURCE_BARRIER::Transition(
          m_VtxBuffer.GetInterfacePtr(),
          D3D12_RESOURCE_STATE_COPY_DEST,
          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

  m_VtxBufferView.BufferLocation = m_VtxBuffer->GetGPUVirtualAddress();
  m_VtxBufferView.SizeInBytes = static_cast<UINT>(bufferSize);
  m_VtxBufferView.StrideInBytes = sizeof(Vertex);
}
//---------------------------------------------------------------------------//
bool RenderManager::_createPSOs()
{
  // Create the pipeline states, which includes compiling and loading shaders.
  {
    ID3DBlobPtr vertexShader;
    ID3DBlobPtr pixelShader;

    ID3DBlobPtr errorBlob;

#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    // Load and compile shaders.
    HRESULT hr = D3DCompileFromFile(
        _getShaderPath(L"TriangleDraw.hlsl").c_str(),
        nullptr,
        nullptr,
        "VSTriangleDraw",
        "vs_5_0",
        compileFlags,
        0,
        &vertexShader,
        &errorBlob);
    if (nullptr == vertexShader)
    {
      if (errorBlob != nullptr)
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
      return false;
    }
    errorBlob = nullptr;
    D3DCompileFromFile(
        _getShaderPath(L"TriangleDraw.hlsl").c_str(),
        nullptr,
        nullptr,
        "PSTriangleDraw",
        "ps_5_0",
        compileFlags,
        0,
        &pixelShader,
        &errorBlob);
    if (nullptr == pixelShader)
    {
      if (errorBlob != nullptr)
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
      return false;
    }

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION",
         0,
         DXGI_FORMAT_R32G32B32_FLOAT,
         0,
         0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"COLOR",
         0,
         DXGI_FORMAT_R32G32B32A32_FLOAT,
         0,
         12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
    };

    // Describe the blend and depth states.
    CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = FALSE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {inputElementDescs, arrayCount32(inputElementDescs)};
    psoDesc.pRootSignature = m_RootSig.GetInterfacePtr();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.GetInterfacePtr());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.GetInterfacePtr());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;

    D3D_EXEC_CHECKED(
        m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_Pso)));
    D3D_NAME_OBJECT(m_Pso);
  }

  return true;
}
//---------------------------------------------------------------------------//
void RenderManager::_loadAssets()
{
  // Init uploads and other helpers
  initializeUpload(m_Dev);
  Initialize_Helpers();

  // Create a gbuffer:
  {
    RenderTextureInit rtInit;
    rtInit.Width = m_Info.m_Width;
    rtInit.Height = m_Info.m_Height;
    rtInit.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtInit.MSAASamples = 1;
    rtInit.ArraySize = 1;
    rtInit.CreateUAV = false;
    rtInit.InitialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rtInit.Name = L"Albedo Target";
    albedoTarget.init(rtInit);
  }

  // Create the root signatures.
  {
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport
    // succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_Dev->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
      featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Graphics root signature.
    {
      CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
      ranges[0].Init(
          D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
          1,
          0,
          0,
          D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

      CD3DX12_ROOT_PARAMETER1
      rootParameters[GraphicsRootParametersCount];
      rootParameters[GraphicsRootCBV].InitAsConstantBufferView(
          0,
          0,
          D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
          D3D12_SHADER_VISIBILITY_ALL);

      CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
      rootSignatureDesc.Init_1_1(
          arrayCount32(rootParameters),
          rootParameters,
          0,
          nullptr,
          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

      ID3DBlobPtr signature;
      ID3DBlobPtr error;
      D3D_EXEC_CHECKED(D3DX12SerializeVersionedRootSignature(
          &rootSignatureDesc, featureData.HighestVersion, &signature, &error));
      D3D_EXEC_CHECKED(m_Dev->CreateRootSignature(
          0,
          signature->GetBufferPointer(),
          signature->GetBufferSize(),
          IID_PPV_ARGS(&m_RootSig)));
      D3D_NAME_OBJECT(m_RootSig);
    }
  }

#pragma region Setup Gbuffer Stuff
  DXGI_FORMAT gbufferFormats[] = {
      albedoTarget.format(),
  };

  // TODO: this stuff should be wrapped as mesh renderer initialization

  // 1. Load and compile shaders.
  ID3DBlobPtr vertexShaderBlob;
  ID3DBlobPtr pixelShaderBlob;

  ID3DBlobPtr errorBlob;

#if defined(_DEBUG)
  UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  UINT compileFlags = 0;
#endif

  HRESULT hr = D3DCompileFromFile(
      _getShaderPath(L"Mesh.hlsl").c_str(),
      nullptr,
      nullptr,
      "VS",
      "vs_5_1",
      compileFlags,
      0,
      &vertexShaderBlob,
      &errorBlob);
  if (nullptr == vertexShaderBlob)
  {
    if (errorBlob != nullptr)
      OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    assert(false && "Shader compilation failed");
  }
  errorBlob = nullptr;
  D3DCompileFromFile(
      _getShaderPath(L"Mesh.hlsl").c_str(),
      nullptr,
      nullptr,
      "PS",
      "ps_5_1",
      compileFlags,
      0,
      &pixelShaderBlob,
      &errorBlob);
  if (nullptr == pixelShaderBlob)
  {
    if (errorBlob != nullptr)
      OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    assert(false && "Shader compilation failed");
  }

  // 2. Create gbuffer Root Sig
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
  rootSignatureDesc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  createRootSignature(m_Dev, &gbufferRootSignature, rootSignatureDesc);

  // 3. Gbuffer PSO

  static const D3D12_INPUT_ELEMENT_DESC standardInputElements[5] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "UV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = gbufferRootSignature;
  psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.GetInterfacePtr());
  psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.GetInterfacePtr());
  psoDesc.RasterizerState = GetRasterizerState(RasterizerState::BackFaceCull);
  psoDesc.BlendState = GetBlendState(BlendState::Disabled);
  psoDesc.DepthStencilState = GetDepthState(DepthState::WritesEnabled);
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = arrayCount32(gbufferFormats);
  for (uint64_t i = 0; i < arrayCount32(gbufferFormats); ++i)
    psoDesc.RTVFormats[i] = gbufferFormats[i];
  psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
  psoDesc.SampleDesc.Count = 1;
  psoDesc.SampleDesc.Quality = 0;
  psoDesc.InputLayout.NumElements = arrayCount32(standardInputElements);
  psoDesc.InputLayout.pInputElementDescs = standardInputElements;

  m_Dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&gbufferPSO));

#pragma endregion
#pragma region Setup Deferred Stuff
  // 1. Create deferred root sig

  // 2.

#pragma endregion

  _createPSOs();

  // Create the command list.
  D3D_EXEC_CHECKED(m_Dev->CreateCommandList(
      0,
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      m_CmdAllocs[m_FrameIndex].GetInterfacePtr(),
      m_Pso.GetInterfacePtr(),
      IID_PPV_ARGS(&m_CmdList)));
  D3D_NAME_OBJECT(m_CmdList);

  _createVertexBuffer();

  // Note: ComPtr's are CPU objects but this resource needs to stay in scope
  // until the command list that references it has finished executing on the
  // GPU. We will flush the GPU at the end of this method to ensure the resource
  // is not prematurely destroyed.
  ID3D12ResourcePtr cbufferCSUpload;

  // Create the triangle constant buffer.
  {
    const UINT bufferSize = sizeof(TriangleParams);

    D3D_EXEC_CHECKED(m_Dev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_TriangleUniforms)));

    D3D_EXEC_CHECKED(m_Dev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&cbufferCSUpload)));

    D3D_NAME_OBJECT(m_TriangleUniforms);

    TriangleParams cbufferCS = {};
    cbufferCS.m_Params[0] = 21;
    cbufferCS.m_Params[1] = int(ceil(4 / 128.0f));
    cbufferCS.m_ParamsFloat[0] = 0.1f;
    cbufferCS.m_ParamsFloat[1] = 1.0f;

    D3D12_SUBRESOURCE_DATA CbufferData = {};
    CbufferData.pData = reinterpret_cast<UINT8*>(&cbufferCS);
    CbufferData.RowPitch = bufferSize;
    CbufferData.SlicePitch = CbufferData.RowPitch;

    UpdateSubresources<1>(
        m_CmdList.GetInterfacePtr(),
        m_TriangleUniforms.GetInterfacePtr(),
        cbufferCSUpload.GetInterfacePtr(),
        0,
        0,
        1,
        &CbufferData);
    m_CmdList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            m_TriangleUniforms.GetInterfacePtr(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
  }

  // Create the frame constant buffer.
  {
    const UINT frameUniformsSize = sizeof(FrameParams) * FRAME_COUNT;

    D3D_EXEC_CHECKED(m_Dev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(frameUniformsSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_FrameUniforms)));

    D3D_NAME_OBJECT(m_FrameUniforms);

    CD3DX12_RANGE readRange(
        0, 0); // We do not intend to read from this resource on the CPU.
    D3D_EXEC_CHECKED(m_FrameUniforms->Map(
        0, &readRange, reinterpret_cast<void**>(&m_FrameUniformsDataPtr)));
    ZeroMemory(m_FrameUniformsDataPtr, frameUniformsSize);
  }

  // Close the command list and execute it to begin the initial GPU setup.
  D3D_EXEC_CHECKED(m_CmdList->Close());
  ID3D12CommandList* ppCommandLists[] = {m_CmdList.GetInterfacePtr()};
  m_CmdQue->ExecuteCommandLists(arrayCount32(ppCommandLists), ppCommandLists);

  // Create synchronization objects and wait until assets have been uploaded to
  // the GPU.
  {
    D3D_EXEC_CHECKED(m_Dev->CreateFence(
        m_RenderContextFenceValue,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&m_RenderContextFence)));
    m_RenderContextFenceValue++;

    m_RenderContextFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_RenderContextFenceEvent == nullptr)
    {
      D3D_EXEC_CHECKED(HRESULT_FROM_WIN32(GetLastError()));
    }

    _waitForRenderContext();
  }
}
//---------------------------------------------------------------------------//
void RenderManager::_populateCommandList()
{
  // Command list allocators can only be reset when the associated
  // command lists have finished execution on the GPU; apps should use
  // fences to determine GPU execution progress.
  D3D_EXEC_CHECKED(m_CmdAllocs[m_FrameIndex]->Reset());

  // However, when ExecuteCommandList() is called on a particular command
  // list, that command list can then be reset at any time and must be before
  // re-recording.
  D3D_EXEC_CHECKED(m_CmdList->Reset(
      m_CmdAllocs[m_FrameIndex].GetInterfacePtr(), m_Pso.GetInterfacePtr()));

  // Set necessary state.
  m_CmdList->SetPipelineState(m_Pso.GetInterfacePtr());
  m_CmdList->SetGraphicsRootSignature(m_RootSig.GetInterfacePtr());

  m_CmdList->SetGraphicsRootConstantBufferView(
      GraphicsRootCBV,
      m_FrameUniforms->GetGPUVirtualAddress() +
          m_FrameIndex * sizeof(FrameParams));

  // ID3D12DescriptorHeap* ppHeaps[] = {/*m_SrvUavHeap.GetInterfacePtr()*/};
  // m_CmdList->SetDescriptorHeaps(arrayCount32(ppHeaps), ppHeaps);

  m_CmdList->IASetVertexBuffers(0, 1, &m_VtxBufferView);
  m_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_CmdList->RSSetScissorRects(1, &m_ScissorRect);

  // Indicate that the back buffer will be used as a render target.
  m_CmdList->ResourceBarrier(
      1,
      &CD3DX12_RESOURCE_BARRIER::Transition(
          m_RenderTargets[m_FrameIndex].GetInterfacePtr(),
          D3D12_RESOURCE_STATE_PRESENT,
          D3D12_RESOURCE_STATE_RENDER_TARGET));

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
      m_RtvHeap->GetCPUDescriptorHandleForHeapStart(),
      m_FrameIndex,
      m_RtvDescriptorSize);
  m_CmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

  // Record commands.
  const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
  m_CmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

  m_CmdList->RSSetViewports(1, &m_Viewport);

  m_CmdList->DrawInstanced(3, 1, 0, 0);

  // Indicate that the back buffer will now be used to present.
  m_CmdList->ResourceBarrier(
      1,
      &CD3DX12_RESOURCE_BARRIER::Transition(
          m_RenderTargets[m_FrameIndex].GetInterfacePtr(),
          D3D12_RESOURCE_STATE_RENDER_TARGET,
          D3D12_RESOURCE_STATE_PRESENT));

  D3D_EXEC_CHECKED(m_CmdList->Close());
}
//---------------------------------------------------------------------------//
void RenderManager::_waitForRenderContext()
{
  // Add a signal command to the queue.
  D3D_EXEC_CHECKED(m_CmdQue->Signal(
      m_RenderContextFence.GetInterfacePtr(), m_RenderContextFenceValue));

  // Instruct the fence to set the event obj when the signal command completes.
  D3D_EXEC_CHECKED(m_RenderContextFence->SetEventOnCompletion(
      m_RenderContextFenceValue, m_RenderContextFenceEvent));
  m_RenderContextFenceValue++;

  // Wait until the signal command has been processed.
  WaitForSingleObject(m_RenderContextFenceEvent, INFINITE);
}
void RenderManager::_waitForGpu()
{
  // Schedule a Signal command in the queue.
  D3D_EXEC_CHECKED(m_CmdQue->Signal(
      m_RenderContextFence.GetInterfacePtr(),
      m_RenderContextFenceValues[m_FrameIndex]));

  // Wait until the fence has been processed.
  D3D_EXEC_CHECKED(m_RenderContextFence->SetEventOnCompletion(
      m_RenderContextFenceValues[m_FrameIndex], m_RenderContextFenceEvent));
  WaitForSingleObjectEx(m_RenderContextFenceEvent, INFINITE, FALSE);

  // Increment the fence value for the current frame.
  m_RenderContextFenceValues[m_FrameIndex]++;
}
//---------------------------------------------------------------------------//
void RenderManager::_moveToNextFrame()
{
  // Assign the current fence value to the current frame.
  m_FrameFenceValues[m_FrameIndex] = m_RenderContextFenceValue;

  // Signal and increment the fence value.
  D3D_EXEC_CHECKED(m_CmdQue->Signal(
      m_RenderContextFence.GetInterfacePtr(), m_RenderContextFenceValue));
  m_RenderContextFenceValue++;

  // Update the frame index.
  m_FrameIndex = m_Swc->GetCurrentBackBufferIndex();

  // If the next frame is not ready to be rendered yet, wait until it is ready.
  if (m_RenderContextFence->GetCompletedValue() <
      m_FrameFenceValues[m_FrameIndex])
  {
    D3D_EXEC_CHECKED(m_RenderContextFence->SetEventOnCompletion(
        m_FrameFenceValues[m_FrameIndex], m_RenderContextFenceEvent));
    WaitForSingleObject(m_RenderContextFenceEvent, INFINITE);
  }
}
//---------------------------------------------------------------------------//
void RenderManager::_restoreD3DResources()
{
  // Give GPU a chance to finish its execution in progress.
  try
  {
    _waitForGpu();
  }
  catch (std::exception)
  {
    // Do nothing, currently attached adapter is unresponsive.
  }
  _releaseD3DResources();
  onLoad();
}
//---------------------------------------------------------------------------//
void RenderManager::_releaseD3DResources()
{
  m_RenderContextFence = nullptr;
  resetComPtrArray(&m_RenderTargets);
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

  m_Info.m_AspectRatio =
      static_cast<float>(p_Width) / static_cast<float>(p_Height);

  m_Info.m_IsInitialized = true;
}
//---------------------------------------------------------------------------//
void RenderManager::onLoad()
{
  DEBUG_BREAK(m_Info.m_IsInitialized);

  UINT width = m_Info.m_Width;
  UINT height = m_Info.m_Height;
  m_Viewport = CD3DX12_VIEWPORT(
      0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
  m_ScissorRect =
      CD3DX12_RECT(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));

  setArrayToZero(m_FrameFenceValues);

  m_FrameUniformsDataPtr = nullptr;

  D3D_EXEC_CHECKED(DXGIDeclareAdapterRemovalSupport());

  cameraInit(&m_Camera, {0.0f, 0.0f, 1500.0f});
  m_Camera.m_MoveSpeed = 2500.0f;

  timerInit(&m_Timer);

  // Load pipeline
  _loadD3D12Pipeline();

  // Load assets
  _loadAssets();
}
//---------------------------------------------------------------------------//
void RenderManager::onDestroy()
{
  _waitForGpu();

  // Ensure that the GPU is no longer referencing resources that are about to be
  // cleaned up by the destructor.
  _waitForRenderContext();

  // Close handles to fence events and threads.
  CloseHandle(m_RenderContextFenceEvent);

  // TODO Release these in mesh renderer
  gbufferRootSignature->Release();
  gbufferPSO->Release();

  // Shutdown render target(s):
  albedoTarget.deinit();

  //_waitForGpu();
  // Shudown uploads and other helpers
  Shutdown_Helpers();
  shutdownUpload();
}
//---------------------------------------------------------------------------//
void RenderManager::onUpdate()
{
  // Wait for the previous Present to complete.
  WaitForSingleObjectEx(m_SwapChainEvent, 100, FALSE);

  timerTick(&m_Timer, nullptr);
  cameraUpdate(&m_Camera, static_cast<float>(timerGetElapsedSeconds(&m_Timer)));

  FrameParams frameUniforms = {};
  XMMATRIX view = cameraGetViewMatrix(&m_Camera);
  CXMMATRIX proj =
      getProjectionMatrix(0.8f, m_Info.m_AspectRatio, 1.0f, 5000.0f);
  XMStoreFloat4x4(&frameUniforms.m_Wvp, XMMatrixMultiply(view, proj));
  XMStoreFloat4x4(&frameUniforms.m_InvView, XMMatrixInverse(nullptr, view));

  UINT8* destination =
      m_FrameUniformsDataPtr + sizeof(FrameParams) * m_FrameIndex;
  memcpy(destination, &frameUniforms, sizeof(FrameParams));
}
//---------------------------------------------------------------------------//
void RenderManager::onRender()
{
  if (m_Info.m_IsInitialized)
  {
    try
    {
      PIXBeginEvent(m_CmdQue.GetInterfacePtr(), 0, L"Render");

      // Record all the commands we need to render the scene into the command
      // list.
      _populateCommandList();

      // Execute the command list.
      ID3D12CommandList* ppCommandLists[] = {m_CmdList.GetInterfacePtr()};
      m_CmdQue->ExecuteCommandLists(
          arrayCount32(ppCommandLists), ppCommandLists);

      PIXEndEvent(m_CmdQue.GetInterfacePtr());

      // Present the frame.
      D3D_EXEC_CHECKED(m_Swc->Present(1, 0));

      _moveToNextFrame();
    }
    catch (HrException& e)
    {
      if (e.Error() == DXGI_ERROR_DEVICE_REMOVED ||
          e.Error() == DXGI_ERROR_DEVICE_RESET)
      {
        _restoreD3DResources();
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
  cameraOnKeyDown(&m_Camera, p_Key);
}
//---------------------------------------------------------------------------//
void RenderManager::onKeyUp(UINT8 p_Key)
{
  cameraOnKeyUp(&m_Camera, p_Key);
}
//---------------------------------------------------------------------------//
void RenderManager::onResize()
{
}
//---------------------------------------------------------------------------//
void RenderManager::onCodeChange()
{
}
//---------------------------------------------------------------------------//
void RenderManager::onShaderChange()
{
  OutputDebugStringA("[RenderManager] Starting shader reload...\n");
  if (_createPSOs())
  {
    OutputDebugStringA("[RenderManager] Shaders loaded\n");
    return;
  }
  else
  {
    OutputDebugStringA("[RenderManager] Failed to reload the shaders\n");
  }
}
//---------------------------------------------------------------------------//
