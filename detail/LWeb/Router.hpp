/**
 * @file detail/LWeb/Router.hpp
 * @brief LWeb 路由：RouterGroup<Middlewares...> 编译期中间件组合与
 *        路由注册，以及唯一落地 Cinatra Aspect 的注册核心。
 *
 * 架构要点：
 * - RouterGroup 是唯一的路由注册类型，Router = RouterGroup<>（无中间件）；
 * - Group 中间件经 std::tuple + std::tuple_cat + std::apply 编译期组合，
 *   注册路由时展开进 aspect pack，直接转发 Cinatra set_http_handler；
 * - 运行时唯一执行层是 Cinatra 原生 do_before/do_after fold，不新增 pipeline。
 *
 * 执行顺序契约（Cinatra 原生行为，勿在本层模拟其他框架）：
 * - before 链：注册序，遇到 false 短路（跳过后续 before 与 handler）；
 * - after 链：注册序，before 中断后仍会执行（ok 在 handler 后被重置）。
 */

#ifndef LTOOL_LWEB_DETAIL_ROUTER_HPP
#define LTOOL_LWEB_DETAIL_ROUTER_HPP

#include <cassert>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "detail/LWeb/Traits.hpp"

namespace LWeb {
namespace detail {

/// 拼接前缀与路径，统一补前导斜杠、去多余斜杠（空路径归一根路径）。
inline std::string JoinPath(std::string_view prefix, std::string_view path) {
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

/// 路由注册核心：去重 + 包装 Handler + 转发 Cinatra Aspect。
/// 这是 LWeb 唯一调用 cinatra::set_http_handler 的地方。
template <cinatra::http_method... Methods, typename Handler,
          typename... Aspects>
void RegisterRoute(cinatra::coro_http_server& server,
                   const std::shared_ptr<std::set<std::string>>& registered,
                   std::string full_path, Handler&& handler,
                   Aspects&&... aspects) {
    static_assert(sizeof...(Methods) >= 1,
                  "LWeb: 至少需要指定一个 HTTP method");
    static_assert(std::copy_constructible<std::decay_t<Handler>>,
                  "LWeb: Handler 必须是可拷贝的（cinatra 需要复制进 "
                  "std::function）");
    static_assert(
        (std::copy_constructible<std::decay_t<Aspects>> && ...),
        "LWeb: Middleware 必须是可拷贝的（cinatra 需要复制进 handler 包装）");
    static_assert(
        (Middleware<std::decay_t<Aspects>> && ...),
        "LWeb: Middleware 必须提供 before 或 after（不能是既无 before 也无 "
        "after 的类型，否则 cinatra 会静默忽略）");

    auto check = [&](cinatra::http_method m) {
        std::string key =
            std::string(cinatra::method_name(m)) + " " + full_path;
        if (!registered->insert(key).second) {
            throw std::invalid_argument("LWeb: 路由重复注册 " + key);
        }
    };
    (check(Methods), ...);

    server.set_http_handler<Methods...>(
        std::move(full_path), WrapHandler(std::forward<Handler>(handler)),
        std::forward<Aspects>(aspects)...);
}

} // namespace detail

/// 路由分组：承载路径前缀、中间件 tuple 与共享的 Controller 存储。
///
/// 使用方式（值语义，Use/Group 返回新类型，必须接收返回值，勿丢弃）：
/// @code
/// auto api  = app.Routes().Group("/api").Use(Cors{});
/// auto sec  = api.Group("/secure").Use(BearerAuth{svc});
/// sec.Get("/profile", handler, RequireRole{"admin"});
/// @endcode
///
/// 中间件执行顺序（注册序）：Application 级 → Group 级（父→子）→ Route 级。
template <typename... Middlewares>
class RouterGroup {
public:
    static_assert((Middleware<Middlewares> && ...),
                  "LWeb: RouterGroup 只能持有 Middleware 类型");

    RouterGroup() = default;

    /// 绑定构造：server + 路径前缀 + 共享去重表 + 中间件 tuple + 共享存储。
    /// owners 为共享的 Controller/Module 存储（由 Application 创建并传播）。
    RouterGroup(cinatra::coro_http_server& server, std::string prefix,
                std::shared_ptr<std::set<std::string>> registered,
                std::tuple<Middlewares...> mws,
                std::shared_ptr<std::vector<std::shared_ptr<void>>> owners = {})
        : server_(&server),
          prefix_(std::move(prefix)),
          registered_(std::move(registered)),
          mws_(std::move(mws)),
          owners_(std::move(owners)) {}

