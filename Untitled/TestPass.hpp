#pragma once

#include "Common/D3D12Wrapper.hpp"
#include <Camera.hpp>

struct TestCompute
{
  void init(ID3D12Device* p_Device, uint32_t w, uint32_t h);
  void deinit(bool p_ReleaseResources);

  void render(
      ID3D12GraphicsCommandList* p_CmdList,
      const uint32_t p_SceneTexIdx,
      const uint32_t p_FogTexIdx,
      const uint32_t p_DepthTexIdx,
      float p_Near,
      float p_Far,
      float p_ScreenWidth,
      float p_ScreenHeight,
      glm::vec3 p_FogGridDims,
      FirstPersonCamera const& p_Camera);

  ID3DBlobPtr m_TestComputeShader = nullptr;

  std::vector<ID3D12PipelineState*> m_PSOs;
  ID3D12RootSignature* m_RootSig = nullptr;
  RenderTexture m_uavTarget;
  uint32_t m_targetWidth;
  uint32_t m_targetHeight;
};
