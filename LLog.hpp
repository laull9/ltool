/**
 * @file LLog.hpp
 * @brief ltool 的轻量级纯头文件日志工具。
 *
 * 提供多级别日志（trace/debug/info/warn/error/fatal）、终端颜色、
 * 来源位置（source_location）、自定义 sink，以及编译期可裁剪的宏入口。
 *
 * 主要接口：
 * - 宏：LLOG_TRACE/LLOG_DEBUG/LLOG_INFO/LLOG_WARN/LLOG_ERROR/LLOG_FATAL，
 *   格式串与 fmt 一致，自动携带调用位置。
 * - 函数：LLog::info/debug/warn/error/fatal/trace/writef 等。
 * - 配置：set_level()、set_location_visible()、set_color_mode()、set_sink()。
 * - 等级常量：LTOOL_LOG_LEVEL_*；编译期用 LTOOL_ACTIVE_LOG_LEVEL 裁剪。
 *
 * 常用示例：
 * @code
 * #include "LLog.hpp"
 *
 * LLog::set_level(LLog::Level::debug);
 * LLOG_INFO("server started on port {}", 8080);
 * LLOG_WARN("disk usage {:.1f}%", 87.5);
 *
 * // 自定义 sink：把日志写到文件或网络
 * LLog::set_sink([](const LLog::Record& r) {
 *     std::cout << LLog::format_record(r, true) << '\n';
 * });
 * @endcode
 */

#ifndef LTOOL_LLOG_INCLUDE
#define LTOOL_LLOG_INCLUDE

#include "detail/LToolConfig.hpp"

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
#include <tuple>
#include <type_traits>
#include <utility>

#if LTOOL_PLATFORM_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

#if LTOOL_HAS_SOURCE_LOCATION
#include <source_location>
#endif

#include "detail/LFmt.hpp"

// 等级常量、LTOOL_CURRENT_SOURCE_LOCATION 与 LLOG_* 宏；模块用户在 import ltool 后单独 include 它。
#include "LLogMacros.hpp"

namespace LLog {

/// 日志级别，数值越大越严重。
enum class Level {
    trace = LTOOL_LOG_LEVEL_TRACE, ///< 最详细的调试跟踪。
    debug = LTOOL_LOG_LEVEL_DEBUG, ///< 调试信息。
    info = LTOOL_LOG_LEVEL_INFO,   ///< 常规信息。
    warn = LTOOL_LOG_LEVEL_WARN,   ///< 警告。
    error = LTOOL_LOG_LEVEL_ERROR, ///< 错误。
    fatal = LTOOL_LOG_LEVEL_FATAL, ///< 致命错误。
    off = LTOOL_LOG_LEVEL_OFF      ///< 关闭日志。
};

/// 终端颜色输出策略。
enum class ColorMode {
    automatic, ///< 仅当输出到终端时着色。
    always,    ///< 始终着色。
    never      ///< 永不着色。
};

#if LTOOL_HAS_SOURCE_LOCATION
/// 编译期来源位置；有 <source_location> 时使用标准类型。
using SourceLocation = std::source_location;
#else
/**
 * @brief 无 <source_location> 时的回退来源位置类型。
 *
 * 保存文件名、行号、函数名和列号；current() 仅返回空位置，实际调用位置由
 * 宏 LTOOL_CURRENT_SOURCE_LOCATION 注入 __FILE__/__LINE__/__func__。
 */
class SourceLocation {
private:
    const char* file_name_ = "";     ///< 文件名。
    const char* function_name_ = ""; ///< 函数名。
    std::uint_least32_t line_ = 0;   ///< 行号。
    std::uint_least32_t column_ = 0; ///< 列号。

public:
    /// 默认构造空位置。
    constexpr SourceLocation() noexcept = default;

    /// 用各字段构造来源位置；explicit 防止字符串字面量被误当作位置隐式转换。
    constexpr explicit SourceLocation(const char* file_name, std::uint_least32_t line,
                                      const char* function_name = "",
                                      std::uint_least32_t column = 0) noexcept
        : file_name_(file_name),
          function_name_(function_name),
          line_(line),
          column_(column) {}

    /// 返回当前调用位置；无标准库支持时由宏填入 __FILE__/__LINE__。
    static constexpr SourceLocation current() noexcept {
        return {};
    }

    /// 返回文件名。
    constexpr const char* file_name() const noexcept {
        return file_name_;
    }

    /// 返回函数名。
    constexpr const char* function_name() const noexcept {
        return function_name_;
    }

    /// 返回行号。
    constexpr std::uint_least32_t line() const noexcept {
        return line_;
    }

