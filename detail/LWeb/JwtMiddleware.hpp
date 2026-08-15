/**
 * @file detail/LWeb/JwtMiddleware.hpp
 * @brief LWeb JWT 认证中间件：JwtAuth / RequireClaim / RequireScope，
 *        以及 Claims 的请求级存取（SetJwtClaims / GetJwtClaims /
 *        JwtClaimsOf）。
 *
 * Claims 请求级存储：复用 cinatra 原生 aspect_data（vector<string>&，
 * 请求级、clear() 自动重置、无全局状态）。JwtAuth 验证成功后把 claims
 * JSON 写入该通道；Handler / 后续中间件经 JwtClaimsOf/GetJwtClaims 读取。
 * 约定：使用 JwtAuth 的请求上，aspect_data 视为 LWeb JWT 专用通道。
 *
 * 线程安全：JwtAuth 无请求级状态（只持 const Jwt*），可多请求共享。
 *
 * 错误语义：认证失败一律 401（AUTH_REQUIRED / INVALID_TOKEN /
 * TOKEN_EXPIRED / ...）；认证成功但权限不足 403（FORBIDDEN）。
 */

#ifndef LTOOL_LWEB_DETAIL_JWTMIDDLEWARE_HPP
#define LTOOL_LWEB_DETAIL_JWTMIDDLEWARE_HPP

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "detail/LWeb/Jwt.hpp"
#include "detail/LWeb/Response.hpp"

