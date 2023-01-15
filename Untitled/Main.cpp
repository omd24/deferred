#include <Utility.hpp>
#include <thread>
#include "FileWatcher.hpp"
#include "RenderManager.hpp"
#include "D3D12Wrapper.hpp"

RenderManager* g_Renderer;
std::unique_ptr<FileWatcher> g_FileWatcher;

static LRESULT CALLBACK msgProc(HWND p_Wnd, UINT p_Message, WPARAM p_WParam, LPARAM p_LParam)
{
  if (g_ImguiCallback != nullptr)
    g_ImguiCallback(nullptr, p_Wnd, p_Message, p_WParam, p_LParam);

  CallBackRegistery* funcRegPtr =
      reinterpret_cast<CallBackRegistery*>(GetWindowLongPtr(p_Wnd, GWLP_USERDATA));
  switch (p_Message)
  {
  case WM_CREATE: {
    // Save the data passed in to CreateWindow.
    LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(p_LParam);
    SetWindowLongPtr(
        p_Wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
  }
    return 0;

  case WM_KEYDOWN:
    g_Renderer->onKeyDown(static_cast<UINT8>(p_WParam));
    return 0;

  case WM_KEYUP:
    g_Renderer->onKeyUp(static_cast<UINT8>(p_WParam));
    return 0;

  case WM_PAINT: {
    // Shader Reload:
    if (g_FileWatcher)
    {
      FileEvent fileEvent;
      while (g_FileWatcher->getNextChange(fileEvent))
      {
        switch (fileEvent.EventType)
        {
        case FileEvent::Type::Modified:
          // std::this_thread::sleep_for(std::chrono::milliseconds(1));
          g_Renderer->onShaderChange();
          break;
        case FileEvent::Type::Added:
          break;
        case FileEvent::Type::Removed:
          break;
        }
      }
    }

    g_Renderer->onUpdate();
    g_Renderer->onRender();
    return 0;
  }

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(p_Wnd, p_Message, p_WParam, p_LParam);
}
//---------------------------------------------------------------------------//
// Execute the application:
//---------------------------------------------------------------------------//
inline int appExec(HINSTANCE p_Instance, int p_CmdShow, CallBackRegistery* p_CallbackReg)
{
  // Parse the command line parameters
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  g_Renderer->parseCmdArgs(argv, argc);
  LocalFree(argv);

  // Initialize the window class.
  WNDCLASSEX windowClass = {0};
  windowClass.cbSize = sizeof(WNDCLASSEX);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = msgProc;
  windowClass.hInstance = p_Instance;
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.lpszClassName = L"DXSampleClass";
  if (RegisterClassEx(&windowClass) == 0)
  {
    WIN32_MSG_BOX("RegisterClassEx() failed");
    return 0;
  }

  RECT windowRect = {
      0,
      0,
      static_cast<LONG>(g_Renderer->m_Info.m_Width),
      static_cast<LONG>(g_Renderer->m_Info.m_Height)};
  AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

  // Create the window and store a handle to it.
  g_WinHandle = CreateWindow(
      windowClass.lpszClassName,
      g_Renderer->m_Info.m_Title.c_str(),
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      windowRect.right - windowRect.left,
      windowRect.bottom - windowRect.top,
      nullptr, // We have no parent window.
      nullptr, // We aren't using menus.
      p_Instance,
      p_CallbackReg);

  if (g_WinHandle == nullptr)
  {
    WIN32_MSG_BOX("CreateWindowEx() failed");
    return 0;
  }

  g_Renderer->onLoad();

  ShowWindow(g_WinHandle, p_CmdShow);

  // Main sample loop.
  MSG msg = {};
  while (msg.message != WM_QUIT)
  {
    // Process any messages in the queue.
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  //// Deinit upload system
  // shutdownUpload();

  g_Renderer->onDestroy();

  // Return this part of the WM_QUIT message to Windows.
  return static_cast<char>(msg.wParam);
}
//---------------------------------------------------------------------------//

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE p_Instance, HINSTANCE, LPSTR, int p_CmdShow)
{
  // Set up the renderer
  void* rendererMem = reinterpret_cast<void*>(::malloc(sizeof(*g_Renderer)));
  DEFER(free_renderer_mem)
  {
    g_Renderer->~RenderManager();
    //::free(rendererMem);
  };
  g_Renderer = new (rendererMem) RenderManager;
  g_Renderer->OnInit(1280, 720, L"D3D12 Draw Untitled");

  // Get shader path:
  UINT pathSize = 512;
  WCHAR* shadersPath = static_cast<WCHAR*>(::calloc(pathSize, sizeof(WCHAR)));
  getShadersPath(shadersPath, pathSize);
  const std::wstring& p_WideStr = std::wstring(shadersPath);
  std::string shaderPathStr = WideStrToStr(p_WideStr);

  // Start the file watcher:
  g_FileWatcher = std::make_unique<FileWatcher>();
  g_FileWatcher->startWatching(shaderPathStr.c_str(), true);

  // Run the application:
  int ret = appExec(p_Instance, p_CmdShow, nullptr);

  ::free(shadersPath);
  return ret;
}
