#include "FileWatcher.hpp"

std::mutex g_FilewatcherMutex;


FileWatcher::FileWatcher()
{
}

FileWatcher::~FileWatcher()
{
  m_Exiting = true;
  m_Watches.clear();
  if (m_IOCP)
  {
    PostQueuedCompletionStatus(m_IOCP, 0, (ULONG_PTR)this, 0);
    CloseHandle(m_IOCP);
  }
}

bool FileWatcher::startWatching(
    const char* pPath, const bool recursiveWatch /*= true*/)
{
  QueryPerformanceFrequency(&m_TimeFrequency);

  HANDLE fileHandle = CreateFileA(
      pPath,
      FILE_LIST_DIRECTORY,
      FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
      nullptr);

  if (!fileHandle)
  {
    return false;
  }

  std::scoped_lock<std::mutex> lock(g_FilewatcherMutex);
  std::unique_ptr<DirectoryWatch> pWatch = std::make_unique<DirectoryWatch>();
  pWatch->m_Recursive = recursiveWatch;
  pWatch->m_FileHandle = fileHandle;
  pWatch->m_DirectoryPath = pPath;
  m_IOCP =
      CreateIoCompletionPort(fileHandle, m_IOCP, (ULONG_PTR)pWatch.get(), 0);
  DEBUG_BREAK(m_IOCP);

  m_Watches.push_back(std::move(pWatch));

  if (!m_Thread.isRunning())
  {
    m_Thread.runThread(
        [](void* pArgs) {
          FileWatcher* pWatcher = (FileWatcher*)pArgs;
          return (DWORD)pWatcher->threadFunction();
        },
        this);
  }

  DEBUG_BREAK(
      PostQueuedCompletionStatus(m_IOCP, 0, (ULONG_PTR)this, 0) == TRUE);
  return true;
}

bool FileWatcher::getNextChange(FileEvent& fileEvent)
{
  std::scoped_lock<std::mutex> lock(g_FilewatcherMutex);
  for (auto& pWatch : m_Watches)
  {
    if (pWatch->m_Changes.size() > 0)
    {
      fileEvent = pWatch->m_Changes[0];
      LARGE_INTEGER currentTime;
      QueryPerformanceCounter(&currentTime);
      float timeDiff = ((float)currentTime.QuadPart - fileEvent.Time.QuadPart) /
                       m_TimeFrequency.QuadPart;
      if (timeDiff > 0.2f)
      {
        pWatch->m_Changes.pop_front();
        return true;
      }
    }
  }
  return false;
}

int FileWatcher::threadFunction()
{
  const uint32_t fileNotifyFlags =
      FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
      FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_FILE_NAME;

  while (!m_Exiting)
  {
    {
      std::scoped_lock<std::mutex> lock(g_FilewatcherMutex);

      for (auto& pWatch : m_Watches)
      {
        if (pWatch && !pWatch->m_IsWatching)
        {
          DWORD bytesFilled = 0;
          ReadDirectoryChangesW(
              pWatch->m_FileHandle,
              pWatch->m_Buffer.data(),
              (DWORD)pWatch->m_Buffer.size(),
              pWatch->m_Recursive,
              fileNotifyFlags,
              &bytesFilled,
              &pWatch->m_Overlapped,
              nullptr);
          pWatch->m_IsWatching = true;
        }
      }
    }

    DWORD numBytes;
    OVERLAPPED* ov;
    ULONG_PTR key;
    while (GetQueuedCompletionStatus(m_IOCP, &numBytes, &key, &ov, INFINITE))
    {
      if ((void*)key == this && numBytes == 0)
      {
        break;
      }

      if (numBytes == 0)
      {
        continue;
      }

      std::scoped_lock<std::mutex> lock(g_FilewatcherMutex);
      DirectoryWatch* pWatch = (DirectoryWatch*)key;

      unsigned offset = 0;
      char outString[MAX_PATH];
      while (offset < numBytes)
      {
        FILE_NOTIFY_INFORMATION* pRecord =
            (FILE_NOTIFY_INFORMATION*)&pWatch->m_Buffer[offset];

        int length = WideCharToMultiByte(
            CP_ACP,
            0,
            pRecord->FileName,
            pRecord->FileNameLength / sizeof(wchar_t),
            outString,
            MAX_PATH - 1,
            nullptr,
            nullptr);

        outString[length] = '\0';

        UINT pathSize = 512;
        WCHAR* shadersPath =
            static_cast<WCHAR*>(::calloc(pathSize, sizeof(WCHAR)));
        DEFER(free_path_mem)
        {
          ::free(shadersPath);
        };
        getShadersPath(shadersPath, pathSize);
        std::wstring ws(shadersPath);
        std::string path(ws.begin(), ws.end());
        FileEvent newEvent;
        newEvent.Path = path;

        switch (pRecord->Action)
        {
        case FILE_ACTION_MODIFIED:
          newEvent.EventType = FileEvent::Type::Modified;
          break;
        case FILE_ACTION_REMOVED:
          newEvent.EventType = FileEvent::Type::Removed;
          break;
        case FILE_ACTION_ADDED:
          newEvent.EventType = FileEvent::Type::Added;
          break;
        case FILE_ACTION_RENAMED_NEW_NAME:
          newEvent.EventType = FileEvent::Type::Added;
          break;
        case FILE_ACTION_RENAMED_OLD_NAME:
          newEvent.EventType = FileEvent::Type::Removed;
          break;
        }

        QueryPerformanceCounter(&newEvent.Time);

        bool add = true;

        // Some events are duplicates
        if (pWatch->m_Changes.size() > 0)
        {
          const FileEvent& prevEvent = pWatch->m_Changes.front();
          add = prevEvent.Path != newEvent.Path ||
                prevEvent.EventType != newEvent.EventType;
        }

        if (add)
        {
          pWatch->m_Changes.push_back(newEvent);
        }

        if (!pRecord->NextEntryOffset)
        {
          break;
        }
        else
        {
          offset += pRecord->NextEntryOffset;
        }
      }

      DWORD bytesFilled = 0;
      ReadDirectoryChangesW(
          pWatch->m_FileHandle,
          pWatch->m_Buffer.data(),
          (DWORD)pWatch->m_Buffer.size(),
          pWatch->m_Recursive,
          fileNotifyFlags,
          &bytesFilled,
          &pWatch->m_Overlapped,
          nullptr);
    }
  }

  return 0;
}

FileWatcher::DirectoryWatch::~DirectoryWatch()
{
  if (m_FileHandle)
  {
    CancelIo(m_FileHandle);
    CloseHandle(m_FileHandle);
    m_FileHandle = nullptr;
  }
}
