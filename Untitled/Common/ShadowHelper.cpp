#include "ShadowHelper.hpp"
#include "Camera.hpp"

#include "D3D12Wrapper.hpp"

namespace ShadowHelper
{

// Transforms from [-1,1] post-projection space to [0,1] UV space
glm::mat4 ShadowScaleOffsetMatrix = glm::transpose(glm::mat4(
    glm::vec4(0.5f, 0.0f, 0.0f, 0.0f),
    glm::vec4(0.0f, -0.5f, 0.0f, 0.0f),
    glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
    glm::vec4(0.5f, 0.5f, 0.0f, 1.0f)));

static bool initialized = false;

void init()
{
  assert(initialized == false);

  initialized = true;
}

void deinit()
{
  assert(initialized);

  initialized = false;
}

void createPSOs()
{
  // TODO
}

void destroyPSOs()
{
  // TODO!
}

void prepareCascades(
    const glm::vec3& lightDir,
    uint64_t shadowMapSize,
    bool stabilize,
    const CameraBase& camera,
    SunShadowConstantsBase& constants,
    OrthographicCamera* cascadeCameras)
{
  const float MinDistance = 0.0f;
  const float MaxDistance = 1.0f;

  // Compute the split distances based on the partitioning mode
  float cascadeSplits[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  if (camera.IsOrthographic())
  {
    for (uint32_t i = 0; i < NumCascades; ++i)
      cascadeSplits[i] = lerp(MinDistance, MaxDistance, (i + 1.0f) / NumCascades);
  }
  else
  {
    float lambda = 0.5f;

    float nearClip = camera.NearClip();
    float farClip = camera.FarClip();
    float clipRange = farClip - nearClip;

    float minZ = nearClip + MinDistance * clipRange;
    float maxZ = nearClip + MaxDistance * clipRange;

    float range = maxZ - minZ;
    float ratio = maxZ / minZ;

    for (uint32_t i = 0; i < NumCascades; ++i)
    {
      float p = (i + 1) / static_cast<float>(NumCascades);
      float log = minZ * std::pow(ratio, p);
      float uniform = minZ + range * p;
      float d = lambda * (log - uniform) + uniform;
      cascadeSplits[i] = (d - nearClip) / clipRange;
    }
  }

  glm::mat4 c0Matrix = glm::mat4(0);

  // Prepare the projections for each cascade
  for (uint64_t cascadeIdx = 0; cascadeIdx < NumCascades; ++cascadeIdx)
  {
    // Get the 8 points of the view frustum in world space
    glm::vec3 frustumCornersWS[8] = {
        glm::vec3(-1.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 1.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        glm::vec3(1.0f, -1.0f, 1.0f),
        glm::vec3(-1.0f, -1.0f, 1.0f),
    };

    float prevSplitDist = cascadeIdx == 0 ? MinDistance : cascadeSplits[cascadeIdx - 1];
    float splitDist = cascadeSplits[cascadeIdx];

    glm::mat4 invViewProj = glm::inverse(camera.ViewProjectionMatrix());
    for (uint64_t i = 0; i < 8; ++i)
      frustumCornersWS[i] = _transformVec3Mat4(frustumCornersWS[i], invViewProj);

    // Get the corners of the current cascade slice of the view frustum
    for (uint64_t i = 0; i < 4; ++i)
    {
      glm::vec3 cornerRay = frustumCornersWS[i + 4] - frustumCornersWS[i];
      glm::vec3 nearCornerRay = cornerRay * prevSplitDist;
      glm::vec3 farCornerRay = cornerRay * splitDist;
      frustumCornersWS[i + 4] = frustumCornersWS[i] + farCornerRay;
      frustumCornersWS[i] = frustumCornersWS[i] + nearCornerRay;
    }

    // Calculate the centroid of the view frustum slice
    glm::vec3 frustumCenter = glm::vec3(0.0f);
    for (uint64_t i = 0; i < 8; ++i)
      frustumCenter += frustumCornersWS[i];
    frustumCenter *= (1.0f / 8.0f);

    // Pick the up vector to use for the light camera
    glm::vec3 upDir = camera.Right();

    glm::vec3 minExtents;
    glm::vec3 maxExtents;

    if (stabilize)
    {
      // This needs to be constant for it to be stable
      upDir = glm::vec3(0.0f, 1.0f, 0.0f);

      // Calculate the radius of a bounding sphere surrounding the frustum corners
      float sphereRadius = 0.0f;
      for (uint64_t i = 0; i < 8; ++i)
      {
        float dist = glm::length(glm::vec3(frustumCornersWS[i]) - frustumCenter);
        sphereRadius = std::max(sphereRadius, dist);
      }

      sphereRadius = std::ceil(sphereRadius * 16.0f) / 16.0f;

      maxExtents = glm::vec3(sphereRadius, sphereRadius, sphereRadius);
      minExtents = -maxExtents;
    }
    else
    {
      // Create a temporary view matrix for the light
      glm::vec3 lightCameraPos = frustumCenter;
      glm::vec3 lookAt = frustumCenter - lightDir;
      glm::mat4 lightView =
          glm::lookAtLH(lightCameraPos, lookAt, upDir);

      // Calculate an AABB around the frustum corners
      const float floatMax = std::numeric_limits<float>::max();
      glm::vec3 mins = glm::vec3(floatMax, floatMax, floatMax);
      glm::vec3 maxes = glm::vec3(-floatMax, -floatMax, -floatMax);
      for (uint32_t i = 0; i < 8; ++i)
      {
        glm::vec3 corner = _transformVec3Mat4(frustumCornersWS[i], lightView);
        mins = glm::min(mins, corner);
        maxes = glm::max(maxes, corner);
      }

      minExtents = glm::vec3(mins);
      maxExtents = glm::vec3(maxes);
    }

    // Adjust the min/max to accommodate the filtering size
    float scale = (shadowMapSize + 7.0f) / shadowMapSize;
    minExtents.x *= scale;
    minExtents.y *= scale;
    maxExtents.x *= scale;
    maxExtents.y *= scale;

    glm::vec3 cascadeExtents = maxExtents - minExtents;

    // Get position of the shadow camera
    glm::vec3 shadowCameraPos = frustumCenter + lightDir * -minExtents.z;

    // Come up with a new orthographic camera for the shadow caster
    OrthographicCamera& shadowCamera = cascadeCameras[cascadeIdx];
    shadowCamera.Initialize(
        minExtents.x, minExtents.y, maxExtents.x, maxExtents.y, 0.0f, cascadeExtents.z);
    shadowCamera.SetLookAt(shadowCameraPos, frustumCenter, upDir);

    if (stabilize)
    {
      // Create the rounding matrix, by projecting the world-space origin and determining
      // the fractional offset in texel space
      glm::mat4 shadowMatrix = shadowCamera.ViewProjectionMatrix();
      glm::vec4 shadowOrigin = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
      shadowOrigin = shadowOrigin * shadowMatrix;
      shadowOrigin = (shadowMapSize / 2.0f) * shadowOrigin;

      glm::vec4 roundedOrigin = glm::round(shadowOrigin);
      glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
      roundOffset = (2.0f / shadowMapSize) * roundOffset;
      roundOffset.z = 0.0f;
      roundOffset.w = 0.0f;

      glm::mat4 shadowProj = shadowCamera.ProjectionMatrix();
      shadowProj[3] = shadowProj[3] + roundOffset;
      shadowCamera.SetProjection(shadowProj);
    }

    glm::mat4 shadowMatrix = shadowCamera.ViewProjectionMatrix();
    shadowMatrix = shadowMatrix * ShadowScaleOffsetMatrix;

    // Store the split distance in terms of view space depth
    const float clipDist = camera.FarClip() - camera.NearClip();
    constants.CascadeSplits[cascadeIdx] = camera.NearClip() + splitDist * clipDist;

    if (cascadeIdx == 0)
    {
      c0Matrix = shadowMatrix;
      constants.ShadowMatrix = shadowMatrix;
      constants.CascadeOffsets[0] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
      constants.CascadeScales[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    else
    {
      // Calculate the position of the lower corner of the cascade partition, in the UV space
      // of the first cascade partition
      glm::mat4 invCascadeMat = glm::inverse(shadowMatrix);
      glm::vec3 cascadeCorner = _transformVec3Mat4(glm::vec3(0.0f, 0.0f, 0.0f), invCascadeMat);
      cascadeCorner = _transformVec3Mat4(cascadeCorner, c0Matrix);

      // Do the same for the upper corner
      glm::vec3 otherCorner = _transformVec3Mat4(glm::vec3(1.0f, 1.0f, 1.0f), invCascadeMat);
      otherCorner = _transformVec3Mat4(otherCorner, c0Matrix);

      // Calculate the scale and offset
      glm::vec3 cascadeScale = glm::vec3(1.0f, 1.0f, 1.f) / (otherCorner - cascadeCorner);
      constants.CascadeOffsets[cascadeIdx] = glm::vec4(-cascadeCorner, 0.0f);
      constants.CascadeScales[cascadeIdx] = glm::vec4(cascadeScale, 1.0f);
    }
  }
}

} // namespace ShadowHelper
