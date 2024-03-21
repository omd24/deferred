
#include "PostProcessor.hpp"
#include "pix3.h"

namespace /*Internal*/
{
PostFxHelper g_PostFxHelper;
}

TempRenderTarget*
PostProcessor::bloom(ID3D12GraphicsCommandList* p_CmdList, const RenderTexture& p_Input)
{
  PIXBeginEvent(p_CmdList, 0, "Bloom");

  const uint64_t bloomWidth = p_Input.m_Texture.Width / 2;
  const uint64_t bloomHeight = p_Input.m_Texture.Height / 2;

  TempRenderTarget* downscale1 =
      g_PostFxHelper.getTempRenderTarget(bloomWidth, bloomHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
  downscale1->m_RenderTarget.makeWritable(p_CmdList);

  g_PostFxHelper.postProcess(m_BloomShader, "Bloom Initial Pass", p_Input, downscale1);

  TempRenderTarget* blurTemp =
      g_PostFxHelper.getTempRenderTarget(bloomWidth, bloomHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
  downscale1->m_RenderTarget.makeReadable(p_CmdList);

  // Blur pass
  for (uint64_t i = 0; i < 2; ++i)
  {
    blurTemp->m_RenderTarget.makeWritable(p_CmdList);

    g_PostFxHelper.postProcess(m_BlurHShader, "Horizontal Bloom Blur", downscale1, blurTemp);

    blurTemp->m_RenderTarget.makeReadable(p_CmdList);
    downscale1->m_RenderTarget.makeWritable(p_CmdList);

    g_PostFxHelper.postProcess(m_BlurVShader, "Vertical Bloom Blur", blurTemp, downscale1);

    downscale1->m_RenderTarget.makeReadable(p_CmdList);
  }

  blurTemp->m_InUse = false;
  PIXEndEvent(p_CmdList); // Bloom

  return downscale1;
}
//---------------------------------------------------------------------------//
void PostProcessor::init()
{
  g_PostFxHelper.init();

  // Load and compile shaders:
  {
    bool res = true;
    ID3DBlobPtr errorBlob;
    ID3DBlobPtr tempToneMapShader = nullptr;
    ID3DBlobPtr tempScaleShader = nullptr;
    ID3DBlobPtr tempBloomShader = nullptr;
    ID3DBlobPtr tempBlurHShader = nullptr;
    ID3DBlobPtr tempBlurVShader = nullptr;

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
      compileShader(
          "tone mapping",
          shaderPath.c_str(),
          nullptr,
          compileFlags,
          ShaderType::Pixel,
          "ToneMap",
          m_ToneMapShader);
    }

    // Scale shader
    {
      compileShader(
          "scale fragment",
          shaderPath.c_str(),
          nullptr,
          compileFlags,
          ShaderType::Pixel,
          "Scale",
          m_ScaleShader);
    }

    // BlurH shader
    {
      compileShader(
          "horizontal blur",
          shaderPath.c_str(),
          nullptr,
          compileFlags,
          ShaderType::Pixel,
          "BlurH",
          m_BlurHShader);
    }

    // BlurV shader
    {
      compileShader(
          "vertical blur",
          shaderPath.c_str(),
          nullptr,
          compileFlags,
          ShaderType::Pixel,
          "BlurV",
          m_BlurVShader);
    }

    // Bloom shader
    {
      compileShader(
          "bloom pixel",
          shaderPath.c_str(),
          nullptr,
          compileFlags,
          ShaderType::Pixel,
          "Bloom",
          m_BloomShader);
    }

    assert(m_ToneMapShader && m_ScaleShader && m_BloomShader && m_BlurHShader && m_BlurVShader);
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
      m_ToneMapShader,
      "Tone Mapping",
      inputs,
      arrayCount32(inputs),
      outputs,
      arrayCount32(outputs));

  bloomTarget->m_InUse = false;

  g_PostFxHelper.end();
}
