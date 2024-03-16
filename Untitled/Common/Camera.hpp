#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef GLM_FORCE_RADIANS
#  define GLM_FORCE_RADIANS
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

// Abstract base class for camera types
class CameraBase
{

protected:
  glm::mat4 view;
  glm::mat4 projection;
  glm::mat4 viewProjection;

  glm::mat4 world;
  glm::vec3 position;
  glm::quat orientation;

  float nearZ = 0.0f;
  float farZ = 0.0f;

  virtual void CreateProjection() = 0;
  void WorldMatrixChanged()
  {
    view = glm::inverse(world);
    viewProjection = view * projection;
  }

public:
  void Initialize(float nearZ, float farZ)
  {
    nearZ = nearZ;
    farZ = farZ;
    world = glm::identity<glm::mat4>();
    view = glm::identity<glm::mat4>();
    position = glm::vec3(0.0f, 0.0f, 0.0f);
    orientation = glm::identity<glm::quat>();
  }

  const glm::mat4& ViewMatrix() const { return view; };
  const glm::mat4& ProjectionMatrix() const { return projection; };
  const glm::mat4& ViewProjectionMatrix() const { return viewProjection; };
  const glm::mat4& WorldMatrix() const { return world; };
  const glm::vec3& Position() const { return position; };
  const glm::quat& Orientation() const { return orientation; };
  float NearClip() const { return nearZ; };
  float FarClip() const { return farZ; };

  glm::vec3 Forward() const
  {
    glm::mat4 transposed = glm::transpose(world);
    return transposed[2];
  }
  glm::vec3 Back() const
  {
    glm::mat4 transposed = glm::transpose(world);
    return -transposed[2];
  }
  glm::vec3 Up() const
  {
    glm::mat4 transposed = glm::transpose(world);
    return transposed[1];
  }
  glm::vec3 Down() const
  {
    glm::mat4 transposed = glm::transpose(world);
    return -transposed[1];
  }
  glm::vec3 Right() const
  {
    glm::mat4 transposed = glm::transpose(world);
    return transposed[0];
  }
  glm::vec3 Left() const
  {
    glm::mat4 transposed = glm::transpose(world);
    return -transposed[0];
  }

  void SetLookAt(const glm::vec3& eye, const glm::vec3& lookAt, const glm::vec3& up)
  {
    view = glm::transpose(glm::lookAtLH(eye, lookAt, up));
    world = glm::transpose(glm::inverse(view));
    position = eye;
    orientation = glm::quat(world);

    WorldMatrixChanged();
  }
  void SetWorldMatrix(const glm::mat4& newWorld)
  {
    world = glm::transpose(newWorld);
    glm::mat4 worldTranspose = glm::transpose(world);
    position = worldTranspose[3];
    orientation = glm::quat(world);

    WorldMatrixChanged();
  }
  void SetPosition(const glm::vec3& newPosition)
  {
    position = newPosition;
    world[0][3] = newPosition[0];
    world[1][3] = newPosition[1];
    world[2][3] = newPosition[2];

    WorldMatrixChanged();
  }
  void SetOrientation(const glm::quat& newOrientation)
  {
    world = glm::mat4(glm::quat(newOrientation));
    world = glm::transpose(world);
    orientation = newOrientation;
    world[0][3] = position[0];
    world[1][3] = position[1];
    world[2][3] = position[2];
    WorldMatrixChanged();
  }
  void SetNearClip(float newNearClip)
  {
    nearZ = newNearClip;
    CreateProjection();
  }
  void SetFarClip(float newFarClip)
  {
    farZ = newFarClip;
    CreateProjection();
  }
  void SetProjection(const glm::mat4& newProjection)
  {
    projection = glm::transpose(newProjection);
    viewProjection = view * projection;
  }

  virtual bool IsOrthographic() const { return false; }
};

// Camera with an orthographic projection
class OrthographicCamera : public CameraBase
{

protected:
  float xMin = 0.0f;
  float xMax = 0.0f;
  float yMin = 0.0f;
  float yMax = 0.0f;

  virtual void CreateProjection() override
  {
    projection = glm::mat4(glm::orthoLH_ZO(xMin, xMax, yMin, yMax, nearZ, farZ));
    projection = glm::transpose(projection);
    viewProjection = view * projection;
  }

public:
  void Initialize(float minX, float minY, float maxX, float maxY, float nearClip, float farClip)
  {
    CameraBase::Initialize(nearClip, farClip);

    assert(maxX > minX && maxX > minY);

    nearZ = nearClip;
    farZ = farClip;
    xMin = minX;
    xMax = maxX;
    yMin = minY;
    yMax = maxY;

    CreateProjection();
  }

