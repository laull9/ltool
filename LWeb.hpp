/**
 * @file LWeb.hpp
 * @brief ltool 的 Web 应用层：基于 Cinatra 的 Router/Controller/Response/HttpClient 薄封装。
 *
 * LWeb 不重新实现 HTTP、socket、协程、TLS 等底层能力，而是把 Cinatra 的
 * 路由注册、Controller 组织、Response/JSON、Middleware 转发整理成更顺手的形态。
 * 底层类型直接复用 Cinatra（Request/Response/Task 均为别名），并始终提供
 * Native() 逃生通道回到原生 API。
 *
 * 设计红线：
 * - Request/Response/Task 直接 alias，不重新造对象；
 * - 所有高级功能允许通过 Native() 回到 Cinatra；
 * - Controller/Module 不继承基类（duck typing），不强制静态 Prefix；
 * - 不做运行时 DI（依赖经构造函数/参数显式传入，生命周期由用户管理）；
 * - Router 只做路径、Method、Handler 绑定；
 * - Middleware 复用 Cinatra Aspect；
 * - JSON 使用 LJson（codec 可替换）；
 * - 不规定业务架构：Minimal API / Controller / Module / Vertical Slice 可混用。
 *
 * 常用示例（四种架构可任意混用）：
 * @code
 * #include "LWeb.hpp"
 *
 * // Controller：自行用 Group 组织路径，不强制静态 Prefix。
 * class UserController {
 * public:
 *     explicit UserController(UserService& svc) : svc_(svc) {}
 *
 *     void Register(LWeb::Router& r) {
 *         auto users = r.Group("/api/users");
 *         users.Get("",     this, &UserController::GetAll);
 *         users.Get("/:id", this, &UserController::GetById);
 *     }
 *
 * private:
 *     UserService& svc_;
 *     void GetAll(LWeb::Request&, LWeb::Response& res) {
 *         LWeb::Json(res, svc_.GetAll());
 *     }
 *     LWeb::Task<> GetById(LWeb::Request& req, LWeb::Response& res) {
 *         auto user = co_await svc_.GetById(LWeb::Param(req, "id"));
 *         LWeb::Json(res, user);
 *     }
 * };
 *
 * // Vertical Slice：静态 Map(Router&, 依赖...)，不创建长期对象。
 * struct CreateUser {
 *     struct Input { std::string name; int age; };
 *     static void Map(LWeb::Router& r, UserRepository& users) {
 *         r.Post("/api/users", [&users](LWeb::Request& req, LWeb::Response& res) {
 *             auto input = LWeb::BodyJson<Input>(req);
 *             LWeb::Created(res, users.Create(input));
 *         });
 *     }
 * };
 *
 * int main() {
 *     UserService svc;
 *     UserRepository repo;
 *     LWeb::Application app{LWeb::ApplicationOptions{.port = 8080}};
 *
 *     app.Get("/health", [](auto&, LWeb::Response& res) {  // Minimal API
 *         LWeb::Ok(res, "ok");
 *     });
 *     app.AddController<UserController>(svc);              // Controller
 *     app.AddModule<AdminModule>(admin_service);           // 通用 Module
 *     app.Map<CreateUser>(repo);                           // Vertical Slice
 *     return app.Run() ? 1 : 0;
 * }
 * @endcode
 *
 * 注意事项：
 * - 协程 Handler 支持 `[](auto&, auto&) -> LWeb::Task<>` 等泛型 lambda；
 * - 路由注册必须在 Start()/Run() 之前完成（之后注册与 in-flight 请求存在数据竞争）；
 * - Controller 生命周期由 Application 管理，析构时先停服务再销毁 Controller；
 * - Header/Query/Param/Body 返回的 string_view 仅在 handler 执行期间有效，
 *   如需跨 co_await 使用请先拷贝为 std::string；
 * - HttpClient 单实例不宜并发使用（底层 cinatra::coro_http_client 不保证并发安全）；
 * - Handler 若是 mutable 有状态对象，状态会跨请求共享（cinatra 单 std::function
 *   可能被多线程并发调用），请保持 handler 无共享可变状态。
 */

