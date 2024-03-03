#pragma once

#include "Common/D3D12Wrapper.hpp"
#include <Camera.hpp>

struct VolumetricFog
{
  void init(ID3D12Device* p_Device);
  void deinit();

  // Helper wrapper for rendering parameters
  struct RenderDesc
  {
    uint32_t ClusterBufferSrv;
    uint32_t DepthBufferSrv;
    uint32_t SpotLightShadowSrv;
    uint32_t UVMapSrv;
    uint32_t TangentFrameSrv;
    uint32_t MaterialIdMapSrv;

    float Near;
    float Far;
    float ScreenWidth;
    float ScreenHeight;

    FirstPersonCamera Camera;
    ConstantBuffer LightsBuffer;
  };

  void render(ID3D12GraphicsCommandList* p_CmdList, const RenderDesc& p_RenderDesc);

  ID3DBlobPtr m_DataInjectionShader = nullptr;
  ID3DBlobPtr m_LightContributionShader = nullptr;
  ID3DBlobPtr m_FinalIntegralShader = nullptr;

  std::vector<ID3D12PipelineState*> m_PSOs;
  ID3D12RootSignature* m_RootSig = nullptr;

  VolumeTexture m_DataVolume;
  VolumeTexture m_ScatteringVolume;
  VolumeTexture m_FinalVolume;

  static constexpr glm::uvec3 m_Dimensions = {128, 128, 128};
};
