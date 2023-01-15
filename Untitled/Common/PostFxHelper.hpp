#pragma once

#include "D3D12Wrapper.hpp"

struct TempRenderTarget
{
  bool m_InUse = false;
  RenderTexture m_RenderTarget;
  uint64_t width() const { return m_RenderTarget.width(); }
  uint64_t height() const { return m_RenderTarget.height(); }
  DXGI_FORMAT format() const { return m_RenderTarget.format(); }
};

struct PostFxHelper
{
  PostFxHelper();
  ~PostFxHelper();

  void init();
  void deinit();

  TempRenderTarget* getTempRenderTarget(
      uint64_t p_Width, uint64_t p_Height, DXGI_FORMAT p_Format, bool p_UseAsUAV = false);

  void begin(ID3D12GraphicsCommandList* p_CmdList);
  void end();

  void postProcess(
      D3D12_SHADER_BYTECODE p_PixelShader,
      const char* p_Name,
      const RenderTexture& p_Input,
      const RenderTexture& p_Output);
  void postProcess(
      D3D12_SHADER_BYTECODE p_PixelShader,
      const char* p_Name,
      const RenderTexture& p_Input,
      const TempRenderTarget* p_Output);
  void postProcess(
      D3D12_SHADER_BYTECODE p_PixelShader,
      const char* p_Name,
      const TempRenderTarget* p_Input,
      const RenderTexture& p_Output);
  void postProcess(
      D3D12_SHADER_BYTECODE p_PixelShader,
      const char* p_Name,
      const TempRenderTarget* p_Input,
      const TempRenderTarget* p_Output);

  void postProcess(
      D3D12_SHADER_BYTECODE p_PixelShader,
      const char* p_Name,
      const uint32_t* p_Inputs,
      uint64_t p_NumInputs,
      const RenderTexture* const* p_Outputs,
      uint64_t p_NumOutputs);

private:
  std::vector<TempRenderTarget*> m_TempRenderTargets;
  std::vector<ID3D12PipelineState*> m_PSOs;
  D3D12_SHADER_BYTECODE m_FullscreenTriangleVS;
  ID3D12GraphicsCommandList* m_CmdList = nullptr;
  ID3D12RootSignature* m_RootSig = nullptr;
};