#ifndef LTOOL_LWEB_INCLUDE
#define LTOOL_LWEB_INCLUDE

#include <concepts>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

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
/// Result::NotFound -> 404；Result::BadRequest -> 400；Result::NoContent -> 204；
/// 字符串 -> 200 文本；Result::Variant<T> 按实际持有类型分发。
/// 协程 Handler 返回 Task<T> / Task<Result::...> 同样适用。
namespace Result {

/// 204 No Content。
struct NoContent {};

/// 404 Not Found（message 可为空）。
struct NotFound {
    std::string message;
};

/// 201 Created，携带 JSON 负载。
template <typename T>
struct Created {
    T value;
};

/// 400 Bad Request（message 可为空）。
struct BadRequest {
    std::string message;
};

/// 多选一结果：handler 可返回其中任意一种（普通 T / Created<T> /
/// NotFound / NoContent / BadRequest）。
template <typename T>
using Variant =
    std::variant<T, Created<T>, NotFound, NoContent, BadRequest>;

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

    /// 把 JSON 文本反序列化为类型 T；解析失败抛异常。
    template <typename T>
    static T Deserialize(std::string_view text) {
        if constexpr (std::same_as<T, LJson::Json>) {
            return LJson::Json::parse(std::string(text));
        }
        else {
            return LJson::read<T>(std::string(text)).value();
        }
    }
};

namespace detail {

/// 判定返回值是否为 Lazy 协程。
template <typename T>
inline constexpr bool is_lazy_v = false;

template <typename T>
inline constexpr bool is_lazy_v<async_simple::coro::Lazy<T>> = true;

/// 提取 Lazy<T> 的载荷类型 T。
template <typename T>
struct lazy_value;

template <typename T>
struct lazy_value<async_simple::coro::Lazy<T>> {
    using type = T;
};

template <typename T>
using lazy_value_t = typename lazy_value<std::remove_cvref_t<T>>::type;

/// Handler 必须是可用 (Request&, Response&) 调用的可调用对象。
template <typename F>
concept HttpHandler = std::invocable<F, Request&, Response&>;

/// Handler 的调用结果类型。
template <typename Handler>
using handler_result_t =
    decltype(std::declval<Handler&>()(std::declval<Request&>(),
                                      std::declval<Response&>()));

/// 文本类型判定：返回值式 Handler 中按 200 文本输出，而非 JSON。
template <typename T>
inline constexpr bool is_text_v =
    std::same_as<T, std::string> || std::same_as<T, std::string_view> ||
    std::same_as<T, const char*> || std::is_array_v<T>;

// 前置声明：WriteResult 定义在 Response helpers 之后（见下方），
// 供 WrapSync/WrapCoro 在包装层调用。
inline void WriteResult(Response& res, Result::NoContent);
inline void WriteResult(Response& res, Result::NotFound not_found);
template <typename T>
void WriteResult(Response& res, Result::Created<T> created);
inline void WriteResult(Response& res, Result::BadRequest bad_request);
template <typename... Ts>
void WriteResult(Response& res, std::variant<Ts...> variant);
template <typename T>
void WriteResult(Response& res, T&& value);

/// 把 Handler 包装为类型化同步 lambda（消除泛型 lambda 对 function_traits 的依赖）。
/// 同时兜底异常：cinatra 对带 :id 等动态路径的同步 handler 不做 try/catch，
/// 这里统一捕获并转 500，避免异常穿透导致进程终止。
/// Handler 可返回 void（直接操作 res），也可返回结果类型（Result::* / 普通类型）
/// 由框架自动映射状态码。
template <typename Handler>
auto WrapSync(Handler&& handler) {
    return [h = std::forward<Handler>(handler)](Request& req,
                                                Response& res) mutable {
        try {
            using ResultT = decltype(h(req, res));
            if constexpr (std::same_as<ResultT, void>) {
                h(req, res);
            }
            else {
                auto result = h(req, res);
                detail::WriteResult(res, std::move(result));
            }
        }
        catch (const std::exception& e) {
            res.set_status_and_content(HttpStatus::internal_server_error,
                                       e.what());
        }
        catch (...) {
            res.set_status_and_content(HttpStatus::internal_server_error,
                                       "unknown exception");
        }
    };
}

/// 把返回 Lazy<T> 的 Handler 包装为 Lazy<void> 协程 lambda。
/// 与 WrapSync 相同：统一兜底协程 handler 中抛出的异常，
/// 并支持协程返回值（Task<T> / Task<Result>）自动映射。
template <typename Handler>
auto WrapCoro(Handler&& handler) {
    return [h = std::forward<Handler>(handler)](Request& req,
                                                Response& res) mutable
               -> Task<> {
        try {
            using ResultT = lazy_value_t<decltype(h(req, res))>;
            if constexpr (std::same_as<ResultT, void>) {
                co_await h(req, res);
            }
            else {
                auto result = co_await h(req, res);
                detail::WriteResult(res, std::move(result));
            }
        }
        catch (const std::exception& e) {
            res.set_status_and_content(HttpStatus::internal_server_error,
                                       e.what());
        }
        catch (...) {
            res.set_status_and_content(HttpStatus::internal_server_error,
                                       "unknown exception");
        }
    };
}

/// 按返回类型选择包装方式：协程 Handler 统一转 Lazy<void>，其余直接转发。
template <typename Handler>
auto WrapHandler(Handler&& handler) {
    using Result = handler_result_t<std::decay_t<Handler>>;
    if constexpr (is_lazy_v<std::remove_reference_t<Result>>) {
        return WrapCoro(std::forward<Handler>(handler));
    }
    else {
        return WrapSync(std::forward<Handler>(handler));
    }
}

} // namespace detail

