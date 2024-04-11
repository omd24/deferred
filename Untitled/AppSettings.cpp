#include "AppSettings.hpp"

namespace AppSettings
{
glm::vec3 SunDirection = glm::vec3(0.2600f, 0.9870f, -0.1600f);
float SunSize = 1.0f;
glm::vec3 GroundAlbedo = glm::vec3(0.25f, 0.25f, 0.25f);
float Turbidity = 2.0f;

bool32 EnableTAA = false;
bool32 EnableSky = false;
uint64_t MaxLightClamp = 32;
bool32 RenderLights = true;
bool32 ComputeUVGradients = true;
float Exposure = -14.0f;
float BloomExposure = -4.0f;
float BloomMagnitude = 1.0f;
float BloomBlurSigma = 2.5f;
float CameraSpeed = 5.0f;
glm::vec3 CameraPosition = glm::vec3(0);
bool32 ShowAlbedoMaps = false;
bool32 ShowNormalMaps = false;
bool32 ShowSpecular = false;
bool32 ShowClusterVisualizer = false;
bool32 ShowLightCounts = false;
bool32 ShowUVGradients = false;
bool32 AnimateLightIntensity = false;
float LightColor[3] = {1.0f, 1.0f, 1.0f};
uint32_t CurrentFrame = 0;

// Volumetric fog:
bool32 FOG_UseLinearClamp = true;
bool32 FOG_SampleUsingTricubicFiltering = false;
bool32 FOG_DisableLightScattering = false;
bool32 FOG_UseClusteredLighting = false;
bool32 FOG_EnableShadowMapSampling = true;
bool32 FOG_EnableTemporalFilter = true;
bool32 FOG_ApplyXYJitter = false;
bool32 FOG_ApplyZJitter = false;
float FOG_ScatteringFactor = 0.5f;
float FOG_TemporalPercentage = 0.998f;
int32_t FOG_NoiseType = 0u;
float FOG_NoiseScale = 0.0f;
float FOG_JitterScaleXY = 0.0f;
float FOG_DitheringScale = 0.0f;
float FOG_LightingNoiseScale = 0.0f;
float FOG_ConstantFogDensityModifier = 0.0f;
float FOG_HeightFogDenisty = 0.5f;
float FOG_HeightFogFalloff = 0.3f;
float FOG_BoxSize = 0.5f;
float FOG_BoxPosition[3] = {0.0f, 2.0f, 0.0f};
float FOG_BoxFogDensity = .9f;
float FOG_BoxColor[3] = {0.0f, 1.0f, 0.0f};
float FOG_PhaseAnisotropy = 0.5f;
float FOG_Exponent = 30.0f;
float FOG_Offset = 0.0f;
glm::vec3 FOG_GridParams = glm::vec3(0.0f);
int32_t FOG_DepthMode = 0;

ConstantBuffer CBuffer;
const uint32_t CBufferRegister = 12;

void init()
{
  ConstantBufferInit cbInit;
  cbInit.Size = sizeof(AppSettingsCBuffer);
  cbInit.Dynamic = true;
  cbInit.Name = L"AppSettings Constant Buffer";
  CBuffer.init(cbInit);
}
void deinit() { CBuffer.deinit(); }
void updateCBuffer()
{
  AppSettingsCBuffer cbData = {};
  cbData.RenderLights = RenderLights;
  cbData.ComputeUVGradients = ComputeUVGradients;
  cbData.Exposure = Exposure;
  cbData.BloomExposure = BloomExposure;
  cbData.BloomMagnitude = BloomMagnitude;
  cbData.BloomBlurSigma = BloomBlurSigma;
  cbData.ShowAlbedoMaps = ShowAlbedoMaps;
  cbData.ShowNormalMaps = ShowNormalMaps;
  cbData.ShowSpecular = ShowSpecular;
  cbData.ShowLightCounts = ShowLightCounts;
  cbData.ShowUVGradients = ShowUVGradients;
  cbData.AnimateLightIntensity = AnimateLightIntensity;
  cbData.CurrentFrame = CurrentFrame;

  cbData.EnableSky = EnableSky;

  // Volumetric data
  cbData.FOG_UseLinearClamp = FOG_UseLinearClamp;
  cbData.FOG_SampleUsingTricubicFiltering = FOG_SampleUsingTricubicFiltering;
  cbData.FOG_DisableLightScattering = FOG_DisableLightScattering;
  cbData.FOG_UseClusteredLighting = FOG_UseClusteredLighting;
  cbData.FOG_EnableShadowMapSampling = FOG_EnableShadowMapSampling;
  cbData.FOG_EnableTemporalFilter = FOG_EnableTemporalFilter;
  cbData.FOG_ApplyXYJitter = FOG_ApplyXYJitter;
  cbData.FOG_ApplyZJitter = FOG_ApplyZJitter;
  cbData.FOG_ScatteringFactor = FOG_ScatteringFactor;
  cbData.FOG_TemporalPercentage = FOG_TemporalPercentage;
  cbData.FOG_NoiseType = FOG_NoiseType;
  cbData.FOG_NoiseScale = FOG_NoiseScale;
  cbData.FOG_JitterScaleXY = FOG_JitterScaleXY;
  cbData.FOG_DitheringScale = FOG_DitheringScale;
  cbData.FOG_LightingNoiseScale = FOG_LightingNoiseScale;
  cbData.FOG_ConstantFogDensityModifier = FOG_ConstantFogDensityModifier;
  cbData.FOG_HeightFogDenisty = FOG_HeightFogDenisty;
  cbData.FOG_HeightFogFalloff = FOG_HeightFogFalloff;
  cbData.FOG_BoxSize = FOG_BoxSize;
  cbData.FOG_BoxFogDensity = FOG_BoxFogDensity;
  cbData.FOG_PhaseAnisotropy = FOG_PhaseAnisotropy;
  cbData.FOG_GridParams = FOG_GridParams;
  cbData.FOG_DepthMode = FOG_DepthMode;

  // Box position (float3)
  memcpy(cbData.FOG_BoxPosition, FOG_BoxPosition, sizeof(cbData.FOG_BoxPosition));
  static_assert(12 == sizeof(cbData.FOG_BoxPosition));

  // Box color (float3)
  memcpy(cbData.FOG_BoxColor, FOG_BoxColor, sizeof(cbData.FOG_BoxColor));
  static_assert(12 == sizeof(cbData.FOG_BoxColor));

  // Light color (float3)
  memcpy(cbData.LightColor, LightColor, sizeof(cbData.LightColor));
  static_assert(12 == sizeof(cbData.LightColor));

  CBuffer.mapAndSetData(&cbData, sizeof(AppSettingsCBuffer));
}
void bindCBufferGfx(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter)
{
  CBuffer.setAsGfxRootParameter(cmdList, rootParameter);
}
void bindCBufferCompute(ID3D12GraphicsCommandList* cmdList, uint32_t rootParameter)
{
  CBuffer.setAsComputeRootParameter(cmdList, rootParameter);
}

uint64_t NumXTiles = 0;
uint64_t NumYTiles = 0;

} // namespace AppSettings
