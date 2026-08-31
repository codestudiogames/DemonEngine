#pragma once
// ==============================================================================
//  DemonEngine::Timer
//  High-resolution frame timer using std::chrono.
// ==============================================================================
#include <chrono>

namespace Demon {

class Timer {
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

public:
    Timer() { reset(); }

    void reset() { m_start = Clock::now(); }

    [[nodiscard]] float elapsedSeconds() const {
        return std::chrono::duration<float>(Clock::now() - m_start).count();
    }
    [[nodiscard]] float elapsedMillis() const { return elapsedSeconds() * 1000.f; }
    [[nodiscard]] float elapsedMicros() const { return elapsedSeconds() * 1'000'000.f; }

private:
    TimePoint m_start;
};

} // namespace Demon
