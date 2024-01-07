#pragma once

#include "Utility.hpp"

namespace ShadowHelper
{

extern glm::mat4 ShadowScaleOffsetMatrix;

void init();
void deinit();

void createPSOs();
void destroyPSOs();

} // namespace ShadowHelper
