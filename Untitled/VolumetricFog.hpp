#pragma once

#include "Common/D3D12Wrapper.hpp"
#include <Camera.hpp>

struct VolumetricFog
{
  void init(ID3D12Device* p_Device);
  void deinit();

  void render(
      ID3D12GraphicsCommandList* p_CmdList,
      uint32_t p_ClusterBufferSrv,
      uint32_t p_DepthBufferSrv,
      uint32_t p_SpotLightShadowSrv,
      FirstPersonCamera const & p_Camera
    );

  ID3DBlobPtr m_DataInjectionShader = nullptr;
  ID3DBlobPtr m_LightContributionShader = nullptr;
  ID3DBlobPtr m_FinalIntegralShader = nullptr;

  std::vector<ID3D12PipelineState*> m_PSOs;
  ID3D12RootSignature* m_RootSig = nullptr;

  VolumeTexture m_DataVolume;
};
