#pragma once

#include <D3D12Wrapper.hpp>

typedef uint32_t bool32;

namespace AppSettings
{
static const uint64_t ClusterTileSize = 16;
static const uint64_t NumZTiles = 16;
static const uint64_t SpotLightElementsPerCluster = 1;

static const uint64_t MaxSpotLights = 32;
static const uint64_t MaxLightClamp = 32;
static const float SpotLightRange = 7.5000f;
static const float SpotShadowNearClip = 0.1000f;

extern bool32 RenderLights;
extern bool32 ComputeUVGradients;
extern float Exposure;
extern float BloomExposure;
extern float BloomMagnitude;
extern float BloomBlurSigma;
extern bool32 ShowAlbedoMaps;
extern bool32 ShowNormalMaps;
extern bool32 ShowSpecular;
extern bool32 ShowLightCounts;
extern bool32 ShowUVGradients;
extern bool32 AnimateLightIntensity;

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
