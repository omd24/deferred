#include <Utility.hpp>
#include <thread>
#include "Renderer.hpp"

Renderer* g_Renderer;

static LRESULT CALLBACK
msgProc(HWND p_Wnd, UINT p_Message, WPARAM p_WParam, LPARAM p_LParam)
{
  CallBackRegistery* funcRegPtr = reinterpret_cast<CallBackRegistery*>(
      GetWindowLongPtr(p_Wnd, GWLP_USERDATA));
  switch (p_Message)
  {
  case WM_CREATE:
  {
    // Save the data passed in to CreateWindow.
    LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(p_LParam);
    SetWindowLongPtr(
        p_Wnd,
        GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
  }
    return 0;

  case WM_KEYDOWN:
    g_Renderer->onKeyDown(static_cast<UINT8>(p_WParam));
    return 0;

  case WM_KEYUP:
    g_Renderer->onKeyUp(static_cast<UINT8>(p_WParam));
    return 0;

  case WM_PAINT:
    g_Renderer->onUpdate();
    g_Renderer->onRender();
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(p_Wnd, p_Message, p_WParam, p_LParam);
}
//---------------------------------------------------------------------------//
// Execute the application:
//---------------------------------------------------------------------------//
inline int
appExec(HINSTANCE p_Instance, int p_CmdShow, CallBackRegistery* p_CallbackReg)
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

// Hot-Reloading Thread:
#if 0
  std::thread([=] {
    // Get current path
    UINT pathSize = 512;
    WCHAR* shadersPath = static_cast<WCHAR*>(::calloc(pathSize, sizeof(WCHAR)));
    DEFER(free_path_mem)
    {
      ::free(shadersPath);
    };
    getShadersPath(shadersPath, pathSize);

    // Create path handle
    HANDLE dirHandle = CreateFile(
        shadersPath,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
    // Create other file-watching required stuff
    FILE_NOTIFY_INFORMATION fileNotifInfo[1024];
    DWORD bytesReturned = 0;
    OVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    // Create event on the overlapped object
    overlapped.hEvent = CreateEvent(NULL, true, false, NULL);

    // Bind overlapped event to directory changes
    ReadDirectoryChangesW(
        dirHandle,
        fileNotifInfo,
        sizeof(fileNotifInfo),
        false,
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        &bytesReturned,
        &overlapped,
        NULL);

    // Start the directory changes reading loop
    MSG msg = {};
    bool quit = false;
    DWORD waitStatus;

    while (!quit)
    {
      // Wait for directory changes
      waitStatus = WaitForSingleObject(overlapped.hEvent, 0);

      switch (waitStatus)
      {
      case WAIT_OBJECT_0:
      {
        GetOverlappedResult(dirHandle, &overlapped, &bytesReturned, false);

        std::wstring fileName(fileNotifInfo->FileName, fileNotifInfo->FileNameLength);
        // Only react to hlsl shaders :shrug:
        if (fileName.find(L".hlsl") != std::wstring::npos)
          g_Renderer->onShaderChange();

        // Reset the overlapped event and wait for directory changes again
        ResetEvent(overlapped.hEvent);

        ReadDirectoryChangesW(
            dirHandle,
            fileNotifInfo,
            sizeof(fileNotifInfo),
            false,
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytesReturned,
            &overlapped,
            NULL);
      }
      break;
      case WAIT_TIMEOUT:
        // Check if it there is a quit message
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
          if (msg.message == WM_QUIT)
          {
            quit = true;
            break;
          }
        }
        break;
      }
    }
  }).detach();
#endif

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

  g_Renderer->onDestroy();

  // Return this part of the WM_QUIT message to Windows.
  return static_cast<char>(msg.wParam);
}
//---------------------------------------------------------------------------//

_Use_decl_annotations_ int WINAPI
WinMain(HINSTANCE p_Instance, HINSTANCE, LPSTR, int p_CmdShow)
{
  // Set up the renderer
  void* rendererMem = reinterpret_cast<void*>(::malloc(sizeof(*g_Renderer)));
  DEFER(free_renderer_mem)
  {
    g_Renderer->~Renderer();
    ::free(rendererMem);
  };
  g_Renderer = new (rendererMem) Renderer;
  g_Renderer->OnInit(1280, 720, L"D3D12 Draw Traingle");

  // Run the application:
  int ret = appExec(p_Instance, p_CmdShow, nullptr);

  return ret;
}
