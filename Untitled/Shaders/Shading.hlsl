#include "BRDF.hlsl"

static const uint MaxSpotLights = 32;

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

struct ShadingConstants
{
    float3 CameraPosWS;

    uint NumXTiles;
    uint NumXYTiles;
    float NearClip;
    float FarClip;
};

struct ShadingInput
{
    uint2 PositionSS;
    float3 PositionWS;
    float3 PositionWS_DX;
    float3 PositionWS_DY;
    float DepthVS;
    float3x3 TangentFrame;

    float4 AlbedoMap;
    float2 NormalMap;
    float RoughnessMap;
    float MetallicMap;

    SamplerState AnisoSampler;

    ShadingConstants ShadingCBuffer;
    LightConstants LightCBuffer;
};

//-------------------------------------------------------------------------------------------------
// Calculates the lighting result for an analytical light source
//-------------------------------------------------------------------------------------------------
float3 CalcLighting(in float3 normal, in float3 lightDir, in float3 peakIrradiance,
                    in float3 diffuseAlbedo, in float3 specularAlbedo, in float roughness,
                    in float3 positionWS, in float3 cameraPosWS)
{
    float3 lighting = diffuseAlbedo * (1.0f / 3.14159f);

    float3 view = normalize(cameraPosWS - positionWS);
    const float nDotL = saturate(dot(normal, lightDir));
    if(nDotL > 0.0f)
    {
        const float nDotV = saturate(dot(normal, view));
        float3 h = normalize(view + lightDir);
        float specular = GGX_Specular(specularAlbedo, roughness, normal, h, view, lightDir);
        lighting += specular;
    }

    return lighting * nDotL * peakIrradiance;
}