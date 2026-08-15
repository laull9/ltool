/**
 * @file detail/LWeb/HttpClient.hpp
 * @brief LWeb HTTP 客户端：cinatra::coro_http_client 的便捷封装。
 *
 * 注意：HttpClient 单实例不宜并发使用（底层 cinatra::coro_http_client
 * 不保证并发安全）。
 */

#ifndef LTOOL_LWEB_DETAIL_HTTPCLIENT_HPP
#define LTOOL_LWEB_DETAIL_HTTPCLIENT_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "detail/LWeb/Types.hpp"

namespace LWeb {

/// 协程 HTTP/HTTPS 客户端（默认使用全局 executor，需 C++20）。
class HttpClient {
public:
    /// 以基础 URL 创建客户端。
    explicit HttpClient(std::string base_url = {})
        : base_url_(std::move(base_url)) {}

    /// 不可拷贝、不可移动（底层 cinatra::coro_http_client 不可移动）。
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /// 设置默认 Bearer Token：之后所有请求自动携带
    /// `Authorization: Bearer <token>`（显式传入的 Authorization 头优先）。
    void SetBearerToken(std::string token) {
        bearer_token_ = std::move(token);
    }

    /// 原始 Cinatra 客户端（逃生通道）。
    cinatra::coro_http_client& Native() { return client_; }

    /// GET 请求。
    Task<ResponseData> Get(std::string_view path, HeaderMap headers = {}) {
        return client_.async_get(FullUrl(path), WithAuth(std::move(headers)));
    }

    /// POST 请求（指定内容类型）。
    Task<ResponseData> Post(std::string_view path, std::string content,
                            cinatra::req_content_type content_type =
                                cinatra::req_content_type::string,
                            HeaderMap headers = {}) {
        return client_.async_post(FullUrl(path), std::move(content),
                                  content_type, WithAuth(std::move(headers)));
    }

    /// PUT 请求。
    Task<ResponseData> Put(std::string_view path, std::string content,
                           cinatra::req_content_type content_type =
                               cinatra::req_content_type::string,
                           HeaderMap headers = {}) {
        return client_.async_put(FullUrl(path), std::move(content),
                                 content_type, WithAuth(std::move(headers)));
    }

    /// DELETE 请求。
    Task<ResponseData> Delete(std::string_view path, std::string content = {},
                              cinatra::req_content_type content_type =
                                  cinatra::req_content_type::string,
                              HeaderMap headers = {}) {
        return client_.async_delete(FullUrl(path), std::move(content),
                                    content_type, WithAuth(std::move(headers)));
    }

    /// PATCH 请求（cinatra 原生不支持 PATCH 带请求体）。
    Task<ResponseData> Patch(std::string_view path, HeaderMap headers = {}) {
        return client_.async_patch(FullUrl(path), WithAuth(std::move(headers)));
    }

    /// POST JSON，返回原始响应。
    template <typename T>
    Task<ResponseData> PostJson(std::string_view path, const T& object,
                                HeaderMap headers = {}) {
        return client_.async_post(FullUrl(path), LWebJsonCodec::Serialize(object),
                                  cinatra::req_content_type::json,
                                  WithAuth(std::move(headers)));
    }

    /// GET 并把响应体反序列化为 T；网络错误或 HTTP 错误状态抛异常。
    template <typename T, typename Codec = LWebJsonCodec>
    Task<T> GetJson(std::string_view path, HeaderMap headers = {}) {
        auto data = co_await client_.async_get(FullUrl(path),
                                               WithAuth(std::move(headers)));
        if (data.net_err) {
            throw std::runtime_error("LWeb HttpClient GET failed: " +
                                     data.net_err.message());
        }
        if (data.status >= 400) {
            throw std::runtime_error("LWeb HttpClient GET failed: HTTP " +
                                     std::to_string(data.status));
        }
        co_return Codec::template Deserialize<T>(data.resp_body);
    }

private:
    /// 注入默认 Bearer Token（显式 Authorization 头优先）。
    HeaderMap WithAuth(HeaderMap headers) const {
        if (!bearer_token_.empty() &&
            headers.find("Authorization") == headers.end()) {
            headers.emplace("Authorization", "Bearer " + bearer_token_);
        }
        return headers;
    }

    /// 拼接基础 URL 与路径，避免重复斜杠。
    std::string FullUrl(std::string_view path) const {
        if (path.empty()) {
            return base_url_;
        }
        if (base_url_.empty()) {
            return std::string(path);
        }
        std::string out = base_url_;
        if (path.front() == '/') {
            if (out.back() == '/') {
                out.pop_back();
            }
            out.append(path);
        }
        else {
            if (out.back() != '/') {
                out.push_back('/');
            }
            out.append(path);
        }
        return out;
    }

    cinatra::coro_http_client client_;
    std::string base_url_;
    std::string bearer_token_;
};

} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_HTTPCLIENT_HPP
