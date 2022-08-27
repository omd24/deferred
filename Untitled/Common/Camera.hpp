#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <directxmath.h>

using namespace DirectX;
//---------------------------------------------------------------------------//
struct Camera
{
  XMFLOAT3 m_InitialPosition;
  XMFLOAT3 m_Position;
  float m_Yaw;   // around y, relative to +z axis
  float m_Pitch; // around x, relative to the xz plane
  XMFLOAT3 m_LookDirection;
  XMFLOAT3 m_UpDirection;
  float m_MoveSpeed; // in units per second
  float m_TurnSpeed; // in radians per second

  struct
  {
    bool w;
    bool a;
    bool s;
    bool d;

    bool left;
    bool right;
    bool up;
    bool down;
  } m_KeysPressed;
};
//---------------------------------------------------------------------------//
inline void cameraReset(Camera* p_Camera)
{
  p_Camera->m_Position = p_Camera->m_InitialPosition;
  p_Camera->m_Yaw = XM_PI;
  p_Camera->m_Pitch = 0.0f;
  p_Camera->m_LookDirection = {0, 0, -1};
}
//---------------------------------------------------------------------------//
inline void cameraInit(Camera* p_Camera, XMFLOAT3 p_Position)
{
  memset(p_Camera, 0, sizeof(*p_Camera));
  p_Camera->m_LookDirection = XMFLOAT3(0, 0, -1);
  p_Camera->m_InitialPosition = p_Position;
  p_Camera->m_UpDirection = XMFLOAT3(0, 1, 0);
  p_Camera->m_MoveSpeed = 20.0f;
  p_Camera->m_TurnSpeed = XM_PIDIV2;

  cameraReset(p_Camera);
}
//---------------------------------------------------------------------------//
inline XMMATRIX getProjectionMatrix(
    float p_Fov,
    float p_AspectRatio,
    float p_NearPlane = 1.0f,
    float p_FarPlane = 1000.0f)
{
  return XMMatrixPerspectiveFovRH(
      p_Fov, p_AspectRatio, p_NearPlane, p_FarPlane);
}
//---------------------------------------------------------------------------//
inline XMMATRIX cameraGetViewMatrix(Camera* p_Camera)
{
  return XMMatrixLookToRH(
      XMLoadFloat3(&p_Camera->m_Position),
      XMLoadFloat3(&p_Camera->m_LookDirection),
      XMLoadFloat3(&p_Camera->m_UpDirection));
}
//---------------------------------------------------------------------------//
inline void cameraOnKeyDown(Camera* p_Camera, WPARAM p_Key)
{
  switch (p_Key)
  {
  case 'W':
    p_Camera->m_KeysPressed.w = true;
    break;
  case 'A':
    p_Camera->m_KeysPressed.a = true;
    break;
  case 'S':
    p_Camera->m_KeysPressed.s = true;
    break;
  case 'D':
    p_Camera->m_KeysPressed.d = true;
    break;
  case VK_LEFT:
    p_Camera->m_KeysPressed.left = true;
    break;
  case VK_RIGHT:
    p_Camera->m_KeysPressed.right = true;
    break;
  case VK_UP:
    p_Camera->m_KeysPressed.up = true;
    break;
  case VK_DOWN:
    p_Camera->m_KeysPressed.down = true;
    break;
  case VK_ESCAPE:
    cameraReset(p_Camera);
    break;
  }
}
//---------------------------------------------------------------------------//
inline void cameraOnKeyUp(Camera* p_Camera, WPARAM p_Key)
{
  switch (p_Key)
  {
  case 'W':
    p_Camera->m_KeysPressed.w = false;
    break;
  case 'A':
    p_Camera->m_KeysPressed.a = false;
    break;
  case 'S':
    p_Camera->m_KeysPressed.s = false;
    break;
  case 'D':
    p_Camera->m_KeysPressed.d = false;
    break;
  case VK_LEFT:
    p_Camera->m_KeysPressed.left = false;
    break;
  case VK_RIGHT:
    p_Camera->m_KeysPressed.right = false;
    break;
  case VK_UP:
    p_Camera->m_KeysPressed.up = false;
    break;
  case VK_DOWN:
    p_Camera->m_KeysPressed.down = false;
    break;
  }
}
//---------------------------------------------------------------------------//
inline void cameraUpdate(Camera* p_Camera, float p_ElapsedSeconds)
{
  // Update move vector in view space:
  XMFLOAT3 move(0, 0, 0);

  if (p_Camera->m_KeysPressed.a)
    move.x -= 1.0f;
  if (p_Camera->m_KeysPressed.d)
    move.x += 1.0f;
  if (p_Camera->m_KeysPressed.w)
    move.z -= 1.0f;
  if (p_Camera->m_KeysPressed.s)
    move.z += 1.0f;

  if (fabs(move.x) > 0.1f && fabs(move.z) > 0.1f)
  {
    XMVECTOR vector = XMVector3Normalize(XMLoadFloat3(&move));
    move.x = XMVectorGetX(vector);
    move.z = XMVectorGetZ(vector);
  }

  float moveInterval = p_Camera->m_MoveSpeed * p_ElapsedSeconds;
  float rotateInterval = p_Camera->m_TurnSpeed * p_ElapsedSeconds;

  if (p_Camera->m_KeysPressed.left)
    p_Camera->m_Yaw += rotateInterval;
  if (p_Camera->m_KeysPressed.right)
    p_Camera->m_Yaw -= rotateInterval;
  if (p_Camera->m_KeysPressed.up)
    p_Camera->m_Pitch += rotateInterval;
  if (p_Camera->m_KeysPressed.down)
    p_Camera->m_Pitch -= rotateInterval;

  // Clamp pitch to prevent from looking too far up or down
  p_Camera->m_Pitch = min(p_Camera->m_Pitch, XM_PIDIV4);
  p_Camera->m_Pitch = max(-XM_PIDIV4, p_Camera->m_Pitch);

  // Move the camera in its local space.
  float x = move.x * -cosf(p_Camera->m_Yaw) - move.z * sinf(p_Camera->m_Yaw);
  float z = move.x * sinf(p_Camera->m_Yaw) - move.z * cosf(p_Camera->m_Yaw);
  p_Camera->m_Position.x += x * moveInterval;
  p_Camera->m_Position.z += z * moveInterval;

  // Determine the look direction
  float r = cosf(p_Camera->m_Pitch);
  p_Camera->m_LookDirection.x = r * sinf(p_Camera->m_Yaw);
  p_Camera->m_LookDirection.y = sinf(p_Camera->m_Pitch);
  p_Camera->m_LookDirection.z = r * cosf(p_Camera->m_Yaw);
}
//---------------------------------------------------------------------------//
