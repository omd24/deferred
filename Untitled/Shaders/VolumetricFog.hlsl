//=================================================================================================
// Includes
//=================================================================================================
#include "GlobalResources.hlsl"
#include "AppSettings.hlsl"
#include "Shadows.hlsl"
#include "LightingHelpers.hlsl"
#include "Quaternion.hlsl"
#include "VolumetricFogHelpers.hlsl"

//=================================================================================================
// Uniforms
//=================================================================================================
struct UniformConstants
{
  row_major float4x4 ProjMat;
  row_major float4x4 InvViewProj;
  row_major float4x4 PrevViewProj;

  float2 Resolution;
  float Near;
  float Far;

  uint MaterialIDMapIdx;
  uint TangentFrameMapIndex;
  uint ClusterBufferIdx;
  uint DepthBufferIdx;

  uint SpotLightShadowIdx;
  uint DataVolumeIdx;
  uint UVMapIdx;
  uint NoiseTextureIdx;

  uint3 Dimensions;
  uint CurrFrame;

  uint ScatterVolumeIdx;
  uint PrevScatterVolumeIdx;
  uint FinalIntegrationVolumeIdx;
  uint unused2;


  float3 CameraPos;
  uint unused3;

  uint NumXTiles;
  uint NumXYTiles;
};
ConstantBuffer<UniformConstants> CBuffer : register(b0);
ConstantBuffer<LightConstants> LightsBuffer : register(b1);

#define ubo_proj_matrix                         CBuffer.ProjMat
#define ubo_inv_view_proj                       CBuffer.InvViewProj
#define ubo_prev_view_proj                      CBuffer.PrevViewProj
#define ubo_screen_resolution                   CBuffer.Resolution
#define ubo_near_distance                       CBuffer.Near
#define ubo_far_distance                        CBuffer.Far
#define ubo_grid_dimensions                     CBuffer.Dimensions
#define ubo_current_frame                       CBuffer.CurrFrame
#define ubo_camera_pos                          CBuffer.CameraPos
#define ubo_num_tiles_x                         CBuffer.NumXTiles
#define ubo_num_tiles_xy                        CBuffer.NumXYTiles
#define ubo_scattering_factor                   AppSettings.FOG_ScatteringFactor
#define ubo_constant_fog_modifier               AppSettings.FOG_ConstantFogDensityModifier
#define ubo_height_fog_density                  AppSettings.FOG_HeightFogDenisty
#define ubo_height_fog_falloff                  AppSettings.FOG_HeightFogFalloff
#define ubo_box_size                            AppSettings.FOG_BoxSize
#define ubo_box_position                        AppSettings.FOG_BoxPosition
#define ubo_box_color                           AppSettings.FOG_BoxColor
#define ubo_box_fog_density                     AppSettings.FOG_BoxFogDensity
#define ubo_phase_anisotropy                    AppSettings.FOG_PhaseAnisotropy
#define ubo_temporal_reprojection_percentage    AppSettings.FOG_TemporalPercentage
#define ubo_noise_type                          AppSettings.FOG_NoiseType
#define ubo_noise_scale                         AppSettings.FOG_NoiseScale

//=================================================================================================
// Resources
//=================================================================================================

Texture2D<uint> MaterialIDMaps[] : register(t0, space104);

// Samplers:
SamplerState PointSampler : register(s0);
SamplerState LinearClampSampler : register(s1);
SamplerState LinearWrapSampler : register(s2);
SamplerState LinearBorderSampler : register(s3);
SamplerComparisonState ShadowMapSampler : register(s4);

// Render targets / UAVs
#if (DATA_INJECTION > 0)
  RWTexture3D<float4> DataVolumeTexture : register(u0);
#endif

#if (LIGHT_SCATTERING > 0) || (TEMPORAL_FILTERING > 0)
  RWTexture3D<float4> ScatterVolumeTexture : register(u0);
#endif

#if (FINAL_INTEGRATION > 0)
  RWTexture3D<float4> FinalIntegrationVolume : register(u0);
