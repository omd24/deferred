typedef SamplerComparisonState ShadowSampler;

//---------------------------------------------------------------------------------
// Helper function for SampleShadowMapPCF

float SampleShadowMap(in float2 baseUV, in float u, in float v, in float2 shadowMapSizeInv,
                      in uint arrayIdx, in float depth, in Texture2DArray shadowMap,
                      in SamplerComparisonState pcfSampler)
{
    float2 uv = baseUV + float2(u, v) * shadowMapSizeInv;
    return shadowMap.SampleCmpLevelZero(pcfSampler, float3(uv, arrayIdx), depth);
}
//---------------------------------------------------------------------------------
// Samples a shadow depth map with optimized PCF filtering
//---------------------------------------------------------------------------------
float SampleShadowMapPCF(in float3 shadowPos, in uint arrayIdx, in Texture2DArray shadowMap,
                               in SamplerComparisonState pcfSampler)
{
    float2 shadowMapSize;
    float numSlices;
    shadowMap.GetDimensions(shadowMapSize.x, shadowMapSize.y, numSlices);

    float lightDepth = shadowPos.z - 0.001f;

    float2 uv = shadowPos.xy * shadowMapSize; // 1 unit - 1 texel

    float2 shadowMapSizeInv = 1.0 / shadowMapSize;

    float2 baseuv = floor(uv + 0.5f);

    float s = (uv.x + 0.5 - baseuv.x);
    float t = (uv.y + 0.5 - baseuv.y);

    baseuv -= float2(0.5, 0.5);
    baseuv *= shadowMapSizeInv;

    float sum = 0;

    // Filter size: 7

    float uw0 = (5 * s - 6);
    float uw1 = (11 * s - 28);
    float uw2 = -(11 * s + 17);
    float uw3 = -(5 * s + 1);

    float u0 = (4 * s - 5) / uw0 - 3;
    float u1 = (4 * s - 16) / uw1 - 1;
    float u2 = -(7 * s + 5) / uw2 + 1;
    float u3 = -s / uw3 + 3;

    float vw0 = (5 * t - 6);
    float vw1 = (11 * t - 28);
    float vw2 = -(11 * t + 17);
    float vw3 = -(5 * t + 1);

    float v0 = (4 * t - 5) / vw0 - 3;
    float v1 = (4 * t - 16) / vw1 - 1;
    float v2 = -(7 * t + 5) / vw2 + 1;
    float v3 = -t / vw3 + 3;

    sum += uw0 * vw0 * SampleShadowMap(baseuv, u0, v0, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw1 * vw0 * SampleShadowMap(baseuv, u1, v0, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw2 * vw0 * SampleShadowMap(baseuv, u2, v0, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw3 * vw0 * SampleShadowMap(baseuv, u3, v0, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);

    sum += uw0 * vw1 * SampleShadowMap(baseuv, u0, v1, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw1 * vw1 * SampleShadowMap(baseuv, u1, v1, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw2 * vw1 * SampleShadowMap(baseuv, u2, v1, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw3 * vw1 * SampleShadowMap(baseuv, u3, v1, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);

    sum += uw0 * vw2 * SampleShadowMap(baseuv, u0, v2, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw1 * vw2 * SampleShadowMap(baseuv, u1, v2, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw2 * vw2 * SampleShadowMap(baseuv, u2, v2, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw3 * vw2 * SampleShadowMap(baseuv, u3, v2, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);

    sum += uw0 * vw3 * SampleShadowMap(baseuv, u0, v3, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw1 * vw3 * SampleShadowMap(baseuv, u1, v3, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw2 * vw3 * SampleShadowMap(baseuv, u2, v3, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);
    sum += uw3 * vw3 * SampleShadowMap(baseuv, u3, v3, shadowMapSizeInv, arrayIdx, lightDepth, shadowMap, pcfSampler);

    return sum * 1.0f / 2704; // 52 * 52 = ((12 + 1)) * 4 pcf)
}
//---------------------------------------------------------------------------------
// Computes the visibility for a spot light using "explicit" position derivatives
//---------------------------------------------------------------------------------
float SpotLightShadowVisibility(in float3 positionWS, in float3 positionNeighborX, in float3 positionNeighborY,
                                in float4x4 shadowMatrix, in uint shadowMapIdx, in float3 shadowPosOffset,
                                in Texture2DArray shadowMap, in ShadowSampler shadowSampler, in float2 clipPlanes)
{
    const float3 posOffset = shadowPosOffset;

    // Project into shadow space
    float4 shadowPos = mul(float4(positionWS + posOffset, 1.0f), shadowMatrix);
    shadowPos.xyz /= shadowPos.w;

    float4 shadowPosDX = mul(float4(positionNeighborX, 1.0f), shadowMatrix);
    shadowPosDX.xyz /= shadowPosDX.w;
    shadowPosDX.xyz -= shadowPos.xyz;

    float4 shadowPosDY = mul(float4(positionNeighborY, 1.0f), shadowMatrix);
    shadowPosDY.xyz /= shadowPosDY.w;
    shadowPosDY.xyz -= shadowPos.xyz;

    return SampleShadowMapPCF(shadowPos.xyz, shadowMapIdx, shadowMap, shadowSampler);
}
//---------------------------------------------------------------------------------
// Calculates the offset to use for sampling the shadow map, based on the surface normal
//---------------------------------------------------------------------------------
float3 GetShadowPosOffset(in float nDotL, in float3 normal, in float shadowMapSize)
{
    const float offsetScale = 4.0f;
    float texelSize = 2.0f / shadowMapSize;
    float nmlOffsetScale = saturate(1.0f - nDotL);
    return texelSize * offsetScale * nmlOffsetScale * normal;
}
//---------------------------------------------------------------------------------