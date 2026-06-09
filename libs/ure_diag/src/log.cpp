#include "ure/log.hpp"
#include "ure/log_sink.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace ure::log {

namespace {

std::atomic<int> g_min_level{static_cast<int>(Level::Info)};
Sink* g_sink = nullptr;
std::mutex g_mutex;

// Default sink: created on first use
Sink* default_sink() {
    static ConsoleSink s_console;
    return &s_console;
}

} // anonymous namespace

void set_min_level(Level lvl) noexcept {
    g_min_level.store(static_cast<int>(lvl));
}

Level min_level() noexcept {
    return static_cast<Level>(g_min_level.load());
}

void set_sink(Sink* sink) noexcept {
    std::lock_guard lock(g_mutex);
    g_sink = sink;
}

Sink* sink() noexcept {
    Sink* s = g_sink;
    return s ? s : default_sink();
}

void log_impl(Level level, Tag tag, std::string&& message,
              const std::source_location& loc) {
    std::lock_guard lock(g_mutex);
    std::string ts = current_timestamp();
    sink()->write(level, tag, loc.file_name(), loc.line(),
                  loc.function_name(), message, ts.c_str());
    if (level == Level::Fatal) {
        sink()->flush();
        std::abort();
    }
}

void flush() {
    std::lock_guard lock(g_mutex);
    sink()->flush();
}

} // namespace ure::log
