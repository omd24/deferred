
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
// Convert linear depth (near...far) to (0...1) value distributed with exponential functions
// This function is performing all calculations, a more optimized one precalculates factors on CPU.
float linearDepthToUV( float near, float far, float linearDepth, int numSlices ) {
    const float oneOverLog2FarOverNear = 1.0f / log2( far / near );
    const float scale = numSlices * oneOverLog2FarOverNear;
    const float bias = - ( numSlices * log2(near) * oneOverLog2FarOverNear );

    return max(log2(linearDepth) * scale + bias, 0.0f) / float(numSlices);
}
// ==========================================================================
// Convert rawDepth (0..1) to linear depth (near...far)
float rawDepthToLinearDepth( float rawDepth, float near, float far ) {
    return near * far / (far + rawDepth * (near - far));
}
// ==========================================================================
// Convert linear depth (near...far) to rawDepth (0..1)
float linearDepthToRawDepth( float linearDepth, float near, float far ) {
    return ( near * far ) / ( linearDepth * ( near - far ) ) - far / ( near - far );
}
// ==========================================================================
// Volumetric fog application
float3 applyVolumetricFog(float2 screenUV, float rawDepth, float near, float far, int numSlices, in Texture3D fogVolume, in SamplerState fogSampler, in float3 color)
{
    // Fog linear depth distribution
    float linearDepth = rawDepthToLinearDepth(rawDepth, near, far );

    // Exponential
    float depthUV = linearDepthToUV(near, far, linearDepth, numSlices);
    float3 froxelUVW = float3(screenUV.xy, depthUV);
    
    float4 scatteringTransmittance = float4(0, 0, 0, 0);
    if (AppSettings.FOG_UseLinearClamp)
      scatteringTransmittance = fogVolume.SampleLevel(fogSampler, froxelUVW, 0);
    else
      scatteringTransmittance = fogVolume[froxelUVW * float3(128, 128, 128)];

    return color.rgb * scatteringTransmittance.a + scatteringTransmittance.rgb;

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
// ==========================================================================
