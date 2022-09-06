#include "MeshRenderer.hpp"

//---------------------------------------------------------------------------//
bool MeshRenderer::_createPSOs()
{
  // TODO:

  return true;
}
void MeshRenderer::_loadAssets()
{
  // TODO:

  // Create Root Signatures

  _createPSOs();
}
//---------------------------------------------------------------------------//
void OnInit(const Model* sceneModel)
{
  // TODO: init the assimp model
}

void MeshRenderer::onLoad(ID3D12GraphicsCommandList* p_CmdList)
{
  DEBUG_BREAK(p_CmdList != nullptr);


  // Load assets
  _loadAssets();
}
void MeshRenderer::onDestroy()
{
  m_GBufferPSO->Release();
  m_MainPassPSO->Release();
  m_GBufferRootSig->Release();
  m_MainPassRootSig->Release();
}
void MeshRenderer::onRenderGbuffer(ID3D12GraphicsCommandList* p_CmdList)
{
  uint64_t numVisible = 0;

  // TODO: Frustum cull meshes

  p_CmdList->SetGraphicsRootSignature(m_GBufferRootSig);
  p_CmdList->SetPipelineState(m_GBufferPSO);
  p_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  // Set constant buffers
  // TODO: Bind mesh constants

  // Bind vertices and indices
  // TODO: Bind mesh VB and VI

  // Draw all visible meshes
  uint32_t currMaterial = uint32_t(-1);
  for (uint64_t i = 0; i < numVisible; ++i)
  {
    uint64_t meshIdx = m_MeshDrawIndices[i];
    // const Mesh& mesh = model->Meshes()[meshIdx];

    // Draw all parts
    // TODO:...
    /*for (uint64_t partIdx = 0; partIdx < mesh.NumMeshParts(); ++partIdx)
    {
      const MeshPart& part = mesh.MeshParts()[partIdx];
      if (part.MaterialIdx != currMaterial)
      {
        p_CmdList->SetGraphicsRoot32BitConstant(1, part.MaterialIdx, 0);
        currMaterial = part.MaterialIdx;
      }
      p_CmdList->DrawIndexedInstanced(
          part.IndexCount,
          1,
          mesh.IndexOffset() + part.IndexStart,
          mesh.VertexOffset(),
          0);
    }*/
  }
}
void MeshRenderer::onRenderMainPass(ID3D12GraphicsCommandList* p_CmdList)
{
  uint64_t numVisible = 0;

  // TODO: Frustum cull meshes

  p_CmdList->SetGraphicsRootSignature(m_MainPassRootSig);
  p_CmdList->SetPipelineState(m_MainPassPSO);
  p_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


  // TODO: Bind descriptor table

  // Set constant buffers
  // TODO: Bind uniforms/constants

  // TODO: set Pixel shader SRVs
  /*uint32_t psSRVs[] = {
      sunShadowMap.SRV(),
      spotLightShadowMap.SRV(),
      materialTextureIndices.SRV,
      mainPassData.DecalBuffer->SRV,
      mainPassData.DecalClusterBuffer->SRV,
      mainPassData.SpotLightClusterBuffer->SRV,
  };*/

  //DX12::BindTempConstantBuffer(
  //    p_CmdList, psSRVs, MainPass_SRVIndices, CmdListMode::Graphics);

  // Bind vertices and indices
  // TODO: Bind model VB and VI

  // Draw all visible meshes
  uint32_t currMaterial = uint32_t(-1);
  for (uint64_t i = 0; i < numVisible; ++i)
  {
    // TODO:
    
    //uint64_t meshIdx = meshDrawIndices[i];
    //const Mesh& mesh = model->Meshes()[meshIdx];

    //// Draw all parts
    //for (uint64_t partIdx = 0; partIdx < mesh.NumMeshParts(); ++partIdx)
    //{
    //  const MeshPart& part = mesh.MeshParts()[partIdx];
    //  if (part.MaterialIdx != currMaterial)
    //  {
    //    p_CmdList->SetGraphicsRoot32BitConstant(
    //        MainPass_MatIndexCBuffer, part.MaterialIdx, 0);
    //    currMaterial = part.MaterialIdx;
    //  }
    //  p_CmdList->DrawIndexedInstanced(
    //      part.IndexCount,
    //      1,
    //      mesh.IndexOffset() + part.IndexStart,
    //      mesh.VertexOffset(),
    //      0);
    //}
  }
}
void MeshRenderer::onShaderChange()
{
  OutputDebugStringA("[MeshRenderer] Starting shader reload...\n");
  if (_createPSOs())
  {
    OutputDebugStringA("[MeshRenderer] Shaders loaded\n");
    return;
  }
  else
  {
    OutputDebugStringA("[MeshRenderer] Failed to reload the shaders\n");
  }
}

//---------------------------------------------------------------------------//
