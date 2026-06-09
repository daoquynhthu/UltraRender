#pragma once

#include "ure/log.hpp"

#include <chrono>
#include <format>

namespace ure::diag {

// ════════════════════════════════════════════════════════════════════
// ScopedTimer — RAII, logs elapsed time on destruction
// ════════════════════════════════════════════════════════════════════
class ScopedTimer {
    const char* name_;
    ure::log::Tag tag_;
    std::chrono::high_resolution_clock::time_point start_;
public:
    ScopedTimer(const char* name, ure::log::Tag tag = ure::log::Tag::None)
        : name_(name), tag_(tag),
          start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - start_).count();
        UR_LOG_INFO(tag_, "{} completed in {:.1f} ms", name_, elapsed);
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

// ════════════════════════════════════════════════════════════════════
// ManualTimer — start/stop/reset, for interval or cumulative timing
// ════════════════════════════════════════════════════════════════════
class ManualTimer {
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point start_;
    Clock::time_point last_lap_;
public:
    ManualTimer() { reset(); }

    void reset() {
        auto now = Clock::now();
        start_ = now;
        last_lap_ = now;
    }

    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(
            Clock::now() - start_).count();
    }

    double lap_ms() {
        auto now = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - last_lap_).count();
        last_lap_ = now;
        return ms;
    }
};

} // namespace ure::diag

#define UR_SCOPE_TIMER(tag) \
    ure::diag::ScopedTimer _ure_timer_##__LINE__(__FUNCTION__, tag)
