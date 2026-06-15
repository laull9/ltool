/**
 * @file detail/LConfig.hpp
 * @brief ltool 的版本、平台和语言特性配置入口。
 */

#ifndef LTOOL_LCONFIG_INCLUDE
#define LTOOL_LCONFIG_INCLUDE

#define LTOOL_VERSION_MAJOR 1
#define LTOOL_VERSION_MINOR 0
#define LTOOL_VERSION_PATCH 0
#define LTOOL_VERSION_STRING "1.0.0"

#if defined(_MSVC_LANG)
#define LTOOL_CPLUSPLUS _MSVC_LANG
#else
#define LTOOL_CPLUSPLUS __cplusplus
#endif

#if defined(__has_include)
#define LTOOL_HAS_INCLUDE(header) __has_include(header)
#else
#define LTOOL_HAS_INCLUDE(header) 0
#endif

#define LTOOL_HAS_CPP11 (LTOOL_CPLUSPLUS >= 201103L)
#define LTOOL_HAS_CPP14 (LTOOL_CPLUSPLUS >= 201402L)
#define LTOOL_HAS_CPP17 (LTOOL_CPLUSPLUS >= 201703L)
#define LTOOL_HAS_CPP20 (LTOOL_CPLUSPLUS >= 202002L)
#define LTOOL_HAS_CPP23 (LTOOL_CPLUSPLUS >= 202302L)

#ifndef LTOOL_USE_EXTERNAL_FMT
#if defined(LSTRING_USE_EXTERNAL_FMT)
#define LTOOL_USE_EXTERNAL_FMT LSTRING_USE_EXTERNAL_FMT
#else
#define LTOOL_USE_EXTERNAL_FMT 0
#endif
#endif

#ifndef LTOOL_USE_MAGIC_ENUM
#if defined(LSTRING_USE_MAGIC_ENUM)
#define LTOOL_USE_MAGIC_ENUM LSTRING_USE_MAGIC_ENUM
#else
#define LTOOL_USE_MAGIC_ENUM 1
#endif
#endif

#ifndef LTOOL_USE_RFL_JSON
#define LTOOL_USE_RFL_JSON 1
#endif

#ifndef LTOOL_USE_NLOHMANN_JSON
#define LTOOL_USE_NLOHMANN_JSON 1
#endif

#ifndef LTOOL_USE_JSONCPP
#if defined(LTOOL_USE_CPPJSON)
#define LTOOL_USE_JSONCPP LTOOL_USE_CPPJSON
#else
#define LTOOL_USE_JSONCPP 1
#endif
#endif

#ifndef LTOOL_USE_SIMDJSON
#define LTOOL_USE_SIMDJSON 1
#endif

#ifndef LTOOL_USE_YYJSON
#define LTOOL_USE_YYJSON 1
#endif

#if LTOOL_HAS_CPP20 && LTOOL_HAS_INCLUDE(<version>)
#include <version>
#endif

#if LTOOL_HAS_CPP20 && defined(__cpp_concepts)
#define LTOOL_HAS_CONCEPTS 1
#else
#define LTOOL_HAS_CONCEPTS 0
#endif

#if LTOOL_HAS_CPP17 && LTOOL_HAS_INCLUDE(<filesystem>)
#define LTOOL_HAS_FILESYSTEM 1
#else
#define LTOOL_HAS_FILESYSTEM 0
#endif

#if LTOOL_HAS_CPP17
#define LTOOL_HAS_OPTIONAL 1
#else
#define LTOOL_HAS_OPTIONAL 0
#endif

#if LTOOL_HAS_CPP20
#define LTOOL_HAS_RANGES 1
#define LTOOL_HAS_SPAN 1
#else
#define LTOOL_HAS_RANGES 0
#define LTOOL_HAS_SPAN 0
#endif

#define LTOOL_HAS_THREAD_POOL LTOOL_HAS_CPP17

#if LTOOL_HAS_CPP20 && LTOOL_HAS_INCLUDE(<source_location>)
#define LTOOL_HAS_SOURCE_LOCATION 1
#else
#define LTOOL_HAS_SOURCE_LOCATION 0
#endif

#if LTOOL_HAS_CPP20 && LTOOL_HAS_INCLUDE(<format>) && defined(__cpp_lib_format)
#define LTOOL_HAS_STD_FORMAT 1
#else
#define LTOOL_HAS_STD_FORMAT 0
#endif

#if LTOOL_USE_MAGIC_ENUM && LTOOL_HAS_CPP17 && LTOOL_HAS_INCLUDE("../pkgs/magic_enum/magic_enum.hpp")
#define LTOOL_HAS_MAGIC_ENUM 1
#else
#define LTOOL_HAS_MAGIC_ENUM 0
#endif

#if LTOOL_HAS_INCLUDE(<re2/re2.h>)
#define LTOOL_HAS_RE2 1
#else
#define LTOOL_HAS_RE2 0
#endif