    /// 延迟绑定 server（供 Application 使用）。
    void Init(
        cinatra::coro_http_server& server,
        std::shared_ptr<std::vector<std::shared_ptr<void>>> owners = {}) {
        server_ = &server;
        if (!registered_) {
            registered_ = std::make_shared<std::set<std::string>>();
        }
        if (owners) {
            owners_ = std::move(owners);
        }
    }

    /// 原始 Cinatra 服务对象（逃生通道）。
    cinatra::coro_http_server& Native() {
        assert(server_ != nullptr);
        return *server_;
    }

    // ---- 作用域组合 ----

    /// 嵌套分组：继承当前全部中间件，路径前缀加深。
    [[nodiscard]] auto Group(std::string_view path) const {
        return RouterGroup<Middlewares...>{*server_,
                                           detail::JoinPath(prefix_, path),
                                           registered_, mws_, owners_};
    }

    /// 追加中间件：编译期合并 tuple（parent + local）。
    /// 注意：Use 改变类型，必须使用返回值，丢弃 = 中间件不生效。
    template <typename... NewMws>
        requires(Middleware<std::decay_t<NewMws>> && ...)
    [[nodiscard]] auto Use(NewMws&&... mws) const {
        return RouterGroup<Middlewares..., std::decay_t<NewMws>...>{
            *server_, prefix_, registered_,
            std::tuple_cat(mws_,
                           std::make_tuple(std::forward<NewMws>(mws)...)),
            owners_};
    }

    // ---- 路由注册（verbs）----

