//=================================================================================================
// Includes
//=================================================================================================
#include "GlobalResources.hlsl"
#include "AppSettings.hlsl"
#include "LightingHelpers.hlsl"

//=================================================================================================
// Uniforms
//=================================================================================================
struct UniformConstants
{
  row_major float4x4 ProjMat;
  row_major float4x4 InvViewProj;

  float2 Resolution;
  float Near;
  float Far;

  uint ClusterBufferIdx;
  uint DepthBufferIdx;
  uint SpotLightShadowIdx;
  uint DataVolumeIdx;
  
  uint ScatterVolumeIdx;
  uint3 Dimensions;

  float3 CameraPos;

  uint NumXTiles;
  uint NumXYTiles;
};
ConstantBuffer<UniformConstants> CBuffer : register(b0);
ConstantBuffer<LightConstants> LightsBuffer : register(b1);

#define NUM_LIGHTS 32
#define NUM_BINS 16.0
#define BIN_WIDTH (1.0 / NUM_BINS)
#define TILE_SIZE 8
#define NUM_WORDS ( ( NUM_LIGHTS + 31 ) / 32 )

#define ubo_proj_matrix             CBuffer.ProjMat
#define ubo_inv_view_proj           CBuffer.InvViewProj
#define ubo_screen_resolution       CBuffer.Resolution
#define ubo_near_distance           CBuffer.Near
#define ubo_far_distance            CBuffer.Far
#define ubo_grid_dimensions         CBuffer.Dimensions
#define ubo_camera_pos              CBuffer.CameraPos
#define ubo_num_tiles_x             CBuffer.NumXTiles
#define ubo_num_tiles_xy            CBuffer.NumXYTiles
#define ubo_scattering_factor       AppSettings.FOG_ScatteringFactor
#define ubo_constant_fog_modifier   AppSettings.FOG_ConstantFogDensityModifier
#define ubo_height_fog_density      AppSettings.FOG_HeightFogDenisty
#define ubo_height_fog_falloff      AppSettings.FOG_HeightFogFalloff
#define ubo_box_position            AppSettings.FOG_BoxPosition
#define ubo_box_color               AppSettings.FOG_BoxColor
#define ubo_box_fog_density         AppSettings.FOG_BoxFogDensity
#define ubo_phase_anisotropy        AppSettings.FOG_PhaseAnisotropy

//=================================================================================================
// Resources
//=================================================================================================
// Samplers:
SamplerState PointSampler : register(s0);
SamplerState LinearClampSampler : register(s1);
SamplerState LinearWrapSampler : register(s2);
SamplerState LinearBorderSampler : register(s3);

// Render targets / UAVs
#if (DATA_INJECTION > 0)
  RWTexture3D<float4> DataVolumeTexture : register(u0);
#endif

#if (LIGHT_SCATTERING > 0)
  RWTexture3D<float4> ScatterVolumeTexture : register(u0);
#endif

#if (FINAL_INTEGRATION > 0)
  RWTexture3D<float4> FinalIntegrationVolume : register(u0);
#endif

// Exponential distribution as in https://advances.realtimerendering.com/s2016/Siggraph2016_idTech6.pdf Page 5.
// Convert slice index to (near...far) value distributed with exponential function.
float sliceToExponentialDepth( float near, float far, int slice, int numSlices ) {
    return near * pow( far / near, (float(slice) + 0.5f) / float(numSlices) );
}

// Convert rawDepth (0..1) to linear depth (near...far)
float rawDepthToLinearDepth( float rawDepth, float near, float far ) {
    return near * far / (far + rawDepth * (near - far));
}

// Convert linear depth (near...far) to rawDepth (0..1)
float linearDepthToRawDepth( float linearDepth, float near, float far ) {
    return ( near * far ) / ( linearDepth * ( near - far ) ) - far / ( near - far );
}

float4 worldFromFroxel(uint3 froxelCoord)
{
  float2 uv = (froxelCoord.xy + 0.5f) / float2(ubo_grid_dimensions.x, ubo_grid_dimensions.y);

#if 0 // THIS CAUSES A CAMERA-DEPENDENT ISSUE (also depends on fog USAGE code)
  float linearZ = (froxelCoord.z + 0.5f) / float(ubo_grid_dimensions.z);
#else
  float linearZ = sliceToExponentialDepth(ubo_near_distance, ubo_far_distance, froxelCoord.z, ubo_grid_dimensions.z);
#endif

  //float rawDepth = linearZ * ubo_proj_matrix._33 + ubo_proj_matrix._43 / linearZ;
  float rawDepth = linearDepthToRawDepth(linearZ, ubo_near_distance, ubo_far_distance);

  uv = 2.0f * uv - 1.0f;
  uv.y *= -1.0f;

  float4 worldPos = mul(float4(uv, rawDepth, 1.0f), ubo_inv_view_proj);
  worldPos.xyz /= worldPos.w;

  return worldPos;
}