    /// 返回列号。
    constexpr std::uint_least32_t column() const noexcept {
        return column_;
    }
};
#endif

namespace detail {

// ---- 剥离技术（strip technique）----
// 公共 writef 形参包为 (Args&&... args)，调用方可以选择在包末尾显式携带一个
// SourceLocation 作为来源位置。下面的工具负责判断、剥离该位置，并把其余参数
// （格式串 + 格式参数）转发给实际格式化输出。

/// C++11 简易 index_sequence，用于展开剥离后的头参数包。
template<std::size_t... Is>
struct index_sequence {};

template<std::size_t N, std::size_t... Is>
struct make_index_sequence : make_index_sequence<N - 1, N - 1, Is...> {};

template<std::size_t... Is>
struct make_index_sequence<0, Is...> {
    using type = index_sequence<Is...>;
};

/// 判断形参包末尾是否为 SourceLocation。
template<class... Args>
struct trailing_is_location;

/// 空包：末尾不存在位置。
template<>
struct trailing_is_location<> : std::false_type {};

/// 只剩一个参数：直接检查其类型。
template<class T>
struct trailing_is_location<T> : std::is_same<typename std::decay<T>::type, SourceLocation> {};

/// 多于一个参数：剥掉头部继续检查末尾。
template<class T, class... Rest>
struct trailing_is_location<T, Rest...> : trailing_is_location<Rest...> {};

/// 实际格式化输出：第一个参数为格式串，其余为格式参数，位置单独传入。
/// 注：格式串经剥离转发后不再是字面量常量表达式，fmt 的 consteval format_string
/// 无法在函数体内构造，因此经 fmt::runtime 走运行时格式化路径（fmt::format_error 兜底）。
template<class LoggerT, class Fmt, class... Rest>
void writef_forward(LoggerT& logger, Level severity, SourceLocation location, Fmt&& fmt_text,
                    Rest&&... rest) {
    if (!logger.enabled(severity)) {
        return;
    }
    logger.write(severity,
                 fmt::format(fmt::runtime(std::forward<Fmt>(fmt_text)),
                             std::forward<Rest>(rest)...),
                 location);
}

/// 按下标展开元组，把除位置外的参数转发给 writef_forward。
template<class LoggerT, class... Args, std::size_t... Is>
void writef_tuple(LoggerT& logger, Level severity, SourceLocation location,
                  std::tuple<Args...>& tuple, index_sequence<Is...>) {
    using tuple_type = std::tuple<Args...>;
    writef_forward(logger, severity, location,
                   std::forward<typename std::tuple_element<Is, tuple_type>::type>(
                       std::get<Is>(tuple))...);
}

/// 分支一（包末尾是 SourceLocation）：剥离末尾位置，转发其余参数。
template<class LoggerT, class... Args>
void writef_split(LoggerT& logger, Level severity, std::true_type, Args&&... args) {
    constexpr std::size_t count = sizeof...(Args);
    std::tuple<Args&&...> tuple = std::forward_as_tuple(std::forward<Args>(args)...);
    writef_tuple(logger, severity, std::get<count - 1>(tuple), tuple,
                 typename make_index_sequence<count - 1>::type{});
}

/// 分支二（包末尾不是 SourceLocation）：使用默认当前位置。
template<class LoggerT, class... Args>
void writef_split(LoggerT& logger, Level severity, std::false_type, Args&&... args) {
    writef_forward(logger, severity, SourceLocation::current(), std::forward<Args>(args)...);
}

} // namespace detail

/// 一条日志记录：等级、时间、线程、消息和来源位置。
struct Record {
    Level severity = Level::info;                                       ///< 日志等级。
    std::chrono::system_clock::time_point time = std::chrono::system_clock::now(); ///< 记录时间。
    std::thread::id thread_id {};                                        ///< 产生日志的线程。
    std::string message;                                                ///< 日志消息。
    SourceLocation location = SourceLocation::current();                ///< 来源位置。
};

/// 日志输出回调类型：接收一条 Record。
using SinkType = std::function<void(const Record&)>;

/// 返回等级的稳定大写显示名称。
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

/// 返回等级的 ANSI 终端颜色转义码。
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

/// 判断环境变量是否存在且非空。
inline bool env_is_set(const char* name) {
#if LTOOL_PLATFORM_WINDOWS
    char* value = nullptr;
    std::size_t size = 0;
    const bool found = _dupenv_s(&value, &size, name) == 0 && value != nullptr;
    const bool result = found && value[0] != '\0';
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
#endif
}

/// 判断环境变量是否恰为字符串 "0"。
inline bool env_is_zero(const char* name) {
#if LTOOL_PLATFORM_WINDOWS
    char* value = nullptr;
    std::size_t size = 0;
    const bool found = _dupenv_s(&value, &size, name) == 0 && value != nullptr;
    const bool result = found && value[0] == '0' && value[1] == '\0';
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value && value[0] == '0' && value[1] == '\0';
#endif
}

/// 判断文件句柄是否连接到一个终端。
inline bool is_terminal(std::FILE* file) {
#if LTOOL_PLATFORM_WINDOWS
    return file && _isatty(_fileno(file)) != 0;
#else
    return file && isatty(fileno(file)) != 0;
#endif
}

/// 结合 ColorMode 与环境变量（NO_COLOR/CLICOLOR*）决定是否着色。
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

/// 判断 severity 是否达到 runtime_level 的开启阈值。
inline bool is_enabled(Level runtime_level, Level severity) noexcept {
    return static_cast<int>(severity) >= static_cast<int>(runtime_level) &&
           severity != Level::off && runtime_level != Level::off;
}

/// 把 time_t 转换为线程安全的本地时间结构。
inline std::tm local_time(std::time_t value) {
    std::tm out {};
#if defined(_WIN32)
    localtime_s(&out, &value);
#else
    localtime_r(&value, &out);
#endif
    return out;
}

/// 把一条 Record 格式化为一行文本；可选来源位置和颜色。
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

/**
 * @brief 线程安全的日志器；配置等级、位置显示、颜色和输出 sink。
 */
class Logger {
private:
    mutable std::mutex mutex_;      ///< 保护内部状态的锁。
    Level min_level_ = Level::info; ///< 当前最低输出等级。
    bool with_location_ = false;    ///< 是否在输出中包含来源位置。
    ColorMode color_mode_ = ColorMode::automatic; ///< 颜色策略。
    SinkType sink_;                 ///< 自定义输出回调；为空时使用默认控制台输出。

