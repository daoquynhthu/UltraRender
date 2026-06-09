#pragma once

#include "ure/log.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef _WIN32
#  include <unistd.h>
#else
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace ure::log {

// ════════════════════════════════════════════════════════════════════
// Sink — abstract output target
// ════════════════════════════════════════════════════════════════════
class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(Level level, Tag tag,
                       const char* file, int line, const char* func,
                       std::string_view message,
                       const char* timestamp) = 0;
    virtual void flush() {}
};

// ════════════════════════════════════════════════════════════════════
// timestamp helper (used by all sinks)
// ════════════════════════════════════════════════════════════════════
inline std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()) % 1000;
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec,
                       static_cast<int>(ms.count()));
}

// ════════════════════════════════════════════════════════════════════
// ConsoleSink — stderr with color (ANSI / Windows)
// ════════════════════════════════════════════════════════════════════
inline const char* ansi_color(Level lvl) {
    switch (lvl) {
        case Level::Trace: return "\x1b[90m";      // bright black (gray)
        case Level::Debug: return "\x1b[36m";      // cyan
        case Level::Info:  return "\x1b[0m";        // default
        case Level::Warn:  return "\x1b[33m";      // yellow
        case Level::Error: return "\x1b[31m";      // red
        case Level::Fatal: return "\x1b[41;97m";   // white on red
    }
    return "\x1b[0m";
}

inline constexpr const char* ansi_reset = "\x1b[0m";

class ConsoleSink : public Sink {
    bool ansi_supported_ = false;
public:
    ConsoleSink() {
#ifdef _WIN32
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(h, &mode)) {
                // Enable virtual terminal processing for ANSI escape codes
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
                ansi_supported_ = true;
            }
        }
#else
        ansi_supported_ = isatty(fileno(stderr));
#endif
    }

    void write(Level level, Tag tag,
               const char* file, int line, const char* func,
               std::string_view message,
               const char* timestamp) override
    {
        std::string tag_part;
        if (tag != Tag::None) {
            tag_part = std::format("[{}]", ure::log::tag_str(tag));
        }

        if (ansi_supported_) {
            std::fprintf(stderr, "%s[%s][%s]%s %s%s %s(%s:%d)%s\n",
                         ansi_color(level),
                         timestamp, level_str(level).data(), tag_part.c_str(),
                         message.data(),
                         ansi_reset,
                         func, file, line,
                         ansi_reset);
        } else {
            std::fprintf(stderr, "[%s][%s]%s %s (%s:%d)\n",
                         timestamp, level_str(level).data(), tag_part.c_str(),
                         message.data(),
                         func, line);
        }
    }

    void flush() override {
        std::fflush(stderr);
    }
};

// ════════════════════════════════════════════════════════════════════
// FileSink — rotating file output (no color)
// ════════════════════════════════════════════════════════════════════
class FileSink : public Sink {
    std::string base_path_;
    size_t max_bytes_;
    int max_files_;
    mutable std::mutex mtx_;
    mutable std::ofstream file_;
    mutable size_t bytes_written_ = 0;
    int current_index_ = 0;

    void rotate() {
        file_.close();
        // Shift older logs: max-1 → max, max-2 → max-1, ...
        for (int i = max_files_ - 1; i > 0; --i) {
            std::string old_name = std::format("{}.{}", base_path_, i);
            std::string new_name = std::format("{}.{}", base_path_, i + 1);
            if (std::filesystem::exists(old_name))
                std::filesystem::rename(old_name, new_name);
        }
        std::string next_name = std::format("{}.{}", base_path_, 1);
        if (std::filesystem::exists(base_path_))
            std::filesystem::rename(base_path_, next_name);
        file_.open(base_path_, std::ios::app);
        bytes_written_ = 0;
    }

public:
    FileSink(std::string path, size_t max_bytes = 10 * 1024 * 1024, int max_files = 3)
        : base_path_(std::move(path)), max_bytes_(max_bytes), max_files_(max_files)
    {
        file_.open(base_path_, std::ios::app);
    }

    ~FileSink() override { if (file_.is_open()) file_.close(); }

    void write(Level level, Tag tag,
               const char* file, int line, const char* func,
               std::string_view message,
               const char* timestamp) override
    {
        std::lock_guard lock(mtx_);
        if (!file_.is_open()) return;

        std::string tag_part;
        if (tag != Tag::None) {
            tag_part = std::format("[{}]", ure::log::tag_str(tag));
        }

        std::string line_out = std::format("{}[{}]{} {} ({}:{})\n",
                                           timestamp, level_str(level).data(), tag_part,
                                           message, func, line);
        file_ << line_out;
        bytes_written_ += line_out.size();

        if (bytes_written_ >= max_bytes_) {
            rotate();
        }
    }

    void flush() override {
        std::lock_guard lock(mtx_);
        if (file_.is_open()) file_.flush();
    }
};

// ════════════════════════════════════════════════════════════════════
// MultiSink — fan-out to multiple sinks
// ════════════════════════════════════════════════════════════════════
class MultiSink : public Sink {
    std::vector<std::unique_ptr<Sink>> sinks_;
public:
    void add(std::unique_ptr<Sink> sink) {
        sinks_.push_back(std::move(sink));
    }

    void write(Level level, Tag tag,
               const char* file, int line, const char* func,
               std::string_view message,
               const char* timestamp) override
    {
        for (auto& s : sinks_) {
            s->write(level, tag, file, line, func, message, timestamp);
        }
    }

    void flush() override {
        for (auto& s : sinks_) s->flush();
    }
};

// ════════════════════════════════════════════════════════════════════
// CallbackSink — forward to an external function (e.g. Python binding)
// ════════════════════════════════════════════════════════════════════
class CallbackSink : public Sink {
    using Callback = std::function<void(Level, Tag, std::string_view)>;
    Callback cb_;
public:
    explicit CallbackSink(Callback cb) : cb_(std::move(cb)) {}

    void write(Level level, Tag tag,
               const char*, int, const char*,
               std::string_view message,
               const char*) override
    {
        if (cb_) cb_(level, tag, message);
    }
};

} // namespace ure::log
