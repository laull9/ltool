# LWeb

`LWeb` 是基于 vendored [Cinatra](https://github.com/qicosmos/cinatra) 的 Web 应用薄封装，定位是 **API normalization**，不是隐藏 Cinatra 的全新框架：底层 `Request` / `Response` / `Task` 直接别名 Cinatra 类型，路由、Controller、JSON、Middleware 只做顺手化整理，任何高级能力都能通过 `Native()` 逃回原生 API。

## 设计红线

1. `Request` / `Response` / `Task` 直接 alias，不重新造对象。
2. 所有高级功能允许通过 `.Native()` 回到 Cinatra。
3. Controller 不继承基类（duck typing）。
4. 不做运行时 DI（构造函数直接注入）。
5. Router 只做路径、Method、Handler 绑定。
6. Middleware 复用 Cinatra Aspect。
7. JSON 使用 `LJson`，Codec 可整体替换。
8. 第一版核心代码控制在约 600 行以内。

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

`LWeb.hpp` 需要 Cinatra（含 asio/async_simple/ylt）与 `LJson`。编译时增加两个 include 路径：

```bash
c++ -std=c++20 -I ltool -I ltool/pkgs/cinatra app.cpp -o app
```

- 依赖均随仓库提供（`ltool/pkgs/cinatra/`），不需要额外安装。
- 需要 C++20（协程）。
- `LWeb` 不在 `LTool.hpp` 聚合入口中（会破坏"无额外 include 路径"约定），请直接 `#include "LWeb.hpp"`。

## Controller 约定

Controller 是普通 C++ 对象：静态 `Prefix` + `Register(Router&)`，用 `std::invoke` 绑定成员函数，支持同步 handler 和协程 handler。

```cpp
class UserController {
public:
    static constexpr std::string_view Prefix = "/api/users";

    explicit UserController(UserService& service) : service_(service) {}

    void Register(LWeb::Router& r) {
        r.Get("",     this, &UserController::GetAll);
        r.Get("/:id", this, &UserController::GetById);
        r.Post("",    this, &UserController::Create);
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
```

注册：

```cpp
LWeb::Application app{LWeb::ApplicationOptions{.port = 8080}};
UserService service;
app.AddController<UserController>(service);  // 构造时注入，不依赖运行时 DI
```

## API 一览

### Application / Router

| API | 说明 |
| --- | --- |
| `Application(options)` | `options`：`port`（0 为系统分配）、`threads`、`address` |
| `AddController<T>(args...)` | 注册 Controller（duck typing） |
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
| `BodyJson<T>(req)` | 请求体 JSON 反序列化（失败抛异常，cinatra 转 503） |

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
- Controller 生命周期由 Application 管理：析构时先停服务（join 线程池）再销毁 Controller，保证 in-flight handler 不悬垂。
- 路径拼接自动归一化：补前导 `/`、去尾斜杠、空路径归一根路径。
- 重复注册同一 `METHOD + path` 会抛 `std::invalid_argument`（同路径不同方法合法）。
- Handler 若为 mutable 有状态对象，状态会跨请求共享，请保持无共享可变状态。
- `BodyJson` / 协程 handler 抛出的异常由 Cinatra 捕获并转为 503。
