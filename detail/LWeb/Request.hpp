/**
 * @file detail/LWeb/Request.hpp
 * @brief LWeb 请求辅助函数：Header / Query / Param / Body / BodyJson，
 *        以及 Typed Request Binding（Param<T> / Query<T> / QueryOr<T> /
 *        Header<T>）。
 *
 * 类型转换失败统一抛 BindingError（400），绝不把 binding error 当 500。
 * 返回的 string_view 仅在 handler 执行期间有效，如需跨 co_await 使用请先
 * 拷贝为 std::string。
 */

#ifndef LTOOL_LWEB_DETAIL_REQUEST_HPP
#define LTOOL_LWEB_DETAIL_REQUEST_HPP

#include <charconv>
#include <concepts>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "detail/LWeb/Error.hpp"
#include "detail/LWeb/Types.hpp"

namespace LWeb {

namespace detail {

/// 整型解析（含 char 底层特例：from_chars 不支持 char）。
template <typename T>
T ParseIntegral(std::string_view text) {
    if constexpr (std::same_as<T, char> || std::same_as<T, signed char> ||
                  std::same_as<T, unsigned char>) {
        int value{};
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(),
                                         value);
        if (ec != std::errc{} || ptr != text.data() + text.size()) {
            throw BindingError("INVALID_VALUE", "value is not a valid integer");
        }
        return static_cast<T>(value);
    }
    else {
        T value{};
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(),
                                         value);
        if (ec != std::errc{} || ptr != text.data() + text.size()) {
            throw BindingError("INVALID_VALUE", "value is not a valid integer");
        }
        return value;
    }
}

/// 字符串 -> 类型 T 的转换；失败抛 BindingError（400）。
/// 支持：string / string_view / bool / 整型 / 浮点 / 整型底层 enum。
template <typename T>
T ParseValue(std::string_view text) {
    if constexpr (std::same_as<T, std::string>) {
        return std::string(text);
    }
    else if constexpr (std::same_as<T, std::string_view>) {
        return text;
    }
    else if constexpr (std::same_as<T, bool>) {
        if (text == "true" || text == "1") {
            return true;
        }
        if (text == "false" || text == "0") {
            return false;
        }
        throw BindingError("INVALID_VALUE",
                           "value is not a valid boolean");
    }
    else if constexpr (std::is_enum_v<T>) {
        using U = std::underlying_type_t<T>;
        return static_cast<T>(ParseIntegral<U>(text));
    }
    else if constexpr (std::is_integral_v<T>) {
        return ParseIntegral<T>(text);
    }
    else if constexpr (std::is_floating_point_v<T>) {
        T value{};
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(),
                                         value);
        if (ec != std::errc{} || ptr != text.data() + text.size()) {
            throw BindingError("INVALID_VALUE",
                               "value is not a valid number");
        }
        return value;
    }
    else {
        static_assert(sizeof(T) == 0,
                      "LWeb: 不支持的绑定类型（支持 string / string_view / "
                      "bool / 整型 / 浮点 / 整型底层 enum）");
    }
}

} // namespace detail

/// 取请求头（不存在时返回空 view）。
inline std::string_view Header(Request& req, std::string_view name) {
    return req.get_header_value(name);
}

/// 取请求头并转换为类型 T；缺失或转换失败抛 BindingError（400）。
template <typename T>
T Header(Request& req, std::string_view name) {
    auto value = req.get_header_value(name);
    if (value.empty()) {
        throw BindingError("MISSING_HEADER",
                           "required header is missing: " +
                               std::string(name));
    }
    return detail::ParseValue<T>(value);
}

/// 取 URL 查询参数（不存在时返回空 view）。
inline std::string_view Query(Request& req, std::string_view key) {
    return req.get_query_value(key);
}

/// 取查询参数并转换为类型 T；缺失或转换失败抛 BindingError（400）。
/// 注意：值合法为空字符串时按缺失处理（get_query_value 无法区分）。
template <typename T>
T Query(Request& req, std::string_view key) {
    auto value = req.get_query_value(key);
    if (value.empty()) {
        throw BindingError("MISSING_QUERY",
                           "required query parameter is missing: " +
                               std::string(key));
    }
    return detail::ParseValue<T>(value);
}

/// 取查询参数并转换为类型 T；缺失时返回 fallback。
template <typename T>
T QueryOr(Request& req, std::string_view key, T fallback) {
    auto value = req.get_query_value(key);
    if (value.empty()) {
        return fallback;
    }
    return detail::ParseValue<T>(value);
}

/// 取 REST 路径参数（如 "/users/:id" 中的 id）。
inline std::string_view Param(Request& req, std::string_view key) {
    auto it = req.params_.find(std::string(key));
    if (it == req.params_.end()) {
        return {};
    }
    return it->second;
}

/// 取路径参数并转换为类型 T；缺失或转换失败抛 BindingError（400）。
template <typename T>
T Param(Request& req, std::string_view key) {
    auto it = req.params_.find(std::string(key));
    if (it == req.params_.end()) {
        throw BindingError("MISSING_PARAM",
                           "required path parameter is missing: " +
                               std::string(key));
    }
    return detail::ParseValue<T>(it->second);
}

/// 取请求体。
inline std::string_view Body(Request& req) {
    return req.get_body();
}

/// 把请求体按 JSON 反序列化为类型 T。
/// 解析失败 -> JsonParseError(400)；结构与类型不匹配 -> BindingError(400)。
template <typename T, typename Codec = LWebJsonCodec>
T BodyJson(Request& req) {
    return Codec::template Deserialize<T>(req.get_body());
}

} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_REQUEST_HPP
