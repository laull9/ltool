#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "LWeb.hpp"

namespace {

/// 每 TEST_CASE 一个独立应用：临时端口 + 启动/停止的 RAII 辅助。
struct TestApp {
    LWeb::Application app;
    std::string base;
    std::optional<async_simple::Future<std::error_code>> future;

    TestApp() : app(LWeb::ApplicationOptions{.port = 0, .threads = 2}) {
        future.emplace(app.Start());
        REQUIRE(app.port() != 0);
        base = "http://127.0.0.1:" + std::to_string(app.port());
    }

    ~TestApp() {
        app.Stop();
        future->wait();
    }
};

/// 同步发起 GET 并返回响应数据。
LWeb::ResponseData Get(LWeb::HttpClient& client, std::string_view path) {
    return async_simple::coro::syncAwait(client.Get(path));
}

/// 同步发起 GET（带请求头）。
LWeb::ResponseData Get(LWeb::HttpClient& client, std::string_view path,
                       LWeb::HeaderMap headers) {
    return async_simple::coro::syncAwait(
        client.Get(path, std::move(headers)));
}

/// 解析响应体为 LJson DOM。
LJson::Json BodyJson(const LWeb::ResponseData& data) {
    return LJson::Json::parse(std::string(data.resp_body));
}

} // namespace

// ============================================================================
// JWT Core
// ============================================================================

TEST_CASE("JWT HS256：jwt.io 黄金向量验证通过", "[LWeb][jwt]") {
    // https://jwt.io 的经典 HS256 示例（外部已知正确向量）。
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "your-256-bit-secret"}};
    const char* golden =
        "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
        "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2"
        "MjM5MDIyfQ."
        "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c";

    auto result = jwt.Verify(golden);
    REQUIRE(result.ok());
    REQUIRE(result.claims.has_value());
    REQUIRE(result.claims->subject == "1234567890");
    REQUIRE(result.claims->Get<std::string>("name").value() == "John Doe");
}

TEST_CASE("JWT HS256：错误 secret 拒绝", "[LWeb][jwt]") {
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "your-256-bit-secret"}};
    const char* golden =
        "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
        "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2"
        "MjM5MDIyfQ."
        "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c";

    LWeb::Jwt wrong{{.algorithm = LWeb::JwtAlgorithm::HS256,
                     .secret = "wrong-secret"}};
    auto result = wrong.Verify(golden);
    REQUIRE(!result.ok());
    REQUIRE(std::string(result.ErrorCode()) == "INVALID_TOKEN");
}

TEST_CASE("JWT Base64URL 编解码往返", "[LWeb][jwt]") {
    const std::string samples[] = {"", "a", "ab", "abc", "abcd",
                                   "hello world", "1234567890"};
    for (const auto& s : samples) {
        auto encoded = LWeb::detail::Base64UrlEncode(s);
        auto decoded = LWeb::detail::Base64UrlDecode(encoded);
        REQUIRE(decoded == s);
        // 字符集只含 URL 安全字符
        REQUIRE(encoded.find_first_not_of(
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                    "0123456789-_") == std::string::npos);
    }
    // 非法字符拒绝
    REQUIRE_THROWS(LWeb::detail::Base64UrlDecode("a b!"));
}

TEST_CASE("JWT Encode/Verify 往返与自定义 claims", "[LWeb][jwt]") {
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "test-secret"}};

    LWeb::JwtClaims claims;
    claims.subject = "user-123";
    claims.issuer = "my-app";
    claims.audience = {"api"};
    claims.jwt_id = "jti-1";
    claims.expires_at = LWeb::JwtTimePoint::clock::now() +
                        std::chrono::minutes(30);
    claims.Set("role", "admin");
    claims.Set("user_id", 42);

    auto token = jwt.Encode(claims);
    REQUIRE(token.find('.') != std::string::npos);

    auto result = jwt.Verify(token);
    REQUIRE(result.ok());
    REQUIRE(result.claims->Subject() == "user-123");
    REQUIRE(result.claims->issuer == "my-app");
    REQUIRE(result.claims->Get<std::string>("role").value() == "admin");
    REQUIRE(result.claims->Get<int>("user_id").value() == 42);
    REQUIRE(result.claims->Contains("role"));
    REQUIRE(!result.claims->Contains("missing"));

    // 篡改 token -> 验签失败
    std::string tampered = token;
    // 修改签名中间字符（所有位有效）：末字符的低位可能是 padding 位，
    // 翻转后解码字节可能不变，导致验签仍通过。
    const auto last_dot = tampered.rfind('.');
    tampered[last_dot + 5] = (tampered[last_dot + 5] == 'A') ? 'B' : 'A';
    auto bad = jwt.Verify(tampered);
    REQUIRE(!bad.ok());
    REQUIRE(std::string(bad.ErrorCode()) == "INVALID_TOKEN");
}

