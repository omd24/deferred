
#include "PostProcessor.hpp"
#include "PostFxHelper.hpp"
#include "pix3.h"

namespace /*Internal*/
{
//---------------------------------------------------------------------------//
ID3D12Fence* g_DX12ComputeFrameFence;
PostFxHelper g_PostFxHelper;
D3D12_SHADER_BYTECODE g_ToneMapShader;
D3D12_SHADER_BYTECODE g_ScaleShader;
D3D12_SHADER_BYTECODE g_BloomShader;
D3D12_SHADER_BYTECODE g_BlurHShader;
D3D12_SHADER_BYTECODE g_BlurVShader;
//---------------------------------------------------------------------------//
TempRenderTarget* bloom(ID3D12GraphicsCommandList* p_CmdList, const RenderTexture& p_Input)
{
  PIXBeginEvent(p_CmdList, 0, "Bloom");

  const uint64_t bloomWidth = p_Input.m_Texture.Width / 2;
  const uint64_t bloomHeight = p_Input.m_Texture.Height / 2;

  TempRenderTarget* downscale1 =
      g_PostFxHelper.getTempRenderTarget(bloomWidth, bloomHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
  downscale1->m_RenderTarget.makeWritable(p_CmdList);

  g_PostFxHelper.postProcess(g_BloomShader, "Bloom Initial Pass", p_Input, downscale1);

  TempRenderTarget* blurTemp =
      g_PostFxHelper.getTempRenderTarget(bloomWidth, bloomHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
  downscale1->m_RenderTarget.makeReadable(p_CmdList);

  // Blur pass
  for (uint64_t i = 0; i < 2; ++i)
  {
    blurTemp->m_RenderTarget.makeWritable(p_CmdList);

    g_PostFxHelper.postProcess(g_BlurHShader, "Horizontal Bloom Blur", downscale1, blurTemp);

    blurTemp->m_RenderTarget.makeReadable(p_CmdList);
    downscale1->m_RenderTarget.makeWritable(p_CmdList);

    g_PostFxHelper.postProcess(g_BlurVShader, "Vertical Bloom Blur", blurTemp, downscale1);

    downscale1->m_RenderTarget.makeReadable(p_CmdList);
  }

  blurTemp->m_InUse = false;
  PIXEndEvent(p_CmdList); // Bloom

  return downscale1;
}
//---------------------------------------------------------------------------//
} // namespace

void PostProcessor::init()
{
  g_PostFxHelper.init();

  // Load and compile shaders:
  {
    ID3DBlobPtr shaderBlob;
    ID3DBlobPtr errorBlob;

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
    shaderPath += L"Shaders\\PostFx.hlsl";

    // Tone mapping shader
    {
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "ToneMap",
          "ps_5_1",
          compileFlags,
          0,
          &shaderBlob,
          &errorBlob);
      if (nullptr == shaderBlob)
      {
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        assert(false && "Shader compilation failed");
      }
      errorBlob = nullptr;
      g_ToneMapShader.pShaderBytecode = shaderBlob->GetBufferPointer();
      g_ToneMapShader.BytecodeLength = shaderBlob->GetBufferSize();
    }

    // Scale shader
    {
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "Scale",
          "ps_5_1",
          compileFlags,
          0,
          &shaderBlob,
          &errorBlob);
      if (nullptr == shaderBlob)
      {
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        assert(false && "Shader compilation failed");
      }
      errorBlob = nullptr;
      g_ScaleShader.pShaderBytecode = shaderBlob->GetBufferPointer();
      g_ScaleShader.BytecodeLength = shaderBlob->GetBufferSize();
    }

    // BlurH shader
    {
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "BlurH",
          "ps_5_1",
          compileFlags,
          0,
          &shaderBlob,
          &errorBlob);
      if (nullptr == shaderBlob)
      {
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        assert(false && "Shader compilation failed");
      }
      errorBlob = nullptr;
      g_BlurHShader.pShaderBytecode = shaderBlob->GetBufferPointer();
      g_BlurHShader.BytecodeLength = shaderBlob->GetBufferSize();
    }

    // BlurV shader
    {
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "BlurV",
          "ps_5_1",
          compileFlags,
          0,
          &shaderBlob,
          &errorBlob);
      if (nullptr == shaderBlob)
      {
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        assert(false && "Shader compilation failed");
      }
      errorBlob = nullptr;
      g_BlurVShader.pShaderBytecode = shaderBlob->GetBufferPointer();
      g_BlurVShader.BytecodeLength = shaderBlob->GetBufferSize();
    }

    // Bloom shader
    {
      HRESULT hr = D3DCompileFromFile(
          shaderPath.c_str(),
          nullptr,
          D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "Bloom",
          "ps_5_1",
          compileFlags,
          0,
          &shaderBlob,
          &errorBlob);
      if (nullptr == shaderBlob)
      {
        if (errorBlob != nullptr)
          OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        assert(false && "Shader compilation failed");
      }
      errorBlob = nullptr;
      g_BloomShader.pShaderBytecode = shaderBlob->GetBufferPointer();
      g_BloomShader.BytecodeLength = shaderBlob->GetBufferSize();
    }
  }
}
void PostProcessor::deinit() { g_PostFxHelper.deinit(); }
void PostProcessor::render(
    ID3D12GraphicsCommandList* p_CmdList,
    const RenderTexture& p_Input,
    const RenderTexture& p_Output)
{
  g_PostFxHelper.begin(p_CmdList);

  TempRenderTarget* bloomTarget = bloom(p_CmdList, p_Input);

  // Apply tone mapping
  uint32_t inputs[2] = {p_Input.srv(), bloomTarget->m_RenderTarget.srv()};
  const RenderTexture* outputs[1] = {&p_Output};
  g_PostFxHelper.postProcess(
      g_ToneMapShader,
      "Tone Mapping",
      inputs,
      arrayCount32(inputs),
      outputs,
      arrayCount32(outputs));

  bloomTarget->m_InUse = false;

  g_PostFxHelper.end();
}
