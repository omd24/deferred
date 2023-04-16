#include "Quaternion.hlsl"

//=================================================================================================
// Helper functions
//=================================================================================================

float4 fromAngleAxis(in float3 p_axis, in float p_angle)
{
  float halfAngle = 0.5 * p_angle;
  float s = sin(halfAngle);

  return float4(s*p_axis.x, s*p_axis.y, s*p_axis.z, cos(halfAngle));
}
float3 rotateVector(in float4 p_quat, in float3 p_vec)
{
  const float3 t = 2.0 * cross(p_quat.xyz, p_vec);
  return p_vec + p_quat.w * t + cross(p_quat.xyz, t);
}

//=================================================================================================
// Uniforms
//=================================================================================================

struct SpriteConstants
{
  row_major float4x4 WorldView;
  row_major float4x4 Projection;
  row_major float4x4 ViewProj;
  row_major float4x4 View;
  row_major float4x4 InvView;
  float4 Params0;
  float4 QuatCamera;
  float4 CamUp;
  float4 CamRight;
  row_major float3x3 InvView3;
};
ConstantBuffer<SpriteConstants> SpriteCBuffer : register(b0);

cbuffer SRVIndicesCB : register(b1)
{
    uint SpriteTextureIdx;
}

//=================================================================================================
// Textures and samplers
//=================================================================================================

Texture2D Tex2DTable[] : register(t0, space0);
SamplerState PointSampler : register(s0);
SamplerState LinearSampler : register(s1);

//=================================================================================================
// VS and PS structs
//=================================================================================================

struct VSInput
{
  float3 PositionOS : POSITION;
  float3 NormalOS : NORMAL;
  float2 UV : UV;
  float3 TangentOS : TANGENT;
  float3 BitangentOS : BITANGENT;
};

struct VSOutput
{
  float4 PositionCS : SV_Position;
  float3 PositionWS : POSITIONWS;
  float2 TexCoord : TEXCOORD;
};

struct PSInput
{
  float4 PositionSS : SV_Position;
  float3 PositionWS : POSITIONWS;
  float2 TexCoord : TEXCOORD;
};

VSOutput VS(in uint VertexIdx
                  : SV_VertexID)
{
  VSOutput output;

  float4 vtxPosition;
  if (VertexIdx == 0)
    vtxPosition = float4(0.0f, 0.0f, 1, 1);
  else if (VertexIdx == 1)
    vtxPosition = float4(1.0f, 0.0f, 1, 1);
  else if (VertexIdx == 2)
    vtxPosition = float4(1.0f, 1.0f, 1, 1);
  else if (VertexIdx == 3)
    vtxPosition = float4(0.0f, 1.0f, 1, 1);

  // UVs
  output.TexCoord = vtxPosition.xy;

  // Scale:
  // float scale = SpriteCBuffer.Params0.x * 10;
  float scale = 1.0;
  vtxPosition.xy *= scale;

  // Billboard:
  output.PositionCS = vtxPosition;
  float3x3 invView = SpriteCBuffer.InvView;

  // float4 quatRot = QuatFrom3x3(SpriteCBuffer.RotationMatrix);
  // output.PositionCS.xyz = rotateVector(quatRot, output.PositionCS.xyz);
  float4x4 worldView = SpriteCBuffer.WorldView;
#if 1
  worldView._m20 = 0;
  worldView._m21 = 0;
  worldView._m22 = 1; 

  worldView._m02 = 0;
  worldView._m12 = 0;
  worldView._m22 = 1; 

  worldView._m10 = 0;
  worldView._m11 = 1;
  worldView._m12 = 0; 

  worldView._m01 = 0;
  worldView._m11 = 1;
  worldView._m21 = 0;

  worldView._m00 = 1;
  worldView._m01 = 0;
  worldView._m02 = 0; 

  worldView._m00 = 1;
  worldView._m10 = 0;
  worldView._m20 = 0; 
  
#else // alternate billboarding method

  float aspect = 1280 / 720;

  float half_width = scale;
  float half_height = scale;

  float3 right = float3(0, 0, 0);
  float3 up = float3(0, 0, 0);

  if (true)
  {
    right = SpriteCBuffer.CamRight.xyz;
    up = SpriteCBuffer.CamUp.xyz;
  }
  else
  {
    // Getting right and up from model-view matrix:
    // http://www.lighthouse3d.com/opengl/billboarding/index.php?billCheat2

    // float4x4 rot90 =
    //   {
    //       { 0, 1, 0, 0 },
    //       { -1, 0, 0, 0 },
    //       { 0, 0, 1, 0 },
    //       { 0, 0, 0, 1 }
    //   };
    // worldView = rot90 * SpriteCBuffer.View;

    right[0] = worldView._m00;
    right[1] = worldView._m01;
    right[2] = worldView._m02;

    up[0] = worldView._m10;
    up[1] = worldView._m11;
    up[2] = worldView._m12;


  }

  // up[0] = worldView._m10;
  // up[1] = worldView._m11;
  // up[2] = worldView._m12;

  if (VertexIdx == 0) // float4(0.0f, 0.0f, 1, 1);
    output.PositionCS = float4(vtxPosition.xyz + half_width * right + half_height * up, 1.0f);
  else if (VertexIdx == 1) // float4(1.0f, 0.0f, 1, 1);
    output.PositionCS = float4(vtxPosition.xyz - half_width * right + half_height * up, 1.0f);
  else if (VertexIdx == 2) // float4(1.0f, 1.0f, 1, 1);
    output.PositionCS = float4(vtxPosition.xyz - half_width * right - half_height * up, 1.0f);
  else if (VertexIdx == 3) // float4(0.0f, 1.0f, 1, 1);
    output.PositionCS = float4(vtxPosition.xyz + half_width * right - half_height * up, 1.0f);

#endif

  output.PositionCS = mul(output.PositionCS, /* transpose */(worldView));
  // output.PositionCS = mul(output.PositionCS, /* transpose */(SpriteCBuffer.View));
  output.PositionCS = mul(output.PositionCS, /* transpose */(SpriteCBuffer.Projection));
  // output.PositionCS = mul(output.PositionCS, /* transpose */(SpriteCBuffer.ViewProj));

  return output;
}

float4 PS(PSInput p_Input) : SV_TARGET
{
  Texture2D spriteTexture = Tex2DTable[SpriteTextureIdx];
  float4 texColor = spriteTexture.Sample(LinearSampler, p_Input.TexCoord);
  float4 ret = float4(0, 0, 0, texColor.w);
  ret.xyz = texColor.xyz;

  // Intensify color:
  ret.x *= 3.0f;
  ret.yz *= 2.5f + (SpriteCBuffer.Params0.x * 0.5f);
  return ret;
}