TEST_CASE("JWT 算法混淆防护：alg 必须等于服务器配置", "[LWeb][jwt]") {
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "test-secret"}};

    // alg=none 直接拒绝
    auto h = LWeb::detail::Base64UrlEncode(R"({"alg":"none","typ":"JWT"})");
    auto p = LWeb::detail::Base64UrlEncode(R"({"sub":"x"})");
    auto none_token = std::string(h) + "." + std::string(p) + ".";
    auto r_none = jwt.Verify(none_token);
    REQUIRE(!r_none.ok());
    REQUIRE(std::string(r_none.ErrorCode()) == "INVALID_TOKEN");

    // alg=RS256（与服务器配置不符）拒绝
    auto h2 = LWeb::detail::Base64UrlEncode(R"({"alg":"RS256","typ":"JWT"})");
    auto rs_token = std::string(h2) + "." + std::string(p) + ".sig";
    auto r_rs = jwt.Verify(rs_token);
    REQUIRE(!r_rs.ok());
    REQUIRE(std::string(r_rs.ErrorCode()) == "INVALID_TOKEN");
}

TEST_CASE("JWT exp/nbf 验证与 clock skew", "[LWeb][jwt]") {
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "test-secret"}};

    // 过期
    LWeb::JwtClaims expired;
    expired.subject = "u";
    expired.expires_at = LWeb::JwtTimePoint::clock::now() -
                         std::chrono::seconds(60);
    auto r = jwt.Verify(jwt.Encode(expired));
    REQUIRE(!r.ok());
    REQUIRE(std::string(r.ErrorCode()) == "TOKEN_EXPIRED");

    // 未生效（nbf 在未来）
    LWeb::JwtClaims not_active;
    not_active.subject = "u";
    not_active.not_before = LWeb::JwtTimePoint::clock::now() +
                            std::chrono::seconds(60);
    auto r2 = jwt.Verify(jwt.Encode(not_active));
    REQUIRE(!r2.ok());
    REQUIRE(std::string(r2.ErrorCode()) == "TOKEN_NOT_ACTIVE");

    // clock skew：过期 60s 但容差 120s -> 通过
    LWeb::Jwt lenient{{.algorithm = LWeb::JwtAlgorithm::HS256,
                       .secret = "test-secret",
                       .clock_skew = std::chrono::seconds(120)}};
    auto r3 = lenient.Verify(jwt.Encode(expired));
    REQUIRE(r3.ok());
}

TEST_CASE("JWT iss/aud 可选验证", "[LWeb][jwt]") {
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "test-secret",
                   .issuer = "my-app",
                   .audience = "api"}};

    // issuer 不匹配
    LWeb::JwtClaims wrong_iss;
    wrong_iss.issuer = "other-app";
    wrong_iss.audience = {"api"};
    auto r1 = jwt.Verify(jwt.Encode(wrong_iss));
    REQUIRE(!r1.ok());
    REQUIRE(std::string(r1.ErrorCode()) == "INVALID_TOKEN_ISSUER");

    // audience 缺失
    LWeb::JwtClaims no_aud;
    no_aud.issuer = "my-app";
    auto r2 = jwt.Verify(jwt.Encode(no_aud));
    REQUIRE(!r2.ok());
    REQUIRE(std::string(r2.ErrorCode()) == "INVALID_TOKEN_AUDIENCE");

    // 全部匹配
    LWeb::JwtClaims ok_c;
    ok_c.issuer = "my-app";
    ok_c.audience = {"api"};
    auto r3 = jwt.Verify(jwt.Encode(ok_c));
    REQUIRE(r3.ok());
}

