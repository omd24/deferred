
#include "Shading.hlsl"

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
    uint MaterialIndicesBufferIdx;
    uint MaterialIDMapIdx;
    uint UVMapIdx;
    uint DepthMapIdx;
    uint TangentFrameMapIndex;
};

ConstantBuffer<DeferredConstants> DeferredCBuffer : register(b2);
ConstantBuffer<SRVIndexConstants> SRVIndices : register(b4);

static const float DeferredUVScale = 2.0f;
static const uint DeferredTileSize = 8;
static const uint ThreadGroupSize = DeferredTileSize * DeferredTileSize;

//=================================================================================================
// Resources
//=================================================================================================

// This is the standard set of descriptor tables that shaders use for accessing the SRV's that they need.
// These must match "NumStandardDescriptorRanges" and "StandardDescriptorRanges()"
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
};

StructuredBuffer<MaterialTextureIndices> MaterialIndexBuffers[] : register(t0, space100);
Texture2D<uint> MaterialIDMaps[] : register(t0, space104);

SamplerState AnisoSampler : register(s0);

//=================================================================================================
// TODO: Deferred texturing
//=================================================================================================
[numthreads(DeferredTileSize, DeferredTileSize, 1)]
void CS(in uint3 DispatchID : SV_DispatchThreadID, in uint GroupIndex : SV_GroupIndex,
                in uint3 GroupID : SV_GroupID, in uint3 GroupThreadID : SV_GroupThreadID)
{

    const uint2 pixelPos = DispatchID.xy;

    // shade sample
    float2 invRTSize = 1.0f / DeferredCBuffer.RTSize;
    float2 screenUV = (pixelPos) * invRTSize;

    StructuredBuffer<MaterialTextureIndices> materialIndicesBuffer = MaterialIndexBuffers[SRVIndices.MaterialIndicesBufferIdx];

    Texture2D<uint> materialIDMap = MaterialIDMaps[SRVIndices.MaterialIDMapIdx];
    uint packedMaterialID = materialIDMap[pixelPos];

    Texture2D uvMap = Tex2DTable[SRVIndices.UVMapIdx];
    float2 uv = uvMap[pixelPos].xy * DeferredUVScale;

    float handedness = packedMaterialID & 0x80 ? -1.0f : 1.0f;
    uint materialID = packedMaterialID & 0x7F;
    uint matTest = materialIDMap.SampleLevel(AnisoSampler, screenUV, 0);
    MaterialTextureIndices matIndices = materialIndicesBuffer[materialID];
    Texture2D AlbedoMap = Tex2DTable[NonUniformResourceIndex(matIndices.Albedo)];
    Texture2D NormalMap = Tex2DTable[NonUniformResourceIndex(matIndices.Normal)];

    float2 uvDX = 0.0f;
    float2 uvDY = 0.0f;

    // Calculate UV gradiants:
    {
    float2 zwGradients = uvMap[pixelPos].zw;
    // Compute gradients, trying not to walk off the edge of the triangle that isn't coplanar
    float4 zwGradUp = uvMap[int2(pixelPos) + int2(0, -1)];
    float4 zwGradDown = uvMap[int2(pixelPos) + int2(0, 1)];
    float4 zwGradLeft = uvMap[int2(pixelPos) + int2(-1, 0)];
    float4 zwGradRight = uvMap[int2(pixelPos) + int2(1, 0)];

    uint matIDUp = materialIDMap[int2(pixelPos) + int2(0, -1)] ;
    uint matIDDown = materialIDMap[int2(pixelPos) + int2(0, 1)];
    uint matIDLeft = materialIDMap[int2(pixelPos) + int2(-1, 0)];
    uint matIDRight = materialIDMap[int2(pixelPos) + int2(1, 0)];

    const float zwGradThreshold = 0.0025f;
    bool up = all(abs(zwGradUp.zw - zwGradients) <= zwGradThreshold) && (matIDUp == packedMaterialID);
    bool down = all(abs(zwGradDown.zw - zwGradients) <= zwGradThreshold) && (matIDDown == packedMaterialID);
    bool left = all(abs(zwGradLeft.zw - zwGradients) <= zwGradThreshold) && (matIDLeft == packedMaterialID);
    bool right = all(abs(zwGradRight.zw - zwGradients) <= zwGradThreshold) && (matIDRight == packedMaterialID);

    if(up)
        uvDY = uv - zwGradUp.xy * DeferredUVScale;
    else if(down)
        uvDY = zwGradDown.xy * DeferredUVScale - uv;
    if(left)
        uvDX = uv - zwGradLeft.xy * DeferredUVScale;
    else if(right)
        uvDX = zwGradRight.xy * DeferredUVScale - uv;
    // Check for wrapping around due to frac(), and correct for it.
    if(uvDX.x > 1.0f)
        uvDX.x -= 2.0f;
    else if(uvDX.x < -1.0f)
        uvDX.x += 2.0f;
    if(uvDX.y > 1.0f)
        uvDX.y -= 2.0f;
    else if(uvDX.y < -1.0f)
        uvDX.y += 2.0f;
    if(uvDY.x > 1.0f)
        uvDY.x -= 2.0f;
    else if(uvDY.x < -1.0f)
        uvDY.x += 2.0f;
    if(uvDY.y > 1.0f)
        uvDY.y -= 2.0f;
    else if(uvDY.y < -1.0f)
        uvDY.y += 2.0f;    
    }

    float4 color = AlbedoMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY);
    // shadingInput.AlbedoMap = AlbedoMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY);

    // Texture2D<float4> AlbedoMapTemp = Tex2DTable[NonUniformResourceIndex(SRVIndices.TangentFrameMapIndex)];
    // float4 color = AlbedoMapTemp.SampleLevel(AnisoSampler, screenUV, 0);

    float3 inp = float3(1.0f, 1.0f, 1.0f);
    float3 temp = CalcLighting(inp, inp, inp,
                    inp, inp, 1.0f,
                    inp, inp);

    OutputTexture[pixelPos] = float4(color.r, color.g, color.b, color.a);
}