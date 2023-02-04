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

struct VSConstants
{
  row_major float4x4 WorldView;
  row_major float4x4 World;
  row_major float4x4 View;
  row_major float4x4 Projection;
  float4 Params0;
  float4 Params1;
};
ConstantBuffer<VSConstants> VSCBuffer : register(b0);

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
#if 1

#if DRAW_SS_QUAD
  float2 vtxPosition = 0.0f;
  if (VertexIdx == 1)
      vtxPosition = float2(1.0f, 0.0f);
  else if (VertexIdx == 2)
      vtxPosition = float2(1.0f, 1.0f);
  else if (VertexIdx == 3)
      vtxPosition = float2(0.0f, 1.0f);

  float textureWH = 10;
  float4 rect = float4(0.0f, 0.0f, textureWH, textureWH);

  // Scale the quad so that it's texture-sized
  float2 positionSS = vtxPosition * rect.zw;

  float scale = 10.0f;
  positionSS *= scale;

  // Rotation
  // positionSS = mul(positionSS, float2x2(cosRotation, -sinRotation, sinRotation, cosRotation));

  // Translate
  float2 pos = float2(10.0f, 10.0f);
  positionSS += pos;

  // Scale by the viewport size, flip Y, then rescale to device coordinates
  float2 viewportSize = float2(1280, 720);
  float2 positionDS = positionSS;
  positionDS /= viewportSize;
  positionDS = positionDS * 2 - 1;
  positionDS.y *= -1;

  // Figure out the texture coordinates
  float2 outTexCoord = vtxPosition;
  outTexCoord.xy *= rect.zw / textureWH;
  outTexCoord.xy += rect.xy / textureWH;

  VSOutput output;
  output.PositionCS = float4(positionDS, 1.0f, 1.0f);
  output.TexCoord = outTexCoord;
  // output.Color = instanceData.Color;
#endif // DRAW_SS_QUAD


  // Experiment
  VSOutput output;

  float4 vtxPosition = float4(0, 0, 1, 1);
  if (VertexIdx == 1)
    vtxPosition = float4(1.0f, 0.0f, 1, 1);
  else if (VertexIdx == 2)
    vtxPosition = float4(1.0f, 1.0f, 1, 1);
  else if (VertexIdx == 3)
    vtxPosition = float4(0.0f, 1.0f, 1, 1);

  // UVs
  output.TexCoord = vtxPosition.xy;

  // Scale:
  float scale = VSCBuffer.Params0.x;
  vtxPosition.xy *= scale;

  output.PositionCS = vtxPosition;
  float4x4 mat = VSCBuffer.WorldView;
  // mat._m31 = 0.0f;
  output.PositionCS = mul(output.PositionCS, mat);
  output.PositionCS = mul(output.PositionCS, VSCBuffer.Projection);

  return output;

#else

  VSOutput output;
  float4 positionOS = float4(p_Input.PositionOS, 1.0f);
  output.PositionCS = positionOS;

#if 0
  // N.B. For billboarding just set the upper left 3x3 of worldView to identity:
  // https://stackoverflow.com/a/15325758/4623650
  matrix worldView = mul(VSCBuffer.World, VSCBuffer.View);
  worldView._m00 = 1;worldView._m01 = 0;worldView._m02 = 0;
  worldView._m10 = 0;worldView._m11 = 1;worldView._m12 = 0;
  worldView._m20 = 0;worldView._m21 = 0;worldView._m22 = 1;
  output.PositionCS = mul(output.PositionCS, worldView);
#endif

  output.PositionCS = mul(output.PositionCS, VSCBuffer.WorldView);
  output.PositionCS = mul(output.PositionCS, VSCBuffer.Projection); 

  /// Per particle scale?
  /// Alternatively just set worldView._m00 and worldView._m11
  // float4x4 scaleMat = { 1.0f, 0.0f, 0.0f, 0.0f, // row 1
  //                       0.0f, 1.0f, 0.0f, 0.0f, // row 2
  //                       0.0f, 0.0f, 1.0f, 0.0f, // row 3
  //                       0.0f, 0.0f, 0.0f, 1.0f, // row 4
  //                     };
  // output.PositionCS = mul(scaleMat, output.PositionCS); 

  output.TexCoord = p_Input.UV;
  return output;

#endif
}

float4 PS(PSInput p_Input) : SV_TARGET
{
  Texture2D spriteTexture = Tex2DTable[SpriteTextureIdx];
  float4 texColor = spriteTexture.Sample(LinearSampler, p_Input.TexCoord);
  float4 ret = float4(0, 0, 0, texColor.w);
  ret.xyz = texColor.xyz * 3.5f + (VSCBuffer.Params0.x * 1.5f);
  return ret;

  // return float4(1.0f, 0.0f, 0.0f, 1.0f);
}