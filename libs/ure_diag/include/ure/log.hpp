#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <source_location>
#include <string_view>

namespace ure::log {

// ── Log level ──
enum class Level : uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5
};

constexpr std::string_view level_str(Level lvl) noexcept {
    switch (lvl) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
    }
    return "?????";
}

// ── Module tag (extensible) ──
enum class Tag : uint8_t {
    None = 0,
    CLI,
    GPU,
    SceneIO,
    Config,
    Core,
    Physics,   // reserved — not migrated in Phase Dx
    Acoustic,  // reserved — not migrated in Phase Dx
    Test
};

constexpr std::string_view tag_str(Tag t) noexcept {
    switch (t) {
        case Tag::None:    return "";
        case Tag::CLI:     return "CLI";
        case Tag::GPU:     return "GPU";
        case Tag::SceneIO: return "SceneIO";
        case Tag::Config:  return "Config";
        case Tag::Core:    return "Core";
        case Tag::Physics: return "Physics";
        case Tag::Acoustic:return "Acoustic";
        case Tag::Test:    return "Test";
    }
    return "?";
}

// ── Sink interface (forward decl, defined in log_sink.hpp) ──
class Sink;

// ── Global state ──
void set_min_level(Level lvl) noexcept;
Level min_level() noexcept;

void set_sink(Sink* sink) noexcept;
Sink* sink() noexcept;

// ── Internal implementation ──
void log_impl(Level level, Tag tag, std::string&& message,
              const std::source_location& loc = std::source_location::current());

// ── Convenience: flush all sinks ──
void flush();

} // namespace ure::log

// ── Macros ──
// Compile-time filter: if constexpr eliminates code entirely when level < UR_LOG_LEVEL
// Runtime filter: checks min_level()

#ifndef UR_LOG_LEVEL
#  ifdef NDEBUG
#    define UR_LOG_LEVEL 2
#  else
#    define UR_LOG_LEVEL 0
#  endif
#endif

#define UR_LOG_TRACE(tag, ...)  UR_LOG_IF(Trace, tag, __VA_ARGS__)
#define UR_LOG_DEBUG(tag, ...)  UR_LOG_IF(Debug, tag, __VA_ARGS__)
#define UR_LOG_INFO(tag, ...)   UR_LOG_IF(Info,  tag, __VA_ARGS__)
#define UR_LOG_WARN(tag, ...)   UR_LOG_IF(Warn,  tag, __VA_ARGS__)
#define UR_LOG_ERROR(tag, ...)  UR_LOG_IF(Error, tag, __VA_ARGS__)
#define UR_LOG_FATAL(tag, ...)  UR_LOG_IF(Fatal, tag, __VA_ARGS__)

#define UR_LOG_IF(level, tag, ...)                                                         \
    do {                                                                                    \
        if constexpr (static_cast<int>(ure::log::Level::level) >= UR_LOG_LEVEL) {           \
            if (static_cast<int>(ure::log::Level::level) >=                                 \
                static_cast<int>(ure::log::min_level())) {                                  \
                ure::log::log_impl(                                                         \
                    ure::log::Level::level,                                                 \
                    ure::log::Tag::tag,                                                     \
                    std::format(__VA_ARGS__),                                               \
                    std::source_location::current());                                       \
            }                                                                               \
        }                                                                                   \
    } while (0)
