//=================================================================================================
// Includes
//=================================================================================================
#include "AppSettings.hlsl"

//=================================================================================================
// Constant buffers
//=================================================================================================
struct UniformConstants
{
  row_major float4x4 ProjMat;
  row_major float4x4 InvViewProj;

  uint X;
  uint Y;
  float Near;
  float Far;

  uint ClusterBufferIdx;
  uint DepthBufferIdx;
  uint SpotLightShadowIdx;
  uint DataVolumeIdx;
};

ConstantBuffer<UniformConstants> CBuffer : register(b0);

//=================================================================================================
// Resources
//=================================================================================================

// TODO: Put this in a common header
// This is the standard set of descriptor tables that shaders use for accessing the SRV's that they
// need. These must match "NumStandardDescriptorRanges" and "StandardDescriptorRanges()"
Texture2D Tex2DTable[] : register(t0, space0);
Texture2DArray Tex2DArrayTable[] : register(t0, space1);
TextureCube TexCubeTable[] : register(t0, space2);
Texture3D Tex3DTable[] : register(t0, space3);
Texture2DMS<float4> Tex2DMSTable[] : register(t0, space4);
ByteAddressBuffer RawBufferTable[] : register(t0, space5);
Buffer<uint> BufferUintTable[] : register(t0, space6);

// Render targets / UAVs
#if (DATA_INJECTION > 0)
  RWTexture3D<float4> DataVolumeTexture : register(u0);
#endif

#if (FINAL_INTEGRATION > 0)
  RWTexture3D<float4> FinalIntegrationVolume : register(u0);
#endif

// Exponential distribution as in https://advances.realtimerendering.com/s2016/Siggraph2016_idTech6.pdf
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


//=================================================================================================
// 1. Data injection
//=================================================================================================
#if (DATA_INJECTION > 0)
[numthreads(8, 8, 1)]
void DataInjectionCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
  const uint3 froxelCoord = DispatchID;

  // transform froxel to world space
  float2 uv = (froxelCoord.xy + 0.5f) / 128.0f;
  float linearZ = (froxelCoord.z + 0.5f) / 128.0f;
  //float rawDepth = linearZ * CBuffer.ProjMat._33 + CBuffer.ProjMat._43 / linearZ;
  float rawDepth = linearDepthToRawDepth(linearZ, 0.1, 35);
  rawDepth = rawDepthToLinearDepth(rawDepth, 0.1, 35);

  uv = 2.0f * uv - 1.0f;
  uv.y *= -1.0f;

  float4 worldPos = mul(float4(uv, rawDepth, 1.0f), CBuffer.InvViewProj);
  worldPos /= worldPos.w;

  if (false)
  {
    DataVolumeTexture[froxelCoord] = float4(worldPos.xyz, rawDepth);;
    return;
  }

  float4 scatteringExtinction = float4(0.05, 0, 0, 0);

  // Add density from box
#if 1
  float3 boxSize = float3(2.0, 2.0, 2.0);
  float3 boxPos = float3(0, 0, 0);
  float3 boxDist = abs(worldPos - boxPos);
  if (all(boxDist <= boxSize)) {
    scatteringExtinction += float4(0, 0.1, 0, 1);
  }
#endif

  DataVolumeTexture[froxelCoord] = scatteringExtinction;
  //DataVolumeTexture[froxelCoord] = float4(DispatchID, 1.0f);
}
#endif //(DATA_INJECTION > 0)

//=================================================================================================
// 2. Light contribution
//=================================================================================================
[numthreads(8, 8, 1)]
void LightContributionCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
  const uint3 froxelCoord = DispatchID;

  //DataVolumeTexture[froxelCoord] = float4(1.0f, 0.0f, 0.0f, 1.0f);
}

//=================================================================================================
// 3. Light contribution
//=================================================================================================
#if (FINAL_INTEGRATION > 0)
[numthreads(8, 8, 1)]
void FinalIntegrationCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    uint3 froxelCoord = DispatchID;

    // FinalIntegrationVolume[froxelCoord] = float4(1, 1, 0, 0);
    // return;

    float3 integratedScattering = float3(0,0,0);
    float integratedTransmittance = 1.0f;

    float currentZ = 0;

    float3 froxelDims = float3(128, 128, 128);
    float3 rcpFroxelDim = 1.0f / froxelDims.xyz;

    for ( int z = 0; z < froxelDims.z; ++z ) {

        froxelCoord.z = z;

        float nextZ = sliceToExponentialDepth(CBuffer.Near, CBuffer.Far, z + 1, int(froxelDims.z) );
        //float nextZ = linearDepthToRawDepth((z + 1) / froxelDims.z, CBuffer.Near, CBuffer.Far);

        const float zStep = abs(nextZ - currentZ);
        currentZ = nextZ;

        // Following equations from Physically Based Sky, Atmosphere and Cloud Rendering by Hillaire
        Texture3D fogVolume = Tex3DTable[CBuffer.DataVolumeIdx];
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