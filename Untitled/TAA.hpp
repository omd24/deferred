#pragma once

#include "Common/D3D12Wrapper.hpp"
#include <Camera.hpp>

struct TAARenderPass
{
  void init(ID3D12Device* p_Device, uint32_t w, uint32_t h);
  void deinit(bool p_ReleaseResources);

  void render(
      ID3D12GraphicsCommandList* p_CmdList,
      FirstPersonCamera const& p_Camera,
      const uint32_t p_SceneTexSrv, 
      const uint32_t p_MotionVectorsSrv);

  ID3DBlobPtr m_TAAShader = nullptr;

  std::vector<ID3D12PipelineState*> m_PSOs;
  ID3D12RootSignature* m_RootSig = nullptr;

  RenderTexture m_uavTargets[2];

  static int ms_CurrOutputTextureIndex;
  static int ms_PrevOutputTextureIndex;
};