// ---- Router：路径、Method、Handler 绑定 ----

/// 路由：负责路径拼接与 handler 注册，支持 Group 嵌套与成员函数绑定。
class Router {
public:
    /// 默认构造（未绑定 server，需先 Init）。
    Router() = default;

    /// 绑定底层 server，prefix 为该路由的路径前缀。
    explicit Router(cinatra::coro_http_server& server, std::string prefix = {})
        : server_(&server),
          prefix_(std::move(prefix)),
          registered_(std::make_shared<std::set<std::string>>()) {}

    /// 绑定底层 server（供 Application 延迟初始化使用）。
    void Init(cinatra::coro_http_server& server) {
        server_ = &server;
        if (!registered_) {
            registered_ = std::make_shared<std::set<std::string>>();
        }
    }

    /// 创建子路由分组（前缀会与当前前缀拼接，并共享去重表）。
    Router Group(std::string_view path) const {
        return Router{server_, JoinPath(prefix_, path), registered_};
    }

    /// 注册 GET 路由。
    template <typename Handler, typename... Aspects>
    void Get(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        Map<cinatra::http_method::GET>(path, std::forward<Handler>(handler),
                                       std::forward<Aspects>(aspects)...);
    }

    /// 注册 GET 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Get(std::string_view path, Object* object, Method method,
             Aspects&&... aspects) {
        Map<cinatra::http_method::GET>(path, object, method,
                                       std::forward<Aspects>(aspects)...);
    }

    /// 注册 POST 路由。
    template <typename Handler, typename... Aspects>
    void Post(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        Map<cinatra::http_method::POST>(path, std::forward<Handler>(handler),
                                        std::forward<Aspects>(aspects)...);
    }

    /// 注册 POST 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Post(std::string_view path, Object* object, Method method,
              Aspects&&... aspects) {
        Map<cinatra::http_method::POST>(path, object, method,
                                        std::forward<Aspects>(aspects)...);
    }

    /// 注册 PUT 路由。
    template <typename Handler, typename... Aspects>
    void Put(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        Map<cinatra::http_method::PUT>(path, std::forward<Handler>(handler),
                                       std::forward<Aspects>(aspects)...);
    }

    /// 注册 PUT 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Put(std::string_view path, Object* object, Method method,
             Aspects&&... aspects) {
        Map<cinatra::http_method::PUT>(path, object, method,
                                       std::forward<Aspects>(aspects)...);
    }

    /// 注册 DELETE 路由。
    template <typename Handler, typename... Aspects>
    void Delete(std::string_view path, Handler&& handler,
                Aspects&&... aspects) {
        Map<cinatra::http_method::DEL>(path, std::forward<Handler>(handler),
                                       std::forward<Aspects>(aspects)...);
    }

    /// 注册 DELETE 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Delete(std::string_view path, Object* object, Method method,
                Aspects&&... aspects) {
        Map<cinatra::http_method::DEL>(path, object, method,
                                       std::forward<Aspects>(aspects)...);
    }

    /// 注册 PATCH 路由。
    template <typename Handler, typename... Aspects>
    void Patch(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        Map<cinatra::http_method::PATCH>(path, std::forward<Handler>(handler),
                                         std::forward<Aspects>(aspects)...);
    }

    /// 注册 PATCH 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Patch(std::string_view path, Object* object, Method method,
               Aspects&&... aspects) {
        Map<cinatra::http_method::PATCH>(path, object, method,
                                         std::forward<Aspects>(aspects)...);
    }

    /// 注册 OPTIONS 路由。
    template <typename Handler, typename... Aspects>
    void Options(std::string_view path, Handler&& handler,
                 Aspects&&... aspects) {
        Map<cinatra::http_method::OPTIONS>(path, std::forward<Handler>(handler),
                                           std::forward<Aspects>(aspects)...);
    }

    /// 注册 HEAD 路由。
    template <typename Handler, typename... Aspects>
    void Head(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        Map<cinatra::http_method::HEAD>(path, std::forward<Handler>(handler),
                                        std::forward<Aspects>(aspects)...);
    }

    /// 注册覆盖所有常用方法的路由。
    template <typename Handler, typename... Aspects>
    void Any(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        Map<cinatra::http_method::GET, cinatra::http_method::HEAD,
            cinatra::http_method::POST, cinatra::http_method::PUT,
            cinatra::http_method::PATCH, cinatra::http_method::DEL,
            cinatra::http_method::OPTIONS>(
            path, std::forward<Handler>(handler),
            std::forward<Aspects>(aspects)...);
    }

    /// 原始 Cinatra 服务对象（逃生通道）。
    cinatra::coro_http_server& Native() {
        assert(server_ != nullptr);
        return *server_;
    }

