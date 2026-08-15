/**
 * @file detail/LWeb/Middleware.hpp
 * @brief LWeb 内置中间件（LWeb::middleware）。
 *
 * 线程安全契约：
 * - 中间件实例在路由注册时拷贝进 Cinatra handler lambda，多请求共享；
 * - 本文件所有内置中间件均无请求级 mutable 成员，可安全并发调用；
 * - 请求级数据走 cinatra 原生 req.set_user_data(std::any)（请求结束自动
 *   重置），并使用专用标记类型隔离单槽位，避免与其他中间件类型冲突。
 *
 * 执行契约（Cinatra 原生行为）：
 * - after 无条件执行（即使 before 中断 / handler 未执行）；
 * - 中间件不得抛异常（LWeb 与 Cinatra 均无法兜底）。
 */

#ifndef LTOOL_LWEB_DETAIL_MIDDLEWARE_HPP
#define LTOOL_LWEB_DETAIL_MIDDLEWARE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "detail/LWeb/Request.hpp"
#include "detail/LWeb/Response.hpp"

namespace LWeb {
namespace middleware {

/// 访问日志：method / path / status / latency。
/// 计时经请求级 user_data 传递（专用标记类型 Mark），绝不存成员变量。
struct AccessLog {
    using Clock = std::chrono::steady_clock;

    /// 专用标记类型：隔离 user_data 单槽位。
    struct Mark {
        Clock::time_point t;
    };

    bool before(Request& req, Response&) {
        req.set_user_data(Mark{Clock::now()});
        return true;
    }

    bool after(Request& req, Response& res) {
        long long ms = 0;
        if (auto ud = req.get_user_data(); ud.has_value()) {
            // 指针式 any_cast：类型冲突/缺失安全降级为 0ms，不抛异常。
            if (auto* mark = std::any_cast<Mark>(&ud)) {
                ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         Clock::now() - mark->t)
                         .count();
            }
        }
        std::string line =
            std::string(req.get_method()) + " " + std::string(req.get_url()) +
            " -> " + std::to_string(static_cast<int>(res.status())) + " " +
            std::to_string(ms) + "ms\n";
        std::fputs(line.c_str(), stderr);
        return true;
    }
};

/// CORS：配置即成员，无请求状态；OPTIONS 预检短路。
/// 注意：中间件仅在路由匹配时执行，预检短路要求该路由注册了 OPTIONS
/// 或使用 Any（GET 路由不会收到 OPTIONS 请求）。
struct Cors {
    std::string allow_origin = "*";
    std::string allow_methods = "GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS";
    std::string allow_headers = "Content-Type, Authorization, X-Request-ID";
    bool allow_credentials = false;

    static Cors AllowAll() { return {}; }

    bool before(Request& req, Response& res) {
        res.add_header("Access-Control-Allow-Origin", allow_origin);
        res.add_header("Access-Control-Allow-Methods", allow_methods);
        res.add_header("Access-Control-Allow-Headers", allow_headers);
        if (allow_credentials) {
            res.add_header("Access-Control-Allow-Credentials", "true");
        }
        if (req.get_method() == "OPTIONS") {
            res.set_status_and_content(HttpStatus::ok, std::string());
            return false;  // 预检短路：跳过 Handler；after 链仍会执行
        }
        return true;
    }
};

/// Bearer Token 认证：token 验证逻辑由用户注入（std::function，可拷贝）。
/// 仅做轻量检查；复杂身份解析请放到 Handler。
struct BearerAuth {
    std::function<bool(std::string_view)> verify;
    std::string realm = "api";

    bool before(Request& req, Response& res) {
        auto auth = req.get_header_value("Authorization");
        if (auth.size() < 8 || auth.substr(0, 7) != "Bearer ") {
            Unauthorized(res, "missing bearer token");
            return false;
        }
        auto token = auth.substr(7);
        if (verify && !verify(token)) {
            Unauthorized(res, "invalid bearer token");
            return false;
        }
        return true;
    }
};

/// 请求 ID：缺省时生成（原子计数 + 时间戳，线程安全），写入响应头。
/// 注意：响应侧专用；请求/追踪用途请见 v0.3 RequestContext 规划。
struct RequestId {
    std::string header_name = "X-Request-ID";

    bool before(Request& req, Response& res) {
        if (req.get_header_value(header_name).empty()) {
            res.add_header(header_name, GenerateId());
        }
        return true;
    }

private:
    static std::string GenerateId() {
        static std::atomic<std::uint64_t> counter{0};
        auto n = counter.fetch_add(1, std::memory_order_relaxed);
        auto t = std::chrono::steady_clock::now().time_since_epoch().count();
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%llx-%llx",
                      static_cast<unsigned long long>(t),
                      static_cast<unsigned long long>(n));
        return buf;
    }
};

/// 安全响应头：X-Content-Type-Options / X-Frame-Options / Referrer-Policy。
struct SecurityHeaders {
    std::string x_content_type_options = "nosniff";
    std::string x_frame_options = "DENY";
    std::string referrer_policy = "no-referrer";

    bool before(Request&, Response& res) {
        res.add_header("X-Content-Type-Options", x_content_type_options);
        res.add_header("X-Frame-Options", x_frame_options);
        res.add_header("Referrer-Policy", referrer_policy);
        return true;
    }
};

/// 固定窗口限流器（扩展模块，非 Core）：跨请求计数需显式共享实例。
struct RateLimiter {
    std::mutex mu;
    std::size_t limit = 100;
    std::chrono::milliseconds window{60000};
    std::size_t count = 0;
    std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();

    bool Allow() {
        std::lock_guard<std::mutex> lk(mu);
        auto now = std::chrono::steady_clock::now();
        if (now - start >= window) {
            count = 0;
            start = now;
        }
        if (count >= limit) {
            return false;
        }
        ++count;
        return true;
    }
};

/// 限流中间件：共享 RateLimiter 提供跨请求计数。
/// 未配置限流器（nullptr）时放行，避免空指针解引用。
struct RateLimit {
    std::shared_ptr<RateLimiter> limiter;

    bool before(Request&, Response& res) {
        if (!limiter) {
            return true;
        }
        if (!limiter->Allow()) {
            res.set_status_and_content(HttpStatus::too_many_requests,
                                       "rate limit exceeded");
            return false;
        }
        return true;
    }
};

} // namespace middleware
} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_MIDDLEWARE_HPP
