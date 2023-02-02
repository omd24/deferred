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

struct VSConstants
{
  row_major float4x4 WorldView;
  row_major float4x4 World;
  row_major float4x4 View;
  row_major float4x4 Projection;
};

ConstantBuffer<VSConstants> VSCBuffer : register(b0);

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
};

struct PSInput
{
  float4 PositionSS : SV_Position;
  float3 PositionWS : POSITIONWS;
};

VSOutput VS(VSInput p_Input)
{
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

  return output;
}

float4 PS(PSInput p_Input) : SV_TARGET
{
  float4 ret = float4(1.0, 0.0f, 0.0f, 0.5f);
  return ret;
}