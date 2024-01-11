//=================================================================================================
// Includes
//=================================================================================================
#include "AppSettings.hlsl"


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

struct ClusterVisConstants
{
    row_major float4x4 Projection;
    float3 ViewMin;
    float NearClip;
    float3 ViewMax;
    float InvClipRange;
    float2 DisplaySize;
    uint NumXTiles;
    uint NumXYTiles;

    uint DecalClusterBufferIdx;
    uint SpotLightClusterBufferIdx;
};

ConstantBuffer<ClusterVisConstants> CBuffer : register(b0);

// ================================================================================================
// Pixel shader for visualizing light counts from an overhead view of the frustum
// ================================================================================================
float4 ClusterVisualizerPS(in float4 PositionPS : SV_Position, in flaot2 TexCoord : TEXCOORD) : SV_Target0
{
    ByteAddressBuffer spotLightClusterBuffer = RawBufferTable[CBuffer.SpotLightClusterBufferIdx];

    float3 viewPos = lerp(CBuffer.ViewMin, CBuffer.ViewMax, float3(TexCoord.x, 0.5f, 1.0f - TexCoord.y));
    float3 projectedPos = mul(float3(viewPos, 1.0f), CBuffer.Projection);
    projectedPos.xyz /= projectedPos.w;
    projectedPos.y *= -1.0f;
    projectedPos.xy = projectedPos.xy * 0.5f + 0.5f;

    float2 screenPos = projectedPos.xy * CBuffer.DisplaySize;
    float normalizedPos = saturate((viewPos.z - CBuffer.NearClip) * CBuffer.InvClipRange);
    uint3 tileCoords = uint3(uint2(screenPos) / ClusterTileSize, normalizedPos * NumZTiles);
    uint clusterIdx = (tileCoords.z * CBuffer.NumXYTiles) + (tileCoords.y * CBuffer.NumXTiles) + tileCoords.x;

    if (projectedPos.x < 0.0f || projectedPos.x > 1.0f || projectedPos.y < 0.0f || projectedPos.y > 1.0f)
        return 0.0f;

    float3 output = 0.05f;

    {
        uint numLights = 0;
        uint clusterOffset = clusterIdx * SpotLightElementsPerCluster;

        [unroll]
        for (uint elemIdx = 0; elemIdx < SpotLightElementsPerCluster; ++elemIdx)
        {
            uint clusterElemMask = spotLightClusterBuffer.Load((clusterOffset + elemIdx) * 4);
            numLights += countbits(clusterElemMask);
        }

        output.x += numLights / 10.0f;
    }

    {
        // TODO decals count
        // output.y
    }

    return float4(output, 0.9f);
}