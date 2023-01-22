#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <math.h>

//---------------------------------------------------------------------------//

struct Timer
{
  void init()
  {
    // Query for the performance counter frequency
    LARGE_INTEGER largeInt;
    QueryPerformanceFrequency(&largeInt);
    m_Frequency = largeInt.QuadPart;
    m_FrequencyD = static_cast<double>(m_Frequency);

    // Init the elapsed time
    QueryPerformanceCounter(&largeInt);
    m_StartTime = largeInt.QuadPart;
    m_Elapsed = largeInt.QuadPart - m_StartTime;
    m_ElapsedF = static_cast<float>(m_Elapsed);
    m_ElapsedSeconds = m_Elapsed / m_Frequency;
    m_ElapsedSecondsD = m_Elapsed / m_FrequencyD;
    m_ElapsedSecondsF = static_cast<float>(m_ElapsedSecondsD);
    m_ElapsedMilliseconds = static_cast<int64_t>(m_ElapsedSecondsD * 1000);
    m_ElapsedMillisecondsD = m_ElapsedSecondsD * 1000;
    m_ElapsedMillisecondsF = static_cast<float>(m_ElapsedMillisecondsD);
    m_ElapsedMicroseconds = static_cast<int64_t>(m_ElapsedMillisecondsD * 1000);
    m_ElapsedMicrosecondsD = m_ElapsedMillisecondsD * 1000;
    m_ElapsedMicrosecondsF = static_cast<float>(m_ElapsedMillisecondsD);

    m_Delta = 0;
    m_DeltaF = 0;
    m_DeltaMilliseconds = 0;
    m_DeltaMillisecondsF = 0;
    m_DeltaMicroseconds = 0;
    m_DeltaMicrosecondsF = 0;
  }
  void deinit() {}

  void update()
  {
    LARGE_INTEGER largeInt;
    QueryPerformanceCounter(&largeInt);
    int64_t currentTime = largeInt.QuadPart - m_StartTime;
    m_Delta = currentTime - m_Elapsed;
    m_DeltaF = static_cast<float>(m_DeltaF);
    m_DeltaSeconds = m_Delta / m_Frequency;
    m_DeltaSecondsD = m_Delta / m_FrequencyD;
    m_DeltaSecondsF = static_cast<float>(m_DeltaSecondsD);
    m_DeltaMillisecondsD = m_DeltaSecondsD * 1000;
    m_DeltaMilliseconds = static_cast<int64_t>(m_DeltaMillisecondsD);
    m_DeltaMillisecondsF = static_cast<float>(m_DeltaMillisecondsD);
    m_DeltaMicrosecondsD = m_DeltaMillisecondsD * 1000;
    m_DeltaMicroseconds = static_cast<int64_t>(m_DeltaMicrosecondsD);
    m_DeltaMicrosecondsF = static_cast<float>(m_DeltaMicrosecondsD);

    m_Elapsed = currentTime;
    m_ElapsedF = static_cast<float>(m_Elapsed);
    m_ElapsedSeconds = m_Elapsed / m_Frequency;
    m_ElapsedSecondsD = m_Elapsed / m_FrequencyD;
    m_ElapsedSecondsF = static_cast<float>(m_ElapsedSecondsD);
    m_ElapsedMilliseconds = static_cast<int64_t>(m_ElapsedSecondsD * 1000);
    m_ElapsedMillisecondsD = m_ElapsedSecondsD * 1000;
    m_ElapsedMillisecondsF = static_cast<float>(m_ElapsedMillisecondsD);
    m_ElapsedMicroseconds = static_cast<int64_t>(m_ElapsedMillisecondsD * 1000);
    m_ElapsedMicrosecondsD = m_ElapsedMillisecondsD * 1000;
    m_ElapsedMicrosecondsF = static_cast<float>(m_ElapsedMillisecondsD);
  }

  int64_t m_StartTime;

  int64_t m_Frequency;
  double m_FrequencyD;

  int64_t m_Elapsed;
  int64_t m_Delta;

  float m_ElapsedF;
  float m_DeltaF;

  double m_ElapsedD;
  double m_DeltaD;

  int64_t m_ElapsedSeconds;
  int64_t m_DeltaSeconds;

  float m_ElapsedSecondsF;
  float m_DeltaSecondsF;

  double m_ElapsedSecondsD;
  double m_DeltaSecondsD;

  int64_t m_ElapsedMilliseconds;
  int64_t m_DeltaMilliseconds;

  float m_ElapsedMillisecondsF;
  float m_DeltaMillisecondsF;

  double m_ElapsedMillisecondsD;
  double m_DeltaMillisecondsD;

  int64_t m_ElapsedMicroseconds;
  int64_t m_DeltaMicroseconds;

  float m_ElapsedMicrosecondsF;
  float m_DeltaMicrosecondsF;

  double m_ElapsedMicrosecondsD;
  double m_DeltaMicrosecondsD;
};
//---------------------------------------------------------------------------//