float vectorToDepthValue( float3 direction, float radius, float rcpNminusF )
{
    const float3 absoluteVec = abs(direction);
    const float localZComponent = max(absoluteVec.x, max(absoluteVec.y, absoluteVec.z));

    const float f = radius;
    const float n = 0.01f;
    // Original value, left for reference.
    //const float normalizedZComponent = -(f / (n - f) - (n * f) / (n - f) / localZComponent);
    const float normalizedZComponent = ( n * f * rcpNminusF ) / localZComponent - f * rcpNminusF;
    return normalizedZComponent;
}

float attenuationSquareFalloff(float3 positionToLight, float lightInverseRadius)
{
    const float distanceSquare = dot(positionToLight, positionToLight);
    const float factor = distanceSquare * lightInverseRadius * lightInverseRadius;
    const float smoothFactor = max(1.0 - factor * factor, 0.0);
    return (smoothFactor * smoothFactor) / max(distanceSquare, 1e-4);
}

// Equations from http://patapom.com/topics/Revision2013/Revision%202013%20-%20Real-time%20Volumetric%20Rendering%20Course%20Notes.pdf
float henyeyGreenstein(float g, float costh)
{
    const float numerator = 1.0 - g * g;
    const float denominator = 4.0 * PI * pow(1.0 + g * g - 2.0 * g * costh, 3.0/2.0);
    return numerator / denominator;
}

float phaseFunction(float3 V, float3 L, float g)
{
    float cosTheta = dot(V, L);

    // TODO: compare other phase functions
    return henyeyGreenstein(g, cosTheta);
}


float4 scatteringExtinctionFromColorDensity(float3 color, float density )
{
    const float extinction = ubo_scattering_factor * density;
    return float4(color * extinction, extinction);
}

//=================================================================================================
// 1. Data injection
//=================================================================================================
#if (DATA_INJECTION > 0)
[numthreads(8, 8, 1)]
void DataInjectionCS(in uint3 DispatchID : SV_DispatchThreadID,
                     in uint3 GroupID : SV_GroupID) 
{   
    const uint3 froxelCoord = DispatchID;

    // transform froxel to world space
    float4 worldPos = worldFromFroxel(froxelCoord);
  
    float fogNoise = 1.0f; // TODO.

    float4 scatteringExtinction = (float4)0;
  
    // Add constant fog
    float constatnFogDensity = ubo_constant_fog_modifier * fogNoise;
    scatteringExtinction += scatteringExtinctionFromColorDensity((float3)0.5, constatnFogDensity);

    // Add height fog
    float heightfog = ubo_height_fog_density * exp(-ubo_height_fog_falloff * max(worldPos.y, 0)) * fogNoise;
    scatteringExtinction += scatteringExtinctionFromColorDensity((float3)0.5, heightfog);

    // Add density from box
    float3 boxSize = float3(1.0, 1.0, 1.0);
    float3 boxPos = ubo_box_position;
    float3 boxDist = abs(worldPos.xyz - boxPos);
    if (all(boxDist <= boxSize))
    {
        scatteringExtinction += scatteringExtinctionFromColorDensity(ubo_box_color, ubo_box_fog_density * fogNoise);
    }
  
    DataVolumeTexture[froxelCoord] = scatteringExtinction;
}
#endif //(DATA_INJECTION > 0)

