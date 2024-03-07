struct AppSettingsCbuffer
{
  bool RenderLights;
  bool ComputeUVGradients;
  float Exposure;
  float BloomExposure;

  float BloomMagnitude;
  float BloomBlurSigma;
  bool ShowAlbedoMaps;
  bool ShowNormalMaps;

  bool ShowSpecular;
  bool ShowLightCounts;
  bool ShowUVGradients;
  bool AnimateLightIntensity;

  float3 LightColor;

  // Volumetric fog settings
  bool FOG_UseLinearClamp;

  bool FOG_DisableLightScattering;
  bool FOG_UseClusteredLighting;
  bool FOG_EnableShadowMapSampling;
  bool FOG_EnableTemporalFilter;

  float FOG_ScatteringFactor;
  float FOG_TemporalPercentage;
  float unused1;
  float unused2;

  float FOG_ConstantFogDensityModifier;
  float FOG_HeightFogDenisty;
  float FOG_HeightFogFalloff;
  float FOG_BoxSize;

  float3 FOG_BoxPosition;
  float FOG_BoxFogDensity;

  float3 FOG_BoxColor;
  float FOG_PhaseAnisotropy;
};

ConstantBuffer<AppSettingsCbuffer> AppSettings : register(b12);


// Global settings
static const uint ClusterTileSize = 16;
static const uint NumZTiles = 16;
static const uint MaxSpotLights = 32;
static const float SpotShadowNearClip = 0.1000f;
static const uint SpotLightElementsPerCluster = 1;