#include "Shading.hlsl"

typedef float4 Quaternion;
Quaternion UnpackQuaternion(in float4 packed)
{
  uint maxCompIdx = uint(packed.w * 3.0f);
  packed.xyz = packed.xyz * 2.0f - 1.0f;
  const float maxRange = 1.0f / sqrt(2.0f);
  packed.xyz *= maxRange;
  float maxComponent =
      sqrt(1.0f - saturate(packed.x * packed.x + packed.y * packed.y + packed.z * packed.z));

  Quaternion q;
  if (maxCompIdx == 0)
    q = Quaternion(maxComponent, packed.xyz);
  else if (maxCompIdx == 1)
    q = Quaternion(packed.x, maxComponent, packed.yz);
  else if (maxCompIdx == 2)
    q = Quaternion(packed.xy, maxComponent, packed.z);
  else
    q = Quaternion(packed.xyz, maxComponent);

  return q;
}
float3x3 QuatTo3x3(in Quaternion q)
{
  float3x3 m = float3x3(
      1.0f - 2.0f * q.y * q.y - 2.0f * q.z * q.z,
      2.0f * q.x * q.y - 2.0f * q.z * q.w,
      2.0f * q.x * q.z + 2.0f * q.y * q.w,
      2.0f * q.x * q.y + 2.0f * q.z * q.w,
      1.0f - 2.0f * q.x * q.x - 2.0f * q.z * q.z,
      2.0f * q.y * q.z - 2.0f * q.x * q.w,
      2.0f * q.x * q.z - 2.0f * q.y * q.w,
      2.0f * q.y * q.z + 2.0f * q.x * q.w,
      1.0f - 2.0f * q.x * q.x - 2.0f * q.y * q.y);
  return transpose(m);
}

//=================================================================================================
// Uniforms
//=================================================================================================
struct DeferredConstants
{
  row_major float4x4 InvViewProj;
  row_major float4x4 Projection;
  float2 RTSize;
  uint NumComputeTilesX;
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

// This is the standard set of descriptor tables that shaders use for accessing the SRV's that they
// need. These must match "NumStandardDescriptorRanges" and "StandardDescriptorRanges()"
Texture2D Tex2DTable[] : register(t0, space0);
Texture2DArray Tex2DArrayTable[] : register(t0, space1);
TextureCube TexCubeTable[] : register(t0, space2);
Texture3D Tex3DTable[] : register(t0, space3);
Texture2DMS<float4> Tex2DMSTable[] : register(t0, space4);
ByteAddressBuffer RawBufferTable[] : register(t0, space5);
Buffer<uint> BufferUintTable[] : register(t0, space6);

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

// Computes world-space position from post-projection depth
float3 PosWSFromDepth(in float zw, in float2 uv)
{
  float linearDepth = DeferredCBuffer.Projection._43 / (zw - DeferredCBuffer.Projection._33);
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
  shadingInput.TangentFrame = tangentFrameMatrix;

  shadingInput.AlbedoMap = AlbedoMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY);
  shadingInput.NormalMap = NormalMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY).xy;
  shadingInput.RoughnessMap = RoughnessMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY).x;
  shadingInput.MetallicMap = MetallicMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY).x;

  shadingInput.SpotLightClusterBuffer = spotLightClusterBuffer;

  shadingInput.AnisoSampler = AnisoSampler;

  shadingInput.ShadingCBuffer = PSCBuffer;
  shadingInput.LightCBuffer = LightCBuffer;

  float3 shadingResult = ShadePixel(shadingInput, spotLightShadowMap, ShadowMapSampler);

  if (AppSettings.ShowUVGradients)
  {
    shadingResult = abs(float3(uvDX, uvDY.x)) * 64.0f;
  }

  // if(AppSettings.ShowUVGradients)
  //     shadingResult = abs(float3(uvDX, uvDY.x)) * 64.0f;

  OutputTexture[pixelPos] = float4(shadingResult, 1.0f);
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