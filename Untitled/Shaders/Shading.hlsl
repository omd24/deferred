#include "BRDF.hlsl"

// Max value that we can store in an fp16 buffer (actually a little less so that we have room for error, real max is 65504)
static const float FP16Max = 65000.0f;

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
//-------------------------------------------------------------------------------------------------
// Calculates the full shading result for a single pixel.
//-------------------------------------------------------------------------------------------------
float3 ShadePixel(in ShadingInput input)
{
    float3 vtxNormalWS = input.TangentFrame._m20_m21_m22;
    float3 normalWS = vtxNormalWS;
    float3 positionWS = input.PositionWS;

    const ShadingConstants CBuffer = input.ShadingCBuffer;

    float3 viewWS = normalize(CBuffer.CameraPosWS - positionWS);

    if(true) // TODO: toggle normal mapping
    {
        // Sample the normal map, and convert the normal to world space
        float3 normalTS;
        normalTS.xy = input.NormalMap * 2.0f - 1.0f;
        normalTS.z = sqrt(1.0f - saturate(normalTS.x * normalTS.x + normalTS.y * normalTS.y));
        normalWS = normalize(mul(normalTS, input.TangentFrame));
    }

    float4 albedoMap =  1.0f;
    if(true) // TODO: toggle albedo usage
        albedoMap = input.AlbedoMap;

    float metallic = saturate(input.MetallicMap);
    float3 diffuseAlbedo = lerp(albedoMap.xyz, 0.0f, metallic);
    float3 specularAlbedo = lerp(0.03f, albedoMap.xyz, metallic) * (true ? 1.0f : 0.0f); // TODO: toggle specular

    float roughnessMap = input.RoughnessMap;
    float roughness = roughnessMap * roughnessMap;

    float depthVS = input.DepthVS;

    // TODO: calculate cluster id:

    float3 positionNeighborX = input.PositionWS + input.PositionWS_DX;
    float3 positionNeighborY = input.PositionWS + input.PositionWS_DY;

    // Add in the primary directional light
    float3 output = 0.0f;

    // TODO:
    // if(true) // enable sun
    // {

    // }

    // Apply the spot lights
    uint numLights = 0;
    if(true) // TODO: toggle lights
    {
        // TODO: Shadow

        // TODO: cluster lights

        for (uint i = 0; i < MaxSpotLights; ++i)
        {
            SpotLight spotLight = input.LightCBuffer.Lights[i];
            float3 surfaceToLight = spotLight.Position - positionWS;
            float distanceToLight = length(surfaceToLight);
            surfaceToLight /= distanceToLight;
            float angleFactor = saturate(dot(surfaceToLight, spotLight.Direction));
            float angularAttenuation = smoothstep(spotLight.AngularAttenuationY, spotLight.AngularAttenuationX, angleFactor);

            if(angularAttenuation > 0.0f)
            {
                float d = distanceToLight / spotLight.Range;
                float falloff = saturate(1.0f - (d * d * d * d));
                falloff = (falloff * falloff) / (distanceToLight * distanceToLight + 1.0f);
                float3 intensity = spotLight.Intensity * angularAttenuation * falloff;

                output += CalcLighting(normalWS, surfaceToLight, intensity, diffuseAlbedo, specularAlbedo,
                                           roughness, positionWS, CBuffer.CameraPosWS);
            }
        }
    }

    // return albedoMap.xyz;

    float3 ambient = 1.0f; // TODO!
    output += ambient * diffuseAlbedo;

    output = clamp(output, 0.0f, FP16Max);

    // DEBUG:
    // return albedoMap.xyz;

    return output;
}