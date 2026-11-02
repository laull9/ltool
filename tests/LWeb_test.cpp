#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "LWeb.hpp"

namespace {

// ---- 测试数据与服务 ----

struct User {
    std::string name;
    int age = 0;
};

/// 模拟异步服务：纯协程实现，不做真实 IO。
struct UserService {
    std::map<std::string, User> users;

    LWeb::Task<User> GetById(std::string_view id) {
        auto it = users.find(std::string(id));
        if (it == users.end()) {
            co_return User{};
        }
        co_return it->second;
    }

    void PutById(std::string_view id, const User& user) {
        users[std::string(id)] = user;
    }
};

/// Controller：自行用 Group 组织路径（不强制静态 Prefix）。
class UserController {
public:
    explicit UserController(UserService& service) : service_(service) {}

    void Register(LWeb::Router& r) {
        auto users = r.Group("/api/users");
        users.Get("", this, &UserController::GetAll);
        users.Get("/:id", this, &UserController::GetById);
        users.Post("", this, &UserController::Create);
        users.Put("/:id", this, &UserController::Update);
        users.Delete("/:id", this, &UserController::Delete);
    }

private:
    UserService& service_;

    /// 同步 handler。
    void GetAll(LWeb::Request&, LWeb::Response& res) {
        LWeb::Json(res, User{"list", 0});
    }

    /// 协程 handler：co_await 服务并返回。
    LWeb::Task<> GetById(LWeb::Request& req, LWeb::Response& res) {
        auto id = LWeb::Param(req, "id");
        auto user = co_await service_.GetById(id);
        LWeb::Json(res, user);
    }

    /// 读取请求体 JSON 并创建。
    void Create(LWeb::Request& req, LWeb::Response& res) {
        auto user = LWeb::BodyJson<User>(req);
        LWeb::Created(res, user);
    }

    void Update(LWeb::Request& req, LWeb::Response& res) {
        auto id = LWeb::Param(req, "id");
        auto user = LWeb::BodyJson<User>(req);
        service_.PutById(id, user);
        LWeb::Json(res, user);
    }

    void Delete(LWeb::Request& req, LWeb::Response& res) {
        service_.users.erase(std::string(LWeb::Param(req, "id")));
        LWeb::NoContent(res);
    }
};

/// 测试用 aspect：before 返回 false 应跳过 handler。
struct GuardAspect {
    static inline std::atomic<int> before_calls{0};

    bool before(LWeb::Request&, LWeb::Response& res) {
        ++before_calls;
        res.set_status(LWeb::HttpStatus::forbidden);
        return false;
    }

    bool after(LWeb::Request&, LWeb::Response&) { return true; }
};

/// 测试用 aspect：before/after 均放行，记录调用次数。
struct LogAspect {
    static inline std::atomic<int> before_calls{0};
    static inline std::atomic<int> after_calls{0};

    bool before(LWeb::Request&, LWeb::Response&) {
        ++before_calls;
        return true;
    }

    bool after(LWeb::Request&, LWeb::Response&) {
        ++after_calls;
        return true;
    }
};

/// 测试用 aspect：before 短路（返回 false），记录调用。
struct ShortCircuitAspect {
    static inline std::atomic<int> before_calls{0};
    static inline std::atomic<int> after_calls{0};

    bool before(LWeb::Request&, LWeb::Response&) {
        ++before_calls;
        return false;
    }

    bool after(LWeb::Request&, LWeb::Response&) {
        ++after_calls;
        return true;
    }
};

/// 自定义 JSON Codec：验证 codec 可替换。
struct CustomCodec {
    static std::string Serialize(const User&) { return "\"custom-serialized\""; }

    template <typename T>
    static T Deserialize(std::string_view) {
        return T{};
    }
};

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

/// 解析响应体为 LJson DOM。
LJson::Json BodyJson(const LWeb::ResponseData& data) {
    return LJson::Json::parse(std::string(data.resp_body));
}

} // namespace

TEST_CASE("LWeb 基础路由：lambda handler 与健康检查", "[LWeb][router]") {
    TestApp server;
    server.app.Get("/health", [](auto&, auto& res) { LWeb::Ok(res, "ok"); });

    LWeb::HttpClient client{server.base};
    auto data = Get(client, "/health");

    REQUIRE(data.net_err == std::error_code{});
    REQUIRE(data.status == 200);
    REQUIRE(data.resp_body == "ok");
}

