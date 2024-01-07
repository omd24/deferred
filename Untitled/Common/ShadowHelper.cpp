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
} // namespace ShadowHelper
