
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

    float4 color = AlbedoMap.SampleLevel(AnisoSampler, uv, 0);
    // shadingInput.AlbedoMap = AlbedoMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY);

    // Texture2D<float4> AlbedoMapTemp = Tex2DTable[NonUniformResourceIndex(SRVIndices.TangentFrameMapIndex)];
    // float4 color = AlbedoMapTemp.SampleLevel(AnisoSampler, screenUV, 0);

    OutputTexture[pixelPos] = float4(color.r, color.g, color.b, color.a);
}