TEST_CASE("LWeb 未注册路由返回 404", "[LWeb][router]") {
    TestApp server;

    LWeb::HttpClient client{server.base};
    auto data = Get(client, "/nope");
    REQUIRE(data.status == 404);
}

TEST_CASE("LWeb Controller：成员函数绑定与依赖注入", "[LWeb][controller]") {
    UserService service;
    service.users["7"] = User{"seven", 7};

    TestApp server;
    server.app.AddController<UserController>(service);

    LWeb::HttpClient client{server.base};

    // GET /api/users  -> 同步成员 handler + Json
    auto list = Get(client, "/api/users");
    REQUIRE(list.status == 200);
    REQUIRE(BodyJson(list)["name"] == "list");

    // GET /api/users/7 -> 协程成员 handler + Param + co_await
    auto got = Get(client, "/api/users/7");
    REQUIRE(got.status == 200);
    auto json = BodyJson(got);
    REQUIRE(json["name"] == "seven");
    REQUIRE(json["age"] == 7);

    // GET /api/users/999 -> 不存在时返回空 User
    auto missing = Get(client, "/api/users/999");
    REQUIRE(missing.status == 200);
    REQUIRE(BodyJson(missing)["age"] == 0);
}

TEST_CASE("LWeb Controller：POST/PUT/DELETE 全方法", "[LWeb][controller]") {
    UserService service;

    TestApp server;
    server.app.AddController<UserController>(service);

    LWeb::HttpClient client{server.base};

    // POST /api/users -> BodyJson + Created(201)
    auto created = async_simple::coro::syncAwait(
        client.PostJson("/api/users", User{"new", 3}));
    REQUIRE(created.status == 201);
    auto json = BodyJson(created);
    REQUIRE(json["name"] == "new");
    REQUIRE(json["age"] == 3);

    // PUT /api/users/1 -> BodyJson + Json(200)
    auto updated = async_simple::coro::syncAwait(
        client.Put("/api/users/1", LWeb::LWebJsonCodec::Serialize(User{"u1", 9}),
                   cinatra::req_content_type::json));
    REQUIRE(updated.status == 200);
    REQUIRE(BodyJson(updated)["name"] == "u1");

    // DELETE /api/users/1 -> NoContent(204)
    auto removed = async_simple::coro::syncAwait(client.Delete("/api/users/1"));
    REQUIRE(removed.status == 204);
    REQUIRE(service.users.count("1") == 0);
}

TEST_CASE("LWeb Group：前缀拼接与嵌套分组", "[LWeb][router]") {
    TestApp server;
    auto api = server.app.Group("/api");
    auto v1 = api.Group("/v1");
    v1.Get("/x", [](auto&, auto& res) { LWeb::Ok(res, "x"); });
    server.app.Routes().Group("/v1").Group("sub").Get("y",
                                                       [](auto&, auto& res) {
                                                           LWeb::Ok(res, "y");
                                                       });

    LWeb::HttpClient client{server.base};
    REQUIRE(Get(client, "/api/v1/x").status == 200);
    REQUIRE(Get(client, "/api/v1/x").resp_body == "x");
    REQUIRE(Get(client, "/v1/sub/y").resp_body == "y");
}

TEST_CASE("LWeb Request helpers：Header/Query/Param/Body", "[LWeb][request]") {
    TestApp server;
    server.app.Post("/reflect", [](auto& req, auto& res) {
        auto text = std::string("h=") + std::string(LWeb::Header(req, "X-Tag")) +
                    ";q=" + std::string(LWeb::Query(req, "q")) +
                    ";b=" + std::string(LWeb::Body(req));
        LWeb::Ok(res, text);
    });

    LWeb::HttpClient client{server.base};
    LWeb::HeaderMap headers{{"X-Tag", "tag-value"}};
    auto data = async_simple::coro::syncAwait(
        client.Post("/reflect?q=query-value", "body-text",
                    cinatra::req_content_type::text, headers));

    REQUIRE(data.status == 200);
    REQUIRE(data.resp_body == "h=tag-value;q=query-value;b=body-text");
}