TEST_CASE("JWT DecodeUnsafe 不验签；CreateToken 便捷生成", "[LWeb][jwt]") {
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "test-secret"}};

    auto token = jwt.CreateToken("u-1", std::chrono::hours(2));
    auto result = jwt.Verify(token);
    REQUIRE(result.ok());
    REQUIRE(result.claims->Subject() == "u-1");

    // DecodeUnsafe：解析不验签（篡改签名后仍可解析出 claims）
    std::string tampered = token;
    // 修改签名中间字符（所有位有效）：末字符的低位可能是 padding 位，
    // 翻转后解码字节可能不变，导致验签仍通过。
    const auto last_dot = tampered.rfind('.');
    tampered[last_dot + 5] = (tampered[last_dot + 5] == 'A') ? 'B' : 'A';
    auto claims = jwt.DecodeUnsafe(tampered);
    REQUIRE(claims.Subject() == "u-1");
    REQUIRE(!jwt.Verify(tampered).ok());
}

// ============================================================================
// JWT 中间件
// ============================================================================

TEST_CASE("JwtAuth 中间件：认证与错误 code 映射", "[LWeb][jwt][middleware]") {
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "test-secret",
                   .issuer = "app",
                   .audience = "api"}};
    TestApp server;

    auto api = server.app.Group("/api")
                   .Use(LWeb::middleware::JwtAuth{jwt});
    api.Get("/me", [](LWeb::Request& req, LWeb::Response& res) {
        const auto claims = LWeb::JwtClaimsOf(req);
        LWeb::Ok(res, std::string(claims.Subject()));
    });
    api.Get("/admin", [](LWeb::Request&, LWeb::Response& res) {
                         LWeb::Ok(res, "admin-ok");
                     },
             LWeb::middleware::RequireClaim{"role", "admin"});
    api.Get("/scope", [](LWeb::Request&, LWeb::Response& res) {
                          LWeb::Ok(res, "scope-ok");
                      },
             LWeb::middleware::RequireScope{"users.read"});

    LWeb::HttpClient client{server.base};

    // 1. 无 Authorization -> 401 AUTH_REQUIRED
    auto no_auth = Get(client, "/api/me");
    REQUIRE(no_auth.status == 401);
    REQUIRE(BodyJson(no_auth)["error"]["code"] == "AUTH_REQUIRED");

    // 2. 格式错误 -> 401 AUTH_REQUIRED
    auto bad_scheme = Get(client, "/api/me",
                          LWeb::HeaderMap{{"Authorization", "Basic xyz"}});
    REQUIRE(bad_scheme.status == 401);
    REQUIRE(BodyJson(bad_scheme)["error"]["code"] == "AUTH_REQUIRED");

    // 3. 垃圾 token -> 401 INVALID_TOKEN
    auto garbage = Get(client, "/api/me",
                       LWeb::HeaderMap{{"Authorization", "Bearer garbage"}});
    REQUIRE(garbage.status == 401);
    REQUIRE(BodyJson(garbage)["error"]["code"] == "INVALID_TOKEN");

    // 4. 过期 token -> 401 TOKEN_EXPIRED
    LWeb::JwtClaims expired;
    expired.subject = "u1";
    expired.issuer = "app";
    expired.audience = {"api"};
    expired.expires_at = LWeb::JwtTimePoint::clock::now() -
                         std::chrono::seconds(5);
    auto expired_token = jwt.Encode(expired);
    auto expired_res = Get(client, "/api/me",
                           LWeb::HeaderMap{{"Authorization",
                                            "Bearer " + expired_token}});
    REQUIRE(expired_res.status == 401);
    REQUIRE(BodyJson(expired_res)["error"]["code"] == "TOKEN_EXPIRED");

    // 5. 有效 token -> 200，claims 可读
    LWeb::JwtClaims claims;
    claims.subject = "u1";
    claims.issuer = "app";
    claims.audience = {"api"};
    claims.Set("role", "admin");
    claims.Set("scope", "users.read users.write");
    auto token = jwt.Encode(claims);

    auto me = Get(client, "/api/me",
                  LWeb::HeaderMap{{"Authorization", "Bearer " + token}});
    REQUIRE(me.status == 200);
    REQUIRE(me.resp_body == "u1");

    // 6. RequireClaim：通过 -> 200；未含 role -> 403
    auto admin_ok = Get(client, "/api/admin",
                        LWeb::HeaderMap{{"Authorization",
                                         "Bearer " + token}});
    REQUIRE(admin_ok.status == 200);
    REQUIRE(admin_ok.resp_body == "admin-ok");

    LWeb::JwtClaims no_role;
    no_role.subject = "u2";
    no_role.issuer = "app";
    no_role.audience = {"api"};
    auto no_role_token = jwt.Encode(no_role);
    auto admin_no = Get(client, "/api/admin",
                        LWeb::HeaderMap{{"Authorization",
                                         "Bearer " + no_role_token}});
    REQUIRE(admin_no.status == 403);
    REQUIRE(BodyJson(admin_no)["error"]["code"] == "FORBIDDEN");

    // 7. RequireScope：含全部 scope -> 200；缺失 -> 403
    auto scope_ok = Get(client, "/api/scope",
                        LWeb::HeaderMap{{"Authorization",
                                         "Bearer " + token}});
    REQUIRE(scope_ok.status == 200);

    LWeb::JwtClaims other_scope;
    other_scope.subject = "u3";
    other_scope.issuer = "app";
    other_scope.audience = {"api"};
    other_scope.Set("scope", "orders.read");
    auto other_scope_token = jwt.Encode(other_scope);
    auto scope_no = Get(client, "/api/scope",
                        LWeb::HeaderMap{{"Authorization",
                                         "Bearer " + other_scope_token}});
    REQUIRE(scope_no.status == 403);
}

