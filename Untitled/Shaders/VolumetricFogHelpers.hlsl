
// ==========================================================================
// Volumetrics Helpers
// ==========================================================================
// Computes world-space position from post-projection depth
float3 PosWSFromDepth(in float zw, in float2 uv, in float4x4 invViewProj)
{
  // float linearDepth = CBuffer.Projection._43 / (zw - CBuffer.Projection._33);
  float4 positionCS = float4(uv * 2.0f - 1.0f, zw, 1.0f);
  positionCS.y *= -1.0f;
  float4 positionWS = mul(positionCS, invViewProj);
  return positionWS.xyz / positionWS.w;
}
// ==========================================================================
// http://www.aortiz.me/2018/12/21/CG.html
// Convert linear depth (near...far) to (0...1) normalized value distributed with exponential functions
// This function is performing all calculations, a more optimized one precalculates factors on CPU.

// See comparisons here:
// https://www.desmos.com/calculator/myr1vu75cu
float linearDepthToUV (float near, float far, float linearDepth, int numSlices)
{
    if (1 == AppSettings.FOG_DepthMode)
      return log2(linearDepth * AppSettings.FOG_GridParams.x + AppSettings.FOG_GridParams.y) * AppSettings.FOG_GridParams.z / float(numSlices);

    // Default
    const float oneOverLog2FarOverNear = 1.0f / log2(far / near);
    const float scale = numSlices * oneOverLog2FarOverNear;
    const float bias = -(numSlices * log2(near) * oneOverLog2FarOverNear);
    return max(log2(linearDepth) * scale + bias, 0.0f) / float(numSlices);

    // According to the doom 2016 equation, this should be the inverse depth function
    // but for some unknown it causes more slicing artifacts which proves the froxel depth distribution is buggy!
    //return (numSlices * log2(linearDepth / near) / log2(far / near) - 0.5f) / float(numSlices);
}
// ==========================================================================
// Convert rawDepth (0..1) to linear depth (near...far)
float rawDepthToLinearDepth (float rawDepth, float near, float far)
{
    return near * far / (far + rawDepth * (near - far));
}
// ==========================================================================
// Convert linear depth (near...far) to rawDepth (0..1)
float linearDepthToRawDepth (float linearDepth, float near, float far)
{
    return ( near * far ) / ( linearDepth * ( near - far ) ) - far / ( near - far );
}
// ==========================================================================
// Volumetric fog application
float3 applyVolumetricFog (float2 screenUV, float rawDepth, float near, float far, int numSlices, in Texture3D fogVolume, in SamplerState fogSampler, in Texture2D blueNoiseTexture, in SamplerState noiseSampler, in float3 color, in uint currFrame, in float ditteringScale)
{
    // Fog linear depth distribution
    float linearDepth = rawDepthToLinearDepth(rawDepth, near, far );

    // Exponential
    float depthUV = linearDepthToUV(near, far, linearDepth, numSlices);
    float3 froxelUVW = float3(screenUV.xy, depthUV);
    
    float4 scatteringTransmittance = float4(0, 0, 0, 0);
    if (AppSettings.FOG_SampleUsingTricubicFiltering)
    {
      scatteringTransmittance = tricubicFiltering(fogVolume, froxelUVW, float3(numSlices, numSlices, numSlices), fogSampler);
    }
    else if (AppSettings.FOG_UseLinearClamp)
      scatteringTransmittance = fogVolume.SampleLevel(fogSampler, froxelUVW, 0);
    else
      scatteringTransmittance = fogVolume[froxelUVW * float3(numSlices, numSlices, numSlices)];

    // Add animated noise to transmittance to remove banding.
    float2 blueNoise = blueNoiseTexture.SampleLevel(noiseSampler, screenUV, 0).rg;
    
    const float kGoldenRatioConjugate = 0.61803398875;
    float blueNoise0 = frac(toLinear1(blueNoise.r) + float(currFrame % 256) * kGoldenRatioConjugate);
    float blueNoise1 = frac(toLinear1(blueNoise.g) + float(currFrame % 256) * kGoldenRatioConjugate);

    float noiseModifier = triangularNoise(blueNoise0, blueNoise1) * ditteringScale;
    scatteringTransmittance.a += noiseModifier;

    // TODO:
    const bool fogOpacityAntiAliasing = false;
    const float scatteringModifier = fogOpacityAntiAliasing ? max( 1 - scatteringTransmittance.a, 0.00000001f ) : 1.0f;

    color.rgb = color.rgb * scatteringTransmittance.a + scatteringTransmittance.rgb * scatteringModifier;

    return color;
}
// ==========================================================================