TEST_CASE("LWeb Response helpers：状态码与内容类型", "[LWeb][response]") {
    TestApp server;
    server.app.Get("/created", [](auto&, auto& res) {
        LWeb::Created(res, User{"c", 1});
    });
    server.app.Get("/nocontext", [](auto&, auto& res) {
        LWeb::NoContent(res);
    });
    server.app.Get("/bad", [](auto&, auto& res) {
        LWeb::BadRequest(res, "bad message");
    });
    server.app.Get("/unauth", [](auto&, auto& res) {
        LWeb::Unauthorized(res, "no token");
    });
    server.app.Get("/err-env", [](auto&, auto& res) {
        LWeb::Error(res, LWeb::HttpStatus::not_found, "USER_NOT_FOUND",
                    "user missing");
    });

    LWeb::HttpClient client{server.base};

    auto created = Get(client, "/created");
    REQUIRE(created.status == 201);
    REQUIRE(BodyJson(created)["name"] == "c");

    REQUIRE(Get(client, "/nocontext").status == 204);

    auto bad = Get(client, "/bad");
    REQUIRE(bad.status == 400);
    auto bad_json = BodyJson(bad);
    REQUIRE(bad_json["error"]["code"] == "BAD_REQUEST");
    REQUIRE(bad_json["error"]["message"] == "bad message");

    auto unauth = Get(client, "/unauth");
    REQUIRE(unauth.status == 401);
    REQUIRE(BodyJson(unauth)["error"]["code"] == "UNAUTHORIZED");

    auto err = Get(client, "/err-env");
    REQUIRE(err.status == 404);
    auto json = BodyJson(err);
    REQUIRE(json["error"]["code"] == "USER_NOT_FOUND");
    REQUIRE(json["error"]["message"] == "user missing");
}

TEST_CASE("LWeb Middleware：Aspect 转发与短路", "[LWeb][middleware]") {
    TestApp server;
    GuardAspect::before_calls = 0;
    LogAspect::before_calls = 0;
    LogAspect::after_calls = 0;

    server.app.Get("/guard", [](auto&, auto& res) { LWeb::Ok(res, "nope"); },
                   GuardAspect{});
    server.app.Get("/log", [](auto&, auto& res) { LWeb::Ok(res, "logged"); },
                   LogAspect{});

    LWeb::HttpClient client{server.base};

    // before 返回 false：跳过 handler，保留 aspect 设置的 403。
    auto guarded = Get(client, "/guard");
    REQUIRE(guarded.status == 403);
    REQUIRE(GuardAspect::before_calls == 1);

    // before/after 均放行：正常返回。
    auto logged = Get(client, "/log");
    REQUIRE(logged.status == 200);
    REQUIRE(logged.resp_body == "logged");
    REQUIRE(LogAspect::before_calls == 1);
    REQUIRE(LogAspect::after_calls == 1);
}

TEST_CASE("LWeb 自定义 JSON Codec 可替换", "[LWeb][json]") {
    TestApp server;
    server.app.Get("/custom", [](auto&, auto& res) {
        LWeb::Json<CustomCodec>(res, User{"ignored", 0});
    });

    LWeb::HttpClient client{server.base};
    auto data = Get(client, "/custom");
    REQUIRE(data.status == 200);
    REQUIRE(data.resp_body == "\"custom-serialized\"");
}

TEST_CASE("LWeb 支持 LJson DOM 直接序列化", "[LWeb][json]") {
    TestApp server;
    server.app.Get("/dom", [](auto&, auto& res) {
        LJson::Json json = LJson::Json::object();
        json["k"] = 42;
        LWeb::Json(res, json);
    });

    LWeb::HttpClient client{server.base};
    auto data = Get(client, "/dom");
    REQUIRE(data.status == 200);
    REQUIRE(BodyJson(data)["k"] == 42);
}

TEST_CASE("LWeb 非法 JSON 请求体：JsonParseError 映射 400（而非 500）",
          "[LWeb][json]") {
    TestApp server;
    server.app.Post("/create", [](auto& req, auto& res) {
        auto user = LWeb::BodyJson<User>(req); // 解析失败抛 JsonParseError
        LWeb::Json(res, user);
    });

    LWeb::HttpClient client{server.base};
    auto data = async_simple::coro::syncAwait(
        client.Post("/create", "{invalid", cinatra::req_content_type::json));
    REQUIRE(data.status == 400);
    REQUIRE(BodyJson(data)["error"]["code"] == "INVALID_JSON_BODY");
}

