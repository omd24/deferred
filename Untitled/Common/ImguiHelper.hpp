#pragma once

#include "Utility.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "ImGui/imgui.h"

namespace ImGuiHelper
{
void init(HWND& window, ID3D12Device* dev);
void deinit();

void beginFrame(
    uint32_t displayWidth,
    uint32_t displayHeight,
    float timeDelta,
    ID3D12Device* dev);
void endFrame(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    uint32_t displayWidth,
    uint32_t displayHeight);

} // namespace ImGuiHelper

inline ImVec2 ToImVec2(glm::vec2 v)
{
  return ImVec2(v.x, v.y);
}

inline glm::vec2 ToFloat2(ImVec2 v)
{
  return glm::vec2(v.x, v.y);
}

inline ImColor ToImColor(glm::vec3 v)
{
  return ImColor(v.x, v.y, v.z);
}

inline ImColor ToImColor(glm::vec4 v)
{
  return ImColor(v.x, v.y, v.z, v.w);
}
