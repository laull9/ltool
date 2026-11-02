/**
 * @file LLogMacros.hpp
 * @brief LLog 的日志宏与等级常量（只含宏，不含任何声明）。
 *
 * LLog.hpp 会自动包含本文件，头文件用户无需关心。C++ 模块无法导出宏，
 * 使用 `import ltool;` 时如需 LLOG_* 宏，请在 import 之后单独包含本文件：
 * @code
 * import ltool;
 * #include "LLogMacros.hpp"
 *
 * LLOG_INFO("server started on port {}", 8080);
 * @endcode
 *
 * 宏展开后引用 ::LLog::detail::log_checked、::LLog::default_logger、
 * ::LLog::detail::make_location 等名字，由 LLog.hpp 或 ltool 模块提供。
 */

#ifndef LTOOL_LLOG_MACROS_INCLUDE
#define LTOOL_LLOG_MACROS_INCLUDE

#define LTOOL_LOG_LEVEL_TRACE 0 ///< trace 等级。
#define LTOOL_LOG_LEVEL_DEBUG 1 ///< debug 等级。
#define LTOOL_LOG_LEVEL_INFO 2  ///< info 等级。
#define LTOOL_LOG_LEVEL_WARN 3  ///< warn 等级。
#define LTOOL_LOG_LEVEL_ERROR 4 ///< error 等级。
#define LTOOL_LOG_LEVEL_FATAL 5 ///< fatal 等级。
#define LTOOL_LOG_LEVEL_OFF 6   ///< 关闭全部日志宏。

/// 编译期日志宏裁剪等级；低于该等级的 LLOG_* 展开为空。
#ifndef LTOOL_ACTIVE_LOG_LEVEL
#define LTOOL_ACTIVE_LOG_LEVEL LTOOL_LOG_LEVEL_TRACE
#endif

/// 调用方来源位置；可显式传给 LLog::writef 等接口。
#define LTOOL_CURRENT_SOURCE_LOCATION \
    ::LLog::detail::make_location(__FILE__, __LINE__, __func__, ::LLog::SourceLocation::current())

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_TRACE
#define LLOG_TRACE(...) \
    ::LLog::detail::log_checked(::LLog::default_logger(), ::LLog::Level::trace, \
                                LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_TRACE(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_DEBUG
#define LLOG_DEBUG(...) \
    ::LLog::detail::log_checked(::LLog::default_logger(), ::LLog::Level::debug, \
                                LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_DEBUG(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_INFO
#define LLOG_INFO(...) \
    ::LLog::detail::log_checked(::LLog::default_logger(), ::LLog::Level::info, \
                                LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_INFO(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_WARN
#define LLOG_WARN(...) \
    ::LLog::detail::log_checked(::LLog::default_logger(), ::LLog::Level::warn, \
                                LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_WARN(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_ERROR
#define LLOG_ERROR(...) \
    ::LLog::detail::log_checked(::LLog::default_logger(), ::LLog::Level::error, \
                                LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_ERROR(...) ((void)0)
#endif

#if LTOOL_ACTIVE_LOG_LEVEL <= LTOOL_LOG_LEVEL_FATAL
#define LLOG_FATAL(...) \
    ::LLog::detail::log_checked(::LLog::default_logger(), ::LLog::Level::fatal, \
                                LTOOL_CURRENT_SOURCE_LOCATION, __VA_ARGS__)
#else
#define LLOG_FATAL(...) ((void)0)
#endif

#endif // LTOOL_LLOG_MACROS_INCLUDE
