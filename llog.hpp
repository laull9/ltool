/**
 * @file LLog.hpp
 * @brief ltool 的轻量级纯头文件日志工具。
 */

#ifndef LTOOL_LLOG_INCLUDE
#define LTOOL_LLOG_INCLUDE

#include "LConfig.hpp"

#if !LTOOL_HAS_CPP11
#error "LLog requires C++11 or later"
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#if LTOOL_PLATFORM_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

#if LTOOL_HAS_SOURCE_LOCATION
#include <source_location>
#endif

#include "LFmt.hpp"

#define LTOOL_LOG_LEVEL_TRACE 0
#define LTOOL_LOG_LEVEL_DEBUG 1
#define LTOOL_LOG_LEVEL_INFO 2
#define LTOOL_LOG_LEVEL_WARN 3
#define LTOOL_LOG_LEVEL_ERROR 4
#define LTOOL_LOG_LEVEL_FATAL 5
#define LTOOL_LOG_LEVEL_OFF 6

#ifndef LTOOL_ACTIVE_LOG_LEVEL
#define LTOOL_ACTIVE_LOG_LEVEL LTOOL_LOG_LEVEL_TRACE
#endif

namespace LLog {

enum class Level {
    trace = LTOOL_LOG_LEVEL_TRACE,
    debug = LTOOL_LOG_LEVEL_DEBUG,
    info = LTOOL_LOG_LEVEL_INFO,
    warn = LTOOL_LOG_LEVEL_WARN,
    error = LTOOL_LOG_LEVEL_ERROR,
    fatal = LTOOL_LOG_LEVEL_FATAL,
    off = LTOOL_LOG_LEVEL_OFF
};

enum class ColorMode {
    automatic,
    always,
    never
};

#if LTOOL_HAS_SOURCE_LOCATION
using SourceLocation = std::source_location;
#define LTOOL_CURRENT_SOURCE_LOCATION ::LLog::SourceLocation::current()
#else
class SourceLocation {
private:
    const char* file_name_ = "";
    const char* function_name_ = "";
    std::uint_least32_t line_ = 0;
    std::uint_least32_t column_ = 0;

public:
    constexpr SourceLocation() noexcept = default;

    constexpr SourceLocation(const char* file_name, std::uint_least32_t line,
                              const char* function_name = "",
                              std::uint_least32_t column = 0) noexcept
        : file_name_(file_name),
          function_name_(function_name),
          line_(line),
          column_(column) {}

    static constexpr SourceLocation current() noexcept {
        return {};
    }

    constexpr const char* file_name() const noexcept {
        return file_name_;
    }

    constexpr const char* function_name() const noexcept {
        return function_name_;
    }

    constexpr std::uint_least32_t line() const noexcept {
        return line_;
    }

    constexpr std::uint_least32_t column() const noexcept {
        return column_;
    }
};
#define LTOOL_CURRENT_SOURCE_LOCATION ::LLog::SourceLocation(__FILE__, __LINE__, __func__)
#endif

struct Record {
    Level severity = Level::info;
    std::chrono::system_clock::time_point time = std::chrono::system_clock::now();
    std::thread::id thread_id {};
    std::string message;
    SourceLocation location = SourceLocation::current();
};

using SinkType = std::function<void(const Record&)>;

inline const char* level_name(Level severity) noexcept {
    switch (severity) {
    case Level::trace:
        return "TRACE";
    case Level::debug:
        return "DEBUG";
    case Level::info:
        return "INFO";
    case Level::warn:
        return "WARN";
    case Level::error:
        return "ERROR";
    case Level::fatal:
        return "FATAL";
    case Level::off:
    default:
        return "OFF";
    }
}

inline const char* level_color(Level severity) noexcept {
    switch (severity) {
    case Level::trace:
        return "\x1b[90m";
    case Level::debug:
        return "\x1b[36m";
    case Level::info:
        return "\x1b[32m";
    case Level::warn:
        return "\x1b[33m";
    case Level::error:
        return "\x1b[31m";
    case Level::fatal:
        return "\x1b[1;37;41m";
    case Level::off:
    default:
        return "";
    }
}

inline bool env_is_set(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
}

inline bool env_is_zero(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] == '0' && value[1] == '\0';
}

inline bool is_terminal(std::FILE* file) {
#if LTOOL_PLATFORM_WINDOWS
    return file && _isatty(_fileno(file)) != 0;
#else
    return file && isatty(fileno(file)) != 0;
#endif
}

inline bool should_use_color(ColorMode mode, std::FILE* file) {
    if (mode == ColorMode::always) {
        return true;
    }
    if (mode == ColorMode::never || env_is_set("NO_COLOR") || env_is_zero("CLICOLOR")) {
        return false;
    }
    if (env_is_set("CLICOLOR_FORCE")) {
        return true;
    }
    return is_terminal(file);
}

inline bool is_enabled(Level runtime_level, Level severity) noexcept {
    return static_cast<int>(severity) >= static_cast<int>(runtime_level) &&
           severity != Level::off && runtime_level != Level::off;
}

inline std::tm local_time(std::time_t value) {
    std::tm out {};
#if defined(_WIN32)
    localtime_s(&out, &value);
#else
    localtime_r(&value, &out);
#endif
    return out;
}