#if LTOOL_USE_RFL_JSON && LTOOL_HAS_CPP20 && LTOOL_HAS_INCLUDE("../pkgs/rfl/json.hpp")
#define LTOOL_HAS_BUNDLED_RFL_JSON 1
#define LTOOL_HAS_RFL_JSON 1
#elif LTOOL_USE_RFL_JSON && LTOOL_HAS_CPP20 && LTOOL_HAS_INCLUDE(<rfl/json.hpp>)
#define LTOOL_HAS_BUNDLED_RFL_JSON 0
#define LTOOL_HAS_RFL_JSON 1
#else
#define LTOOL_HAS_BUNDLED_RFL_JSON 0
#define LTOOL_HAS_RFL_JSON 0
#endif

#if LTOOL_USE_NLOHMANN_JSON && LTOOL_HAS_INCLUDE(<nlohmann/json.hpp>)
#define LTOOL_HAS_NLOHMANN_JSON_HPP 1
#define LTOOL_HAS_NLOHMANN_SINGLE_HPP 0
#define LTOOL_HAS_NLOHMANN_JSON 1
#elif LTOOL_USE_NLOHMANN_JSON && LTOOL_HAS_INCLUDE(<nlohmann.hpp>)
#define LTOOL_HAS_NLOHMANN_JSON_HPP 0
#define LTOOL_HAS_NLOHMANN_SINGLE_HPP 1
#define LTOOL_HAS_NLOHMANN_JSON 1
#else
#define LTOOL_HAS_NLOHMANN_JSON_HPP 0
#define LTOOL_HAS_NLOHMANN_SINGLE_HPP 0
#define LTOOL_HAS_NLOHMANN_JSON 0
#endif

#if LTOOL_USE_JSONCPP && LTOOL_HAS_INCLUDE(<json/json.h>)
#define LTOOL_HAS_JSONCPP 1
#else
#define LTOOL_HAS_JSONCPP 0
#endif

#if LTOOL_USE_SIMDJSON && LTOOL_HAS_CPP17 && LTOOL_HAS_INCLUDE(<simdjson.h>)
#define LTOOL_HAS_SIMDJSON 1
#else
#define LTOOL_HAS_SIMDJSON 0
#endif

#if LTOOL_USE_YYJSON && LTOOL_HAS_INCLUDE("../pkgs/rfl/thirdparty/yyjson.h")
#define LTOOL_HAS_YYJSON 1
#define LTOOL_HAS_RFL_YYJSON 1
#elif LTOOL_USE_YYJSON && LTOOL_HAS_INCLUDE(<yyjson.h>)
#define LTOOL_HAS_YYJSON 1
#define LTOOL_HAS_RFL_YYJSON 0
#elif LTOOL_USE_YYJSON && LTOOL_HAS_INCLUDE(<rfl/thirdparty/yyjson.h>)
#define LTOOL_HAS_YYJSON 1
#define LTOOL_HAS_RFL_YYJSON 1
#else
#define LTOOL_HAS_YYJSON 0
#define LTOOL_HAS_RFL_YYJSON 0
#endif

#if defined(_WIN32)
#define LTOOL_PLATFORM_WINDOWS 1
#else
#define LTOOL_PLATFORM_WINDOWS 0
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define LTOOL_PLATFORM_APPLE 1
#else
#define LTOOL_PLATFORM_APPLE 0
#endif

#if defined(__linux__)
#define LTOOL_PLATFORM_LINUX 1
#else
#define LTOOL_PLATFORM_LINUX 0
#endif

#if !LTOOL_PLATFORM_WINDOWS && LTOOL_HAS_INCLUDE(<iconv.h>)
#define LTOOL_HAS_ICONV 1
#else
#define LTOOL_HAS_ICONV 0
#endif

#if defined(__clang__)
#define LTOOL_COMPILER_CLANG 1
#else
#define LTOOL_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define LTOOL_COMPILER_GCC 1
#else
#define LTOOL_COMPILER_GCC 0
#endif

#if defined(_MSC_VER)
#define LTOOL_COMPILER_MSVC 1
#else
#define LTOOL_COMPILER_MSVC 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define LTOOL_LIKELY(expr) (__builtin_expect(!!(expr), 1))
#define LTOOL_UNLIKELY(expr) (__builtin_expect(!!(expr), 0))
#else
#define LTOOL_LIKELY(expr) (expr)
#define LTOOL_UNLIKELY(expr) (expr)
#endif

#define LTOOL_UNUSED(value) (void)(value)

namespace LTool {

struct Version {
    int major;
    int minor;
    int patch;
};

inline constexpr Version current_version() noexcept {
    return {LTOOL_VERSION_MAJOR, LTOOL_VERSION_MINOR, LTOOL_VERSION_PATCH};
}

inline constexpr const char* version_string() noexcept {
    return LTOOL_VERSION_STRING;
}

} // namespace LTool

#endif // LTOOL_LCONFIG_INCLUDE