TEST_CASE("JwtAuth 可组合 Group 中间件（Controller 内 Use）", "[LWeb][jwt]") {
    LWeb::Jwt jwt{{.algorithm = LWeb::JwtAlgorithm::HS256,
                   .secret = "test-secret"}};
    TestApp server;

    // Controller：Register 内通过 r.Group().Use(JwtAuth) 声明保护。
    class SecuredController {
    public:
        explicit SecuredController(const LWeb::Jwt& jwt) : jwt_(jwt) {}

        void Register(LWeb::Router& root) {
            auto r = root.Group("/secure").Use(
                LWeb::middleware::JwtAuth{jwt_});
            r.Get("/hello", [](LWeb::Request& req, LWeb::Response& res) {
                const auto claims = LWeb::JwtClaimsOf(req);
                LWeb::Ok(res, std::string(claims.Subject()));
            });
        }

    private:
        const LWeb::Jwt& jwt_;
    };

    server.app.AddController<SecuredController>(jwt);

    LWeb::HttpClient client{server.base};

    auto no_auth = Get(client, "/secure/hello");
    REQUIRE(no_auth.status == 401);

    auto token = jwt.CreateToken("u9", std::chrono::hours(1));
    auto ok = Get(client, "/secure/hello",
                  LWeb::HeaderMap{{"Authorization", "Bearer " + token}});
    REQUIRE(ok.status == 200);
    REQUIRE(ok.resp_body == "u9");
}

