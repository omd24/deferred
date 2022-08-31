#pragma once
#include <Utility.hpp>
#include "Thread.hpp"

struct FileEvent
{
  enum class Type
  {
    Modified,
    Removed,
    Added,
  };
  Type EventType;
  std::string Path;
  LARGE_INTEGER Time;
};

class FileWatcher
{
public:
  FileWatcher();
  ~FileWatcher();

  bool startWatching(const char* p_Path, const bool p_RecursiveWatch = true);
  bool getNextChange(FileEvent& p_FileEvent);

private:
  struct DirectoryWatch
  {
    ~DirectoryWatch();
    bool m_IsWatching = false;
    bool m_Recursive;
    HANDLE m_FileHandle;
    OVERLAPPED m_Overlapped{};
    std::deque<FileEvent> m_Changes;
    std::array<char, 1 << 16> m_Buffer{};
    std::string m_DirectoryPath;
  };

  int threadFunction();

  HANDLE m_IOCP = nullptr;
  bool m_Exiting = false;
  std::mutex m_Mutex;
  LARGE_INTEGER m_TimeFrequency{};
  Thread m_Thread;
  std::vector<std::unique_ptr<DirectoryWatch>> m_Watches;
};
