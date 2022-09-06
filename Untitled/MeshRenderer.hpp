#pragma once

#include "Utility.hpp"
#include "Model.hpp"

struct MainPassData
{
  float unused = 0.0f;
};
struct ShadingConstants
{
  float unused0 = 0.0f;
  float unused1 = 0.0f;
  float unused2 = 0.0f;
  float unused3 = 0.0f;
};

struct MeshRenderer
{
  MeshRenderer()
  {
    // Do nothing
  }

  //---------------------------------------------------------------------------//
  void OnInit(const Model* sceneModel);

  void onLoad(ID3D12GraphicsCommandList* p_CmdList);
  void onDestroy();
  void onRenderGbuffer(ID3D12GraphicsCommandList* p_CmdList);
  void onRenderMainPass(ID3D12GraphicsCommandList* p_CmdList);
  void onShaderChange();

  //---------------------------------------------------------------------------//
private:
  ID3D12PipelineState* m_MainPassPSO = nullptr;
  ID3D12RootSignature* m_MainPassRootSig = nullptr;
  ID3D12RootSignature* m_GBufferRootSig = nullptr;
  ID3D12PipelineState* m_GBufferPSO = nullptr;

  std::vector<uint32_t> m_MeshDrawIndices;

  bool _createPSOs();
  void _loadAssets();
};