TEST_CASE("JwtAuth 安全持有与加固项", "[LWeb][jwt][middleware]") {
    auto jwt = std::make_shared<LWeb::Jwt>(
        LWeb::JwtOptions{.algorithm = LWeb::JwtAlgorithm::HS256,
                         .secret = "test-secret"});
    TestApp server;

    // 先注册全部路由，再发请求。
    auto api = server.app.Group("/api").Use(
        LWeb::middleware::JwtAuth{jwt}); // shared_ptr 安全持有
    api.Get("/me", [](LWeb::Request& req, LWeb::Response& res) {
        const auto claims = LWeb::JwtClaimsOf(req);
        LWeb::Ok(res, std::string(claims.Subject()));
    });

    // extra_verify 撤销检查：返回 false -> 401 INVALID_TOKEN
    auto revoked = std::make_shared<LWeb::Jwt>(
        LWeb::JwtOptions{.secret = "test-secret"});
    auto api2 = server.app.Group("/v2").Use(
        LWeb::middleware::JwtAuth{revoked,
                                  [](const LWeb::JwtClaims&) { return false; }});
    api2.Get("/x", [](LWeb::Request&, LWeb::Response& res) {
        LWeb::Ok(res, "x");
    });

    LWeb::HttpClient client{server.base};
    auto token = jwt->CreateToken("s1", std::chrono::hours(1));

    // Bearer scheme 大小写不敏感（RFC 7235）
    auto lower = Get(client, "/api/me",
                     LWeb::HeaderMap{{"Authorization",
                                      "bearer " + token}});
    REQUIRE(lower.status == 200);
    REQUIRE(lower.resp_body == "s1");

    // extra_verify 拒绝已撤销 token
    auto r = Get(client, "/v2/x",
                 LWeb::HeaderMap{{"Authorization", "Bearer " + token}});
    REQUIRE(r.status == 401);
    REQUIRE(BodyJson(r)["error"]["code"] == "INVALID_TOKEN");

    // 超长 token：传输层先被 cinatra 8KB 请求头限制拦截（431）。
    // 注意：431 后同一客户端连接会被污染（cinatra 行为），用新连接验证。
    std::string huge(9000, 'a');
    LWeb::HttpClient fresh{server.base};
    auto big = async_simple::coro::syncAwait(
        fresh.Get("/api/me",
                  LWeb::HeaderMap{{"Authorization", "Bearer " + huge}}));
    REQUIRE(big.status == 431);

    // API 层直测：超长 token 拒绝（不触发大 JSON 解析）
    auto api_res = jwt->Verify(huge);
    REQUIRE(!api_res.ok());
    REQUIRE(std::string(api_res.ErrorCode()) == "INVALID_TOKEN");

    // base64url 非规范 padding 拒绝
    REQUIRE_THROWS(LWeb::detail::Base64UrlDecode("AB=C"));
}

// ============================================================================
// 普通路由中间件 + 全局默认 JWT
// ============================================================================

/// 测试用短路中间件：before 返回 false，证明普通路由上的中间件生效。
struct BlockAll {
    bool before(LWeb::Request&, LWeb::Response& res) {
        LWeb::Forbidden(res, "BLOCKED", "blocked by test middleware");
        return false;
    }
};