private:
    /// 内部构造：绑定 server 并共享去重表（Group 传播）。
    Router(cinatra::coro_http_server* server, std::string prefix,
           const std::shared_ptr<std::set<std::string>>& registered)
        : server_(server),
          prefix_(std::move(prefix)),
          registered_(registered) {}

    /// 拼接前缀与路径，统一补前导斜杠、去多余斜杠（空路径归一根路径）。
    static std::string JoinPath(std::string_view prefix, std::string_view path) {
        std::string out;
        out.reserve(prefix.size() + path.size() + 1);
        out.append(prefix);
        if (!path.empty()) {
            if (out.empty() || out.back() != '/') {
                out.push_back('/');
            }
            if (path.front() == '/') {
                path.remove_prefix(1);
            }
            out.append(path);
        }
        // 去尾斜杠（保留根路径 "/"）。
        while (out.size() > 1 && out.back() == '/') {
            out.pop_back();
        }
        if (out.empty()) {
            out.push_back('/');
        }
        else if (out.front() != '/') {
            out.insert(out.begin(), '/');
        }
        return out;
    }

    /// 记录路由 key（"METHOD path"）并检测重复注册。
    template <cinatra::http_method... Methods>
    void Dedupe(std::string_view path) {
        auto check = [&](cinatra::http_method method) {
            std::string key = std::string(cinatra::method_name(method)) + " " +
                              std::string(path);
            if (!registered_->insert(key).second) {
                throw std::invalid_argument("LWeb: 路由重复注册 " + key);
            }
        };
        (check(Methods), ...);
    }

    /// 绑定 handler（lambda/函数指针/仿函数，可携带 aspect）。
    template <cinatra::http_method... Methods, typename Handler,
              typename... Aspects>
        requires detail::HttpHandler<std::decay_t<Handler>>
    void Map(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        static_assert(sizeof...(Methods) >= 1,
                      "LWeb: 至少需要指定一个 HTTP method");
        static_assert(std::copy_constructible<std::decay_t<Handler>>,
                      "LWeb: Handler 必须是可拷贝的（cinatra 需要复制进 "
                      "std::function）");
        static_assert(
            (std::copy_constructible<std::decay_t<Aspects>> && ...),
            "LWeb: Aspect 必须是可拷贝的（cinatra 需要复制进 handler 包装）");
        assert(server_ != nullptr && "LWeb: Router 未绑定 server");
        auto full_path = JoinPath(prefix_, path);
        Dedupe<Methods...>(full_path);
        server_->set_http_handler<Methods...>(
            full_path, detail::WrapHandler(std::forward<Handler>(handler)),
            std::forward<Aspects>(aspects)...);
    }

    /// 绑定成员函数 Handler（方法签名固定为 (Request&, Response&)）。
    template <cinatra::http_method... Methods, typename Method, typename Object,
              typename... Aspects>
        requires std::is_member_function_pointer_v<std::remove_cvref_t<Method>>
    void Map(std::string_view path, Object* object, Method method,
             Aspects&&... aspects) {
        static_assert(
            std::is_invocable_v<Method, Object&, Request&, Response&>,
            "LWeb: 成员函数 Handler 签名必须是 (LWeb::Request&, "
            "LWeb::Response&) 并可在该控制器实例上调用");
        Map<Methods...>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<Aspects>(aspects)...);
    }

    cinatra::coro_http_server* server_ = nullptr;
    std::string prefix_;
    std::shared_ptr<std::set<std::string>> registered_;
};

