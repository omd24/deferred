
typedef float4 Quaternion;

float4 PackQuaternion(in Quaternion q)
{
  Quaternion absQ = abs(q);
  float absMaxComponent = max(max(absQ.x, absQ.y), max(absQ.z, absQ.w));

  uint maxCompIdx = 0;
  float maxComponent = q.x;

  [unroll] for (uint i = 0; i < 4; ++i)
  {
    if (absQ[i] == absMaxComponent)
    {
      maxCompIdx = i;
      maxComponent = q[i];
    }
  }

  if (maxComponent < 0.0f)
    q *= -1.0f;

  float3 components;
  if (maxCompIdx == 0)
    components = q.yzw;
  else if (maxCompIdx == 1)
    components = q.xzw;
  else if (maxCompIdx == 2)
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
  if (trace > 0)
  {
    float s = 0.5f / sqrt(trace + 1.0f);
    q.w = 0.25f / s;
    q.x = (a[2][1] - a[1][2]) * s;
    q.y = (a[0][2] - a[2][0]) * s;
    q.z = (a[1][0] - a[0][1]) * s;
  }
  else
  {
    if (a[0][0] > a[1][1] && a[0][0] > a[2][2])
    {
      float s = 2.0f * sqrt(1.0f + a[0][0] - a[1][1] - a[2][2]);
      q.w = (a[2][1] - a[1][2]) / s;
      q.x = 0.25f * s;
      q.y = (a[0][1] + a[1][0]) / s;
      q.z = (a[0][2] + a[2][0]) / s;
    }
    else if (a[1][1] > a[2][2])
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