#endif
// ==========================================================================
// Noise helpers
float generateNoise(float2 pixel, int frame, float scale)
{
    // Animated blue noise using golden ratio.
    if (0 == ubo_noise_type)
    {
        float2 uv = float2(pixel.xy / ubo_grid_dimensions.xy);
        // Read blue noise from texture
        Texture2D blueNoiseTexture = Tex2DTable[CBuffer.NoiseTextureIdx];
        float2 blueNoise = blueNoiseTexture.SampleLevel(LinearWrapSampler, uv, 0).rg;
        const float kGoldenRatioConjugate = 0.61803398875;
        float blueNoise0 = frac(toLinear1(blueNoise.r) + float(frame % 256) * kGoldenRatioConjugate);
        float blueNoise1 = frac(toLinear1(blueNoise.g) + float(frame % 256) * kGoldenRatioConjugate);

        return triangularNoise(blueNoise0, blueNoise1) * scale;
    }
    // Interleaved gradient noise
    if (1 == ubo_noise_type)
    {
        float noise0 = interleavedGradientNoise(pixel, frame);
        float noise1 = interleavedGradientNoise(pixel, frame + 1);

        return triangularNoise(noise0, noise1) * scale;
    }

    // Initial noise attempt, left for reference.
    return (interleavedGradientNoise(pixel, frame) * scale) - (scale * 0.5f);
}
// ==========================================================================
// Exponential distribution as in https://advances.realtimerendering.com/s2016/Siggraph2016_idTech6.pdf Page 5.
// Convert slice index to (near...far) value distributed with exponential function.
float sliceToExponentialDepth( float near, float far, int slice, int numSlices )
{
    return near * pow( far / near, (float(slice) + 0.5f) / float(numSlices) );
}
// ==========================================================================
float sliceToExponentialDepthJittered(float near, float far, float jitter, int slice, int numSlices)
{
    return near * pow(far / near, (float(slice) + 0.5f + jitter) / float(numSlices));
}
// ==========================================================================
float4 worldFromFroxel(uint3 froxelCoord)
{
    float2 uv = (froxelCoord.xy + 0.5f) / float2(ubo_grid_dimensions.x, ubo_grid_dimensions.y);

    float linearZ = 0.0f;
    if (AppSettings.FOG_ApplyZJitter)
    {
        float jittering = generateNoise(froxelCoord.xy * 1.0f, ubo_current_frame, ubo_noise_scale);
        linearZ = sliceToExponentialDepthJittered(ubo_near_distance, ubo_far_distance, jittering, froxelCoord.z, int(ubo_grid_dimensions.z));
    }
    else
    {
        linearZ = sliceToExponentialDepth(ubo_near_distance, ubo_far_distance, froxelCoord.z, ubo_grid_dimensions.z);
    }

    //
    // THIS CAUSES A CAMERA-DEPENDENT ISSUE (also depends on fog USAGE code)
    // Kept for legacy reasons:
    // float linearZ = (froxelCoord.z + 0.5f) / float(ubo_grid_dimensions.z);
    //

    //
    // NOTE: we can aso obtain rawDepth from proj matrix
    // float rawDepth = linearZ * ubo_proj_matrix._33 + ubo_proj_matrix._43 / linearZ;
    //
    
    float rawDepth = linearDepthToRawDepth(linearZ, ubo_near_distance, ubo_far_distance);

    uv = 2.0f * uv - 1.0f;
    uv.y *= -1.0f;

    float4 worldPos = mul(float4(uv, rawDepth, 1.0f), ubo_inv_view_proj);
    worldPos.xyz /= worldPos.w;

    return worldPos;
}
// ==========================================================================
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
// ==========================================================================
float attenuationSquareFalloff(float3 positionToLight, float lightInverseRadius)
{
    const float distanceSquare = dot(positionToLight, positionToLight);
    const float factor = distanceSquare * lightInverseRadius * lightInverseRadius;
    const float smoothFactor = max(1.0 - factor * factor, 0.0);
    return (smoothFactor * smoothFactor) / max(distanceSquare, 1e-4);
}
// ==========================================================================
// Equations from http://patapom.com/topics/Revision2013/Revision%202013%20-%20Real-time%20Volumetric%20Rendering%20Course%20Notes.pdf
float henyeyGreenstein(float g, float costh)
{
    const float numerator = 1.0 - g * g;
    const float denominator = 4.0 * PI * pow(1.0 + g * g - 2.0 * g * costh, 3.0/2.0);
    return numerator / denominator;
}
// ==========================================================================
float phaseFunction(float3 V, float3 L, float g)
{
    float cosTheta = dot(V, L);

    // TODO: compare other phase functions
    return henyeyGreenstein(g, cosTheta);
}
// ==========================================================================
float4 scatteringExtinctionFromColorDensity(float3 color, float density )
{
    const float extinction = ubo_scattering_factor * density;
    return float4(color * extinction, extinction);
}
// ==========================================================================
// Computes world-space position from post-projection depth
float3 PosWSFromDepth(in float zw, in float2 uv)
{
  // float linearDepth = DeferredCBuffer.Projection._43 / (zw - DeferredCBuffer.Projection._33);
  float4 positionCS = float4(uv * 2.0f - 1.0f, zw, 1.0f);
  positionCS.y *= -1.0f;
  float4 positionWS = mul(positionCS, ubo_inv_view_proj);
  return positionWS.xyz / positionWS.w;
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
    float3 boxSize = ubo_box_size;
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
    
                    uint2 pixelPos = uint2(uint(froxelCoord.x * 1.0f / ubo_grid_dimensions.x * ubo_screen_resolution.x),
                        uint(froxelCoord.y * 1.0f / ubo_grid_dimensions.y * ubo_screen_resolution.y));

                    float spotLightVisibility = 1.0f;
                    if (AppSettings.FOG_EnableShadowMapSampling)
                    {
                        // Spotlight shadow
                        Texture2DArray spotLightShadowMap = Tex2DArrayTable[CBuffer.SpotLightShadowIdx];
                        float2 shadowMapSize;
                        float numSlices;
                        spotLightShadowMap.GetDimensions(shadowMapSize.x, shadowMapSize.y, numSlices);

                        // Calculate vertex normal for shadow sampling
                        Texture2D tangentFrameMap = Tex2DTable[CBuffer.TangentFrameMapIndex];
                        Texture2D<uint> materialIDMap = MaterialIDMaps[CBuffer.MaterialIDMapIdx];
                        Quaternion tangentFrame = UnpackQuaternion(tangentFrameMap[pixelPos]);
                        uint packedMaterialID = materialIDMap[pixelPos];
                        float handedness = packedMaterialID & 0x80 ? -1.0f : 1.0f;
                        float3x3 tangentFrameMatrix = QuatTo3x3(tangentFrame);
                        tangentFrameMatrix._m10_m11_m12 *= handedness;
                        float3 vtxNormalWS = tangentFrameMatrix._m20_m21_m22;

                        // Calculate position neighbors
                        Texture2D depthMap = Tex2DTable[CBuffer.DepthBufferIdx];
                        float zw = depthMap[pixelPos].x;
                        Texture2D uvMap = Tex2DTable[CBuffer.UVMapIdx];
                        float2 zwGradients = uvMap[pixelPos].zw;
                        zwGradients = sign(zwGradients) * pow(abs(zwGradients), 2.0f);
                        float2 zwNeighbors = saturate(zw.xx + zwGradients);
                        float2 invRTSize = 1.0f / ubo_screen_resolution;
                        float2 screenUV = (pixelPos + 0.5f) * invRTSize;
                        float3 positionDX =
                            PosWSFromDepth(zwNeighbors.x, screenUV + (int2(1, 0) * invRTSize)) - worldPos;
                        float3 positionDY =
                            PosWSFromDepth(zwNeighbors.y, screenUV + (int2(0, 1) * invRTSize)) - worldPos;
                        float3 positionNeighborX = worldPos + positionDX;
                        float3 positionNeighborY = worldPos + positionDY;

                        // Calcualte spotlight shadow visibility
                        const float3 shadowPosOffset = GetShadowPosOffset(saturate(dot(vtxNormalWS, surfaceToLight)), vtxNormalWS, shadowMapSize.x);
                        spotLightVisibility = SpotLightShadowVisibility(worldPos, positionNeighborX, positionNeighborY,
                                                                    LightsBuffer.ShadowMatrices[spotLightIdx],
                                                                    spotLightIdx, shadowPosOffset, spotLightShadowMap, ShadowMapSampler,
                                                                    float2(SpotShadowNearClip, spotLight.Range));
                    } // end of shadow sampling
    
                    // Calculate phase function
                    const float3 L = surfaceToLight; // normalized
                    float p = phaseFunction(V, -L, ubo_phase_anisotropy);
    
                    lighting += AppSettings.LightColor * intensity * p * spotLightVisibility;
                }
            } // end of light loops
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
                        if (AppSettings.FOG_EnableShadowMapSampling)
                        {
                            // Spotlight shadow
                            Texture2DArray spotLightShadowMap = Tex2DArrayTable[CBuffer.SpotLightShadowIdx];
                            float2 shadowMapSize;
                            float numSlices;
                            spotLightShadowMap.GetDimensions(shadowMapSize.x, shadowMapSize.y, numSlices);
    
                            // Calculate vertex normal for shadow sampling
                            Texture2D tangentFrameMap = Tex2DTable[CBuffer.TangentFrameMapIndex];
                            Texture2D<uint> materialIDMap = MaterialIDMaps[CBuffer.MaterialIDMapIdx];
    
                            Quaternion tangentFrame = UnpackQuaternion(tangentFrameMap[pixelPos]);
                            uint packedMaterialID = materialIDMap[pixelPos];
                            float handedness = packedMaterialID & 0x80 ? -1.0f : 1.0f;
                            float3x3 tangentFrameMatrix = QuatTo3x3(tangentFrame);
                            tangentFrameMatrix._m10_m11_m12 *= handedness;

                            float3 vtxNormalWS = tangentFrameMatrix._m20_m21_m22;

                            // Calculate position neighbors
                            Texture2D depthMap = Tex2DTable[CBuffer.DepthBufferIdx];
                            float zw = depthMap[pixelPos].x;
                            Texture2D uvMap = Tex2DTable[CBuffer.UVMapIdx];
                            float2 zwGradients = uvMap[pixelPos].zw;
                            zwGradients = sign(zwGradients) * pow(abs(zwGradients), 2.0f);
                            float2 zwNeighbors = saturate(zw.xx + zwGradients);

                            float2 invRTSize = 1.0f / ubo_screen_resolution;
                            float2 screenUV = (pixelPos + 0.5f) * invRTSize;

                            float3 positionDX =
                                PosWSFromDepth(zwNeighbors.x, screenUV + (int2(1, 0) * invRTSize)) - worldPos;
                            float3 positionDY =
                                PosWSFromDepth(zwNeighbors.y, screenUV + (int2(0, 1) * invRTSize)) - worldPos;

                            float3 positionNeighborX = worldPos + positionDX;
                            float3 positionNeighborY = worldPos + positionDY;

                            // Calcualte spotlight shadow visibility
                            const float3 shadowPosOffset = GetShadowPosOffset(saturate(dot(vtxNormalWS, surfaceToLight)), vtxNormalWS, shadowMapSize.x);
                            spotLightVisibility = SpotLightShadowVisibility(worldPos, positionNeighborX, positionNeighborY,
                                                                        LightsBuffer.ShadowMatrices[spotLightIdx],
                                                                        spotLightIdx, shadowPosOffset, spotLightShadowMap, ShadowMapSampler,
                                                                        float2(SpotShadowNearClip, spotLight.Range));
                        } // end of shadow sampling

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