// ---- Application：程序生命周期与服务所有权 ----

/// Application 配置项。
struct ApplicationOptions {
    uint16_t port = 8080;                    ///< 监听端口（0 表示由系统分配）。
    std::size_t threads =                    ///< 工作线程数。
        std::thread::hardware_concurrency();
    std::string address = "0.0.0.0";         ///< 监听地址。
};

/// Web 应用入口：拥有服务、路由与 Controller 生命周期。
class Application {
public:
    /// 以指定配置创建应用（路由注册必须在 Start/Run 之前完成）。
    explicit Application(ApplicationOptions options = {})
        : server_(options.threads, options.port, options.address) {
        router_.Init(server_);
    }

    /// 注册 Controller：只需成员 void Register(Router&)，依赖经构造函数
    /// 显式传入（可为空）。路径由 Controller 自己在 Register 里用 Group 组织，
    /// 框架不强制静态 Prefix。
    template <typename Controller, typename... Args>
    Controller& AddController(Args&&... args) {
        static_assert(requires(Controller& c, Router& r) { c.Register(r); },
                      "LWeb: Controller 需要成员 "
                      "void Register(LWeb::Router&)（路径在 Register 内 "
                      "通过 r.Group(...) 组织）");
        return RegisterObject<Controller>(std::forward<Args>(args)...);
    }

