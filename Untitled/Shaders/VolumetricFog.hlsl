//=================================================================================================
// Includes
//=================================================================================================
#include "GlobalResources.hlsl"
#include "AppSettings.hlsl"

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

  float PhaseAnisotropy01;
  float3 CameraPos;
};
ConstantBuffer<UniformConstants> CBuffer : register(b0);

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
#define ubo_scattering_factor       AppSettings.FOG_ScatteringFactor
#define ubo_constant_fog_modifier   AppSettings.FOG_ConstantFogDensityModifier
#define ubo_height_fog_density      AppSettings.FOG_HeightFogDenisty
#define ubo_height_fog_falloff      AppSettings.FOG_HeightFogFalloff
#define ubo_box_position            AppSettings.FOG_BoxPosition
#define ubo_box_fog_density         AppSettings.FOG_BoxFogDensity

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

float phaseFunction(float3 V, float3 L, float g)
{
    float cosTheta = dot(V, L);

    // TODO
    return 1.0f;
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
        float4 boxFogColor = float4(0, 1, 0, 1);
        scatteringExtinction += scatteringExtinctionFromColorDensity(boxFogColor.rgb, ubo_box_fog_density * fogNoise);
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

#if 1

    ScatterVolumeTexture[froxelCoord] = scatteringExtinction;
    return;
#endif

    float extinction = scatteringExtinction.a;
    float3 lighting = float3(0, 0, 0);

    ByteAddressBuffer spotLightClusterBuffer = RawBufferTable[CBuffer.ClusterBufferIdx];

    if ( extinction >= 0.01f ) {
        float3 V = normalize(CBuffer.CameraPos.xyz - worldPos);

        // Read clustered lighting data
        // Calculate linear depth.
        float linearZ = froxelCoord.z * rcpFroxelDim.z;
        linearZ = rawDepthToLinearDepth(linearZ, ubo_near_distance, ubo_far_distance) / ubo_far_distance;
        // Select bin
        int binIndex = int( linearZ / BIN_WIDTH );
        int clusterIdx = 0; //TODO
        uint clusterOffset = clusterIdx * SpotLightElementsPerCluster; // TODO 
        uint clusterElemMask = spotLightClusterBuffer.Load((clusterOffset + binIndex) * 4); // TODO
        uint binValue = clusterElemMask; // TODO: Remove

        uint minLightId = binValue & 0xFFFF;
        uint maxLightId = ( binValue >> 16 ) & 0xFFFF;

        uint2 position = uint2(uint(froxelCoord.x * 1.0f / ubo_grid_dimensions.x * ubo_screen_resolution.x),
                               uint(froxelCoord.y * 1.0f / ubo_grid_dimensions.y * ubo_screen_resolution.y));
        uint2 tile = position / uint( TILE_SIZE );

        uint stride = uint(NUM_WORDS) * (uint(ubo_screen_resolution.x) / uint(TILE_SIZE));
        // Select base address
        uint address = tile.y * stride + tile.x;

        if ( minLightId != NUM_LIGHTS + 1 ) {
            for ( uint lightId = minLightId; lightId <= maxLightId; ++lightId ) {
                uint wordId = lightId / 32;
                uint bitId = lightId % 32;

#if 0
                if ( ( tiles[ address + wordId ] & ( 1 << bitId ) ) != 0 ) {
                    // uint globalLightIndex = lightIndices[ lightId ];
                    // Light spotlight = lights[ globalLightIndex ];

                    // TODO: properly use light clustering.
                    float3 lightPosition = spotlight.worldPos;
                    float lightRadius = spotlight.radius;
                    if (length(worldPos - lightPosition) < lightRadius) {

                        // Calculate spot light contribution
                        
                        // TODO: add shadows
                        float3 shadowPositionToLight = worldPos - lightPosition;
                        const float currentDepth = vectorToDepthValue(shadowPositionToLight, lightRadius, spotlight.rcpNminusF);
                        const float bias = 0.0001f;
                        const uint shadowLightIndex = globalLightIndex;

                        const uint samples = 4;
                        float shadow = 0;
                        for(uint i = 0; i < samples; ++i) {

                            vec2 diskOffset = vogelDiskOffset(i, 4, 0.1f);
                            float3 samplingPosition = shadowPositionToLight + diskOffset.xyx * 0.0005f;

                            Texture3D fogData = Tex3DTable[CBuffer.DataVolumeIdx];
                            TextureCube cubemapShadows =  TexCubeTable[CBuffer.cubemapShadowsIndex];
                            // const float closestDepth = cubemapShadows.SampleLevel(float4(samplingPosition, shadowLightIndex)).r
                            shadow += currentDepth - bias < closestDepth ? 1 : 0;
                        }

                        shadow /= samples;

                        const float3 L = normalize(lightPosition - worldPos);
                        float attenuation = attenuationSquareFalloff(L, 1.0f / lightRadius) * shadow;

                        lighting += spotlight.color * spotlight.intensity * phaseFunction(V, -L, PhaseAnisotropy01) * attenuation;
                    }
                }
#endif
            }
        }
    }

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

#if 1
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
#if 1
            Texture3D fog = Tex3DTable[CBuffer.ScatterVolumeIdx];
            float4 c = fog[froxelCoord];
            integratedScattering += c.rgb;
            integratedTransmittance = 1.0;
            FinalIntegrationVolume[froxelCoord] = float4(integratedScattering, integratedTransmittance);
            continue;
#endif

        float nextZ = sliceToExponentialDepth(ubo_near_distance, ubo_far_distance, z + 1, int(froxelDims.z) );
        //float nextZ = linearDepthToRawDepth((z + 1) / froxelDims.z, ubo_near_distance, ubo_far_distance);

        const float zStep = abs(nextZ - currentZ);
        currentZ = nextZ;

        // Following equations from Physically Based Sky, Atmosphere and Cloud Rendering by Hillaire
        Texture3D fogVolume = Tex3DTable[CBuffer.ScatterVolumeIdx];
        const float4 sampledScatteringExtinction = fogVolume[froxelCoord * rcpFroxelDim];

#if 1 // TEST
        integratedScattering += sampledScatteringExtinction;
        integratedTransmittance = 1.0f;

#else
        const float3 sampledScattering = sampledScatteringExtinction.xyz;
        const float sampledExtinction = sampledScatteringExtinction.w;
        const float clampedExtinction = max(sampledExtinction, 0.00001f);

        const float transmittance = exp(-sampledExtinction * zStep);

        const float3 scattering = (sampledScattering - (sampledScattering * transmittance)) / clampedExtinction;

        integratedScattering += scattering * integratedTransmittance;
        integratedTransmittance *= transmittance;
#endif

        float3 storedScattering = integratedScattering;
        FinalIntegrationVolume[froxelCoord] = float4(storedScattering, integratedTransmittance);
    }
}
#endif //(FINAL_INTEGRATION > 0)