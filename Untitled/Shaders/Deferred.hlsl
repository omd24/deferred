
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
    uint DepthMapIdx;
    uint AlbedoIndex;
};

ConstantBuffer<DeferredConstants> DeferredCBuffer : register(b2);
ConstantBuffer<SRVIndexConstants> SRVIndices : register(b4);

static const uint DeferredTileSize = 8;
static const uint ThreadGroupSize = DeferredTileSize * DeferredTileSize;

//=================================================================================================
// Resources
//=================================================================================================
Texture2D Tex2DTable[] : register(t0, space0);
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

    uint materialID = materialIDMap[pixelPos];
    uint matTest = materialIDMap.SampleLevel(AnisoSampler, screenUV, 0);
    MaterialTextureIndices matIndices = materialIndicesBuffer[materialID];
    Texture2D AlbedoMap = Tex2DTable[NonUniformResourceIndex(matIndices.Albedo)];
    Texture2D NormalMap = Tex2DTable[NonUniformResourceIndex(matIndices.Normal)];

    // shadingInput.AlbedoMap = AlbedoMap.SampleGrad(AnisoSampler, uv, uvDX, uvDY);

    Texture2D<float4> AlbedoMapTemp = Tex2DTable[NonUniformResourceIndex(SRVIndices.AlbedoIndex)];
    float4 color = AlbedoMapTemp.SampleLevel(AnisoSampler, screenUV, 0);

    OutputTexture[pixelPos] = float4(color.r, color.g, color.b, color.a);
}