#include "ImguiHelper.hpp"
#include "D3D12Wrapper.hpp"

#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"

#include "../AppSettings.hpp"

namespace ImGuiHelper
{

static ID3D12DescriptorHeap* g_ImguiHeap = nullptr;

static void WindowMessageCallback(void* context, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  ImGuiIO& io = ImGui::GetIO();
  switch (msg)
  {
  case WM_LBUTTONDOWN:
    io.MouseDown[0] = true;
    return;
  case WM_LBUTTONUP:
    io.MouseDown[0] = false;
    return;
  case WM_RBUTTONDOWN:
    io.MouseDown[1] = true;
    return;
  case WM_RBUTTONUP:
    io.MouseDown[1] = false;
    return;
  case WM_MBUTTONDOWN:
    io.MouseDown[2] = true;
    return;
  case WM_MBUTTONUP:
    io.MouseDown[2] = false;
    return;
  case WM_MOUSEWHEEL:
    io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1.0f : -1.0f;
    return;
  case WM_MOUSEMOVE:
    io.MousePos.x = (signed short)(lParam);
    io.MousePos.y = (signed short)(lParam >> 16);
    return;
  case WM_KEYDOWN:
    if (wParam < 256)
      io.KeysDown[wParam] = 1;
    return;
  case WM_KEYUP:
    if (wParam < 256)
      io.KeysDown[wParam] = 0;
    return;
  case WM_CHAR:
    if (wParam > 0 && wParam < 0x10000)
      io.AddInputCharacter(uint16_t(wParam));
    return;
  }
}

static void renderInternal()
{
  // Rendering
  {
    ImGuiWindowFlags window_flags = 0;
    // window_flags |= ImGuiWindowFlags_NoTitleBar;
    // window_flags |= ImGuiWindowFlags_NoScrollbar;
    // window_flags |= ImGuiWindowFlags_MenuBar;
    // window_flags |= ImGuiWindowFlags_NoMove;
    // window_flags |= ImGuiWindowFlags_NoResize;
    // window_flags |= ImGuiWindowFlags_NoCollapse;
    // window_flags |= ImGuiWindowFlags_NoNav;
    // window_flags |= ImGuiWindowFlags_NoBackground;
    // window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;

    // Note! to disable close button, pass NULL
    static bool open = true;
    ImGui::Begin("App Settings:", &open, window_flags);

    // App settings controls:
    ImGui::Checkbox("Render Lights", (bool*)&AppSettings::RenderLights);

    ImGui::ColorEdit3("Lights Color", AppSettings::LightColor, ImGuiColorEditFlags_DisplayRGB);
    // Fog options:
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Volumetric Fog", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Checkbox("Use Linear Sampler", (bool*)&AppSettings::FOG_UseLinearClamp);
      ImGui::Checkbox("Disable Light Scattering", (bool*)&AppSettings::FOG_DisableLightScattering);
      ImGui::Checkbox("Use Clustered Lighting", (bool*)&AppSettings::FOG_UseClusteredLighting);
      ImGui::Checkbox("Enable Shadow Map Sampling", (bool*)&AppSettings::FOG_EnableShadowMapSampling);
      ImGui::SliderFloat("Scattering Factor", &AppSettings::FOG_ScatteringFactor, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Constant Fog Modifier", &AppSettings::FOG_ConstantFogDensityModifier, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Height Fog Density", &AppSettings::FOG_HeightFogDenisty, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Height Fog Falloff", &AppSettings::FOG_HeightFogFalloff, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Phase Anisotropy", &AppSettings::FOG_PhaseAnisotropy, 0.0f, 1.0f, "%.2f");

      ImGui::SeparatorText("Noise Options");
      ImGui::Checkbox("Enable Z-Jitter", (bool*)&AppSettings::FOG_ApplyZJitter);
      ImGui::RadioButton("Blue Noise", &AppSettings::FOG_NoiseType, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Interleaved Gradient Noise", &AppSettings::FOG_NoiseType, 1);
      ImGui::SliderFloat("Noise Scale", &AppSettings::FOG_NoiseScale, 0.0f, 1.0f, "%.3f");
      ImGui::Checkbox("Enable XY-Jitter", (bool*)&AppSettings::FOG_ApplyXYJitter);
      ImGui::SliderFloat("Jitter Scale (XY)", &AppSettings::FOG_JitterScaleXY, 0.0f, 5.0f, "%.3f");
      ImGui::SliderFloat("Dithering Scale", &AppSettings::FOG_DitheringScale, 0.0f, 0.1f, "%.3f");

      ImGui::SeparatorText("Temporal Pass Options");
      ImGui::Checkbox("Enable Temporal Filter", (bool*)&AppSettings::FOG_EnableTemporalFilter);
      ImGui::SliderFloat("Temporal Reprojection Percentage", &AppSettings::FOG_TemporalPercentage, 0.0f, 1.0f, "%.2f");
      
      ImGui::SeparatorText("Box Volume Options");
      ImGui::SliderFloat("Box Size", &AppSettings::FOG_BoxSize, 0.0f, 10.0f, "%.2f");
      ImGui::SliderFloat3("Box Position", AppSettings::FOG_BoxPosition, -5.f, 10.f, "%2.2f", ImGuiSliderFlags_None);
      ImGui::SliderFloat("Box Density", &AppSettings::FOG_BoxFogDensity, 0.0f, 20.0f, "%.2f");
      ImGui::ColorEdit3("Box Color", AppSettings::FOG_BoxColor, ImGuiColorEditFlags_DisplayRGB);
    }

    // Post processing options:
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_None))
    {
      ImGui::SliderFloat(
          "Exposure", &AppSettings::Exposure, -24.0f, 24.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::SliderFloat("Bloom-Exposure", &AppSettings::BloomExposure, -10.0f, 0.0f, "%.1f");
      ImGui::SliderFloat("Bloom-Mag", &AppSettings::BloomMagnitude, 0.0f, 2.0f, "%.2f");
      ImGui::SliderFloat("Bloom-Sigma", &AppSettings::BloomBlurSigma, 0.5f, 2.5f, "%.2f");
    }

    // Other options:
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Miscellaneous", ImGuiTreeNodeFlags_None))
    {
      ImGui::SliderFloat("Camera Speed", &AppSettings::CameraSpeed, 1.0f, 5.0f, "%.2f");
      ImGui::Checkbox("Show Albedo", (bool*)&AppSettings::ShowAlbedoMaps);
      ImGui::Checkbox("Show Normals", (bool*)&AppSettings::ShowNormalMaps);
      ImGui::Checkbox("Show Specular", (bool*)&AppSettings::ShowSpecular);
      // ImGui::Checkbox("Show Light Counts", &AppSettings::ShowLightCounts);
      ImGui::Checkbox("Show Cluster  Visualizer", (bool*)&AppSettings::ShowClusterVisualizer);
      ImGui::Checkbox("Show UV Gradients", (bool*)&AppSettings::ShowUVGradients);
      // ImGui::Checkbox("Animate Light Intensity", &AppSettings::AnimateLightIntensity);
      // ImGui::Checkbox("Compute UV Gradients", &AppSettings::ComputeUVGradients);
    }

    ImGui::Separator();
    ImGui::Text(
        "Camera position x = %.5f, y = %.2f, z = %.2f",
        AppSettings::CameraPosition.x,
        AppSettings::CameraPosition.y,
        AppSettings::CameraPosition.z);

    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::End();
  }
}

