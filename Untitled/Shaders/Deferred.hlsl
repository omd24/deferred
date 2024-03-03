#include "Shading.hlsl"
#include "Quaternion.hlsl"

//=================================================================================================
// Uniforms
//=================================================================================================
struct DeferredConstants
{
  row_major float4x4 InvViewProj;
  row_major float4x4 Projection;
  float2 RTSize;
  uint NumComputeTilesX;
  float Near;
  float Far;
};
struct SRVIndexConstants
{
  uint SpotLightShadowMapIdx;
  uint MaterialIndicesBufferIdx;
  uint SpotLightClusterBufferIdx;
  uint MaterialIDMapIdx;
  uint UVMapIdx;
  uint DepthMapIdx;
  uint TangentFrameMapIndex;
  uint FogVolumeIdx;
};

ConstantBuffer<ShadingConstants> PSCBuffer : register(b0);
ConstantBuffer<DeferredConstants> DeferredCBuffer : register(b2);
ConstantBuffer<LightConstants> LightCBuffer : register(b3);
ConstantBuffer<SRVIndexConstants> SRVIndices : register(b4);

static const float DeferredUVScale = 2.0f;
static const uint DeferredTileSize = 8;
static const uint ThreadGroupSize = DeferredTileSize * DeferredTileSize;

//=================================================================================================
// Resources
//=================================================================================================

RWTexture2D<float4> OutputTexture : register(u0);

struct MaterialTextureIndices
{
  uint Albedo;
  uint Normal;
  uint Roughness;
  uint Metallic;
};

StructuredBuffer<MaterialTextureIndices> MaterialIndexBuffers[] : register(t0, space100);
Texture2D<uint> MaterialIDMaps[] : register(t0, space104);

SamplerState AnisoSampler : register(s0);
SamplerComparisonState ShadowMapSampler : register(s1);
SamplerState LinearSampler : register(s2);

// Computes world-space position from post-projection depth
float3 PosWSFromDepth(in float zw, in float2 uv)
{
  // float linearDepth = DeferredCBuffer.Projection._43 / (zw - DeferredCBuffer.Projection._33);
  float4 positionCS = float4(uv * 2.0f - 1.0f, zw, 1.0f);
  positionCS.y *= -1.0f;
  float4 positionWS = mul(positionCS, DeferredCBuffer.InvViewProj);
  return positionWS.xyz / positionWS.w;
}

