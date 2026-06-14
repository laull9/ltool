/**
 * @file llog.hpp
 * @brief ltool 的轻量级纯头文件日志工具。
 */

#ifndef LTOOL_LLOG_INCLUDE
#define LTOOL_LLOG_INCLUDE

#include "lconfig.hpp"

#if !LTOOL_HAS_CPP11
#error "llog requires C++11 or later"
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

#include "lfmt.hpp"

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

namespace llog {

enum class level {
    trace = LTOOL_LOG_LEVEL_TRACE,
    debug = LTOOL_LOG_LEVEL_DEBUG,
    info = LTOOL_LOG_LEVEL_INFO,
    warn = LTOOL_LOG_LEVEL_WARN,
    error = LTOOL_LOG_LEVEL_ERROR,
    fatal = LTOOL_LOG_LEVEL_FATAL,
    off = LTOOL_LOG_LEVEL_OFF
};

enum class color_mode {
    automatic,
    always,
    never
};

#if LTOOL_HAS_SOURCE_LOCATION
using source_location = std::source_location;
#define LTOOL_CURRENT_SOURCE_LOCATION ::llog::source_location::current()
#else
class source_location {
private:
    const char* file_name_ = "";
    const char* function_name_ = "";
    std::uint_least32_t line_ = 0;
    std::uint_least32_t column_ = 0;

public:
    constexpr source_location() noexcept = default;

    constexpr source_location(const char* file_name, std::uint_least32_t line,
                              const char* function_name = "",
                              std::uint_least32_t column = 0) noexcept
        : file_name_(file_name),
          function_name_(function_name),
          line_(line),
          column_(column) {}