    /// 默认输出：warn 及以上写 stderr，其余写 stdout。
    static void default_sink(const Record& item, bool with_location, ColorMode colors) {
        bool to_error = static_cast<int>(item.severity) >= static_cast<int>(Level::warn);
        std::FILE* file = to_error ? stderr : stdout;
        auto text = format_record(item, with_location, should_use_color(colors, file));
        auto& stream = to_error ? std::cerr : std::cout;
        stream << text << '\n';
    }

public:
    /// 构造默认配置的日志器。
    Logger() = default;

    /// 设置最低输出等级。
    void set_level(Level value) {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = value;
    }

    /// 返回当前最低输出等级。
    Level min_level() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return min_level_;
    }

    /// 设置是否在输出中包含来源位置。
    void set_location_visible(bool value) {
        std::lock_guard<std::mutex> lock(mutex_);
        with_location_ = value;
    }

    /// 返回当前是否显示来源位置。
    bool location_visible() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return with_location_;
    }

    /// 设置颜色策略。
    void set_color_mode(ColorMode value) {
        std::lock_guard<std::mutex> lock(mutex_);
        color_mode_ = value;
    }

    /// 颜色开关便捷版：true 始终着色，false 永不着色。
    void set_color_enabled(bool value) {
        set_color_mode(value ? ColorMode::always : ColorMode::never);
    }

    /// 返回当前颜色策略。
    ColorMode colors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return color_mode_;
    }

    /// 设置自定义输出 sink；为空时回退到默认控制台输出。
    void set_sink(SinkType sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = std::move(sink);
    }

    /// 清空自定义 sink，恢复默认控制台输出。
    void reset_sink() {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = nullptr;
    }

    /// 判断指定等级当前是否开启。
    bool enabled(Level severity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_enabled(min_level_, severity);
    }

    /// 写一条日志记录；未开启或设置禁用时直接返回。
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

    /// 写一条 C 字符串日志记录。
    void write(Level severity, const char* message,
               SourceLocation location = SourceLocation::current()) {
        write(severity, std::string(message ? message : ""), location);
    }

    /// 用 fmt 格式串写一条日志记录；参数包末尾可携带显式位置。省略位置时，
    /// C++20 下位置指向 LLog.hpp 内部（writef_split 中 SourceLocation::current()
    /// 的调用处），C++11 回退下为空位置；需要调用方位置时请用 LLOG_* 宏或
    /// 显式传入 LTOOL_CURRENT_SOURCE_LOCATION。
    template<class... Args>
    void writef(Level severity, Args&&... args) {
        detail::writef_split(*this, severity, detail::trailing_is_location<Args...>{},
                             std::forward<Args>(args)...);
    }
};