  float MinX() const { return xMin; };
  float MinY() const { return yMin; };
  float MaxX() const { return xMax; };
  float MaxY() const { return yMax; };

  void SetMinX(float minX)
  {
    xMin = minX;
    CreateProjection();
  }
  void SetMinY(float minY)
  {
    yMin = minY;
    CreateProjection();
  }
  void SetMaxX(float maxX)
  {
    xMax = maxX;
    CreateProjection();
  }
  void SetMaxY(float maxY)
  {
    yMax = maxY;
    CreateProjection();
  }

  bool IsOrthographic() const override { return true; }
};

// Camera with a perspective projection
class PerspectiveCamera : public CameraBase
{
public:
  float width = 1024.0f;

protected:
  float aspect = 0.0f;
  float fov = 0.0f;

  virtual void CreateProjection() override
  {
    projection = glm::mat4(glm::perspectiveFovLH_ZO(fov, width, width / aspect, nearZ, farZ));
    projection = glm::transpose(projection);
    viewProjection = view * projection;
  }

public:
  void Initialize(float aspectRatio, float fieldOfView, float nearClip, float farClip, float w)
  {
    CameraBase::Initialize(nearClip, farClip);
    assert(aspectRatio > 0);
    assert(fieldOfView > 0 && fieldOfView <= 3.14159f);
    nearZ = nearClip;
    farZ = farClip;
    aspect = aspectRatio;
    fov = fieldOfView /* * 180.0f / 3.14f*/;
    width = w;
    CreateProjection();
  }

  float AspectRatio() const { return aspect; };
  float FieldOfView() const { return fov; };

  void SetAspectRatio(float aspectRatio)
  {
    aspect = aspectRatio;
    CreateProjection();
  }
  void SetFieldOfView(float fieldOfView)
  {
    fov = fieldOfView;
    CreateProjection();
  }
};

// Perspective camera that rotates about Z and Y axes
class FirstPersonCamera : public PerspectiveCamera
{

protected:
  float xRot = 0.0f;
  float yRot = 0.0f;

public:
  void Initialize(
      float aspectRatio = 16.0f / 9.0f,
      float fieldOfView = glm::pi<float>(),
      float nearClip = 0.01f,
      float farClip = 100.0f,
      float width = 1024.0f)
  {
    PerspectiveCamera::Initialize(aspectRatio, fieldOfView, nearClip, farClip, width);
    xRot = 0.0f;
    yRot = 0.0f;
  }

  float XRotation() const { return xRot; };
  float YRotation() const { return yRot; };

  void SetXRotation(float xRotation)
  {
    xRot = glm::clamp(xRotation, -glm::half_pi<float>(), glm::half_pi<float>());
    glm::quat myquaternion = glm::quat(glm::vec3(xRot, yRot, 0)); // pitch, yaw, roll
    SetOrientation(myquaternion);
  }

  float XMScalarModAngle(float Angle) noexcept
  {
    // Note: The modulo is performed with unsigned math only to work
    // around a precision error on numbers that are close to PI

    // Normalize the range from 0.0f to XM_2PI
    Angle = Angle + glm::pi<float>();
    // Perform the modulo, unsigned
    float fTemp = fabsf(Angle);
    fTemp = fTemp - (2.0f * glm::pi<float>() *
                     static_cast<float>(static_cast<int32_t>(fTemp / glm::two_pi<float>())));
    // Restore the number to the range of -XM_PI to XM_PI-epsilon
    fTemp = fTemp - glm::pi<float>();
    // If the modulo'd value was negative, restore negation
    if (Angle < 0.0f)
    {
      fTemp = -fTemp;
    }
    return fTemp;
  }

  void SetYRotation(float yRotation)
  {
    yRot = XMScalarModAngle(yRotation);
    glm::quat myquaternion = glm::quat(glm::vec3(xRot, yRot, 0)); // pitch, yaw, roll
    SetOrientation(myquaternion);
  }

  void ApplyJittering (float x, float y)
  {
    // Reset camera projection
    CreateProjection();

    // projection.m20 += x;
    // projection.m21 += y;

    // TODO: unify all the 3D math stuff to avoid these discrepancies!!!
    projection[0][2] += x;
    projection[1][2] += y;

    /*glm::mat4 jitteringMatrix = glm::mat4(1);
    jitteringMatrix[3] = glm::vec4(x, y, 0.0f, 1);
    projection = jitteringMatrix * projection;*/

    viewProjection = view * projection;
  }
};
//---------------------------------------------------------------------------//
