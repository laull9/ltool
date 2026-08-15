/**
 * @file detail/LWeb/Response.hpp
 * @brief LWeb 响应辅助函数与错误协议。
 *
 * 统一 HTTP 错误协议：默认返回 JSON
 *   {"error":{"code":"...","message":"..."}}
 * 只有显式调用 Text()/Ok()/Status() 才返回普通文本。
 * 错误快捷函数（BadRequest/Unauthorized/Forbidden/NotFound/InternalError）
 * 缺省 code 自动生成（BAD_REQUEST / UNAUTHORIZED / FORBIDDEN / NOT_FOUND /
 * INTERNAL_SERVER_ERROR）。
 */

#ifndef LTOOL_LWEB_DETAIL_RESPONSE_HPP
#define LTOOL_LWEB_DETAIL_RESPONSE_HPP

#include <string>
#include <string_view>
#include <utility>

#include "detail/LWeb/Error.hpp"
#include "detail/LWeb/Traits.hpp"

namespace LWeb {

namespace detail {

/// 设置响应 Content-Type。
inline void SetContentType(Response& res, std::string_view type) {
    res.add_header("Content-Type", std::string(type));
}

/// 用 LJson 构造错误响应体 {"error":{"code":...,"message":...}}。
inline std::string BuildErrorBody(std::string_view code,
                                  std::string_view message) {
    LJson::Json body = LJson::Json::object();
    body["error"]["code"] = std::string(code);
    body["error"]["message"] = std::string(message);
    return body.dump();
}

} // namespace detail

/// 以指定状态码与文本内容响应（低层文本写入，仅 Text/Ok/Status 使用）。
inline void Status(Response& res, HttpStatus status, std::string_view content) {
    detail::SetContentType(res, "text/plain; charset=utf-8");
    res.set_status_and_content(status, std::string(content));
}

/// 200 OK（文本）。
inline void Ok(Response& res, std::string_view content) {
    Status(res, HttpStatus::ok, content);
}

/// 200 OK（文本，与 Ok 等价）。
inline void Text(Response& res, std::string_view content) {
    Ok(res, content);
}

/// 204 No Content。
inline void NoContent(Response& res) {
    res.set_status_and_content(HttpStatus::no_content, std::string());
}

/// 200 OK（JSON，codec 可替换；字符串请使用 Ok/Text）。
template <typename Codec = LWebJsonCodec, typename T>
void Json(Response& res, const T& object) {
    auto text = Codec::Serialize(object);
    detail::SetContentType(res, "application/json");
    res.set_status_and_content(HttpStatus::ok, std::move(text));
}

/// 以指定状态码返回 JSON。
template <typename Codec = LWebJsonCodec, typename T>
void Json(Response& res, const T& object, HttpStatus status) {
    auto text = Codec::Serialize(object);
    detail::SetContentType(res, "application/json");
    res.set_status_and_content(status, std::move(text));
}

/// 201 Created（字符串参数按文本，其余按 JSON）。
inline void Created(Response& res, std::string_view content) {
    Status(res, HttpStatus::created, content);
}

/// 201 Created（JSON）。
template <typename Codec = LWebJsonCodec, typename T>
    requires(!std::convertible_to<T, std::string_view>)
void Created(Response& res, const T& object) {
    Json<Codec>(res, object, HttpStatus::created);
}

/// 统一错误响应：{"error":{"code":...,"message":...}}。
inline void Error(Response& res, HttpStatus status, std::string_view code,
                  std::string_view message) {
    detail::SetContentType(res, "application/json");
    res.set_status_and_content(status,
                               detail::BuildErrorBody(code, message));
}

// ---- 错误快捷函数：默认 JSON 协议（缺省 code 自动生成）----

/// 400 Bad Request（JSON，code 自动为 BAD_REQUEST）。
inline void BadRequest(Response& res, std::string_view message) {
    Error(res, HttpStatus::bad_request, "BAD_REQUEST", message);
}

/// 400 Bad Request（显式 code）。
inline void BadRequest(Response& res, std::string_view code,
                       std::string_view message) {
    Error(res, HttpStatus::bad_request, code, message);
}

/// 401 Unauthorized（JSON，code 自动为 UNAUTHORIZED）。
inline void Unauthorized(Response& res, std::string_view message) {
    Error(res, HttpStatus::unauthorized, "UNAUTHORIZED", message);
}

/// 401 Unauthorized（显式 code）。
inline void Unauthorized(Response& res, std::string_view code,
                         std::string_view message) {
    Error(res, HttpStatus::unauthorized, code, message);
}

/// 403 Forbidden（JSON，code 自动为 FORBIDDEN）。
inline void Forbidden(Response& res, std::string_view message) {
    Error(res, HttpStatus::forbidden, "FORBIDDEN", message);
}

/// 403 Forbidden（显式 code）。
inline void Forbidden(Response& res, std::string_view code,
                      std::string_view message) {
    Error(res, HttpStatus::forbidden, code, message);
}

/// 404 NotFound（JSON，code 自动为 NOT_FOUND）。
inline void NotFound(Response& res, std::string_view message) {
    Error(res, HttpStatus::not_found, "NOT_FOUND", message);
}

/// 404 NotFound（显式 code）。
inline void NotFound(Response& res, std::string_view code,
                     std::string_view message) {
    Error(res, HttpStatus::not_found, code, message);
}

/// 500 Internal Server Error（JSON）。
inline void InternalError(Response& res, std::string_view message) {
    Error(res, HttpStatus::internal_server_error, "INTERNAL_SERVER_ERROR",
          message);
}

// ---- 异常 -> 统一错误响应 ----

namespace detail {

/// 把 HttpException 映射为 JSON 错误响应（status = 异常携带的状态码）。
inline void WriteErrorResponse(Response& res, const HttpException& e) {
    Error(res, e.Status(), e.Code(), e.what());
}

/// 内部错误（std::exception / unknown）-> 500 JSON。
inline void WriteInternalError(Response& res, std::string_view message) {
    InternalError(res, message);
}

} // namespace detail

// ---- 返回值式 Handler 的结果类型映射（类型定义见 Types.hpp） ----

namespace detail {

/// 把 Handler 的返回值写入响应：结果类型映射状态码，普通类型走 JSON。
inline void WriteResult(Response& res, Result::NoContent) {
    NoContent(res);
}

/// 404：code 缺省自动生成 NOT_FOUND。
inline void WriteResult(Response& res, Result::NotFound not_found) {
    Error(res, HttpStatus::not_found,
          not_found.code.empty() ? "NOT_FOUND" : not_found.code,
          not_found.message.empty() ? "not found" : not_found.message);
}

/// 400：code 缺省自动生成 BAD_REQUEST。
inline void WriteResult(Response& res, Result::BadRequest bad_request) {
    Error(res, HttpStatus::bad_request,
          bad_request.code.empty() ? "BAD_REQUEST" : bad_request.code,
          bad_request.message.empty() ? "bad request" : bad_request.message);
}

/// 401：code 缺省自动生成 UNAUTHORIZED。
inline void WriteResult(Response& res, Result::Unauthorized unauthorized) {
    Error(res, HttpStatus::unauthorized,
          unauthorized.code.empty() ? "UNAUTHORIZED" : unauthorized.code,
          unauthorized.message.empty() ? "unauthorized" : unauthorized.message);
}

/// 403：code 缺省自动生成 FORBIDDEN。
inline void WriteResult(Response& res, Result::Forbidden forbidden) {
    Error(res, HttpStatus::forbidden,
          forbidden.code.empty() ? "FORBIDDEN" : forbidden.code,
          forbidden.message.empty() ? "forbidden" : forbidden.message);
}

/// 409：code 缺省自动生成 CONFLICT。
inline void WriteResult(Response& res, Result::Conflict conflict) {
    Error(res, HttpStatus::conflict,
          conflict.code.empty() ? "CONFLICT" : conflict.code,
          conflict.message.empty() ? "conflict" : conflict.message);
}

/// 201 Created：负载走 JSON。
template <typename T>
void WriteResult(Response& res, Result::Created<T> created) {
    Json(res, created.value, HttpStatus::created);
}

/// std::variant（如 Result::Variant<T>）按实际持有类型转发。
template <typename... Ts>
void WriteResult(Response& res, std::variant<Ts...> variant) {
    std::visit(
        [&res](auto&& value) {
            WriteResult(res, std::forward<decltype(value)>(value));
        },
        std::move(variant));
}

/// 其他类型：文本按 200 文本，其余按 200 JSON 输出。
template <typename T>
void WriteResult(Response& res, T&& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (is_text_v<U>) {
        Ok(res, std::string_view(value));
    }
    else {
        Json(res, std::forward<T>(value));
    }
}

} // namespace detail

} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_RESPONSE_HPP