    /// 注册通用模块：只需成员 void Register(Router&)，依赖经构造函数显式传入。
    /// Module 与 Controller 行为一致，仅语义区分（Controller 可视为 Module 的
    /// 一种业务化命名）。
    template <typename Module, typename... Args>
    Module& AddModule(Args&&... args) {
        static_assert(requires(Module& m, Router& r) { m.Register(r); },
                      "LWeb: Module 需要成员 void Register(LWeb::Router&)");
        return RegisterObject<Module>(std::forward<Args>(args)...);
    }

    /// Vertical Slice：调用特征类型的静态方法 T::Map(Router&, 依赖...)，
    /// 不创建长期 Feature 对象。依赖生命周期由用户管理。
    template <typename Feature, typename... Args>
    void Map(Args&&... args) {
        static_assert(
            requires { Feature::Map(std::declval<Router&>(),
                                    std::declval<Args&>()...); },
            "LWeb: Feature 需要静态方法 "
            "static void Map(LWeb::Router&, 依赖...)");
        Feature::Map(router_, args...);
    }

    /// 注册 GET 路由。
    template <typename Handler, typename... Aspects>
    void Get(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Get(path, std::forward<Handler>(handler),
                    std::forward<Aspects>(aspects)...);
    }

    /// 注册 GET 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Get(std::string_view path, Object* object, Method method,
             Aspects&&... aspects) {
        router_.Get(path, object, method, std::forward<Aspects>(aspects)...);
    }

    /// 注册 POST 路由。
    template <typename Handler, typename... Aspects>
    void Post(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Post(path, std::forward<Handler>(handler),
                     std::forward<Aspects>(aspects)...);
    }

    /// 注册 POST 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Post(std::string_view path, Object* object, Method method,
              Aspects&&... aspects) {
        router_.Post(path, object, method, std::forward<Aspects>(aspects)...);
    }

    /// 注册 PUT 路由。
    template <typename Handler, typename... Aspects>
    void Put(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Put(path, std::forward<Handler>(handler),
                    std::forward<Aspects>(aspects)...);
    }

    /// 注册 PUT 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Put(std::string_view path, Object* object, Method method,
             Aspects&&... aspects) {
        router_.Put(path, object, method, std::forward<Aspects>(aspects)...);
    }

    /// 注册 DELETE 路由。
    template <typename Handler, typename... Aspects>
    void Delete(std::string_view path, Handler&& handler,
                Aspects&&... aspects) {
        router_.Delete(path, std::forward<Handler>(handler),
                       std::forward<Aspects>(aspects)...);
    }

    /// 注册 DELETE 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Delete(std::string_view path, Object* object, Method method,
                Aspects&&... aspects) {
        router_.Delete(path, object, method, std::forward<Aspects>(aspects)...);
    }

    /// 注册 PATCH 路由。
    template <typename Handler, typename... Aspects>
    void Patch(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Patch(path, std::forward<Handler>(handler),
                      std::forward<Aspects>(aspects)...);
    }

    /// 注册 PATCH 路由（成员函数绑定）。
    template <typename Object, typename Method, typename... Aspects>
    void Patch(std::string_view path, Object* object, Method method,
               Aspects&&... aspects) {
        router_.Patch(path, object, method, std::forward<Aspects>(aspects)...);
    }

    /// 注册 OPTIONS 路由。
    template <typename Handler, typename... Aspects>
    void Options(std::string_view path, Handler&& handler,
                 Aspects&&... aspects) {
        router_.Options(path, std::forward<Handler>(handler),
                        std::forward<Aspects>(aspects)...);
    }

    /// 注册 HEAD 路由。
    template <typename Handler, typename... Aspects>
    void Head(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Head(path, std::forward<Handler>(handler),
                     std::forward<Aspects>(aspects)...);
    }

    /// 注册覆盖所有常用方法的路由。
    template <typename Handler, typename... Aspects>
    void Any(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Any(path, std::forward<Handler>(handler),
                    std::forward<Aspects>(aspects)...);
    }

    /// 创建子路由分组。
    Router Group(std::string_view path) const {
        return router_.Group(path);
    }

    /// 根路由对象。
    Router& Routes() { return router_; }

    /// 同步启动并阻塞，返回启动错误码（服务停止时返回）。
    std::error_code Run() { return server_.sync_start(); }

    /// 异步启动（listen 已同步完成，返回的 Future 在停止时 ready）。
    async_simple::Future<std::error_code> Start() {
        return server_.async_start();
    }

    /// 停止服务并等待线程池退出。
    void Stop() { server_.stop(); }

    /// 实际监听端口（port=0 时为系统分配值）。
    uint16_t port() const { return server_.port(); }

    /// 原始 Cinatra 服务对象（逃生通道）。
    cinatra::coro_http_server& Native() { return server_; }

