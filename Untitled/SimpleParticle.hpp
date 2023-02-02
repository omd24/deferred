#pragma once

#include "Common/D3D12Wrapper.hpp"
#include "Common/Model.hpp"

struct SimpleParticle
{
  void init(DXGI_FORMAT p_OutputFormat, DXGI_FORMAT p_DepthFormat, const glm::vec3& p_CameraDir);
  void deinit();
  void render(
      ID3D12GraphicsCommandList* p_CmdList,
      const glm::mat4& p_ViewMat,
      const glm::mat4& p_ProjMat,
      const float p_DeltaTime);
  void createPSOs();
  void destroyPSOs();

private:
  ID3DBlobPtr m_DrawVS = nullptr;
  ID3DBlobPtr m_DrawPS = nullptr;
  ID3D12RootSignature* m_DrawRootSig = nullptr;
  ID3D12PipelineState* m_DrawPSO = nullptr;
  Model m_QuadModel;
  DXGI_FORMAT m_OutputFormat;
  DXGI_FORMAT m_DepthFormat;

  void compileShaders();
};