inline std::string format_record(const Record& item, bool with_location, bool with_color = false) {
    auto tt = std::chrono::system_clock::to_time_t(item.time);
    auto tm = local_time(tt);

    std::ostringstream out;
    if (with_color) {
        out << "\x1b[90m";
    }
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (with_color) {
        out << "\x1b[0m";
    }

    out << ' ';
    if (with_color) {
        out << level_color(item.severity);
    }
    out << '[' << level_name(item.severity) << ']';
    if (with_color) {
        out << "\x1b[0m";
    }

    if (with_color) {
        out << " \x1b[90m";
    } else {
        out << ' ';
    }
    out << "[tid " << item.thread_id << "] ";
    if (with_color) {
        out << "\x1b[0m";
    }
    out << item.message;

    if (with_location && item.location.file_name() && item.location.file_name()[0] != '\0') {
        if (with_color) {
            out << "\x1b[90m";
        }
        out << " (" << item.location.file_name() << ':' << item.location.line() << ')';
        if (with_color) {
            out << "\x1b[0m";
        }
    }

    return out.str();
}

class Logger {
private:
    mutable std::mutex mutex_;
    Level min_level_ = Level::info;
    bool with_location_ = false;
    ColorMode color_mode_ = ColorMode::automatic;
    SinkType sink_;

    static void default_sink(const Record& item, bool with_location, ColorMode colors) {
        bool to_error = static_cast<int>(item.severity) >= static_cast<int>(Level::warn);
        std::FILE* file = to_error ? stderr : stdout;
        auto text = format_record(item, with_location, should_use_color(colors, file));
        auto& stream = to_error ? std::cerr : std::cout;
        stream << text << '\n';
    }

public:
    Logger() = default;

    void set_level(Level value) {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = value;
    }

    Level min_level() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return min_level_;
    }

    void set_location_visible(bool value) {
        std::lock_guard<std::mutex> lock(mutex_);
        with_location_ = value;
    }

    bool location_visible() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return with_location_;
    }

    void set_color_mode(ColorMode value) {
        std::lock_guard<std::mutex> lock(mutex_);
        color_mode_ = value;
    }

    void set_color_enabled(bool value) {
        set_color_mode(value ? ColorMode::always : ColorMode::never);
    }

    ColorMode colors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return color_mode_;
    }

    void set_sink(SinkType sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = std::move(sink);
    }

    void reset_sink() {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = nullptr;
    }

    bool enabled(Level severity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_enabled(min_level_, severity);
    }

    void write(Level severity, const std::string& message,
               SourceLocation location = SourceLocation::current()) {
        SinkType sink;
        bool with_location = false;
        ColorMode colors = ColorMode::automatic;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!is_enabled(min_level_, severity)) {
                return;
            }
            sink = sink_;
            with_location = with_location_;
            colors = color_mode_;
        }

        Record item;
        item.severity = severity;
        item.time = std::chrono::system_clock::now();
        item.thread_id = std::this_thread::get_id();
        item.message = message;
        item.location = location;

        if (sink) {
            sink(item);
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            default_sink(item, with_location, colors);
        }
    }

    void write(Level severity, const char* message,
               SourceLocation location = SourceLocation::current()) {
        write(severity, std::string(message ? message : ""), location);
    }

    template<class... Args>
    void writef(Level severity, SourceLocation location, fmt::format_string<Args...> fmt_text,
                Args&&... args) {
        if (!enabled(severity)) {
            return;
        }
        write(severity, fmt::format(fmt_text, std::forward<Args>(args)...), location);
    }
};

inline Logger& default_logger() {
    static Logger instance;
    return instance;
}

inline void set_level(Level value) {
    default_logger().set_level(value);
}

inline void set_location_visible(bool value) {
    default_logger().set_location_visible(value);
}

inline void set_color_mode(ColorMode value) {
    default_logger().set_color_mode(value);
}

inline void set_color_enabled(bool value) {
    default_logger().set_color_enabled(value);
}

inline void set_sink(SinkType sink) {
    default_logger().set_sink(std::move(sink));
}

inline void reset_sink() {
    default_logger().reset_sink();
}

inline bool enabled(Level severity) {
    return default_logger().enabled(severity);
}

inline void write(Level severity, const std::string& message,
                  SourceLocation location = SourceLocation::current()) {
    default_logger().write(severity, message, location);
}

inline void write(Level severity, const char* message,
                  SourceLocation location = SourceLocation::current()) {
    default_logger().write(severity, message, location);
}

template<class... Args>
inline void writef(Level severity, SourceLocation location, fmt::format_string<Args...> fmt_text,
                   Args&&... args) {
    default_logger().writef(severity, location, fmt_text, std::forward<Args>(args)...);
}

inline void trace(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::trace, message, location);
}

inline void trace(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::trace, message, location);
}

inline void debug(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::debug, message, location);
}

inline void debug(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::debug, message, location);
}

inline void info(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::info, message, location);
}

inline void info(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::info, message, location);
}

inline void warn(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::warn, message, location);
}

inline void warn(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::warn, message, location);
}

inline void error(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::error, message, location);
}

inline void error(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::error, message, location);
}

inline void fatal(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::fatal, message, location);
}

inline void fatal(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::fatal, message, location);
}

} // namespace LLog

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_TRACE
#define LLOG_TRACE(...) \
    ::LLog::writef(::LLog::Level::trace, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_TRACE(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_DEBUG
#define LLOG_DEBUG(...) \
    ::LLog::writef(::LLog::Level::debug, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_DEBUG(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_INFO
#define LLOG_INFO(...) \
    ::LLog::writef(::LLog::Level::info, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_INFO(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_WARN
#define LLOG_WARN(...) \
    ::LLog::writef(::LLog::Level::warn, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_WARN(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_ERROR
#define LLOG_ERROR(...) \
    ::LLog::writef(::LLog::Level::error, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_ERROR(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_FATAL
#define LLOG_FATAL(...) \
    ::LLog::writef(::LLog::Level::fatal, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_FATAL(...) ((void)0)
#endif

#endif // LTOOL_LLOG_INCLUDE
