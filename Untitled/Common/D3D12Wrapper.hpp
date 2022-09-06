#pragma once

#include "Utility.hpp"

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
