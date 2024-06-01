#pragma once

#include "Common/D3D12Wrapper.hpp"
#include <Camera.hpp>

struct GpuDrivenRenderer
{
  void init(ID3D12Device* p_Device);
  void deinit();

  void render(ID3D12GraphicsCommandList* p_CmdList);

  void addMesh(int32_t p_IndexCount);

  ID3DBlobPtr m_DataShader = nullptr;

  std::vector<ID3D12PipelineState*> m_PSOs;
  ID3D12RootSignature* m_RootSig = nullptr;
};