TEST_CASE("LWeb 合法 JSON 但结构不匹配：BindingError 映射 400", "[LWeb][json]") {
    TestApp server;
    server.app.Post("/create", [](auto& req, auto& res) {
        auto user = LWeb::BodyJson<User>(req); // 结构不匹配抛 BindingError
        LWeb::Json(res, user);
    });

    LWeb::HttpClient client{server.base};
    auto data = async_simple::coro::syncAwait(
        client.Post("/create", R"({"age": 1})", cinatra::req_content_type::json));
    REQUIRE(data.status == 400);
    REQUIRE(BodyJson(data)["error"]["code"] == "JSON_BINDING_ERROR");
}

TEST_CASE("LWeb HttpClient：GetJson/PostJson 往返", "[LWeb][httpclient]") {
    TestApp server;
    server.app.Post("/users", [](auto& req, auto& res) {
        auto user = LWeb::BodyJson<User>(req);
        LWeb::Created(res, user);
    });
    server.app.Get("/users/:id", [](auto& req, auto& res) {
        LWeb::Json(res, User{"id-" + std::string(LWeb::Param(req, "id")), 5});
    });

    LWeb::HttpClient client{server.base};

    auto posted = async_simple::coro::syncAwait(
        client.PostJson<User>("/users", User{"roundtrip", 11}));
    REQUIRE(posted.status == 201);

    auto user = async_simple::coro::syncAwait(
        client.GetJson<User>("/users/42"));
    REQUIRE(user.name == "id-42");
    REQUIRE(user.age == 5);
}