#if (TEMPORAL_FILTERING > 0)

[numthreads(8, 8, 1)]
void TemporalFilterCS(in uint3 DispatchID : SV_DispatchThreadID)
{
    uint3 froxelCoord = DispatchID;
    float3 froxelDims = float3(ubo_grid_dimensions.x, ubo_grid_dimensions.y, ubo_grid_dimensions.z);
    float3 rcpFroxelDim = 1.0f / froxelDims.xyz;

    // Note: We write to final integration volume in the scattering pass.
    Texture3D currScatterVolume = Tex3DTable[CBuffer.FinalIntegrationVolumeIdx];
    float4 scatteringExtinction = currScatterVolume.SampleLevel(LinearClampSampler, froxelCoord * rcpFroxelDim, 0);

    // Temporal reprojection
    if (AppSettings.FOG_EnableTemporalFilter)
    {
        float3 worldPos = worldFromFroxel(froxelCoord).xyz;
        float4 sceenSpaceCenterLast = mul(float4(worldPos, 1.0), ubo_prev_view_proj);
        float3 ndc = sceenSpaceCenterLast.xyz / sceenSpaceCenterLast.w;

        float linearZ = rawDepthToLinearDepth(ndc.z, ubo_near_distance, ubo_far_distance);

        // Exponential
        float depthUV = linearDepthToUV(ubo_near_distance, ubo_far_distance, linearZ, int(froxelDims.z));
        float3 historyUV = float3(ndc.x * .5 + .5, ndc.y * -.5 + .5, depthUV);

        // If history UV is outside the frustum, skip
        if (all(historyUV >= float3(0.0f, 0.0f, 0.0f)) && all(historyUV <= float3(1.0f, 1.0f, 1.0f)))
        {
            // Fetch history sample
            Texture3D previousLightScatteringTexture = Tex3DTable[CBuffer.PrevScatterVolumeIdx];
            float4 history = previousLightScatteringTexture.SampleLevel(LinearClampSampler, historyUV, 0);

            history = max(history, scatteringExtinction);

            scatteringExtinction.rgb = lerp(scatteringExtinction.rgb, history.rgb, ubo_temporal_reprojection_percentage);
            scatteringExtinction.a = lerp(scatteringExtinction.a, history.a, ubo_temporal_reprojection_percentage);

            // DEBUG: test where pixels are being sampled.
            //scattering = float3(1,0,0);
        }
    }

    ScatterVolumeTexture[froxelCoord] = scatteringExtinction;
}

#endif //(TEMPORAL_FILTERING > 0)