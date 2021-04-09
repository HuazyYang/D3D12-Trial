#pragma once
#include <chrono>

class GameTimer {
public:
  GameTimer() {
    Reset();
  }

  void Reset() {
    start_stamp_ = clock_.now();
    parsed_stamp_ = start_stamp_;
    parsed_ = false;
  }
  void Stop() {
    if(!parsed_) {
      parsed_stamp_ = clock_.now();
      parsed_ = true;
    }
  }
  void Resume() {
    if(parsed_) {
      auto stamp = clock_.now();
      if(stamp < parsed_stamp_)
        stamp = parsed_stamp_;

      start_stamp_ += stamp - parsed_stamp_;
      parsed_ = false;
    }
  }
  void Tick() {
    if(!parsed_) {
      prev_stamp_ = clock_.now();
    }
  }
  double DeltaElasped() const {
    if(!parsed_) {
      std::chrono::duration<double> tick = clock_.now() - prev_stamp_;
      return tick.count();
    } else return 0.0;
  }

  double TotalElapsed() {
    std::chrono::duration<double> tick;
    if(!parsed_)
      tick = clock_.now() - start_stamp_;
    else
      tick = parsed_stamp_ - start_stamp_;
    return tick.count();
  }

public:
  std::chrono::steady_clock clock_;
  std::chrono::steady_clock::time_point start_stamp_;
  std::chrono::steady_clock::time_point parsed_stamp_;
  std::chrono::steady_clock::time_point prev_stamp_;
  bool parsed_ = false;
};