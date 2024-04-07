#pragma once

#include "glm/gtc/packing.hpp"

struct Half4
{
  uint16_t x;
  uint16_t y;
  uint16_t z;
  uint16_t w;

  Half4() : x(0), y(0), z(0), w(0) {}

  Half4(uint16_t x, uint16_t y, uint16_t z, uint16_t w) : x(x), y(y), z(z), w(w) {}

  Half4(float x_, float y_, float z_, float w_)
  {
    x = glm::packHalf1x16(x_);
    y = glm::packHalf1x16(y_);
    z = glm::packHalf1x16(z_);
    w = glm::packHalf1x16(w_);
  }

  explicit Half4(const glm::vec4& v)
  {
    x = glm::packHalf1x16(v.x);
    y = glm::packHalf1x16(v.y);
    z = glm::packHalf1x16(v.z);
    w = glm::packHalf1x16(v.w);
  }

  glm::vec3 ToFloat3() const
  {
    return glm::vec3(glm::unpackHalf1x16(x), glm::unpackHalf1x16(y), glm::unpackHalf1x16(z));
  }

  glm::vec4 ToFloat4() const
  {
    return glm::vec4(
        glm::unpackHalf1x16(x),
        glm::unpackHalf1x16(y),
        glm::unpackHalf1x16(z),
        glm::unpackHalf1x16(w));
  }
};