#if (LIGHT_SCATTERING > 0)
//=================================================================================================
// 2. Light contribution
//=================================================================================================
[numthreads(8, 8, 1)]
void LightContributionCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    const uint3 froxelCoord = DispatchID;

    // Check coordinates boundaries
    float3 worldPos = worldFromFroxel(froxelCoord).xyz;

    float3 rcpFroxelDim = 1.0f / ubo_grid_dimensions.xyz;
    float3 fogDataUVW = froxelCoord * rcpFroxelDim;

    Texture3D fogData = Tex3DTable[CBuffer.DataVolumeIdx];

    float4 scatteringExtinction = float4(0, 0, 0, 0);
    if (AppSettings.FOG_UseLinearClamp)
        scatteringExtinction = fogData.SampleLevel(LinearClampSampler, fogDataUVW, 0);
    else // use load
        scatteringExtinction = fogData[froxelCoord];

    if (AppSettings.FOG_DisableLightScattering)
    {
        ScatterVolumeTexture[froxelCoord] = scatteringExtinction;

        // skip light scattering
        return;
    }

    float extinction = scatteringExtinction.a;
    float3 lighting = 1;

    ByteAddressBuffer spotLightClusterBuffer = RawBufferTable[CBuffer.ClusterBufferIdx];

    if (extinction >= 0.01f)
    {
        float3 V = normalize(ubo_camera_pos.xyz - worldPos);

        // Read clustered lighting data:

        // Calculate linear depth.
        float linearZ = froxelCoord.z * rcpFroxelDim.z;
        linearZ = rawDepthToLinearDepth(linearZ, ubo_near_distance, ubo_far_distance) / ubo_far_distance;

        // Either loop  through all lights or use clustering
        if (false == AppSettings.FOG_UseClusteredLighting) 
        {
            [unroll] for (uint spotLightIdx = 0; spotLightIdx < MaxSpotLights; ++spotLightIdx)
            {
                SpotLight spotLight = LightsBuffer.Lights[spotLightIdx];
                float3 surfaceToLight = spotLight.Position - worldPos;
                float distanceToLight = length(surfaceToLight);
                surfaceToLight /= distanceToLight;
                float angleFactor = saturate(dot(surfaceToLight, spotLight.Direction));
                float angularAttenuation =
                    smoothstep(spotLight.AngularAttenuationY, spotLight.AngularAttenuationX, angleFactor);
                
                if (angularAttenuation > 0.0f)
                {
                    float d = distanceToLight / spotLight.Range;
                    float falloff = saturate(1.0f - (d * d * d * d)) + 0.5;
                    falloff = (falloff * falloff) / (distanceToLight * distanceToLight + 1.0f);
                    float3 intensity = spotLight.Intensity * angularAttenuation * falloff;
    
                    float spotLightVisibility = 1.0f;
    
                    // Calculate phase function
                    const float3 L = surfaceToLight; // normalized
                    float p = phaseFunction(V, -L, ubo_phase_anisotropy);
    
                    lighting += AppSettings.LightColor * intensity * p * spotLightVisibility;
                }
            }
        } 
        else // Use clustered lighting for light scattering
        {
            // Compute shared cluster lookup data
            uint2 pixelPos = uint2(uint(froxelCoord.x * 1.0f / ubo_grid_dimensions.x * ubo_screen_resolution.x),
                                   uint(froxelCoord.y * 1.0f / ubo_grid_dimensions.y * ubo_screen_resolution.y));
            float zRange = ubo_far_distance - ubo_near_distance;
            float normalizedZ = saturate((linearZ - ubo_near_distance) / zRange);
            uint zTile = normalizedZ * NumZTiles;
            uint3 tileCoords = uint3(pixelPos / ClusterTileSize, zTile);
            uint clusterIdx = (tileCoords.z * ubo_num_tiles_xy) + (tileCoords.y * ubo_num_tiles_x) + tileCoords.x;

            uint clusterOffset = clusterIdx * SpotLightElementsPerCluster;

            // Loop over the number of 4-byte elements needed for each cluster
            [unroll]
            for (uint elemIdx = 0; elemIdx < SpotLightElementsPerCluster; ++elemIdx)
            {
                // Loop until we've processed every raised bit
                uint clusterElemMask = spotLightClusterBuffer.Load((clusterOffset + elemIdx) * 4);

                while (clusterElemMask)
                {
                    uint bitIdx = firstbitlow(clusterElemMask);
                    clusterElemMask &= ~(1u << bitIdx);
                    uint spotLightIdx = bitIdx + (elemIdx * 32);
                    SpotLight spotLight = LightsBuffer.Lights[spotLightIdx];
    
                    float3 surfaceToLight = spotLight.Position - worldPos;
                    float distanceToLight = length(surfaceToLight);
                    surfaceToLight /= distanceToLight;
                    float angleFactor = saturate(dot(surfaceToLight, spotLight.Direction));
                    float angularAttenuation =
                        smoothstep(spotLight.AngularAttenuationY, spotLight.AngularAttenuationX, angleFactor);
    
                    if (angularAttenuation > 0.0f)
                    {
                        float d = distanceToLight / spotLight.Range;
                        float falloff = saturate(1.0f - (d * d * d * d)) + 0.5;
                        falloff = (falloff * falloff) / (distanceToLight * distanceToLight + 1.0f);
                        float3 intensity = spotLight.Intensity * angularAttenuation * falloff;
    
                        float spotLightVisibility = 1.0f;
  
#if 0 //TODO: Shadow sampling
                    //
                    // Spotlight shadow visibility
                    const float3 shadowPosOffset = GetShadowPosOffset(saturate(dot(vtxNormalWS, surfaceToLight)), vtxNormalWS, shadowMapSize.x);

                    float spotLightVisibility = SpotLightShadowVisibility(worldPos, positionNeighborX, positionNeighborY,
                                                                        LightsBuffer.ShadowMatrices[spotLightIdx],
                                                                        spotLightIdx, shadowPosOffset, spotLightShadowMap, shadowSampler,
                                                                        float2(SpotShadowNearClip, spotLight.Range));

                    output += CalcLighting(
                        normalWS,
                        surfaceToLight,
                        intensity,
                        diffuseAlbedo,
                        specularAlbedo,
                        roughness,
                        worldPos,
                        ubo_camera_pos) * AppSettings.LightColor * (spotLightVisibility);
#endif  
                    // Calculate phase function
                    const float3 L = surfaceToLight; // normalized
                    float p = phaseFunction(V, -L, ubo_phase_anisotropy);

                    lighting += AppSettings.LightColor * intensity * p * spotLightVisibility;
                    }
                } // end of while(clusterElemMask)
            } // end of for(elemIdx : SpotLightElementsPerCluster)
        } // end of use clustered lighting
    } // end of if(extinction >= 0.01f)
    
    float3 scattering = scatteringExtinction.rgb * lighting;
    ScatterVolumeTexture[froxelCoord] = float4(scattering, extinction);
}
#endif //(LIGHT_SCATTERING > 0)

