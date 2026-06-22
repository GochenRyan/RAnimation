#include <fmt/base.h>
#include <fmt/color.h>

#include <Tools/Timer.h>

void Timer::Start()
{
    if (mRunning)
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: timer already running\n", __FUNCTION__);
        return;
    }

    mRunning = true;
    mStartTime = std::chrono::steady_clock::now();
}

float Timer::Stop()
{
    if (!mRunning)
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: timer not running\n", __FUNCTION__);
        return 0;
    }
    mRunning = false;

    auto stopTime = std::chrono::steady_clock::now();
    float timerMilliSeconds = std::chrono::duration_cast<std::chrono::microseconds>(stopTime - mStartTime).count() /
                              1000.0f;

    return timerMilliSeconds;
}