TEST_CASE("普通路由可直接设置中间件（无需 Group）", "[LWeb][middleware]") {
    // 全局默认 JWT 配置入口（进程级，路由注册前设置一次）
    LWeb::SetDefaultJwt(
        LWeb::JwtOptions{.algorithm = LWeb::JwtAlgorithm::HS256,
                         .secret = "global-secret"});
    TestApp server;

    // 1) Application 普通路由 + 路由级短路中间件
    server.app.Get("/blocked", [](LWeb::Request&, LWeb::Response& res) {
        LWeb::Ok(res, "nope");
    }, BlockAll{});

    // 2) Application 普通路由 + 全局缺省 JwtAuth{}（无需持有 Jwt 实例）
    server.app.Get("/me", [](LWeb::Request& req, LWeb::Response& res) {
        const auto claims = LWeb::JwtClaimsOf(req);
        LWeb::Ok(res, std::string(claims.Subject()));
    }, LWeb::middleware::JwtAuth{});

    // 3) 普通路由 + 多个中间件组合（JwtAuth{} + RequireClaim）
    server.app.Get("/admin", [](LWeb::Request&, LWeb::Response& res) {
        LWeb::Ok(res, "admin-ok");
    }, LWeb::middleware::JwtAuth{},
       LWeb::middleware::RequireClaim{"role", "admin"});

    // 4) Router::Use 链式（普通路由先挂中间件再注册）
    auto r = server.app.Routes().Use(
        LWeb::middleware::JwtAuth{});
    r.Get("/guarded", [](LWeb::Request&, LWeb::Response& res) {
        LWeb::Ok(res, "guarded-ok");
    });

    LWeb::HttpClient client{server.base};

    // 短路中间件：普通路由生效（403，handler 未执行）
    auto blocked = Get(client, "/blocked");
    REQUIRE(blocked.status == 403);
    REQUIRE(BodyJson(blocked)["error"]["code"] == "BLOCKED");

    // JwtAuth{}：无 token 401；有效 token 200
    auto no_auth = Get(client, "/me");
    REQUIRE(no_auth.status == 401);

    auto token = LWeb::DefaultJwt().CreateToken("u-global",
                                                std::chrono::hours(1));
    auto me = Get(client, "/me",
                  LWeb::HeaderMap{{"Authorization", "Bearer " + token}});
    REQUIRE(me.status == 200);
    REQUIRE(me.resp_body == "u-global");

    // 多中间件组合：认证过但无 role -> 403；有 role -> 200
    auto admin_no = Get(client, "/admin",
                        LWeb::HeaderMap{{"Authorization", "Bearer " + token}});
    REQUIRE(admin_no.status == 403);

    LWeb::JwtClaims claims;
    claims.subject = "u-admin";
    claims.Set("role", "admin");
    auto admin_token = LWeb::DefaultJwt().Encode(claims);
    auto admin_ok = Get(client, "/admin",
                        LWeb::HeaderMap{{"Authorization",
                                         "Bearer " + admin_token}});
    REQUIRE(admin_ok.status == 200);
    REQUIRE(admin_ok.resp_body == "admin-ok");

    // Router::Use 链式路由同样受保护
    auto guarded_no = Get(client, "/guarded");
    REQUIRE(guarded_no.status == 401);
    auto guarded_ok = Get(client, "/guarded",
                          LWeb::HeaderMap{{"Authorization",
                                           "Bearer " + token}});
    REQUIRE(guarded_ok.status == 200);
}

// ============================================================================
// Typed Request Binding
// ============================================================================

TEST_CASE("Typed Binding：Param/Query/QueryOr/Header", "[LWeb][binding]") {
    TestApp server;
    server.app.Get("/typed/:id", [](LWeb::Request& req, LWeb::Response& res) {
        auto id = LWeb::Param<int>(req, "id");
        auto page = LWeb::QueryOr<int>(req, "page", 1);
        auto limit = LWeb::Query<int>(req, "limit");
        auto rid = LWeb::Header<uint64_t>(req, "X-Request-ID");
        auto flag = LWeb::QueryOr<bool>(req, "flag", false);
        LWeb::Ok(res, std::to_string(id) + "|" + std::to_string(page) + "|" +
                          std::to_string(limit) + "|" + std::to_string(rid) +
                          "|" + (flag ? "T" : "F"));
    });
    server.app.Get("/typed-missing/:id",
                   [](LWeb::Request& req, LWeb::Response& res) {
                       auto id = LWeb::Param<int>(req, "id");
                       LWeb::Ok(res, std::to_string(id));
                   });

    LWeb::HttpClient client{server.base};

    auto ok = Get(client, "/typed/42?page=3&limit=10&flag=true",
                  LWeb::HeaderMap{{"X-Request-ID", "77"}});
    REQUIRE(ok.status == 200);
    REQUIRE(ok.resp_body == "42|3|10|77|T");

    // 缺省参数走 fallback（limit 仍必填）
    auto fallback = Get(client, "/typed/7?limit=5",
                        LWeb::HeaderMap{{"X-Request-ID", "1"}});
    REQUIRE(fallback.status == 200);
    REQUIRE(fallback.resp_body == "7|1|5|1|F");

    // 类型转换失败 -> 400（不 500）
    auto bad_id = Get(client, "/typed/abc?limit=1",
                      LWeb::HeaderMap{{"X-Request-ID", "1"}});
    REQUIRE(bad_id.status == 400);

    // 缺少必填 query -> 400
    auto missing = Get(client, "/typed/1",
                       LWeb::HeaderMap{{"X-Request-ID", "1"}});
    REQUIRE(missing.status == 400);
}

