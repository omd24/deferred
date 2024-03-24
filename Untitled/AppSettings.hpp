#pragma once

#include <D3D12Wrapper.hpp>

typedef uint32_t bool32;

namespace AppSettings
{
static const uint64_t ClusterTileSize = 16;
static const uint64_t NumZTiles = 16;
static const uint64_t SpotLightElementsPerCluster = 1;

static const uint64_t MaxSpotLights = 32;
static const float SpotLightRange = 7.5000f;
static const float SpotShadowNearClip = 0.1000f;

extern bool32 EnableTAA;
extern uint64_t MaxLightClamp;
extern bool32 RenderLights;
extern bool32 ComputeUVGradients;
extern float Exposure;
extern float BloomExposure;
extern float BloomMagnitude;
extern float BloomBlurSigma;
extern float CameraSpeed;
extern glm::vec3 CameraPosition;
extern bool32 ShowAlbedoMaps;
extern bool32 ShowNormalMaps;
extern bool32 ShowSpecular;
extern bool32 ShowLightCounts;
extern bool32 ShowClusterVisualizer;
extern bool32 ShowUVGradients;
extern bool32 AnimateLightIntensity;
extern float LightColor[3];
extern uint32_t CurrentFrame;

// Volumetric fog
extern bool32 FOG_UseLinearClamp;
extern bool32 FOG_SampleUsingTricubicFiltering;
extern int32_t FOG_DepthMode;
extern bool32 FOG_DisableLightScattering;
extern bool32 FOG_UseClusteredLighting;
extern bool32 FOG_EnableShadowMapSampling;
extern bool32 FOG_EnableTemporalFilter;
extern bool32 FOG_ApplyXYJitter;
extern bool32 FOG_ApplyZJitter;
extern float FOG_ScatteringFactor;
extern float FOG_TemporalPercentage;
extern int32_t FOG_NoiseType;
extern float FOG_NoiseScale;
extern float FOG_JitterScaleXY;
extern float FOG_DitheringScale;
extern float FOG_LightingNoiseScale;
extern float FOG_ConstantFogDensityModifier;
extern float FOG_HeightFogDenisty;
extern float FOG_HeightFogFalloff;
extern float FOG_BoxSize;
extern float FOG_BoxPosition[3];
extern float FOG_BoxFogDensity;
extern float FOG_BoxColor[3];
extern float FOG_PhaseAnisotropy;
extern float FOG_Exponent;
extern float FOG_Offset;
extern glm::vec3 FOG_GridParams;

// Be ware of the alignment rules:
// https://maraneshi.github.io/HLSL-ConstantBufferLayoutVisualizer/
struct AppSettingsCBuffer
{
  bool32 RenderLights;
  bool32 ComputeUVGradients;
  float Exposure;
  float BloomExposure;

  float BloomMagnitude;
  float BloomBlurSigma;
  bool32 ShowAlbedoMaps;
  bool32 ShowNormalMaps;

  bool32 ShowSpecular;
  bool32 ShowLightCounts;
  bool32 ShowUVGradients;
  bool32 AnimateLightIntensity;

  float LightColor[3];
  uint32_t CurrentFrame;

  // Volumetric fog
  bool32 FOG_UseLinearClamp;
  bool32 FOG_SampleUsingTricubicFiltering;
  int32_t FOG_DepthMode;
  float unused2;

  bool32 FOG_DisableLightScattering;
  bool32 FOG_UseClusteredLighting;
  bool32 FOG_EnableShadowMapSampling;
  bool32 FOG_EnableTemporalFilter;

  glm::vec3 FOG_GridParams;
  bool32 FOG_ApplyXYJitter;

  bool32 FOG_ApplyZJitter;
  float FOG_ScatteringFactor;
  float FOG_TemporalPercentage;
  int32_t FOG_NoiseType;

  float FOG_NoiseScale;
  float FOG_JitterScaleXY;
  float FOG_DitheringScale;
  float FOG_LightingNoiseScale;

  float FOG_ConstantFogDensityModifier;
  float FOG_HeightFogDenisty;
  float FOG_HeightFogFalloff;
  float FOG_BoxSize;

  float FOG_BoxPosition[3];
  float FOG_BoxFogDensity;

  float FOG_BoxColor[3];
  float FOG_PhaseAnisotropy;
};

extern ConstantBuffer CBuffer;
const extern uint32_t CBufferRegister;

void init();
void deinit();
void updateCBuffer();
void bindCBufferGfx(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter);
void bindCBufferCompute(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter);

extern uint64_t NumXTiles;
extern uint64_t NumYTiles;

}; // namespace AppSettings