void init(HWND& window, ID3D12Device* dev)
{
  g_ImguiCallback = WindowMessageCallback;

  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.KeyMap[ImGuiKey_Tab] = VK_TAB;
  io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
  io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
  io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
  io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
  io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
  io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
  io.KeyMap[ImGuiKey_Home] = VK_HOME;
  io.KeyMap[ImGuiKey_End] = VK_END;
  io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
  io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
  io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
  io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
  io.KeyMap[ImGuiKey_A] = 'A';
  io.KeyMap[ImGuiKey_C] = 'C';
  io.KeyMap[ImGuiKey_V] = 'V';
  io.KeyMap[ImGuiKey_X] = 'X';
  io.KeyMap[ImGuiKey_Y] = 'Y';
  io.KeyMap[ImGuiKey_Z] = 'Z';

  // The following has been deprecated in new ImGui
  // io.ImeWindowHandle = window;

  ImGui::GetMainViewport()->PlatformHandleRaw = (void*)window;

  // Styling imgui:
  {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameBorderSize = 1.0f;
    // ImGui::StyleColorsClassic();
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();
    ImVec4 pitchBlack = {.0f, .0f, .0f, 1.0f};
    ImVec4 black = {.10f, .10f, .10f, 1.0f};
    ImVec4 darkGray = {.20f, .20f, .20f, 1.0f};
    ImVec4 gray = {.30f, .30f, .30f, 1.0f};
    ImVec4 lightGray = {.45f, .45f, .45f, 1.0f};
    ImVec4 paleGray = {.75f, .75f, .75f, 1.0f};
    ImVec4 white = {1.0f, 1.0f, 1.0f, 1.0f};
    memcpy(&style.Colors[ImGuiCol_WindowBg], &pitchBlack, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_FrameBg], &darkGray, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_FrameBgActive], &pitchBlack, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_FrameBgHovered], &gray, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_TitleBg], &pitchBlack, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_TitleBgActive], &pitchBlack, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_SliderGrab], &paleGray, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_SliderGrabActive], &gray, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_Button], &black, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_ButtonActive], &darkGray, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_ButtonHovered], &pitchBlack, sizeof(ImVec4));
    memcpy(&style.Colors[ImGuiCol_CheckMark], &paleGray, sizeof(ImVec4));
  }

  // Create a dedicated descriptor heap for imgui
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_ImguiHeap));
  }

  // Setup Platform/Renderer backends
  ImGui_ImplWin32_Init(window);
  ImGui_ImplDX12_Init(
      dev,
      2,
      DXGI_FORMAT_R8G8B8A8_UNORM,
      g_ImguiHeap,
      g_ImguiHeap->GetCPUDescriptorHandleForHeapStart(),
      g_ImguiHeap->GetGPUDescriptorHandleForHeapStart());
}
void deinit()
{
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  if (g_ImguiHeap)
  {
    g_ImguiHeap->Release();
    g_ImguiHeap = nullptr;
  }
}

void beginFrame(uint32_t displayWidth, uint32_t displayHeight, float timeDelta, ID3D12Device* dev)
{
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(float(displayWidth), float(displayHeight));
  io.DeltaTime = timeDelta;

  // Read keyboard modifiers inputs
  io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;

  // Start the Dear ImGui frame
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
}
void endFrame(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
    uint32_t displayWidth,
    uint32_t displayHeight)
{
  renderInternal();
  ImGui::Render();

  // Render Dear ImGui graphics
  const float clear_color_with_alpha[4] = {0.1f, 0.3f, 0.2f, 1.0f};
  // cmdList->ClearRenderTargetView(rtv, clear_color_with_alpha, 0, NULL);
  cmdList->OMSetRenderTargets(1, &rtv, FALSE, NULL);
  cmdList->SetDescriptorHeaps(1, &g_ImguiHeap);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

} // namespace ImGuiHelper