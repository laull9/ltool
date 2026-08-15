/**
 * @file detail/LWeb/Types.hpp
 * @brief LWeb 基础类型：底层别名、返回值式 Handler 结果类型、JSON Codec、
 *        Middleware 概念。
 *
 * 本文件不依赖 LWeb 内部其他文件，是 detail/LWeb 各模块的地基。
 */

#ifndef LTOOL_LWEB_DETAIL_TYPES_HPP
#define LTOOL_LWEB_DETAIL_TYPES_HPP

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#include "detail/LWeb/Error.hpp"
#include "LJson.hpp"
#include "pkgs/cinatra/cinatra.hpp"

namespace LWeb {

// ---- 底层类型别名：直接复用 Cinatra，不重新造对象 ----

/// Cinatra HTTP 请求对象（别名）。
using Request = cinatra::coro_http_request;

/// Cinatra HTTP 响应对象（别名）。
using Response = cinatra::coro_http_response;

/// HTTP 状态码（复用 cinatra 的状态枚举，避免重复维护）。
using HttpStatus = cinatra::status_type;

/// HTTP 客户端响应数据（别名）。
using ResponseData = cinatra::resp_data;

/// 请求头容器（别名）。
using HeaderMap = std::unordered_map<std::string, std::string>;

/// 协程任务类型（复用 async_simple::coro::Lazy）。
template <typename T = void>
using Task = async_simple::coro::Lazy<T>;

// ---- 返回值式 Handler 的结果类型 ----

/// Handler 可直接返回的结果类型集合。
///
/// 返回值式 Handler（可选）：handler 返回结果类型而非操作 Response，
/// 由框架自动映射状态码与响应体：
/// @code
/// LWeb::Result::Created<User> Create(LWeb::Request& req) {
///     return {LWeb::BodyJson<User>(req)};
/// }
///
/// LWeb::Result::Variant<User> GetById(LWeb::Request& req) {
///     auto id = LWeb::Param(req, "id");
///     if (id.empty()) {
///         return LWeb::Result::BadRequest{"missing id"};
///     }
///     return User{...};                       // 裸类型 -> 200 JSON
/// }
/// @endcode
///
/// 自动映射：普通类型 -> 200 JSON；Result::Created<T> -> 201 JSON；
/// 错误结果 -> JSON {"error":{"code","message"}}（code 省略时自动生成
/// NOT_FOUND / BAD_REQUEST / UNAUTHORIZED / FORBIDDEN / CONFLICT）；
/// 字符串 -> 200 文本；Result::Variant<T> 按实际持有类型分发。
/// 协程 Handler 返回 Task<T> / Task<Result::...> 同样适用。
namespace Result {

/// 204 No Content。
struct NoContent {};

/// 404 Not Found：`{message}` 自动补 code；`{code, message}` 显式指定。
struct NotFound {
    std::string code;
    std::string message;

    NotFound() = default;
    NotFound(std::string message) : message(std::move(message)) {}
    NotFound(std::string code, std::string message)
        : code(std::move(code)), message(std::move(message)) {}
};

/// 400 Bad Request。
struct BadRequest {
    std::string code;
    std::string message;

    BadRequest() = default;
    BadRequest(std::string message) : message(std::move(message)) {}
    BadRequest(std::string code, std::string message)
        : code(std::move(code)), message(std::move(message)) {}
};

/// 401 Unauthorized。
struct Unauthorized {
    std::string code;
    std::string message;

    Unauthorized() = default;
    Unauthorized(std::string message) : message(std::move(message)) {}
    Unauthorized(std::string code, std::string message)
        : code(std::move(code)), message(std::move(message)) {}
};

/// 403 Forbidden。
struct Forbidden {
    std::string code;
    std::string message;

    Forbidden() = default;
    Forbidden(std::string message) : message(std::move(message)) {}
    Forbidden(std::string code, std::string message)
        : code(std::move(code)), message(std::move(message)) {}
};

/// 409 Conflict。
struct Conflict {
    std::string code;
    std::string message;

    Conflict() = default;
    Conflict(std::string message) : message(std::move(message)) {}
    Conflict(std::string code, std::string message)
        : code(std::move(code)), message(std::move(message)) {}
};

/// 201 Created，携带 JSON 负载。
template <typename T>
struct Created {
    T value;
};

/// 多选一结果：handler 可返回其中任意一种（普通 T / Created<T> /
/// NotFound / BadRequest / Unauthorized / Forbidden / Conflict / NoContent）。
template <typename T>
using Variant = std::variant<T, Created<T>, NotFound, BadRequest, Unauthorized,
                             Forbidden, Conflict, NoContent>;

} // namespace Result

// ---- JSON Codec：默认使用 LJson，可整体替换 ----

/// 默认 JSON 编解码器：序列化用 LJson 反射，反序列化失败抛异常。
struct LWebJsonCodec {
    /// 把任意可反射类型序列化为 JSON 文本。
    template <typename T>
    static std::string Serialize(const T& value) {
        if constexpr (std::same_as<std::remove_cvref_t<T>, LJson::Json>) {
            return value.dump();
        }
        else {
            return LJson::write(value);
        }
    }

    /// 把 JSON 文本反序列化为类型 T。
    /// 两步解析以区分错误：JSON 语法错误 -> JsonParseError(400)；
    /// 结构与目标类型不匹配 -> BindingError(400)。
    template <typename T>
    static T Deserialize(std::string_view text) {
        if constexpr (std::same_as<T, LJson::Json>) {
            try {
                return LJson::Json::parse(std::string(text));
            }
            catch (const HttpException&) {
                throw;
            }
            catch (const std::exception& e) {
                throw JsonParseError(e.what());
            }
        }
        else {
            LJson::Json json;
            try {
                json = LJson::Json::parse(std::string(text));
            }
            catch (const HttpException&) {
                throw;
            }
            catch (const std::exception& e) {
                throw JsonParseError(e.what());
            }
            try {
                return LJson::read<T>(json).value();
            }
            catch (const HttpException&) {
                throw;
            }
            catch (const std::exception& e) {
                throw BindingError("JSON_BINDING_ERROR", e.what());
            }
        }
    }
};

// ---- Middleware Concepts ----

/// 只有 before 的中间件（可中断请求）。
///
/// 检测表达式与 Cinatra has_before 完全同构（std::declval<T>() 右值形式），
/// 并额外校验返回值可转 bool（Cinatra 内部执行 ok = aspect.before(...)）。
/// 用途：Use / 路由注册处的 static_assert，把 Cinatra 的"静默忽略"变成
/// 编译错误。异步中间件（before 返回 Task<bool>）不满足可转 bool，
/// 会在编译期被拒绝——这是期望行为（v0.1/v0.2 只支持同步中间件）。
template <typename T>
concept BeforeMiddleware = requires {
    { std::declval<T>().before(std::declval<Request&>(),
                               std::declval<Response&>()) }
        -> std::convertible_to<bool>;
};

/// 只有 after 的中间件。
template <typename T>
concept AfterMiddleware = requires {
    { std::declval<T>().after(std::declval<Request&>(),
                              std::declval<Response&>()) }
        -> std::convertible_to<bool>;
};

/// 中间件：提供 before 或 after 之一的普通 C++ 类型。
template <typename T>
concept Middleware = BeforeMiddleware<T> || AfterMiddleware<T>;

} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_TYPES_HPP
