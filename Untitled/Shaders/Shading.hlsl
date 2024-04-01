#include "GlobalResources.hlsl"
#include "Shadows.hlsl"
#include "BRDF.hlsl"
#include "AppSettings.hlsl"
#include "LightingHelpers.hlsl"

#include "VolumetricFogHelpers.hlsl"

struct SH9Color
{
	float3 c[9];
};

struct ShadingConstants
{
  float3 SunDirectionWS;
  float CosSunAngularRadius;
  float3 SunIrradiance;
  float SinSunAngularRadius;

  float3 CameraPosWS;
  uint NumXTiles;

  uint NumXYTiles;
  float NearClip;
  float FarClip;
  uint NumFroxelGridSlices;

  SH9Color SkySH;
};

struct ShadingInput
{
  uint2 PositionSS;
  float3 PositionWS;
  float3 PositionWS_DX;
  float3 PositionWS_DY;
  float DepthVS;
  float RawDepth;
  float3x3 TangentFrame;

  float4 AlbedoMap;
  float2 NormalMap;
  float RoughnessMap;
  float MetallicMap;

  ByteAddressBuffer SpotLightClusterBuffer;
  Texture3D         FogVolume;
  Texture2D         BlueNoiseTexture;

  SamplerState AnisoSampler;
  SamplerState LinearClampSampler;
  SamplerState LinearWrapSampler;

  ShadingConstants ShadingCBuffer;
  LightConstants LightCBuffer;

  float2 InvRTSize;
  uint CurrFrame;
  float DitheringScale;
};