TEST_CASE("Typed Binding：enum 支持", "[LWeb][binding]") {
    enum class Role { user = 0, admin = 1 };
    TestApp server;
    server.app.Get("/role/:v", [](LWeb::Request& req, LWeb::Response& res) {
        auto role = LWeb::Param<Role>(req, "v");
        LWeb::Ok(res, role == Role::admin ? "admin" : "user");
    });

    LWeb::HttpClient client{server.base};
    REQUIRE(Get(client, "/role/1").resp_body == "admin");
    REQUIRE(Get(client, "/role/0").resp_body == "user");
    // v0.1 局限：enum 绑定只做底层整型解析，不做枚举值域校验
    // （越界值仍可表示，见文档）。
    REQUIRE(Get(client, "/role/2").resp_body == "user");
}

// ============================================================================
// Result / Error 协议
// ============================================================================

TEST_CASE("Result 新增 Unauthorized/Forbidden/Conflict 映射", "[LWeb][result]") {
    TestApp server;
    server.app.Get("/r/unauth",
                   [](auto&, auto&) -> LWeb::Result::Unauthorized {
                       return {"TOKEN_EXPIRED", "access token expired"};
                   });
    server.app.Get("/r/forbidden",
                   [](auto&, auto&) -> LWeb::Result::Forbidden {
                       return {"insufficient permission"};
                   });
    server.app.Get("/r/conflict", [](auto&, auto&) -> LWeb::Result::Conflict {
        return {"DUPLICATE_USER", "user already exists"};
    });

    LWeb::HttpClient client{server.base};

    auto unauth = Get(client, "/r/unauth");
    REQUIRE(unauth.status == 401);
    REQUIRE(BodyJson(unauth)["error"]["code"] == "TOKEN_EXPIRED");

    auto forbidden = Get(client, "/r/forbidden");
    REQUIRE(forbidden.status == 403);
    REQUIRE(BodyJson(forbidden)["error"]["code"] == "FORBIDDEN");
    REQUIRE(BodyJson(forbidden)["error"]["message"] ==
            "insufficient permission");

    auto conflict = Get(client, "/r/conflict");
    REQUIRE(conflict.status == 409);
    REQUIRE(BodyJson(conflict)["error"]["code"] == "DUPLICATE_USER");
}

TEST_CASE("Handler 抛出 HttpException 按状态码映射", "[LWeb][error]") {
    TestApp server;
    server.app.Get("/pay", [](auto&, auto&) {
        throw LWeb::HttpException(LWeb::HttpStatus::payment_required,
                                  "PAYMENT_REQUIRED", "need payment");
    });
    server.app.Get("/crash", [](auto&, auto&) {
        throw std::runtime_error("boom");
    });

    LWeb::HttpClient client{server.base};

    auto pay = Get(client, "/pay");
    REQUIRE(pay.status == 402);
    REQUIRE(BodyJson(pay)["error"]["code"] == "PAYMENT_REQUIRED");

    auto crash = Get(client, "/crash");
    REQUIRE(crash.status == 500);
    REQUIRE(BodyJson(crash)["error"]["code"] == "INTERNAL_SERVER_ERROR");
}

// ============================================================================
// HttpClient Bearer Token
// ============================================================================

TEST_CASE("HttpClient SetBearerToken 自动携带 Authorization", "[LWeb][httpclient]") {
    TestApp server;
    server.app.Get("/auth", [](LWeb::Request& req, LWeb::Response& res) {
        LWeb::Ok(res, std::string(LWeb::Header(req, "Authorization")));
    });

    LWeb::HttpClient client{server.base};
    client.SetBearerToken("tok-123");
    auto data = Get(client, "/auth");
    REQUIRE(data.status == 200);
    REQUIRE(data.resp_body == "Bearer tok-123");
}