namespace detail {

/// 供 LTOOL_CURRENT_SOURCE_LOCATION 使用：有 std::source_location 时返回 current
/// （在宏展开处求值，即调用方位置），否则用 __FILE__/__LINE__/__func__ 构造。
/// 宏因此与配置无关，模块用户无需看到 LTOOL_HAS_SOURCE_LOCATION。
inline SourceLocation make_location(const char* file, unsigned long line, const char* function,
                                    SourceLocation current) noexcept {
#if LTOOL_HAS_SOURCE_LOCATION
    LTOOL_UNUSED(file);
    LTOOL_UNUSED(line);
    LTOOL_UNUSED(function);
    return current;
#else
    LTOOL_UNUSED(current);
    return SourceLocation(file, static_cast<std::uint_least32_t>(line), function);
#endif
}

/// 编译期校验入口：格式串以 fmt::format_string<Args...> 传参，保留 consteval 编译期校验。
/// 供 LLOG_* 宏使用（宏展开时格式串是字面量，可走编译期校验）；
/// 与剥离版 writef 不同，该路径不经过 fmt::runtime，格式串与参数不匹配会在编译期报错。
template<class... Args>
void log_checked(Logger& logger, Level severity, SourceLocation location,
                 fmt::format_string<Args...> fmt_text, Args&&... args) {
    if (!logger.enabled(severity)) {
        return;
    }
    logger.write(severity, fmt::format(fmt_text, std::forward<Args>(args)...), location);
}

} // namespace detail

/// 返回进程级默认日志器（单例）。
inline Logger& default_logger() {
    static Logger instance;
    return instance;
}

/// 设置默认日志器的最低输出等级。
inline void set_level(Level value) {
    default_logger().set_level(value);
}

/// 设置默认日志器是否显示来源位置。
inline void set_location_visible(bool value) {
    default_logger().set_location_visible(value);
}

/// 设置默认日志器的颜色策略。
inline void set_color_mode(ColorMode value) {
    default_logger().set_color_mode(value);
}

/// 设置默认日志器颜色开关。
inline void set_color_enabled(bool value) {
    default_logger().set_color_enabled(value);
}

/// 设置默认日志器的自定义 sink。
inline void set_sink(SinkType sink) {
    default_logger().set_sink(std::move(sink));
}

/// 清空默认日志器的自定义 sink。
inline void reset_sink() {
    default_logger().reset_sink();
}

/// 判断默认日志器是否开启指定等级。
inline bool enabled(Level severity) {
    return default_logger().enabled(severity);
}

/// 用默认日志器写一条字符串日志。
inline void write(Level severity, const std::string& message,
                  SourceLocation location = SourceLocation::current()) {
    default_logger().write(severity, message, location);
}

/// 用默认日志器写一条 C 字符串日志。
inline void write(Level severity, const char* message,
                  SourceLocation location = SourceLocation::current()) {
    default_logger().write(severity, message, location);
}

/// 用默认日志器按 fmt 格式串写日志；参数包末尾可携带显式位置。
template<class... Args>
inline void writef(Level severity, Args&&... args) {
    default_logger().writef(severity, std::forward<Args>(args)...);
}

/// 输出 trace 级日志。
inline void trace(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::trace, message, location);
}

/// 输出 trace 级日志（C 字符串版本）。
inline void trace(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::trace, message, location);
}

/// 输出 debug 级日志。
inline void debug(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::debug, message, location);
}

/// 输出 debug 级日志（C 字符串版本）。
inline void debug(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::debug, message, location);
}

/// 输出 info 级日志。
inline void info(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::info, message, location);
}

/// 输出 info 级日志（C 字符串版本）。
inline void info(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::info, message, location);
}

/// 输出 warn 级日志。
inline void warn(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::warn, message, location);
}

/// 输出 warn 级日志（C 字符串版本）。
inline void warn(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::warn, message, location);
}

/// 输出 error 级日志。
inline void error(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::error, message, location);
}

/// 输出 error 级日志（C 字符串版本）。
inline void error(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::error, message, location);
}

/// 输出 fatal 级日志。
inline void fatal(const std::string& message, SourceLocation location = SourceLocation::current()) {
    write(Level::fatal, message, location);
}

/// 输出 fatal 级日志（C 字符串版本）。
inline void fatal(const char* message, SourceLocation location = SourceLocation::current()) {
    write(Level::fatal, message, location);
}

} // namespace LLog

#endif // LTOOL_LLOG_INCLUDE
