#pragma once

#include "Utility.hpp"

struct Quaternion
{
  float x, y, z, w;

  Quaternion() { *this = Quaternion::Identity(); }
  Quaternion(float x_, float y_, float z_, float w_)
  {
    // NOTE: glm component order differs
    x = x_;
    y = y_;
    z = z_;
    w = w_;
  }
  Quaternion(const glm::vec3& axis, float angle) { *this = Quaternion::FromAxisAngle(axis, angle); }

  Quaternion& operator=(const glm::quat& other)
  {
    // NOTE: glm component order differs
    x = other.x;
    y = other.y;
    z = other.z;
    w = other.w;
    return *this;
  }

  static Quaternion Identity()
  {
    // NOTE: glm component order differs
    return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
  }
  static Quaternion FromAxisAngle(const glm::vec3& axis, float angle)
  {
    assert(false); // TODO!}
  };

  glm::mat3 ToMat3() const
  {
    // glm component orders differs from human understanding ^^
    // GLM_FUNC_QUALIFIER GLM_CONSTEXPR qua<T, Q>::qua(T _w, T _x, T _y, T _z)
    glm::quat q = glm::quat(w, x, y, z);
    return glm::mat3_cast(q);
  }
};
