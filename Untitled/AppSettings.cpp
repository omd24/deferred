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
