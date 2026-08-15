# LWeb

`LWeb` 是基于 vendored [Cinatra](https://github.com/qicosmos/cinatra) 的 Web 应用薄封装，定位是 **API normalization**，不是隐藏 Cinatra 的全新框架：底层 `Request` / `Response` / `Task` 直接别名 Cinatra 类型，路由、Handler 绑定、JSON、Middleware 只做顺手化整理，任何高级能力都能通过 `Native()` 逃回原生 API。

## 设计红线

1. `Request` / `Response` / `Task` 直接 alias，不重新造对象。
2. 所有高级功能允许通过 `.Native()` 回到 Cinatra。
3. Controller / Module 不继承基类（duck typing），不强制静态 Prefix。
4. 不做运行时 DI（依赖经构造函数/参数显式传入，生命周期由用户管理）。
5. Router 只做路径、Method、Handler 绑定。
6. Middleware 复用 Cinatra Aspect。
7. JSON 使用 `LJson`，Codec 可整体替换。
8. **不规定业务架构**：Minimal API / Controller / Module / Vertical Slice 可任意混用，Service/Repository/CommandBus/Database 等只是普通 C++ 类型。

## 快速开始

```cpp
#include "LWeb.hpp"

int main() {
    LWeb::Application app{LWeb::ApplicationOptions{.port = 8080}};

    app.Get("/health", [](auto&, auto& res) {
        LWeb::Ok(res, "ok");
    });

    return app.Run() ? 1 : 0;
}
```

## 依赖与 include

`LWeb.hpp` 需要 Cinatra（含 asio/async_simple/ylt）与 `LJson`。依赖均随仓库提供（`ltool/pkgs/cinatra/`），且 **Cinatra 已做本地化适配**（内部 include 全部改写为相对路径），使用方无需额外 include 路径，与其他 ltool 模块一致：

```bash
c++ -std=c++20 -I ltool app.cpp -o app
```

- 依赖随仓库提供，不需要额外安装，不需要在构建系统中声明额外 include/链接。
- 需要 C++20（协程）。
- `LWeb` 不在 `LTool.hpp` 聚合入口中（避免给所有使用方引入 Cinatra 的编译成本），请直接 `#include "LWeb.hpp"`。

## 架构模式（可混用）

LWeb 只做 `Route → Handler`，不规定业务分层。以下四种方式可以同时存在于一个应用。

### 1. Minimal API

```cpp
app.Get("/health", handler);

auto api = app.Group("/api");       // 分组减少重复路径
auto users = api.Group("/users");
users.Get("", list_handler);
users.Get("/:id", get_handler);
```

### 2. Controller

Controller 是普通 C++ 对象：只需成员 `void Register(Router&)`，**不强制静态 Prefix**，路径在 `Register` 内用 `Group` 自行组织。依赖经构造函数显式传入，可以有依赖也可以没有：

```cpp
class UserController {
public:
    explicit UserController(UserService& service) : service_(service) {}

    void Register(LWeb::Router& r) {
        auto users = r.Group("/api/users");
        users.Get("",     this, &UserController::GetAll);
        users.Get("/:id", this, &UserController::GetById);
        users.Post("",    this, &UserController::Create);
    }

private:
    UserService& service_;

    void GetAll(LWeb::Request&, LWeb::Response& res) {
        LWeb::Json(res, service_.GetAll());
    }

    LWeb::Task<> GetById(LWeb::Request& req, LWeb::Response& res) {
        auto user = co_await service_.GetById(LWeb::Param(req, "id"));
        LWeb::Json(res, user);
    }

    void Create(LWeb::Request& req, LWeb::Response& res) {
        LWeb::Created(res, LWeb::BodyJson<User>(req));
    }
};

app.AddController<UserController>(service);      // 有依赖
app.AddController<HealthController>();           // 无依赖
```

### 3. 通用 Module

与 Controller 行为一致（`void Register(Router&)`），仅语义区分：

```cpp
struct AdminModule {
    explicit AdminModule(AdminService& svc) : svc_(svc) {}
    void Register(LWeb::Router& r) {
        auto admin = r.Group("/admin");
        admin.Get("/stats", this, &AdminModule::Stats);
    }
private:
    AdminService& svc_;
    void Stats(LWeb::Request&, LWeb::Response& res) { LWeb::Json(res, svc_.Stats()); }
};

app.AddModule<AdminModule>(admin_service);
```

### 4. Vertical Slice（`Map<T>`）

调用特征类型的**静态** `T::Map(Router&, 依赖...)`，不创建长期对象；Feature 自带 DTO/逻辑/端点：

```cpp
struct CreateUser {
    struct Input { std::string name; int age; };

    static void Map(LWeb::Router& r, UserRepository& users) {
        r.Post("/api/users", [&users](LWeb::Request& req, LWeb::Response& res) {
            auto input = LWeb::BodyJson<Input>(req);
            auto user = users.Create({0, std::move(input.name), input.age});
            LWeb::Created(res, user);
        });
    }
};

app.Map<CreateUser>(user_repo);
```

### 5. 混合架构

```cpp
app.Get("/health", health_handler);              // Minimal API
app.AddController<AuthController>(auth_service); // Controller
app.AddModule<AdminModule>(admin_service);       // Module
app.Map<CreateOrder>(orders, payments);          // Vertical Slice
```

## 返回值式 Handler（可选）

Handler 可以不操作 `Response`，而是直接返回结果，由框架自动映射状态码与响应体。结果类型位于 `LWeb::Result` 命名空间：

| 返回类型 | 映射 |
| --- | --- |
| 普通类型（如 `User`） | 200 JSON |
| `std::string` / `std::string_view` / `const char*` | 200 文本 |
| `Result::Created<T>` | 201 JSON |
| `Result::NotFound{msg}` | 404 |
| `Result::BadRequest{msg}` | 400 |
| `Result::NoContent{}` | 204 |
| `Result::Variant<T>` | 按实际持有类型分发（一个 handler 返回多种结果） |

```cpp
LWeb::Result::Created<User> Create(LWeb::Request& req) {
    return {LWeb::BodyJson<User>(req)};
}

LWeb::Result::Variant<User> GetById(LWeb::Request& req) {
    auto id = LWeb::Param(req, "id");
    if (id.empty()) {
        return LWeb::Result::BadRequest{"missing id"};
    }
    auto user = users.Find(id);
    if (!user) {
        return LWeb::Result::NotFound{"user not found"};
    }
    return *user;                    // 裸类型 -> 200 JSON
}
```

协程 Handler 返回 `Task<T>` / `Task<Result::...>` 同样适用：

```cpp
app.Get("/x", [](auto&, auto&) -> LWeb::Task<LWeb::Result::Created<User>> {
    co_return LWeb::Result::Created<User>{User{"u", 1}};
});
```

> 注：`LWeb::Result<User>` 的 variant 便捷别名命名为 `Result::Variant<T>`，
> 以避免与 `namespace Result` 同名冲突。

## API 一览

### Application / Router

| API | 说明 |
| --- | --- |
| `Application(options)` | `options`：`port`（0 为系统分配）、`threads`、`address` |
| `AddController<T>(args...)` | 注册 Controller（`Register(Router&)`，可带依赖） |
| `AddModule<T>(args...)` | 注册通用 Module（`Register(Router&)`） |
| `Map<T>(args...)` | Vertical Slice（静态 `T::Map(Router&, 依赖...)`） |
| `Get/Post/Put/Delete/Patch/Options/Head/Any(path, handler, aspects...)` | 注册路由，handler 支持 lambda 与 `(this, &T::Method)` |
| `Group(path)` | 子路由分组，前缀自动拼接 |
| `Routes()` | 根 Router |
| `Run()` / `Start()` / `Stop()` / `port()` | 生命周期控制 |
| `Native()` | 返回 `cinatra::coro_http_server&` |

### Request / Response helpers

| API | 说明 |
| --- | --- |
| `Header(req, name)` / `Query(req, key)` / `Param(req, key)` / `Body(req)` | 读取请求（返回 `string_view`，仅 handler 执行期间有效） |
| `Ok/Text(res, str)` | 200 文本 |
| `Json(res, obj[, status])` | 200/指定状态 JSON，`Json<MyCodec>(res, obj)` 可换 codec |
| `Created/BadRequest/Unauthorized/Forbidden/NotFound/InternalError` | 状态码快捷函数 |
| `NoContent(res)` | 204 |
| `Error(res, status, code, message)` | 统一错误体 `{"error":{"code":...,"message":...}}` |
| `BodyJson<T>(req)` | 请求体 JSON 反序列化（失败抛异常，LWeb 兜底转 500） |

### Middleware（Aspect）

```cpp
struct AccessLog {
    bool before(LWeb::Request& req, LWeb::Response& res) {
        // 返回 false 跳过 handler；此时需自行写响应（如 401）
        return true;
    }
    bool after(LWeb::Request&, LWeb::Response&) { return true; }
};

r.Get("/users", handler, AccessLog{}, Auth{});
```

Aspect 按注册顺序执行 `before`；任一返回 false 则跳过 handler，`after` 仍会全部执行。

### HttpClient

```cpp
LWeb::HttpClient client{"https://api.example.com"};

auto data   = co_await client.Get("/users");
auto json   = co_await client.PostJson("/users", create_user);
auto user   = co_await client.GetJson<User>("/users/123");
auto native = client.Native();  // cinatra::coro_http_client&
```

注意：`GetJson` 在网络错误或 HTTP ≥400 时抛异常；单实例不宜并发使用。

## 注意事项

- 路由注册必须在 `Start()` / `Run()` 之前完成。
- Controller / Module 生命周期由 Application 管理：析构时先停服务（join 线程池）再销毁，保证 in-flight handler 不悬垂；`Map<T>` 不持有对象，依赖生命周期由用户管理。
- 路径拼接自动归一化：补前导 `/`、去尾斜杠、空路径归一根路径。
- 重复注册同一 `METHOD + path` 会抛 `std::invalid_argument`（同路径不同方法合法）。
- Handler 若为 mutable 有状态对象，状态会跨请求共享，请保持无共享可变状态。
- `BodyJson` / 协程 handler 抛出的异常由 LWeb 统一捕获并转为 500。
  （cinatra 对带 `:id` 等动态路径的同步/协程 handler 不做异常捕获，LWeb 在包装层兜底。）
