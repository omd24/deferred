

struct VSConstants
{
    row_major float4x4 World;
	row_major float4x4 View;
    row_major float4x4 WorldViewProjection;
    float NearClip;
    float FarClip;
};

struct MatIndexConstants
{
    uint MatIndex;
};

ConstantBuffer<VSConstants> VSCBuffer : register(b0);
ConstantBuffer<MatIndexConstants> MatIndexCBuffer : register(b2);

// =================================================================

struct VSInput
{
    float3 PositionOS 		    : POSITION;
    float3 NormalOS 		    : NORMAL;
    float2 UV 		            : UV;
	float3 TangentOS 		    : TANGENT;
	float3 BitangentOS		    : BITANGENT;
};

struct VSOutput
{
    float4 PositionCS 		    : SV_Position;

    float3 NormalWS 		    : NORMALWS;
    float3 PositionWS           : POSITIONWS;
    float DepthVS               : DEPTHVS;
	float3 TangentWS 		    : TANGENTWS;
	float3 BitangentWS 		    : BITANGENTWS;
	float2 UV 		            : UV;
};

struct PSInput
{
    float4 PositionSS 		    : SV_Position;

    float3 NormalWS 		    : NORMALWS;
    float3 PositionWS           : POSITIONWS;
    float DepthVS               : DEPTHVS;
    float3 TangentWS 		    : TANGENTWS;
	float3 BitangentWS 		    : BITANGENTWS;
};

struct PSOutputGBuffer
{
    float4 Color : SV_Target0;
};

VSOutput VS(in VSInput input, in uint VertexID : SV_VertexID)
{
    VSOutput result;

    // result.position = position;
    // result.color = color;

    result.PositionCS = float4(0, 0, 0, 1);
    return result;
}

PSOutputGBuffer PS(in PSInput input)
{
    PSOutputGBuffer result;

    result.Color = float4(0, 0, 0, 1);
    return result;
}