//=================================================================================================
// Shade a single sample point, given a pixel position
//=================================================================================================
void ShadeSample(in uint2 pixelPos)
{
  Texture2DArray spotLightShadowMap = Tex2DArrayTable[SRVIndices.SpotLightShadowMapIdx];

  StructuredBuffer<MaterialTextureIndices> materialIndicesBuffer =
      MaterialIndexBuffers[SRVIndices.MaterialIndicesBufferIdx];

  ByteAddressBuffer spotLightClusterBuffer = RawBufferTable[SRVIndices.SpotLightClusterBufferIdx];

  Texture2D tangentFrameMap = Tex2DTable[SRVIndices.TangentFrameMapIndex];
  Texture2D uvMap = Tex2DTable[SRVIndices.UVMapIdx];
  Texture2D<uint> materialIDMap = MaterialIDMaps[SRVIndices.MaterialIDMapIdx];
  Texture2D depthMap = Tex2DTable[SRVIndices.DepthMapIdx];
  Texture3D fogVolume = Tex3DTable[SRVIndices.FogVolumeIdx];

  Quaternion tangentFrame = UnpackQuaternion(tangentFrameMap[pixelPos]);
  float2 uv = uvMap[pixelPos].xy * DeferredUVScale;
  uint packedMaterialID = materialIDMap[pixelPos];
  float zw = depthMap[pixelPos].x;

  // Recover the tangent frame handedness from the material ID, and then reconstruct the w component
  float handedness = packedMaterialID & 0x80 ? -1.0f : 1.0f;
  float3x3 tangentFrameMatrix = QuatTo3x3(tangentFrame);
  tangentFrameMatrix._m10_m11_m12 *= handedness;

  float2 zwGradients = uvMap[pixelPos].zw;
  float2 uvDX = 0.0f;
  float2 uvDY = 0.0f;

  // Calculate UV gradiants:
  {
    // Compute gradients, trying not to walk off the edge of the triangle that isn't     coplanar
    float4 zwGradUp = uvMap[int2(pixelPos) + int2(0, -1)];
    float4 zwGradDown = uvMap[int2(pixelPos) + int2(0, 1)];
    float4 zwGradLeft = uvMap[int2(pixelPos) + int2(-1, 0)];
    float4 zwGradRight = uvMap[int2(pixelPos) + int2(1, 0)];

    uint matIDUp = materialIDMap[int2(pixelPos) + int2(0, -1)];
    uint matIDDown = materialIDMap[int2(pixelPos) + int2(0, 1)];
    uint matIDLeft = materialIDMap[int2(pixelPos) + int2(-1, 0)];
    uint matIDRight = materialIDMap[int2(pixelPos) + int2(1, 0)];

    const float zwGradThreshold = 0.0025f;
    bool up =
        all(abs(zwGradUp.zw - zwGradients) <= zwGradThreshold) && (matIDUp == packedMaterialID);
    bool down =
        all(abs(zwGradDown.zw - zwGradients) <= zwGradThreshold) && (matIDDown == packedMaterialID);
    bool left =
        all(abs(zwGradLeft.zw - zwGradients) <= zwGradThreshold) && (matIDLeft == packedMaterialID);
    bool right = all(abs(zwGradRight.zw - zwGradients) <= zwGradThreshold) &&
                 (matIDRight == packedMaterialID);

    if (up)
      uvDY = uv - zwGradUp.xy * DeferredUVScale;
    else if (down)
      uvDY = zwGradDown.xy * DeferredUVScale - uv;
    if (left)
      uvDX = uv - zwGradLeft.xy * DeferredUVScale;
    else if (right)
      uvDX = zwGradRight.xy * DeferredUVScale - uv;
    // Check for wrapping around due to frac(), and correct for it.
    if (uvDX.x > 1.0f)
      uvDX.x -= 2.0f;
    else if (uvDX.x < -1.0f)
      uvDX.x += 2.0f;
    if (uvDX.y > 1.0f)
      uvDX.y -= 2.0f;
    else if (uvDX.y < -1.0f)
      uvDX.y += 2.0f;
    if (uvDY.x > 1.0f)
      uvDY.x -= 2.0f;
    else if (uvDY.x < -1.0f)
      uvDY.x += 2.0f;
    if (uvDY.y > 1.0f)
      uvDY.y -= 2.0f;
    else if (uvDY.y < -1.0f)
      uvDY.y += 2.0f;
  }

  float2 invRTSize = 1.0f / DeferredCBuffer.RTSize;

  // Reconstruct the surface position from the depth buffer
  float linearDepth = DeferredCBuffer.Projection._43 / (zw - DeferredCBuffer.Projection._33);
  float2 screenUV = (pixelPos + 0.5f) * invRTSize;
  float3 positionWS = PosWSFromDepth(zw, screenUV);

  // Compute the position derivatives using the stored Z derivatives
  zwGradients = sign(zwGradients) * pow(abs(zwGradients), 2.0f);
  float2 zwNeighbors = saturate(zw.xx + zwGradients);
  float3 positionDX =
      PosWSFromDepth(zwNeighbors.x, screenUV + (int2(1, 0) * invRTSize)) - positionWS;
  float3 positionDY =
      PosWSFromDepth(zwNeighbors.y, screenUV + (int2(0, 1) * invRTSize)) - positionWS;

  uint materialID = packedMaterialID & 0x7F;

  MaterialTextureIndices matIndices = materialIndicesBuffer[materialID];
  Texture2D AlbedoMap = Tex2DTable[NonUniformResourceIndex(matIndices.Albedo)];
  Texture2D NormalMap = Tex2DTable[NonUniformResourceIndex(matIndices.Normal)];
  Texture2D RoughnessMap = Tex2DTable[NonUniformResourceIndex(matIndices.Roughness)];
  Texture2D MetallicMap = Tex2DTable[NonUniformResourceIndex(matIndices.Metallic)];

  ShadingInput shadingInput;
  shadingInput.PositionSS = pixelPos;
  shadingInput.PositionWS = positionWS;
  shadingInput.PositionWS_DX = positionDX;
  shadingInput.PositionWS_DY = positionDY;
  shadingInput.DepthVS = linearDepth;
  shadingInput.RawDepth = zw;
  shadingInput.TangentFrame = tangentFrameMatrix;

  shadingInput.AlbedoMap = AlbedoMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY);
  shadingInput.NormalMap = NormalMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY).xy;
  shadingInput.RoughnessMap = RoughnessMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY).x;
  shadingInput.MetallicMap = MetallicMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY).x;

  shadingInput.SpotLightClusterBuffer = spotLightClusterBuffer;
  shadingInput.FogVolume = fogVolume;

  shadingInput.AnisoSampler = AnisoSampler;
  shadingInput.LinearSampler = LinearSampler;

  shadingInput.ShadingCBuffer = PSCBuffer;
  shadingInput.LightCBuffer = LightCBuffer;

  shadingInput.InvRTSize = invRTSize;

  float3 shadingResult = ShadePixel(shadingInput, spotLightShadowMap, ShadowMapSampler);

  if (AppSettings.ShowUVGradients)
  {
    shadingResult = abs(float3(uvDX, uvDY.x)) * 64.0f;
  }

  // if(AppSettings.ShowUVGradients)
  //     shadingResult = abs(float3(uvDX, uvDY.x)) * 64.0f;

  OutputTexture[pixelPos] = float4(shadingResult, 1.0f);

  // TEST Fog volume:
#if 0
  const float near = DeferredCBuffer.Near;
  const float far = DeferredCBuffer.Far;
  const int numSlices = 128;
  float depthUV = linearDepthToUV(near, far, linearDepth, numSlices );
  float3 froxelUVW = float3(screenUV.xy, depthUV);

  float t = 0;
  float3 boxSize = float3(2.0, 2.0, 2.0);
  float3 boxPos = float3(0, 1, 0);
  float3 boxDist = abs(positionWS - boxPos);
  if (all(boxDist <= boxSize)) {
    t = 1.0;
  }

  float4 fogSample = fogVolume[froxelUVW];
  float4 blend = lerp(OutputTexture[pixelPos], fogSample, t);
  OutputTexture[pixelPos] = blend;
#endif

  // TEST
  // OutputTexture[pixelPos] = AlbedoMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY);
}

//=================================================================================================
// Deferred texturing
//=================================================================================================
[numthreads(DeferredTileSize, DeferredTileSize, 1)] void CS(in uint3 DispatchID
                                                            : SV_DispatchThreadID,
                                                              in uint GroupIndex
                                                            : SV_GroupIndex, in uint3 GroupID
                                                            : SV_GroupID, in uint3 GroupThreadID
                                                            : SV_GroupThreadID) {
  const uint2 pixelPos = DispatchID.xy;
  ShadeSample(pixelPos);
}