TEST_CASE("LWeb HttpClient 错误路径：GetJson 网络失败抛异常", "[LWeb][httpclient]") {
    LWeb::HttpClient client{"http://127.0.0.1:1"}; // 无服务端口

    bool threw = false;
    try {
        auto user =
            async_simple::coro::syncAwait(client.GetJson<User>("/x"));
        LWeb::LWebJsonCodec::Serialize(user); // 避免未使用告警
    }
    catch (const std::exception&) {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("LWeb Native() 逃生通道可用", "[LWeb][native]") {
    LWeb::ApplicationOptions options{.port = 0, .threads = 2};
    LWeb::Application app{options};
    auto& server = app.Native();
    server.set_max_http_body_size(1024); // 原生 API 直接可用

    auto future = app.Start();
    REQUIRE(app.port() != 0);
    app.Stop();
    future.wait();
}

TEST_CASE("LWeb 协程 handler 支持返回 Lazy<T> 的成员函数", "[LWeb][controller]") {
    TestApp server;
    server.app.Get("/value", [](auto&, auto& res) {
        auto f = []() -> LWeb::Task<User> { co_return User{"coro", 8}; };
        auto user = async_simple::coro::syncAwait(f());
        LWeb::Json(res, user);
    });

    LWeb::HttpClient client{server.base};
    auto data = Get(client, "/value");
    REQUIRE(data.status == 200);
    REQUIRE(BodyJson(data)["name"] == "coro");
    REQUIRE(BodyJson(data)["age"] == 8);
}

TEST_CASE("LWeb 协程 handler 抛异常转 500", "[LWeb][controller]") {
    TestApp server;

    // 异常发生在协程体启动阶段。
    server.app.Get("/coro-throw", [](auto&, auto& res) -> LWeb::Task<> {
        throw std::runtime_error("coro boom");
        co_return;
    });

    // 异常发生在 co_await 的协程内部。
    server.app.Get("/coro-await", [](auto&, auto& res) -> LWeb::Task<> {
        auto inner = []() -> LWeb::Task<> {
            throw std::runtime_error("inner boom");
            co_return;
        };
        co_await inner();
    });

    // 异常发生在协程 handler 调用 BodyJson 时。
    server.app.Post("/coro-create", [](auto& req, auto& res) -> LWeb::Task<> {
        auto user = LWeb::BodyJson<User>(req); // 非法 JSON 抛异常
        LWeb::Json(res, user);
        co_return;
    });

    LWeb::HttpClient client{server.base};

    auto a = Get(client, "/coro-throw");
    REQUIRE(a.status == 500);
    auto b = Get(client, "/coro-await");
    REQUIRE(b.status == 500);
    auto c = async_simple::coro::syncAwait(
        client.Post("/coro-create", "{bad", cinatra::req_content_type::json));
    REQUIRE(c.status == 400); // 非法 JSON -> JsonParseError(400)，不再 500
    REQUIRE(BodyJson(c)["error"]["code"] == "INVALID_JSON_BODY");
}

TEST_CASE("LWeb Any/Options/Patch 路由与 HttpClient.Patch", "[LWeb][router]") {
    TestApp server;
    server.app.Any("/any", [](auto&, auto& res) { LWeb::Ok(res, "any"); });
    server.app.Options("/opt", [](auto&, auto& res) { LWeb::Ok(res, "opt"); });
    server.app.Patch("/p", [](auto&, auto& res) { LWeb::Ok(res, "patched"); });

    LWeb::HttpClient client{server.base};

    REQUIRE(Get(client, "/any").resp_body == "any");
    auto posted = async_simple::coro::syncAwait(client.Post("/any", "x"));
    REQUIRE(posted.resp_body == "any");
    auto del = async_simple::coro::syncAwait(client.Delete("/any"));
    REQUIRE(del.resp_body == "any");

    auto opt = async_simple::coro::syncAwait(
        client.Native().async_options(server.base + "/opt"));
    REQUIRE(opt.status == 200);
    REQUIRE(opt.resp_body == "opt");

    auto patched = async_simple::coro::syncAwait(client.Patch("/p"));
    REQUIRE(patched.status == 200);
    REQUIRE(patched.resp_body == "patched");
}

TEST_CASE("LWeb 自定义 Codec 的 Deserialize 反向路径", "[LWeb][json]") {
    TestApp server;
    server.app.Post("/custom-create", [](auto& req, auto& res) {
        auto user = LWeb::BodyJson<User, CustomCodec>(req); // 忽略输入返回默认 User
        LWeb::Json(res, user);
    });
    server.app.Get("/custom-json", [](auto&, auto& res) {
        LWeb::Ok(res, "{\"name\":\"ignored\",\"age\":1}");
    });

    LWeb::HttpClient client{server.base};
    auto data = async_simple::coro::syncAwait(
        client.Post("/custom-create", "{\"name\":\"x\"}",
                    cinatra::req_content_type::json));
    REQUIRE(data.status == 200);
    REQUIRE(BodyJson(data)["name"] == "");

    // GetJson 使用自定义 codec：忽略响应体，返回默认 User。
    auto user = async_simple::coro::syncAwait(
        client.GetJson<User, CustomCodec>("/custom-json"));
    REQUIRE(user.name == "");
    REQUIRE(user.age == 0);
}

TEST_CASE("LWeb GetJson 对 HTTP 错误状态抛异常", "[LWeb][httpclient]") {
    TestApp server;
    server.app.Get("/exists", [](auto&, auto& res) {
        LWeb::NotFound(res, "missing");
    });

    LWeb::HttpClient client{server.base};
    bool threw = false;
    try {
        auto user = async_simple::coro::syncAwait(client.GetJson<User>("/exists"));
        LWeb::LWebJsonCodec::Serialize(user);
    }
    catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("LWeb 路径拼接边界：前缀/尾斜杠/根路径归一化", "[LWeb][router]") {
    TestApp server;
    server.app.Group("/api/").Get("/x", [](auto&, auto& res) {
        LWeb::Ok(res, "ax");
    });
    server.app.Routes().Group("no-slash").Get("y", [](auto&, auto& res) {
        LWeb::Ok(res, "ny");
    });
    server.app.Routes().Group("/").Get("/root", [](auto&, auto& res) {
        LWeb::Ok(res, "root");
    });
    server.app.Routes().Get("/trail/", [](auto&, auto& res) {
        LWeb::Ok(res, "trail");
    });

    LWeb::HttpClient client{server.base};
    REQUIRE(Get(client, "/api/x").resp_body == "ax");
    REQUIRE(Get(client, "/no-slash/y").resp_body == "ny");
    REQUIRE(Get(client, "/root").resp_body == "root");
    REQUIRE(Get(client, "/trail").resp_body == "trail");
}

TEST_CASE("LWeb HttpClient URL 拼接边界", "[LWeb][httpclient]") {
    TestApp server;
    server.app.Get("/api/users/42", [](auto&, auto& res) {
        LWeb::Ok(res, "u42");
    });

    // 基础 URL 无尾斜杠 + 路径带前导斜杠。
    LWeb::HttpClient c1{server.base};
    REQUIRE(Get(c1, "/api/users/42").resp_body == "u42");

    // 基础 URL 带尾斜杠 + 路径带前导斜杠。
    LWeb::HttpClient c2{server.base + "/"};
    REQUIRE(Get(c2, "/api/users/42").resp_body == "u42");

    // 基础 URL 带尾斜杠 + 路径无前导斜杠。
    LWeb::HttpClient c3{server.base + "/"};
    REQUIRE(Get(c3, "api/users/42").resp_body == "u42");

    // 基础 URL 为空 + 完整路径。
    LWeb::HttpClient c4{""};
    auto data = async_simple::coro::syncAwait(c4.Get(server.base + "/api/users/42"));
    REQUIRE(data.resp_body == "u42");
}

TEST_CASE("LWeb 多 Aspect 组合执行顺序与短路", "[LWeb][middleware]") {
    TestApp server;
    LogAspect::before_calls = 0;
    LogAspect::after_calls = 0;
    ShortCircuitAspect::before_calls = 0;
    ShortCircuitAspect::after_calls = 0;

    server.app.Get("/multi", [](auto&, auto& res) { LWeb::Ok(res, "nope"); },
                   LogAspect{}, ShortCircuitAspect{});

    LWeb::HttpClient client{server.base};
    auto data = Get(client, "/multi");

    // before 按注册顺序执行；第二个短路导致 handler 被跳过。
    REQUIRE(LogAspect::before_calls == 1);
    REQUIRE(ShortCircuitAspect::before_calls == 1);
    // after 仍会全部执行。
    REQUIRE(LogAspect::after_calls == 1);
    REQUIRE(ShortCircuitAspect::after_calls == 1);
    // handler 未执行：响应体是默认状态内容而非 "nope"。
    REQUIRE(data.resp_body != "nope");
}

TEST_CASE("LWeb 重复路由注册抛异常，同路径不同方法合法", "[LWeb][router]") {
    LWeb::ApplicationOptions options{.port = 0, .threads = 2};
    LWeb::Application app{options};

    app.Get("/dup", [](auto&, auto& res) { LWeb::Ok(res, "1"); });
    REQUIRE_THROWS_AS(
        app.Get("/dup", [](auto&, auto& res) { LWeb::Ok(res, "2"); }),
        std::invalid_argument);

    // 同路径不同方法允许。
    app.Post("/dup", [](auto&, auto& res) { LWeb::Ok(res, "p"); });

    // 不同分组中同路径也允许（仅按完整路径判重）。
    app.Routes().Group("/v1").Get("/dup", [](auto&, auto& res) {
        LWeb::Ok(res, "v1");
    });
}

// ---- 架构扩展：Module / Vertical Slice / 返回值式 Handler / 混合架构 ----

/// 无依赖 Controller：验证 AddController 不强制依赖与 Prefix。
struct HealthController {
    void Register(LWeb::Router& r) {
        r.Get("/health", [](auto&, auto& res) { LWeb::Ok(res, "ok"); });
    }
};

/// 通用 Module：只需 Register(Router&)，依赖经构造传入。
struct AdminModule {
    explicit AdminModule(int& hits) : hits_(hits) {}

    void Register(LWeb::Router& r) {
        r.Get("/admin", [this](auto&, auto& res) {
            ++hits_;
            LWeb::Ok(res, "admin");
        });
    }

private:
    int& hits_;
};

/// Vertical Slice Feature：静态 Map(Router&, 依赖...)，不创建长期对象。
struct CreateUserSlice {
    struct Input {
        std::string name;
        int age;
    };

    static void Map(LWeb::Router& r, UserService& service) {
        r.Post("/slice/users", [&service](LWeb::Request& req,
                                          LWeb::Response& res) {
            auto input = LWeb::BodyJson<Input>(req);
            service.PutById(input.name, User{input.name, input.age});
            LWeb::Created(res, User{input.name, input.age});
        });
    }
};

/// 返回值式 Handler 控制器。
struct ReturnController {
    void Register(LWeb::Router& r) {
        r.Get("/ret/created", [](auto&, auto&) -> LWeb::Result::Created<User> {
            return {User{"created", 1}};
        });
        r.Get("/ret/notfound", [](auto&, auto&) -> LWeb::Result::NotFound {
            return {"no such user"};
        });
        r.Get("/ret/nocontent", [](auto&, auto&) -> LWeb::Result::NoContent {
            return {};
        });
        r.Get("/ret/badreq", [](auto&, auto&) -> LWeb::Result::BadRequest {
            return {"bad input"};
        });
        r.Get("/ret/plain", [](auto&, auto&) -> LWeb::Result::Variant<User> {
            return User{"plain", 2};
        });
        r.Get("/ret/variant-404", [](auto&, auto&) -> LWeb::Result::Variant<User> {
            return LWeb::Result::NotFound{"missing"};
        });
        r.Get("/ret/text", [](auto&, auto&) -> std::string {
            return "plain text";
        });
        r.Get("/ret/coro", [](auto&, auto&)
                                  -> LWeb::Task<LWeb::Result::Created<User>> {
            co_return LWeb::Result::Created<User>{User{"coro", 3}};
        });
    }
};

TEST_CASE("LWeb AddController 支持无依赖与自组路径", "[LWeb][controller]") {
    TestApp server;
    server.app.AddController<HealthController>();

    LWeb::HttpClient client{server.base};
    auto data = Get(client, "/health");
    REQUIRE(data.status == 200);
    REQUIRE(data.resp_body == "ok");
}

TEST_CASE("LWeb AddModule 注册通用模块", "[LWeb][module]") {
    TestApp server;
    int hits = 0;
    server.app.AddModule<AdminModule>(hits);

    LWeb::HttpClient client{server.base};
    auto data = Get(client, "/admin");
    REQUIRE(data.status == 200);
    REQUIRE(data.resp_body == "admin");
    REQUIRE(hits == 1);
}

TEST_CASE("LWeb Map<T> Vertical Slice：静态 Map(Router&, 依赖)", "[LWeb][slice]") {
    UserService service;
    TestApp server;
    server.app.Map<CreateUserSlice>(service);

    LWeb::HttpClient client{server.base};
    auto data = async_simple::coro::syncAwait(
        client.Post("/slice/users", "{\"name\":\"slice\",\"age\":9}",
                    cinatra::req_content_type::json));
    REQUIRE(data.status == 201);
    auto json = BodyJson(data);
    REQUIRE(json["name"] == "slice");
    REQUIRE(service.users.count("slice") == 1);
}

TEST_CASE("LWeb 返回值式 Handler 自动映射状态码", "[LWeb][result]") {
    TestApp server;
    server.app.AddController<ReturnController>();

    LWeb::HttpClient client{server.base};

    auto created = Get(client, "/ret/created");
    REQUIRE(created.status == 201);
    REQUIRE(BodyJson(created)["name"] == "created");

    auto notfound = Get(client, "/ret/notfound");
    REQUIRE(notfound.status == 404);
    REQUIRE(BodyJson(notfound)["error"]["code"] == "NOT_FOUND");
    REQUIRE(BodyJson(notfound)["error"]["message"] == "no such user");

    REQUIRE(Get(client, "/ret/nocontent").status == 204);

    auto badreq = Get(client, "/ret/badreq");
    REQUIRE(badreq.status == 400);
    REQUIRE(BodyJson(badreq)["error"]["code"] == "BAD_REQUEST");
    REQUIRE(BodyJson(badreq)["error"]["message"] == "bad input");

    auto plain = Get(client, "/ret/plain");
    REQUIRE(plain.status == 200);
    REQUIRE(BodyJson(plain)["name"] == "plain");

    auto variant404 = Get(client, "/ret/variant-404");
    REQUIRE(variant404.status == 404);

    auto text = Get(client, "/ret/text");
    REQUIRE(text.status == 200);
    REQUIRE(text.resp_body == "plain text"); // std::string 按 200 文本输出

    auto coro = Get(client, "/ret/coro");
    REQUIRE(coro.status == 201);
    REQUIRE(BodyJson(coro)["name"] == "coro");
}

TEST_CASE("LWeb 混合架构：Minimal API + Controller + Module + Slice 共存",
          "[LWeb][architecture]") {
    TestApp server;
    int hits = 0;
    UserService service;

    server.app.Get("/ping", [](auto&, auto& res) { LWeb::Ok(res, "pong"); });
    server.app.AddController<HealthController>();
    server.app.AddController<UserController>(service);
    server.app.AddModule<AdminModule>(hits);
    server.app.Map<CreateUserSlice>(service);

    LWeb::HttpClient client{server.base};

    REQUIRE(Get(client, "/ping").resp_body == "pong");
    REQUIRE(Get(client, "/health").resp_body == "ok");
    REQUIRE(Get(client, "/api/users/1").status == 200);
    auto created = async_simple::coro::syncAwait(
        client.Post("/slice/users", "{\"name\":\"mix\",\"age\":1}",
                    cinatra::req_content_type::json));
    REQUIRE(created.status == 201);
    REQUIRE(Get(client, "/admin").resp_body == "admin");
}
