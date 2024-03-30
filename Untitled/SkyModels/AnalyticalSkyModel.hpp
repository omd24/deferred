//=================================================================================================
//
//  from MJP's DX12 Sample Framework
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

#pragma once

#include "Utility.hpp"
#include "SphericalHarmonics.hpp"

// HosekSky forward declares
struct ArHosekSkyModelState;

// Cached data for the procedural sky model
struct SkyCache
{
  ArHosekSkyModelState* StateR = nullptr;
  ArHosekSkyModelState* StateG = nullptr;
  ArHosekSkyModelState* StateB = nullptr;
  glm::vec3 SunDirection;
  glm::vec3 SunRadiance;
  glm::vec3 SunIrradiance;
  float SunSize = 0.0f;
  float Turbidity = 0.0f;
  glm::vec3 Albedo;
  float Elevation = 0.0f;
  Texture CubeMap;
  SH9Color sh;

  void Init(
      const glm::vec3& sunDirection,
      float sunSize,
      const glm::vec3& groundAlbedo,
      float turbidity,
      bool createCubemap);
  void Shutdown();
  ~SkyCache();

  bool Initialized() const { return StateR != nullptr; }

  glm::vec3 Sample(glm::vec3 sampleDir) const;
};

class Skybox
{

public:
  Skybox();
  ~Skybox();

  void Initialize();
  void Shutdown();

  void CreatePSOs(DXGI_FORMAT rtFormat, DXGI_FORMAT depthFormat, uint32_t numMSAASamples);
  void DestroyPSOs();

  void RenderEnvironmentMap(
      ID3D12GraphicsCommandList* cmdList,
      const glm::mat4& view,
      const glm::mat4& projection,
      const Texture* environmentMap,
      glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f));

  void RenderSky(
      ID3D12GraphicsCommandList* cmdList,
      const glm::mat4& view,
      const glm::mat4& projection,
      const SkyCache& skyCache,
      bool enableSun = true,
      const glm::vec3& scale = glm::vec3(1.0f, 1.0f, 1.0f));

protected:
  void RenderCommon(
      ID3D12GraphicsCommandList* cmdList,
      const Texture* environmentMap,
      const glm::mat4& view,
      const glm::mat4& projection,
      glm::vec3 scale);

  struct VSConstants
  {
    glm::mat4 View;
    glm::mat4 Projection;
  };

  struct PSConstants
  {
    glm::vec3 SunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float CosSunAngularRadius = 0.0f;
    glm::vec3 SunColor = glm::vec3(1.0f);
    uint32_t Padding = 0;
    glm::vec3 Scale = glm::vec3(1.0f);
    uint32_t EnvMapIdx = uint32_t(-1);
  };

  ID3DBlobPtr vertexShader;
  ID3DBlobPtr pixelShader;
  StructuredBuffer vertexBuffer;
  FormattedBuffer indexBuffer;
  VSConstants vsConstants;
  PSConstants psConstants;
  ID3D12RootSignature* rootSignature;
  ID3D12PipelineState* pipelineState;
};
