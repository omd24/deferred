#pragma once

#include "Common/D3D12Wrapper.hpp"
#include <Camera.hpp>

struct MotionVector
{
  void init(ID3D12Device* p_Device, uint32_t w, uint32_t h);
  void deinit(bool p_ReleaseResources);

  // Helper wrapper for rendering parameters
  struct RenderDesc
  {
    uint32_t DepthMapIdx;
    glm::vec2 JitterXY;
    glm::vec2 PreviousJitterXY;
    FirstPersonCamera Camera;
    glm::mat4 PrevViewProj;
  };

  void render(
    ID3D12GraphicsCommandList* p_CmdList,
    const RenderDesc& p_RenderDesc);

  ID3DBlobPtr m_MotionVectorShader = nullptr;

  std::vector<ID3D12PipelineState*> m_PSOs;
  ID3D12RootSignature* m_RootSig = nullptr;
  RenderTexture m_uavTarget;
};