    template <typename Handler, typename... RouteAspects>
    void Get(std::string_view path, Handler&& handler,
             RouteAspects&&... aspects) {
        Map<cinatra::http_method::GET>(path, std::forward<Handler>(handler),
                                       std::forward<RouteAspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... RouteAspects>
    void Get(std::string_view path, Object* object, Method method,
             RouteAspects&&... aspects) {
        Map<cinatra::http_method::GET>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<RouteAspects>(aspects)...);
    }

    template <typename Handler, typename... RouteAspects>
    void Post(std::string_view path, Handler&& handler,
              RouteAspects&&... aspects) {
        Map<cinatra::http_method::POST>(path, std::forward<Handler>(handler),
                                        std::forward<RouteAspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... RouteAspects>
    void Post(std::string_view path, Object* object, Method method,
              RouteAspects&&... aspects) {
        Map<cinatra::http_method::POST>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<RouteAspects>(aspects)...);
    }

    template <typename Handler, typename... RouteAspects>
    void Put(std::string_view path, Handler&& handler,
             RouteAspects&&... aspects) {
        Map<cinatra::http_method::PUT>(path, std::forward<Handler>(handler),
                                       std::forward<RouteAspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... RouteAspects>
    void Put(std::string_view path, Object* object, Method method,
             RouteAspects&&... aspects) {
        Map<cinatra::http_method::PUT>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<RouteAspects>(aspects)...);
    }

    template <typename Handler, typename... RouteAspects>
    void Delete(std::string_view path, Handler&& handler,
                RouteAspects&&... aspects) {
        Map<cinatra::http_method::DEL>(path, std::forward<Handler>(handler),
                                       std::forward<RouteAspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... RouteAspects>
    void Delete(std::string_view path, Object* object, Method method,
                RouteAspects&&... aspects) {
        Map<cinatra::http_method::DEL>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<RouteAspects>(aspects)...);
    }

    template <typename Handler, typename... RouteAspects>
    void Patch(std::string_view path, Handler&& handler,
               RouteAspects&&... aspects) {
        Map<cinatra::http_method::PATCH>(path, std::forward<Handler>(handler),
                                         std::forward<RouteAspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... RouteAspects>
    void Patch(std::string_view path, Object* object, Method method,
               RouteAspects&&... aspects) {
        Map<cinatra::http_method::PATCH>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<RouteAspects>(aspects)...);
    }

    template <typename Handler, typename... RouteAspects>
    void Options(std::string_view path, Handler&& handler,
                 RouteAspects&&... aspects) {
        Map<cinatra::http_method::OPTIONS>(path, std::forward<Handler>(handler),
                                           std::forward<RouteAspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... RouteAspects>
    void Options(std::string_view path, Object* object, Method method,
                 RouteAspects&&... aspects) {
        Map<cinatra::http_method::OPTIONS>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<RouteAspects>(aspects)...);
    }

    template <typename Handler, typename... RouteAspects>
    void Head(std::string_view path, Handler&& handler,
              RouteAspects&&... aspects) {
        Map<cinatra::http_method::HEAD>(path, std::forward<Handler>(handler),
                                        std::forward<RouteAspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... RouteAspects>
    void Head(std::string_view path, Object* object, Method method,
              RouteAspects&&... aspects) {
        Map<cinatra::http_method::HEAD>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<RouteAspects>(aspects)...);
    }

    template <typename Handler, typename... RouteAspects>
    void Any(std::string_view path, Handler&& handler,
             RouteAspects&&... aspects) {
        Map<cinatra::http_method::GET, cinatra::http_method::HEAD,
            cinatra::http_method::POST, cinatra::http_method::PUT,
            cinatra::http_method::PATCH, cinatra::http_method::DEL,
            cinatra::http_method::OPTIONS>(
            path, std::forward<Handler>(handler),
            std::forward<RouteAspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... RouteAspects>
    void Any(std::string_view path, Object* object, Method method,
             RouteAspects&&... aspects) {
        Map<cinatra::http_method::GET, cinatra::http_method::HEAD,
            cinatra::http_method::POST, cinatra::http_method::PUT,
            cinatra::http_method::PATCH, cinatra::http_method::DEL,
            cinatra::http_method::OPTIONS>(
            path,
            [object, method](Request& req, Response& res) mutable
                -> decltype(auto) {
                    return std::invoke(method, object, req, res);
                },
            std::forward<RouteAspects>(aspects)...);
    }

    // ---- Controller / Module 注册 ----
    // 在带中间件的 Group 上注册时，Controller::Register 需接受本 Group 类型：
    //   - 根 Group（Router = RouterGroup<>）上可用现有 void Register(Router&)；
    //   - 或把 Register 写成模板 template<typename Group> void Register(Group&)，
    //     即可挂到任意带中间件的 Group 下继承其中间件。
    template <typename Controller, typename... Args>
    Controller& AddController(Args&&... args) {
        static_assert(
            requires(Controller& c, RouterGroup& g) { c.Register(g); },
            "LWeb: Controller 需要成员 Register(...) 接受本 Group 类型");
        auto object = std::make_shared<Controller>(std::forward<Args>(args)...);
        object->Register(*this);
        if (!owners_) {
            owners_ = std::make_shared<std::vector<std::shared_ptr<void>>>();
        }
        owners_->push_back(std::move(object));
        return *static_cast<Controller*>(owners_->back().get());
    }

    template <typename Module, typename... Args>
    Module& AddModule(Args&&... args) {
        static_assert(requires(Module& m, RouterGroup& g) { m.Register(g); },
                      "LWeb: Module 需要成员 Register(...) 接受本 Group 类型");
        auto object = std::make_shared<Module>(std::forward<Args>(args)...);
        object->Register(*this);
        if (!owners_) {
            owners_ = std::make_shared<std::vector<std::shared_ptr<void>>>();
        }
        owners_->push_back(std::move(object));
        return *static_cast<Module*>(owners_->back().get());
    }

private:
    /// 注册核心：把「Group 中间件 tuple」与「Route 中间件」编译期合并为
    /// 一个 aspect pack 交给 detail::RegisterRoute。
    template <cinatra::http_method... Methods, typename Handler,
              typename... RouteAspects>
        requires detail::HttpHandler<std::decay_t<Handler>>
    void Map(std::string_view path, Handler&& handler,
             RouteAspects&&... aspects) {
        assert(server_ != nullptr && "LWeb: RouterGroup 未绑定 server");
        auto full_path = detail::JoinPath(prefix_, path);
        std::apply(
            [&](auto&... gmws) {
                // 展开顺序 = 中间件执行顺序：Group 中间件在前，Route 中间件在后
                detail::RegisterRoute<Methods...>(*server_, registered_,
                                                  full_path, handler, gmws...,
                                                  aspects...);
            },
            mws_);
    }

    cinatra::coro_http_server* server_ = nullptr;
    std::string prefix_;
    std::shared_ptr<std::set<std::string>> registered_;
    std::tuple<Middlewares...> mws_;
    /// 共享的 Controller/Module 存储（Application 创建，经 Group/Use 传播）。
    std::shared_ptr<std::vector<std::shared_ptr<void>>> owners_;
};

/// 无中间件的根路由（现有 void Register(LWeb::Router&) 契约保持不变）。
using Router = RouterGroup<>;

} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_ROUTER_HPP
