#pragma once

#include "Common/D3D12Wrapper.hpp"

struct PostProcessor
{
  void init();
  void deinit();
  void render(
      ID3D12GraphicsCommandList* p_CmdList,
      const RenderTexture& p_Input,
      const RenderTexture& p_Output);
};
