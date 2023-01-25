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