private:
    /// 创建可注册对象并调用其 Register(Router&)，由 Application 持有生命周期。
    template <typename T, typename... Args>
    T& RegisterObject(Args&&... args) {
        auto object = std::make_shared<T>(std::forward<Args>(args)...);
        object->Register(router_);
        controllers_.push_back(std::move(object));
        return *static_cast<T*>(controllers_.back().get());
    }

    // 析构顺序：server_ 先析构（join 线程池），Controller 最后析构，
    // 保证 in-flight handler 中捕获的 this 不会悬垂。
    std::vector<std::shared_ptr<void>> controllers_;
    Router router_;
    cinatra::coro_http_server server_;
};

// ---- Request helpers：不封装对象，只提供便捷函数 ----

/// 取请求头（不存在时返回空 view）。
inline std::string_view Header(Request& req, std::string_view name) {
    return req.get_header_value(name);
}

/// 取 URL 查询参数（不存在时返回空 view）。
inline std::string_view Query(Request& req, std::string_view key) {
    return req.get_query_value(key);
}

/// 取 REST 路径参数（如 "/users/:id" 中的 id）。
inline std::string_view Param(Request& req, std::string_view key) {
    auto it = req.params_.find(std::string(key));
    if (it == req.params_.end()) {
        return {};
    }
    return it->second;
}

/// 取请求体。
inline std::string_view Body(Request& req) {
    return req.get_body();
}

// ---- Response helpers ----

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

/// 以指定状态码与文本内容响应。
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

/// 400 Bad Request（文本消息）。
inline void BadRequest(Response& res, std::string_view message) {
    Status(res, HttpStatus::bad_request, message);
}

/// 401 Unauthorized（文本消息）。
inline void Unauthorized(Response& res, std::string_view message) {
    Status(res, HttpStatus::unauthorized, message);
}

/// 403 Forbidden（文本消息）。
inline void Forbidden(Response& res, std::string_view message) {
    Status(res, HttpStatus::forbidden, message);
}

/// 404 NotFound（文本消息）。
inline void NotFound(Response& res, std::string_view message) {
    Status(res, HttpStatus::not_found, message);
}

/// 500 Internal Server Error（文本消息）。
inline void InternalError(Response& res, std::string_view message) {
    Status(res, HttpStatus::internal_server_error, message);
}

/// 统一错误响应：{"error":{"code":...,"message":...}}。
inline void Error(Response& res, HttpStatus status, std::string_view code,
                  std::string_view message) {
    detail::SetContentType(res, "application/json");
    res.set_status_and_content(status,
                               detail::BuildErrorBody(code, message));
}

/// 400 Bad Request 统一错误响应。
inline void BadRequest(Response& res, std::string_view code,
                       std::string_view message) {
    Error(res, HttpStatus::bad_request, code, message);
}

/// 404 NotFound 统一错误响应。
inline void NotFound(Response& res, std::string_view code,
                       std::string_view message) {
    Error(res, HttpStatus::not_found, code, message);
}

// ---- Body helpers ----

/// 把请求体按 JSON 反序列化为类型 T；解析失败抛异常。
template <typename T, typename Codec = LWebJsonCodec>
T BodyJson(Request& req) {
    return Codec::template Deserialize<T>(req.get_body());
}

