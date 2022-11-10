#include "ImguiHelper.hpp"
#include "D3D12Wrapper.hpp"

#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"

namespace ImGuiHelper
{

static ID3D12DescriptorHeap* g_ImguiHeap = nullptr;

static void WindowMessageCallback(
    void* context, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

  io.ImeWindowHandle = window;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsLight();

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

void beginFrame(
    uint32_t displayWidth,
    uint32_t displayHeight,
    float timeDelta,
    ID3D12Device* dev)
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
  // Rendering
  {
    static float f = 0.0f;
    static int counter = 0;
    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and
                                   // append into it.

    ImGui::Text("This is some useful text."); // Display some text (you can use
                                              // a format strings too)
    ImGui::Checkbox(
        "Demo Window",
        &show_demo_window); // Edit bools storing our window open/close state
    ImGui::Checkbox("Another Window", &show_another_window);

    ImGui::SliderFloat(
        "float",
        &f,
        0.0f,
        1.0f); // Edit 1 float using a slider from 0.0f to 1.0f

    if (ImGui::Button("Button")) // Buttons return true when clicked (most
                                 // widgets return true when edited/activated)
      counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);

    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::End();
  }

  ImGui::Render();

  // Render Dear ImGui graphics
  const float clear_color_with_alpha[4] = {0.1f, 0.3f, 0.2f, 1.0f};
  // cmdList->ClearRenderTargetView(rtv, clear_color_with_alpha, 0, NULL);
  cmdList->OMSetRenderTargets(1, &rtv, FALSE, NULL);
  cmdList->SetDescriptorHeaps(1, &g_ImguiHeap);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

} // namespace ImGuiHelper