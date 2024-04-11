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
  uint CurrentFrame;

  bool EnableSky;
  float3 unused;

  // Volumetric fog settings
  bool FOG_UseLinearClamp;
  bool FOG_SampleUsingTricubicFiltering;
  int FOG_DepthMode;
  float unused2;

  bool FOG_DisableLightScattering;
  bool FOG_UseClusteredLighting;
  bool FOG_EnableShadowMapSampling;
  bool FOG_EnableTemporalFilter;

  float3 FOG_GridParams;
  bool FOG_ApplyXYJitter;

  bool FOG_ApplyZJitter;
  float FOG_ScatteringFactor;
  float FOG_TemporalPercentage;
  int FOG_NoiseType;

  float FOG_NoiseScale;
  float FOG_JitterScaleXY;
  float FOG_DitheringScale;
  float FOG_LightingNoiseScale;

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


// Max value that we can store in an fp16 buffer (actually a little less so that we have room for
// error, real max is 65504)
static const float FP16Max = 65000.0f;