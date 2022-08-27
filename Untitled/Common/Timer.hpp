#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <math.h>

static constexpr UINT64 TicksPerSecond = 10'000'000;
//---------------------------------------------------------------------------//
struct Timer
{
  // Source timing data, acquired in Timer initializazion:
  // (in QPC units aka [cpu cycle] counts per seconds)
  LARGE_INTEGER m_QpcFrequency;
  LARGE_INTEGER m_QpcLastTime;
  UINT64 m_QpcMaxDelta;

  // Derived timing data (in standard tick units):
  UINT64 m_ElapsedTicks;
  UINT64 m_TotalTicks;
  UINT64 m_LeftOverTicks;

  // Tracking the framerate:
  UINT32 m_FrameCount;
  UINT32 m_FramesPerSecond;
  UINT32 m_FramesThisSecond;
  UINT64 m_QpcSecondCounter; // deltaTime in terms of counts per sec aka Qpc

  // Configuring fixed timestep mode:
  bool m_IsFixedTimeStep;
  UINT64 m_TargetElapsedTicks;
};
//---------------------------------------------------------------------------//
inline void timerInit(Timer* p_Timer)
{
  memset(p_Timer, 0, sizeof(*p_Timer));
  p_Timer->m_FramesThisSecond = false;
  p_Timer->m_TargetElapsedTicks = TicksPerSecond / 60;

  QueryPerformanceFrequency(&p_Timer->m_QpcFrequency);
  QueryPerformanceCounter(&p_Timer->m_QpcLastTime);

  // Initialize max delta to 1/10 of counts per "one second"
  p_Timer->m_QpcMaxDelta = p_Timer->m_QpcFrequency.QuadPart / 10;
}
//---------------------------------------------------------------------------//
static double ticksToSeconds(UINT64 p_Ticks)
{
  return static_cast<double>(p_Ticks) / TicksPerSecond;
}
//---------------------------------------------------------------------------//
static UINT64 secondsToTicks(double p_Seconds)
{
  return static_cast<UINT64>(p_Seconds * TicksPerSecond);
}
//---------------------------------------------------------------------------//
inline double timerGetElapsedSeconds(Timer* p_Timer)
{
  return ticksToSeconds(p_Timer->m_ElapsedTicks);
}
//---------------------------------------------------------------------------//
inline double timerGetTotalSeconds(Timer* p_Timer)
{
  return ticksToSeconds(p_Timer->m_TotalTicks);
}
//---------------------------------------------------------------------------//
inline void timerSetTargetElapsedSeconds(Timer* p_Timer, double p_TargetElapsed)
{
  p_Timer->m_TargetElapsedTicks = secondsToTicks(p_TargetElapsed);
}
//---------------------------------------------------------------------------//
// Call this after an intentional timing discontinuity, e.g., a blocking IO op,
// to avoid having the fixed timestep logic attempt catch-up update calls
inline void timerResetElapsedTime(Timer* p_Timer)
{
  QueryPerformanceCounter(&p_Timer->m_QpcLastTime);

  p_Timer->m_LeftOverTicks = 0;
  p_Timer->m_FramesPerSecond = 0;
  p_Timer->m_FramesThisSecond = 0;
  p_Timer->m_QpcSecondCounter = 0;
}
//---------------------------------------------------------------------------//
typedef void (*updateFuncCallback)(void);
// Update Timer state, calling the specified update callback if needed
inline void timerTick(Timer* p_Timer, updateFuncCallback p_Update = nullptr)
{
  LARGE_INTEGER currentTime;
  QueryPerformanceCounter(&currentTime);

  UINT64 timeDelta = currentTime.QuadPart - p_Timer->m_QpcLastTime.QuadPart;

  p_Timer->m_QpcLastTime = currentTime;
  p_Timer->m_QpcSecondCounter += timeDelta;

  // Clamp excessively large time deltas (e.g. after paused in the debugger)
  if (timeDelta > p_Timer->m_QpcMaxDelta)
    timeDelta = p_Timer->m_QpcMaxDelta;

  // Convert QPC units into a standard tick format
  // (This cannot overflow due to the previous clamp)
  timeDelta *= TicksPerSecond;
  timeDelta /= p_Timer->m_QpcFrequency.QuadPart;

  UINT32 lastFrameCount = p_Timer->m_FrameCount;

  if (p_Timer->m_IsFixedTimeStep)
  {
    // Fixed timestep update logic:
    //
    // If the app is running very close to the
    // "target elapsed time" i.e., within 1/4000 of a second,
    // just clamp the delatTime value to exactly match the target value.
    // (This prevents tiny errors from accumulating over time)
    if (abs(static_cast<int>(timeDelta - p_Timer->m_TargetElapsedTicks)) <
        TicksPerSecond / 4000)
      timeDelta = p_Timer->m_TargetElapsedTicks;

    p_Timer->m_LeftOverTicks += timeDelta;

    while (p_Timer->m_LeftOverTicks >= p_Timer->m_TargetElapsedTicks)
    {
      p_Timer->m_ElapsedTicks = p_Timer->m_TargetElapsedTicks;
      p_Timer->m_TotalTicks += p_Timer->m_TargetElapsedTicks;
      p_Timer->m_LeftOverTicks -= p_Timer->m_TargetElapsedTicks;
      p_Timer->m_FrameCount++;

      if (p_Update)
        p_Update();
    }
  }
  else
  {
    // Variable timestep update logic:
    p_Timer->m_ElapsedTicks = timeDelta;
    p_Timer->m_TotalTicks += timeDelta;
    p_Timer->m_LeftOverTicks = 0;
    p_Timer->m_FrameCount++;

    if (p_Update)
      p_Update();
  }

  // Track current framerate:
  if (p_Timer->m_FrameCount != lastFrameCount)
    p_Timer->m_FramesThisSecond++;

  // update frame data, if deltaTime in Qpc aka count per sec is greater than
  // the source counts per "one second" (acquired in Timer initialitzaion),
  if (p_Timer->m_QpcSecondCounter >=
      static_cast<UINT64>(p_Timer->m_QpcFrequency.QuadPart))
  {
    p_Timer->m_FramesPerSecond = p_Timer->m_FramesThisSecond;
    p_Timer->m_FramesThisSecond = 0;
    p_Timer->m_QpcSecondCounter %= p_Timer->m_QpcFrequency.QuadPart;
  }
}
//---------------------------------------------------------------------------//
