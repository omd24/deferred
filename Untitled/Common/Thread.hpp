#pragma once

#include "Utility.hpp"

struct Thread
{
  using ThreadFunction = DWORD(__stdcall*)(void*);

  Thread()
  {
  }
  ~Thread()
  {
    stopThread();
  }

  bool isRunning() const
  {
    return m_Handle != nullptr;
  }

  bool runThread(ThreadFunction p_Function, void* p_Args)
  {
    if (m_Handle)
    {
      return false;
    }
    m_Handle =
        CreateThread(nullptr, 0, p_Function, p_Args, 0, (DWORD*)&m_ThreadId);
    if (m_Handle == nullptr)
    {
      // auto error = GetLastError();
      DebugBreak();
      return false;
    }
    return true;
  }
  void stopThread()
  {
    if (!m_Handle)
    {
      return;
    }
    WaitForSingleObject((HANDLE)m_Handle, INFINITE);
    if (CloseHandle((HANDLE)m_Handle) == 0)
    {
      // auto error = GetLastError();
      DebugBreak();
    }
    m_Handle = nullptr;
  }

private:
  static unsigned int m_MainThread;
  uint32_t m_ThreadId = 0;
  void* m_Handle = nullptr;
};