//=================================================================================================
// 3. Final integration
//=================================================================================================
#if (FINAL_INTEGRATION > 0)
[numthreads(8, 8, 1)]
void FinalIntegrationCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    uint3 froxelCoord = DispatchID;

    float3 integratedScattering = float3(0,0,0);
    float integratedTransmittance = 1.0f;

    float3 froxelDims = float3(ubo_grid_dimensions.x, ubo_grid_dimensions.y, ubo_grid_dimensions.z);
    float3 rcpFroxelDim = 1.0f / froxelDims.xyz;

#if 0
    Texture3D fog = Tex3DTable[CBuffer.ScatterVolumeIdx];
    for ( int z = 0; z < froxelDims.z; ++z )
    {
        froxelCoord.z = z;

        if (AppSettings.FOG_UseLinearClamp)
        {
            float3 sampledScatterng =
                fog.SampleLevel(LinearClampSampler, froxelCoord * rcpFroxelDim, 0).rgb;
            integratedScattering += sampledScatterng;
        }
        else
        {
            // Load
            integratedScattering += fog[froxelCoord].rgb;
        }

        FinalIntegrationVolume[froxelCoord] = float4(integratedScattering, 1.0);
    }

    return;
#endif

    float currentZ = 0;
    for ( int z = 0; z < froxelDims.z; ++z )
    {
        froxelCoord.z = z;
        float nextZ = sliceToExponentialDepth(ubo_near_distance, ubo_far_distance, z + 1, int(froxelDims.z));

        const float zStep = abs(nextZ - currentZ);
        currentZ = nextZ;

        // Following equations from Physically Based Sky, Atmosphere and Cloud Rendering by Hillaire
        Texture3D fogScatterData = Tex3DTable[CBuffer.ScatterVolumeIdx];
        float4 sampledScatteringExtinction = 0;

        if (AppSettings.FOG_UseLinearClamp)
        {
            sampledScatteringExtinction =
                fogScatterData.SampleLevel(LinearClampSampler, froxelCoord * rcpFroxelDim, 0);
        }
        else
        {
            // Load
            sampledScatteringExtinction = fogScatterData[froxelCoord];
        }

        const float3 sampledScattering = sampledScatteringExtinction.xyz;
        const float sampledExtinction = sampledScatteringExtinction.w;
        const float clampedExtinction = max(sampledExtinction, 0.00001f);

        const float transmittance = exp(-sampledExtinction * zStep);

        const float3 scattering = (sampledScattering - (sampledScattering * transmittance)) / clampedExtinction;

        integratedScattering += scattering * integratedTransmittance;
        integratedTransmittance *= transmittance;

        float3 storedScattering = integratedScattering;
        FinalIntegrationVolume[froxelCoord] = float4(storedScattering, integratedTransmittance);
    }
}
#endif //(FINAL_INTEGRATION > 0)