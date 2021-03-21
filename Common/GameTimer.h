#pragma once

class GameTimer {
public:
  GameTimer();
  void Reset();
  void Start();
  void Stop();
  void Tick();
  float TotalTime() const;
  float DeltaTime() const;

private:
  double mSecondsPerCount;
  double mDeltaTime;  // Cache delata time for query.

  __int64 mBaseTime;
  __int64 mPausedTime;
  __int64 mCurrTime;

  bool mPaused;
};