/**
 * @file detail/LWeb/Error.hpp
 * @brief LWeb 统一异常层级与 HTTP 错误语义。
 *
 * 分类（Handler 包装层统一映射，见 Traits.hpp）：
 * - HttpException       -> 异常自身携带的 status + code + message（JSON）
 * - JsonParseError      -> 400 INVALID_JSON_BODY
 * - BindingError        -> 400（Param/Query/Header/BodyJson 绑定失败）
 * - std::exception      -> 500 INTERNAL_SERVER_ERROR
 * - unknown             -> 500 INTERNAL_SERVER_ERROR
 */

#ifndef LTOOL_LWEB_DETAIL_ERROR_HPP
#define LTOOL_LWEB_DETAIL_ERROR_HPP

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "pkgs/cinatra/cinatra.hpp"

namespace LWeb {

/// 携带 HTTP 状态码与错误 code 的异常：Handler 中可直接 throw，
/// 由包装层映射为统一 JSON 错误响应
/// {"error":{"code":...,"message":...}}。
class HttpException : public std::exception {
public:
    HttpException(cinatra::status_type status, std::string code,
                  std::string message)
        : status_(status),
          code_(std::move(code)),
          message_(std::move(message)) {}

    cinatra::status_type Status() const noexcept { return status_; }

    const std::string& Code() const noexcept { return code_; }

    const char* what() const noexcept override { return message_.c_str(); }

private:
    cinatra::status_type status_;
    std::string code_;
    std::string message_;
};

/// JSON 请求体解析失败 -> 400。
class JsonParseError : public HttpException {
public:
    explicit JsonParseError(std::string message = "invalid JSON body")
        : HttpException(cinatra::status_type::bad_request, "INVALID_JSON_BODY",
                        std::move(message)) {}
};

/// 请求绑定失败（JSON 结构与目标类型不匹配、参数转换失败等）-> 400。
class BindingError : public HttpException {
public:
    BindingError(std::string code, std::string message)
        : HttpException(cinatra::status_type::bad_request, std::move(code),
                        std::move(message)) {}

    explicit BindingError(std::string message)
        : HttpException(cinatra::status_type::bad_request, "BINDING_ERROR",
                        std::move(message)) {}
};

} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_ERROR_HPP
