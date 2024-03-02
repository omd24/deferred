#include "AppSettings.hpp"

namespace AppSettings
{
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

// Volumetric fog:
bool32 FOG_UseLinearClamp = true;
bool32 FOG_DisableLightScattering = false;
float FOG_ScatteringFactor = 1.0f;
float FOG_ConstantFogDensityModifier = 0.05f;
float FOG_HeightFogDenisty = 1.0f;
float FOG_HeightFogFalloff = 1.0f;
float FOG_BoxPosition[3] = {0.0f, 2.0f, 0.0f};
float FOG_BoxFogDensity = .2f;

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
  
  // Volumetric data
  cbData.FOG_UseLinearClamp = FOG_UseLinearClamp;
  cbData.FOG_DisableLightScattering = FOG_DisableLightScattering;
  cbData.FOG_ScatteringFactor = FOG_ScatteringFactor;
  cbData.FOG_ConstantFogDensityModifier = FOG_ConstantFogDensityModifier;
  cbData.FOG_HeightFogDenisty = FOG_HeightFogDenisty;
  cbData.FOG_HeightFogFalloff = FOG_HeightFogFalloff;
  cbData.FOG_BoxFogDensity = FOG_BoxFogDensity;

  // Box position (float3)
  memcpy(cbData.FOG_BoxPosition, FOG_BoxPosition, sizeof(cbData.FOG_BoxPosition));
  static_assert(12 == sizeof(cbData.FOG_BoxPosition));

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
