#pragma once

#include "Utility.hpp"


const uint64_t NumCascades = 4;
const float MaxShadowFilterSize = 9.0f;

struct SunShadowConstantsBase
{
  glm::mat4 ShadowMatrix;
  float CascadeSplits[NumCascades] = {};
  glm::vec4 CascadeOffsets[NumCascades];
  glm::vec4 CascadeScales[NumCascades];
};

namespace ShadowHelper
{

extern glm::mat4 ShadowScaleOffsetMatrix;

void init();
void deinit();

void createPSOs();
void destroyPSOs();

} // namespace ShadowHelper