namespace LWeb {

/// 把已验证的 Claims 绑定到请求（写入 cinatra 请求级 aspect_data）。
/// 同请求只应有一个 JwtAuth；aspect_data 视为 LWeb JWT 专用通道。
inline void SetJwtClaims(Request& req, JwtClaims claims) {
    req.set_aspect_data(
        std::vector<std::string>{claims.ToJson().dump()});
}

/// 读取请求上的 JWT Claims；未认证返回 nullopt。
inline std::optional<JwtClaims> GetJwtClaims(Request& req) {
    auto& data = req.get_aspect_data();
    if (data.empty()) {
        return std::nullopt;
    }
    try {
        // 固定读取槽位 0（SetJwtClaims 以单条目向量整体替换 aspect_data）。
        return JwtClaims::FromJson(LJson::Json::parse(data.front()));
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

/// 便捷读取：认证后直接使用；未认证返回空 Claims（与 Header() 语义一致，
/// 不会抛异常）。通常应在 JwtAuth 之后调用。
inline JwtClaims JwtClaimsOf(Request& req) {
    auto claims = GetJwtClaims(req);
    return claims ? std::move(*claims) : JwtClaims{};
}

namespace middleware {

namespace detail {

/// ASCII 大小写不敏感的前缀比较（RFC 7235：认证 scheme 大小写不敏感）。
inline bool IEqualsPrefix(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        auto a = static_cast<unsigned char>(text[i]);
        auto b = static_cast<unsigned char>(prefix[i]);
        if (a >= 'A' && a <= 'Z') {
            a += 32;
        }
        if (b >= 'A' && b <= 'Z') {
            b += 32;
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

} // namespace detail

/// JWT 认证中间件：从 Authorization: Bearer 提取并验证 JWT，成功后将
/// Claims 写入请求级 aspect_data。
///
/// 用法（三种构造）：
/// @code
/// // 1) 全局默认 JWT（先 LWeb::SetDefaultJwt(...) 配置一次）
/// app.Get("/profile", handler, LWeb::middleware::JwtAuth{});
/// // 2) 显式实例（jwt 须活得比服务器久）
/// app.Get("/profile", handler, LWeb::middleware::JwtAuth{jwt});
/// // 3) 安全持有（shared_ptr 保证生命周期）
/// app.Get("/profile", handler, LWeb::middleware::JwtAuth{shared_jwt});
/// @endcode
///
/// 生命周期：
/// - 持有 const Jwt& 的形式要求 jwt 活得比服务器久（与 RouterGroup 持有
///   server 指针的约定一致）；
/// - 需要安全持有（防悬垂）时请传 std::shared_ptr<const Jwt>；
/// - JwtAuth{} 缺省构造要求全局默认 JWT 已设置（否则注册期即抛异常）。
/// 可选 extra_verify 回调（如撤销检查），返回 false 视为 INVALID_TOKEN。
class JwtAuth {
public:
    /// 缺省构造：使用全局默认 JWT（须先 LWeb::SetDefaultJwt，否则抛异常）。
    JwtAuth() : owned_jwt_(LWeb::DefaultJwtShared()) {
        jwt_ = owned_jwt_ ? owned_jwt_.get() : nullptr;
        if (!jwt_) {
            throw std::logic_error(
                "LWeb: JwtAuth{} 需要全局默认 JWT，请先调用 "
                "LWeb::SetDefaultJwt(...)");
        }
    }

    /// 生命周期契约：jwt 必须活得比服务器久。
    explicit JwtAuth(const Jwt& jwt) : jwt_(&jwt) {}

    /// 安全持有：由 shared_ptr 保证 jwt 生命周期。
    explicit JwtAuth(std::shared_ptr<const Jwt> jwt)
        : owned_jwt_(std::move(jwt)), jwt_(owned_jwt_.get()) {
        if (!jwt_) {
            throw std::invalid_argument("LWeb: JwtAuth requires a non-null Jwt");
        }
    }

    JwtAuth(const Jwt& jwt, std::function<bool(const JwtClaims&)> extra_verify)
        : jwt_(&jwt), extra_verify_(std::move(extra_verify)) {}

    JwtAuth(std::shared_ptr<const Jwt> jwt,
            std::function<bool(const JwtClaims&)> extra_verify)
        : owned_jwt_(std::move(jwt)),
          jwt_(owned_jwt_ ? owned_jwt_.get() : nullptr),
          extra_verify_(std::move(extra_verify)) {
        if (!jwt_) {
            throw std::invalid_argument("LWeb: JwtAuth requires a non-null Jwt");
        }
    }

    bool before(Request& req, Response& res) const {
        auto auth = req.get_header_value("Authorization");
        if (auth.size() < 8 ||
            !detail::IEqualsPrefix(auth.substr(0, 7), "Bearer ")) {
            LWeb::Unauthorized(res, "AUTH_REQUIRED",
                               "missing bearer token");
            return false;
        }
        auto result = jwt_->Verify(auth.substr(7));
        if (!result.ok()) {
            LWeb::Unauthorized(res, result.ErrorCode(),
                               result.Message());
            return false;
        }
        if (extra_verify_ && !extra_verify_(*result.claims)) {
            LWeb::Unauthorized(res, "INVALID_TOKEN",
                               "token rejected by verifier");
            return false;
        }
        SetJwtClaims(req, std::move(*result.claims));
        return true;
    }

private:
    std::shared_ptr<const Jwt> owned_jwt_;
    const Jwt* jwt_;
    std::function<bool(const JwtClaims&)> extra_verify_;
};

/// 要求指定 claim 等于给定值；认证失败 401，权限不足 403。
/// 典型用法（配合 JwtAuth）：
/// @code
/// r.Delete("/:id", handler, JwtAuth{jwt}, RequireClaim{"role", "admin"});
/// @endcode
struct RequireClaim {
    std::string name;
    std::string value;

    bool before(Request& req, Response& res) const {
        auto claims = GetJwtClaims(req);
        if (!claims) {
            LWeb::Unauthorized(res, "AUTH_REQUIRED",
                               "authentication required");
            return false;
        }
        auto got = claims->Get<std::string>(name);
        if (!got || *got != value) {
            LWeb::Forbidden(res, "FORBIDDEN",
                            "insufficient permission");
            return false;
        }
        return true;
    }
};

/// 要求 scope claim 包含全部给定 scope（AND 语义）。scope 支持
/// OAuth 风格空格分隔字符串（"users.read users.write"）或 JSON 数组。
/// 用法：RequireScope{"users.read"} 或 RequireScope{"users.read", "users.write"}。
struct RequireScope {
    std::vector<std::string> required;

    RequireScope() = default;

    template <typename... Scopes>
        requires(std::convertible_to<Scopes, std::string> && ...)
    RequireScope(Scopes&&... scopes) {
        (required.push_back(std::string(std::forward<Scopes>(scopes))), ...);
    }

    bool before(Request& req, Response& res) const {
        auto claims = GetJwtClaims(req);
        if (!claims) {
            LWeb::Unauthorized(res, "AUTH_REQUIRED",
                               "authentication required");
            return false;
        }

        std::set<std::string> granted;
        if (auto scope = claims->Get<std::string>("scope")) {
            std::string_view sv(*scope);
            std::size_t pos = 0;
            while (pos <= sv.size()) {
                auto end = sv.find(' ', pos);
                if (end == std::string_view::npos) {
                    end = sv.size();
                }
                if (end > pos) {
                    granted.emplace(sv.substr(pos, end - pos));
                }
                pos = end + 1;
            }
        }
        else if (claims->Contains("scope")) {
            const auto scopes = claims->payload["scope"];
            if (scopes.is_array()) {
                const auto n = scopes.size();
                for (std::size_t i = 0; i < n; ++i) {
                    // 非字符串元素视为不可授予（跳过，不抛异常——权限
                    // 判断失败必须 403 而非 500）。
                    if (scopes[i].is_string()) {
                        granted.emplace(
                            scopes[i].get_as<std::string>());
                    }
                }
            }
        }

        for (const auto& scope : required) {
            if (granted.find(scope) == granted.end()) {
                LWeb::Forbidden(res, "FORBIDDEN",
                                "insufficient permission");
                return false;
            }
        }
        return true;
    }
};

} // namespace middleware
} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_JWTMIDDLEWARE_HPP
