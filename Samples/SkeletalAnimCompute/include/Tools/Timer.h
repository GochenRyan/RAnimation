/* Timer class */
#pragma once

#include <chrono>

class Timer {
  public:
    void Start();
    /* stops timer and returns millisconds since start, in microsecond resolution */
    float Stop();

  private:
    bool mRunning = false;
    std::chrono::time_point<std::chrono::steady_clock> mStartTime{};
};