    static constexpr source_location current() noexcept {
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
#define LTOOL_CURRENT_SOURCE_LOCATION ::llog::source_location(__FILE__, __LINE__, __func__)
#endif

struct record {
    level severity = level::info;
    std::chrono::system_clock::time_point time = std::chrono::system_clock::now();
    std::thread::id thread_id {};
    std::string message;
    source_location location = source_location::current();
};

using sink_type = std::function<void(const record&)>;

inline const char* level_name(level severity) noexcept {
    switch (severity) {
    case level::trace:
        return "TRACE";
    case level::debug:
        return "DEBUG";
    case level::info:
        return "INFO";
    case level::warn:
        return "WARN";
    case level::error:
        return "ERROR";
    case level::fatal:
        return "FATAL";
    case level::off:
    default:
        return "OFF";
    }
}

inline const char* level_color(level severity) noexcept {
    switch (severity) {
    case level::trace:
        return "\x1b[90m";
    case level::debug:
        return "\x1b[36m";
    case level::info:
        return "\x1b[32m";
    case level::warn:
        return "\x1b[33m";
    case level::error:
        return "\x1b[31m";
    case level::fatal:
        return "\x1b[1;37;41m";
    case level::off:
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

inline bool should_use_color(color_mode mode, std::FILE* file) {
    if (mode == color_mode::always) {
        return true;
    }
    if (mode == color_mode::never || env_is_set("NO_COLOR") || env_is_zero("CLICOLOR")) {
        return false;
    }
    if (env_is_set("CLICOLOR_FORCE")) {
        return true;
    }
    return is_terminal(file);
}

inline bool is_enabled(level runtime_level, level severity) noexcept {
    return static_cast<int>(severity) >= static_cast<int>(runtime_level) &&
           severity != level::off && runtime_level != level::off;
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

inline std::string format_record(const record& item, bool with_location, bool with_color = false) {
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

class logger {
private:
    mutable std::mutex mutex_;
    level min_level_ = level::info;
    bool with_location_ = false;
    color_mode color_mode_ = color_mode::automatic;
    sink_type sink_;

    static void default_sink(const record& item, bool with_location, color_mode colors) {
        bool to_error = static_cast<int>(item.severity) >= static_cast<int>(level::warn);
        std::FILE* file = to_error ? stderr : stdout;
        auto text = format_record(item, with_location, should_use_color(colors, file));
        auto& stream = to_error ? std::cerr : std::cout;
        stream << text << '\n';
    }

public:
    logger() = default;

    void set_level(level value) {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = value;
    }

    level min_level() const {
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

    void set_color_mode(color_mode value) {
        std::lock_guard<std::mutex> lock(mutex_);
        color_mode_ = value;
    }

    void set_color_enabled(bool value) {
        set_color_mode(value ? color_mode::always : color_mode::never);
    }

    color_mode colors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return color_mode_;
    }

    void set_sink(sink_type sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = std::move(sink);
    }

    void reset_sink() {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = nullptr;
    }

    bool enabled(level severity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_enabled(min_level_, severity);
    }

    void write(level severity, const std::string& message,
               source_location location = source_location::current()) {
        sink_type sink;
        bool with_location = false;
        color_mode colors = color_mode::automatic;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!is_enabled(min_level_, severity)) {
                return;
            }
            sink = sink_;
            with_location = with_location_;
            colors = color_mode_;
        }

        record item;
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

    void write(level severity, const char* message,
               source_location location = source_location::current()) {
        write(severity, std::string(message ? message : ""), location);
    }

    template<class... Args>
    void writef(level severity, source_location location, fmt::format_string<Args...> fmt_text,
                Args&&... args) {
        if (!enabled(severity)) {
            return;
        }
        write(severity, fmt::format(fmt_text, std::forward<Args>(args)...), location);
    }
};

inline logger& default_logger() {
    static logger instance;
    return instance;
}

inline void set_level(level value) {
    default_logger().set_level(value);
}

inline void set_location_visible(bool value) {
    default_logger().set_location_visible(value);
}

inline void set_color_mode(color_mode value) {
    default_logger().set_color_mode(value);
}

inline void set_color_enabled(bool value) {
    default_logger().set_color_enabled(value);
}

inline void set_sink(sink_type sink) {
    default_logger().set_sink(std::move(sink));
}

inline void reset_sink() {
    default_logger().reset_sink();
}

inline bool enabled(level severity) {
    return default_logger().enabled(severity);
}

inline void write(level severity, const std::string& message,
                  source_location location = source_location::current()) {
    default_logger().write(severity, message, location);
}

inline void write(level severity, const char* message,
                  source_location location = source_location::current()) {
    default_logger().write(severity, message, location);
}

template<class... Args>
inline void writef(level severity, source_location location, fmt::format_string<Args...> fmt_text,
                   Args&&... args) {
    default_logger().writef(severity, location, fmt_text, std::forward<Args>(args)...);
}

inline void trace(const std::string& message, source_location location = source_location::current()) {
    write(level::trace, message, location);
}

inline void trace(const char* message, source_location location = source_location::current()) {
    write(level::trace, message, location);
}

inline void debug(const std::string& message, source_location location = source_location::current()) {
    write(level::debug, message, location);
}

inline void debug(const char* message, source_location location = source_location::current()) {
    write(level::debug, message, location);
}

inline void info(const std::string& message, source_location location = source_location::current()) {
    write(level::info, message, location);
}

inline void info(const char* message, source_location location = source_location::current()) {
    write(level::info, message, location);
}

inline void warn(const std::string& message, source_location location = source_location::current()) {
    write(level::warn, message, location);
}

inline void warn(const char* message, source_location location = source_location::current()) {
    write(level::warn, message, location);
}

inline void error(const std::string& message, source_location location = source_location::current()) {
    write(level::error, message, location);
}

inline void error(const char* message, source_location location = source_location::current()) {
    write(level::error, message, location);
}

inline void fatal(const std::string& message, source_location location = source_location::current()) {
    write(level::fatal, message, location);
}

inline void fatal(const char* message, source_location location = source_location::current()) {
    write(level::fatal, message, location);
}

} // namespace llog

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_TRACE
#define LLOG_TRACE(...) \
    ::llog::writef(::llog::level::trace, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_TRACE(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_DEBUG
#define LLOG_DEBUG(...) \
    ::llog::writef(::llog::level::debug, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_DEBUG(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_INFO
#define LLOG_INFO(...) \
    ::llog::writef(::llog::level::info, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_INFO(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_WARN
#define LLOG_WARN(...) \
    ::llog::writef(::llog::level::warn, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_WARN(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_ERROR
#define LLOG_ERROR(...) \
    ::llog::writef(::llog::level::error, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_ERROR(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_FATAL
#define LLOG_FATAL(...) \
    ::llog::writef(::llog::level::fatal, LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_FATAL(...) ((void)0)
#endif

#endif // LTOOL_LLOG_INCLUDE