// ---- 返回值式 Handler 的结果类型映射（定义见上方 Result 命名空间） ----

namespace detail {

/// 把 Handler 的返回值写入响应：结果类型映射状态码，普通类型走 JSON。
inline void WriteResult(Response& res, Result::NoContent) {
    NoContent(res);
}

/// 404：有消息则输出文本，无消息则只设置状态码。
inline void WriteResult(Response& res, Result::NotFound not_found) {
    if (not_found.message.empty()) {
        res.set_status_and_content(HttpStatus::not_found, std::string());
    }
    else {
        LWeb::NotFound(res, not_found.message);
    }
}

/// 201 Created：负载走 JSON。
template <typename T>
void WriteResult(Response& res, Result::Created<T> created) {
    Json(res, created.value, HttpStatus::created);
}

/// 400：有消息则输出文本，无消息则只设置状态码。
inline void WriteResult(Response& res, Result::BadRequest bad_request) {
    if (bad_request.message.empty()) {
        res.set_status_and_content(HttpStatus::bad_request, std::string());
    }
    else {
        LWeb::BadRequest(res, bad_request.message);
    }
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

// ---- HttpClient：cinatra::coro_http_client 的便捷封装 ----

/// 协程 HTTP/HTTPS 客户端（默认使用全局 executor，需 C++20）。
class HttpClient {
public:
    /// 以基础 URL 创建客户端。
    explicit HttpClient(std::string base_url = {})
        : base_url_(std::move(base_url)) {}

    /// 不可拷贝、不可移动（底层 cinatra::coro_http_client 不可移动）。
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /// 原始 Cinatra 客户端（逃生通道）。
    cinatra::coro_http_client& Native() { return client_; }

    /// GET 请求。
    Task<ResponseData> Get(std::string_view path, HeaderMap headers = {}) {
        return client_.async_get(FullUrl(path), std::move(headers));
    }

    /// POST 请求（指定内容类型）。
    Task<ResponseData> Post(std::string_view path, std::string content,
                            cinatra::req_content_type content_type =
                                cinatra::req_content_type::string,
                            HeaderMap headers = {}) {
        return client_.async_post(FullUrl(path), std::move(content),
                                  content_type, std::move(headers));
    }

    /// PUT 请求。
    Task<ResponseData> Put(std::string_view path, std::string content,
                           cinatra::req_content_type content_type =
                               cinatra::req_content_type::string,
                           HeaderMap headers = {}) {
        return client_.async_put(FullUrl(path), std::move(content),
                                 content_type, std::move(headers));
    }

    /// DELETE 请求。
    Task<ResponseData> Delete(std::string_view path, std::string content = {},
                              cinatra::req_content_type content_type =
                                  cinatra::req_content_type::string,
                              HeaderMap headers = {}) {
        return client_.async_delete(FullUrl(path), std::move(content),
                                    content_type, std::move(headers));
    }

    /// PATCH 请求（cinatra 原生不支持 PATCH 带请求体）。
    Task<ResponseData> Patch(std::string_view path, HeaderMap headers = {}) {
        return client_.async_patch(FullUrl(path), std::move(headers));
    }

    /// POST JSON，返回原始响应。
    template <typename T>
    Task<ResponseData> PostJson(std::string_view path, const T& object,
                                HeaderMap headers = {}) {
        return client_.async_post(FullUrl(path), LWebJsonCodec::Serialize(object),
                                  cinatra::req_content_type::json,
                                  std::move(headers));
    }

    /// GET 并把响应体反序列化为 T；网络错误或 HTTP 错误状态抛异常。
    template <typename T, typename Codec = LWebJsonCodec>
    Task<T> GetJson(std::string_view path, HeaderMap headers = {}) {
        auto data = co_await client_.async_get(FullUrl(path),
                                               std::move(headers));
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
};

} // namespace LWeb

#endif // LTOOL_LWEB_INCLUDE
