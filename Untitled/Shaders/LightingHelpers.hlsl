

#define PI 3.1415926538

struct SpotLight
{
  float3 Position;
  float AngularAttenuationX;

  float3 Direction;
  float AngularAttenuationY;

  float3 Intensity;
  float Range;
};

struct LightConstants
{
  SpotLight Lights[MaxSpotLights];
  float4x4 ShadowMatrices[MaxSpotLights];
};