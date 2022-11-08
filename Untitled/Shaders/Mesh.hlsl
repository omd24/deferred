
// This is the standard set of descriptor tables that shaders use for accessing the SRV's that they need.
// These must match "NumStandardDescriptorRanges" and "StandardDescriptorRanges()" from DX12_Helpers.h
Texture2D Tex2DTable[] : register(t0, space0);
Texture2DArray Tex2DArrayTable[] : register(t0, space1);
TextureCube TexCubeTable[] : register(t0, space2);
Texture3D Tex3DTable[] : register(t0, space3);
Texture2DMS<float4> Tex2DMSTable[] : register(t0, space4);
ByteAddressBuffer RawBufferTable[] : register(t0, space5);
Buffer<uint> BufferUintTable[] : register(t0, space6);

typedef float4 Quaternion;

float4 PackQuaternion(in Quaternion q)
{
    Quaternion absQ = abs(q);
    float absMaxComponent = max(max(absQ.x, absQ.y), max(absQ.z, absQ.w));

    uint maxCompIdx = 0;
    float maxComponent = q.x;

    [unroll]
    for(uint i = 0; i < 4; ++i)
    {
        if(absQ[i] == absMaxComponent)
        {
            maxCompIdx = i;
            maxComponent = q[i];
        }
    }

    if(maxComponent < 0.0f)
        q *= -1.0f;

    float3 components;
    if(maxCompIdx == 0)
        components = q.yzw;
    else if(maxCompIdx == 1)
        components = q.xzw;
    else if(maxCompIdx == 2)
        components = q.xyw;
    else
        components = q.xyz;

    const float maxRange = 1.0f / sqrt(2.0f);
    components /= maxRange;
    components = components * 0.5f + 0.5f;

    return float4(components, maxCompIdx / 3.0f);
}
Quaternion QuatFrom3x3(in float3x3 m)
{
    float3x3 a = transpose(m);
    Quaternion q;
    float trace = a[0][0] + a[1][1] + a[2][2];
    if(trace > 0)
    {
        float s = 0.5f / sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (a[2][1] - a[1][2]) * s;
        q.y = (a[0][2] - a[2][0]) * s;
        q.z = (a[1][0] - a[0][1]) * s;
    }
    else
    {
        if(a[0][0] > a[1][1] && a[0][0] > a[2][2])
        {
            float s = 2.0f * sqrt(1.0f + a[0][0] - a[1][1] - a[2][2]);
            q.w = (a[2][1] - a[1][2]) / s;
            q.x = 0.25f * s;
            q.y = (a[0][1] + a[1][0]) / s;
            q.z = (a[0][2] + a[2][0]) / s;
        }
        else if(a[1][1] > a[2][2])
        {
            float s = 2.0f * sqrt(1.0f + a[1][1] - a[0][0] - a[2][2]);
            q.w = (a[0][2] - a[2][0]) / s;
            q.x = (a[0][1] + a[1][0]) / s;
            q.y = 0.25f * s;
            q.z = (a[1][2] + a[2][1]) / s;
        }
        else
        {
            float s = 2.0f * sqrt(1.0f + a[2][2] - a[0][0] - a[1][1]);
            q.w = (a[1][0] - a[0][1]) / s;
            q.x = (a[0][2] + a[2][0]) / s;
            q.y = (a[1][2] + a[2][1]) / s;
            q.z = 0.25f * s;
        }
    }
    return q;
}

//=================================================================================================
// Uniforms
//=================================================================================================
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

//=================================================================================================
// VS and PS structs
//=================================================================================================
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
    float2 UV 		            : UV;
};

struct PSOutputGBuffer
{
    float4 Color : SV_Target0;
    uint MaterialID : SV_Target1;
};

//=================================================================================================
// Gbuffer shaders entry-points
//=================================================================================================
VSOutput VS(in VSInput input, in uint VertexID : SV_VertexID)
{
    VSOutput output;

    float3 positionOS = input.PositionOS;

    // Calc the world-space position
    output.PositionWS = mul(float4(positionOS, 1.0f), VSCBuffer.World).xyz;

    // Calc the clip-space position
    output.PositionCS = mul(float4(positionOS, 1.0f), VSCBuffer.WorldViewProjection);
    output.DepthVS = output.PositionCS.w;

	// Rotate the normal into world space
    output.NormalWS = normalize(mul(float4(input.NormalOS, 0.0f), VSCBuffer.World)).xyz;

	// Rotate the rest of the tangent frame into world space
	output.TangentWS = normalize(mul(float4(input.TangentOS, 0.0f), VSCBuffer.World)).xyz;
	output.BitangentWS = normalize(mul(float4(input.BitangentOS, 0.0f), VSCBuffer.World)).xyz;

    // Pass along the texture coordinates
    output.UV = input.UV;

    return output;
}
PSOutputGBuffer PS(in PSInput input)
{
    PSOutputGBuffer result; 
    float3 normalWS = normalize(input.NormalWS);
    float3 tangentWS = normalize(input.TangentWS);
    float3 bitangentWS = normalize(input.BitangentWS);

    // The tangent frame can have arbitrary handedness, so we force it to be left-handed and then
    // pack the handedness into the material ID
    float handedness = dot(bitangentWS, cross(normalWS, tangentWS)) > 0.0f ? 1.0f : -1.0f;
    bitangentWS *= handedness;

    Quaternion tangentFrame = QuatFrom3x3(float3x3(tangentWS, bitangentWS, normalWS));

    result.Color = PackQuaternion(tangentFrame);

    result.MaterialID = MatIndexCBuffer.MatIndex & 0x7F;
    if(handedness == -1.0f)
        result.MaterialID |= 0x80;

    return result;
}