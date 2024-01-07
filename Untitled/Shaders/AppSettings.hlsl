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
};

ConstantBuffer<AppSettingsCbuffer> AppSettings : register(b12);


// Global settings
static const uint MaxSpotLights = 32;
static const float SpotShadowNearClip = 0.1000f;