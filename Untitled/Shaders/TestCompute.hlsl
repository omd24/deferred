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

  uint sceneTexIdx;
  uint fogTexIdx;
  uint depthMapIdx;
  float near;

  float far;
  float2 Resolution;
  float pad0;

  float3 FogGridDimensions;
  float pad1;

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
RWTexture2D<float4> OutputTexture : register(u0);

// Sampler(s)
SamplerState PointSampler : register(s0);
SamplerState LinearSampler : register(s1);
SamplerState LinearBorderSampler : register(s2);
SamplerState LinearClampSampler : register(s3);

// Computes world-space position from post-projection depth
float3 PosWSFromDepth(in float zw, in float2 uv, in float4x4 invViewProj)
{
  // float linearDepth = CBuffer.Projection._43 / (zw - CBuffer.Projection._33);
  float4 positionCS = float4(uv * 2.0f - 1.0f, zw, 1.0f);
  positionCS.y *= -1.0f;
  float4 positionWS = mul(positionCS, invViewProj);
  return positionWS.xyz / positionWS.w;
}

// http://www.aortiz.me/2018/12/21/CG.html
// Convert linear depth (near...far) to (0...1) value distributed with exponential functions
// This function is performing all calculations, a more optimized one precalculates factors on CPU.
float linearDepthToUV( float near, float far, float linearDepth, int numSlices ) {
    const float oneOverLog2FarOverNear = 1.0f / log2( far / near );
    const float scale = numSlices * oneOverLog2FarOverNear;
    const float bias = - ( numSlices * log2(near) * oneOverLog2FarOverNear );

    return max(log2(linearDepth) * scale + bias, 0.0f) / float(numSlices);
}

// Convert rawDepth (0..1) to linear depth (near...far)
float rawDepthToLinearDepth( float rawDepth, float near, float far ) {
    return near * far / (far + rawDepth * (near - far));
}

// Volumetric fog application
float4 getVolumetricFog( float2 screenUV, float rawDepth, float near, float far, int numSlices)
{
    // Fog linear depth distribution
    float linearDepth = rawDepthToLinearDepth(rawDepth, near, far );
    //float depthUV = linearDepth / far;
    // Exponential
    float depthUV = linearDepthToUV(near, far, linearDepth, numSlices);
    float3 froxelUVW = float3(screenUV.xy, depthUV);
    
    Texture3D fogVolume = Tex3DTable[CBuffer.fogTexIdx];
    float4 fogSample = fogVolume.SampleLevel(LinearSampler, froxelUVW, 0);
    //float4 fogSample = fogVolume[froxelUVW];

    // return float4(screenUV.xy, linearDepth, depthUV);
    return fogSample;

    // float4 scatteringTransmittance = float4(0,0,0,0);
    // Add animated noise to transmittance to remove banding.
    // float2 blue_noise = texture(global_textures[nonuniformEXT(blue_noise_128_rg_texture_index)], screenUV ).rg;
    // const float k_golden_ratio_conjugate = 0.61803398875;
    // float blue_noise0 = fract(ToLinear1(blue_noise.r) + float(current_frame % 256) * k_golden_ratio_conjugate);
    // float blue_noise1 = fract(ToLinear1(blue_noise.g) + float(current_frame % 256) * k_golden_ratio_conjugate);

    // float noise_modifier = triangular_noise(blue_noise0, blue_noise1) * volumetric_fog_application_dithering_scale;
    // scatteringTransmittance.a += noise_modifier;

    // const float scattering_modifier = enable_volumetric_fog_opacity_anti_aliasing() ? max( 1 - scatteringTransmittance.a, 0.00000001f ) : 1.0f;

    // color.rgb = color.rgb * scatteringTransmittance.a + scatteringTransmittance.rgb * scattering_modifier;

    // return color;
}

//=================================================================================================
// Test compute shader
//=================================================================================================
[numthreads(8, 8, 1)]
void TestCS(in uint3 DispatchID : SV_DispatchThreadID) 
{
    const uint2 pixelPos = DispatchID.xy;

  // transform to world space

    Texture2D depthMap = Tex2DTable[CBuffer.depthMapIdx];
    float z = depthMap[pixelPos].x;
    float2 invRTSize = 1.0f / float2(CBuffer.Resolution.x, CBuffer.Resolution.y);
    float2 screenUV = (pixelPos + 0.5f) * invRTSize;

    // Sample scene texture
    Texture2D sceneTexture = Tex2DTable[CBuffer.sceneTexIdx];
    float2 inputSize = 0.0f;
    sceneTexture.GetDimensions(inputSize.x, inputSize.y);
    float2 texCoord = float2(pixelPos.x, pixelPos.y) / inputSize;
    float4 sceneColor = sceneTexture.SampleLevel(PointSampler, texCoord, 0);

    float t = 1;

    const float near = CBuffer.near;
    const float far = CBuffer.far;
    float4 fogSample3 = getVolumetricFog(screenUV, z, near, far, CBuffer.FogGridDimensions.z);

    float4 blend2 = lerp(sceneColor, fogSample3, t);
    OutputTexture[pixelPos] = blend2;
}
