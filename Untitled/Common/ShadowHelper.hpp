#pragma once

#include "Utility.hpp"

class Camera;
class OrthographicCamera;
class PerspectiveCamera;
struct ModelSpotLight;
struct DepthBuffer;
struct RenderTexture;

namespace ShadowHelper
{

extern glm::mat4 ShadowScaleOffsetMatrix;

void init();
void deinit();

void createPSOs();
void destroyPSOs();

} // namespace ShadowHelper
