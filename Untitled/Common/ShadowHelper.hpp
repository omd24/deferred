#pragma once

#include "Utility.hpp"
#include "Camera.hpp"

const uint64_t NumCascades = 4;
const float MaxShadowFilterSize = 9.0f;

struct SunShadowConstantsBase
{
  glm::mat4 ShadowMatrix;
  float CascadeSplits[NumCascades] = {};
  glm::vec4 CascadeOffsets[NumCascades];
  glm::vec4 CascadeScales[NumCascades];
};

struct SunShadowConstantsDepthMap
{
  SunShadowConstantsBase Base;
  uint32_t Dummy[4] = {};
};

namespace ShadowHelper
{

extern glm::mat4 ShadowScaleOffsetMatrix;

void init();
void deinit();

void createPSOs();
void destroyPSOs();

void prepareCascades(
    const glm::vec3& lightDir,
    uint64_t shadowMapSize,
    bool stabilize,
    const CameraBase& camera,
    SunShadowConstantsBase& constants,
    OrthographicCamera* cascadeCameras);

} // namespace ShadowHelper
