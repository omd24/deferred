#pragma once

#include "Common/D3D12Wrapper.hpp"
#include "PostFxHelper.hpp"

struct PostProcessor
{
  void init();
  void deinit();
  void render(
      ID3D12GraphicsCommandList* p_CmdList,
      const RenderTexture& p_Input,
      const RenderTexture& p_Output);

  ID3DBlobPtr m_ToneMapShader = nullptr;
  ID3DBlobPtr m_ScaleShader = nullptr;
  ID3DBlobPtr m_BloomShader = nullptr;
  ID3DBlobPtr m_BlurHShader = nullptr;
  ID3DBlobPtr m_BlurVShader = nullptr;

private:
  TempRenderTarget* bloom(ID3D12GraphicsCommandList* p_CmdList, const RenderTexture& p_Input);
};