//-------------------------------------------------------------------------------------------------
// Calculates the lighting result for an analytical light source
//-------------------------------------------------------------------------------------------------
float3 CalcLighting(
    in float3 normal,
    in float3 lightDir,
    in float3 peakIrradiance,
    in float3 diffuseAlbedo,
    in float3 specularAlbedo,
    in float roughness,
    in float3 positionWS,
    in float3 cameraPosWS)
{
  // diffuse term of BRDF model (Lambertian)
  float3 diffuse = diffuseAlbedo * (1.0f / Pi);

  float3 view = normalize(cameraPosWS - positionWS);
  float3 h = normalize(view + lightDir);
  const float nDotL = saturate(dot(normal, lightDir));
  float nDotV = saturate(dot(normal, view));

  if (nDotL > 0.0f)
  {
    float3 specular = GGX_Specular(specularAlbedo, roughness, normal, h, view, lightDir);
    // float3 specular = Beckmann_Specular(specularAlbedo, roughness, normal, h, view, lightDir);
    return (specular + diffuse) * nDotL * peakIrradiance;
  }
  else
  {
    return diffuse * nDotL * peakIrradiance;
  }
}
//-------------------------------------------------------------------------------------------------
// Calculates the full shading result for a single pixel.
//-------------------------------------------------------------------------------------------------
float3 ShadePixel(in ShadingInput input, in Texture2DArray spotLightShadowMap, in SamplerComparisonState shadowSampler)
{
    float3 vtxNormalWS = input.TangentFrame._m20_m21_m22;
    float3 normalWS = vtxNormalWS;
    float3 positionWS = input.PositionWS;
  
    const ShadingConstants CBuffer = input.ShadingCBuffer;
  
    float3 viewWS = normalize(CBuffer.CameraPosWS - positionWS);
  
    if (true) // TODO: toggle normal mapping
    {
      // Sample the normal map, and convert the normal to world space
      float3 normalTS;
      normalTS.xy = input.NormalMap * 2.0f - 1.0f;
      normalTS.z = sqrt(1.0f - saturate(normalTS.x * normalTS.x + normalTS.y * normalTS.y));
      normalWS = normalize(mul(normalTS, input.TangentFrame));
    }
  
    float4 albedoMap = 1.0f;
    if (true) // TODO: toggle albedo usage
      albedoMap = input.AlbedoMap;
  
    float metallic = saturate(input.MetallicMap);
    float3 diffuseAlbedo = lerp(albedoMap.xyz, 0.0f, metallic);
    float3 specularAlbedo = lerp(0.03f, albedoMap.xyz, metallic);
  
    float roughnessMap = input.RoughnessMap;
    float roughness = roughnessMap * roughnessMap;
  
    float depthVS = input.DepthVS;
  
    // Compute shared cluster lookup data
    uint2 pixelPos = uint2(input.PositionSS);
    float zRange = CBuffer.FarClip - CBuffer.NearClip;
    float normalizedZ = saturate((depthVS - CBuffer.NearClip) / zRange);
    uint zTile = normalizedZ * NumZTiles;
    uint3 tileCoords = uint3(pixelPos / ClusterTileSize, zTile);
    uint clusterIdx = (tileCoords.z * CBuffer.NumXYTiles) + (tileCoords.y * CBuffer.NumXTiles) + tileCoords.x;
    // clusterIdx = 0;
  
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
    if (AppSettings.RenderLights)
    {
      // Spotlight shadow
      float2 shadowMapSize;
      float numSlices;
      spotLightShadowMap.GetDimensions(shadowMapSize.x, shadowMapSize.y, numSlices);    
  
      uint clusterOffset = clusterIdx * SpotLightElementsPerCluster;
  
      // Loop over the number of 4-byte elements needed for each cluster
      [unroll]
      for (uint elemIdx = 0; elemIdx < SpotLightElementsPerCluster; ++elemIdx)
      {
        // Loop until we've processed every raised bit
        uint clusterElemMask = input.SpotLightClusterBuffer.Load((clusterOffset + elemIdx) * 4);
  
        while(clusterElemMask)
        {
          uint bitIdx = firstbitlow(clusterElemMask);
          clusterElemMask &= ~(1u << bitIdx);
          uint spotLightIdx = bitIdx + (elemIdx * 32);
          SpotLight spotLight = input.LightCBuffer.Lights[spotLightIdx];
        
          float3 surfaceToLight = spotLight.Position - positionWS;
          float distanceToLight = length(surfaceToLight);
          surfaceToLight /= distanceToLight;
          float angleFactor = saturate(dot(surfaceToLight, spotLight.Direction));
          float angularAttenuation =
            smoothstep(spotLight.AngularAttenuationY, spotLight.AngularAttenuationX, angleFactor);
  
          if (angularAttenuation > 0.0f)
          {
            float d = distanceToLight / spotLight.Range;
            float falloff = saturate(1.0f - (d * d * d * d));
            falloff = (falloff * falloff) / (distanceToLight * distanceToLight + 1.0f);
            float3 intensity = spotLight.Intensity * angularAttenuation * falloff;
  
            // Spotlight shadow visibility
            const float3 shadowPosOffset = GetShadowPosOffset(saturate(dot(vtxNormalWS, surfaceToLight)), vtxNormalWS, shadowMapSize.x);
  
            float spotLightVisibility = SpotLightShadowVisibility(positionWS, positionNeighborX, positionNeighborY,
                                                                input.LightCBuffer.ShadowMatrices[spotLightIdx],
                                                                spotLightIdx, shadowPosOffset, spotLightShadowMap, shadowSampler,
                                                                float2(SpotShadowNearClip, spotLight.Range));
  
            output += CalcLighting(
                normalWS,
                surfaceToLight,
                intensity,
                diffuseAlbedo,
                specularAlbedo,
                roughness,
                positionWS,
                CBuffer.CameraPosWS) * AppSettings.LightColor * (spotLightVisibility);
          }
  
          ++numLights;
        } // end of while(clusterElemMask)
      } // end of for(elemIdx : SpotLightElementsPerCluster)
    } // end of if(AppSettings.RenderLights)

  float3 ambient = 1.0f; // TODO!
  output += ambient * diffuseAlbedo;

  // Debug views:
  if (AppSettings.ShowAlbedoMaps)
  {
    return input.AlbedoMap.xyz;
  }
  if (AppSettings.ShowNormalMaps)
  {
    return normalWS;
  }
  if (AppSettings.ShowSpecular)
  {
    // return diffuseAlbedo.xyz;
    return specularAlbedo.xyz;
  }


    // Apply Fog
    float z = input.RawDepth;
    float2 invRTSize = input.InvRTSize;
    float2 screenUV = (pixelPos + 0.5f) * invRTSize;

    const float near = CBuffer.NearClip;
    const float far = CBuffer.FarClip;
    output = applyVolumetricFog(screenUV, z, near, far, CBuffer.NumFroxelGridSlices, input.FogVolume, input.LinearClampSampler, input.BlueNoiseTexture, input.LinearWrapSampler, output, input.CurrFrame, input.DitheringScale);

    // Clamp output value
    output = clamp(output, 0.0f, FP16Max);

  return output;
}