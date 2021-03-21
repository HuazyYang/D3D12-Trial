#include "GameTimer.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

//
// GameTimer implementation
//
GameTimer::GameTimer()
  : mSecondsPerCount(0), mBaseTime(0), mPausedTime(0), mCurrTime(0),
  mPaused(false) {
  LARGE_INTEGER liPerfFreq;
  QueryPerformanceFrequency(&liPerfFreq);

  mSecondsPerCount = 1.0 / liPerfFreq.QuadPart;
}

void GameTimer::Reset() {
  QueryPerformanceCounter((PLARGE_INTEGER)&mBaseTime);
  mCurrTime = mBaseTime;
  mDeltaTime = 0.0;

  mPausedTime = 0;
  mPaused = false;
}

void GameTimer::Start() {

  if (mPaused) {
    LARGE_INTEGER startTime;
    QueryPerformanceCounter(&startTime);

    mPausedTime += (startTime.QuadPart - mCurrTime);
    mCurrTime = startTime.QuadPart;

    mPaused = false;
  }
}

void GameTimer::Stop() {

  if (!mPaused) {
    LARGE_INTEGER liNow;
    QueryPerformanceCounter(&liNow);

    mDeltaTime = (liNow.QuadPart - mCurrTime) * mSecondsPerCount;
    mCurrTime = liNow.QuadPart;

    mPaused = true;
  }
}

void GameTimer::Tick() {

  if (!mPaused) {
    LARGE_INTEGER liNow;

    QueryPerformanceCounter(&liNow);

    mDeltaTime = (liNow.QuadPart - mCurrTime) * mSecondsPerCount;
    mCurrTime = liNow.QuadPart;

    // Force nonnegative.  The DXSDK's CDXUTTimer mentions that if the 
    // processor goes into a power save mode or we get shuffled to another
    // processor, then mDeltaTime can be negative.
    if (mDeltaTime < 0.0) {
      mDeltaTime = 0.0;
    }
  }
}

float GameTimer::TotalTime() const {
  return static_cast<float>((mCurrTime - mBaseTime - mPausedTime) * mSecondsPerCount);
}

float GameTimer::DeltaTime() const {
  return (float)mDeltaTime;
}