/**
 * @file detail/LWeb/Application.hpp
 * @brief LWeb 应用入口：拥有 cinatra::coro_http_server 与 Controller
 *        生命周期，提供根路由与 Application 级中间件入口。
 *
 * 要点：
 * - Application 非模板：cinatra::coro_http_server 不可移动，必须由
 *   Application 持有；中间件类型由根 RouterGroup 承载（不存于此）；
 * - app.Use(...) 返回带中间件的根 Group，之后的注册必须通过返回值进行；
 * - app.Get/Post/... 等便捷方法注册在无中间件根上（兼容旧 API），
 *   需要中间件时请用 app.Use(...)/app.Routes().Use(...) 的返回值；
 * - 根访问器命名 Routes() 而非 Router()：成员函数 Router() 会遮蔽类型名
 *   Router（C++ 名查找陷阱）。
 */

#ifndef LTOOL_LWEB_DETAIL_APPLICATION_HPP
#define LTOOL_LWEB_DETAIL_APPLICATION_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "detail/LWeb/Router.hpp"

namespace LWeb {

/// Application 配置项。
///
/// 直接映射到 Cinatra server 能力：
/// - max_body_size  -> set_max_http_body_size
/// - max_header_size-> set_max_http_header_size
/// - idle_timeout   -> set_timeout_duration（空闲连接回收，0 表示禁用）
/// 高级配置请使用 Native()。
struct ApplicationOptions {
    std::string address = "0.0.0.0";         ///< 监听地址。
    uint16_t port = 8080;                    ///< 监听端口（0 表示由系统分配）。
    std::size_t threads =                    ///< 工作线程数。
        std::thread::hardware_concurrency();
    std::size_t max_body_size = 16 * 1024 * 1024;  ///< 请求体上限（字节）。
    std::size_t max_header_size = 8 * 1024;        ///< 请求头上限（字节）。
    std::chrono::seconds idle_timeout{60};        ///< 空闲连接回收超时（秒）。
};

/// Web 应用入口：拥有服务、路由与 Controller 生命周期。
class Application {
public:
    /// 以指定配置创建应用（路由注册必须在 Start/Run 之前完成）。
    explicit Application(ApplicationOptions options = {})
        : server_(options.threads, options.port, options.address) {
        server_.set_max_http_body_size(
            static_cast<std::int64_t>(options.max_body_size));
        server_.set_max_http_header_size(options.max_header_size);
        if (options.idle_timeout.count() > 0) {
            server_.set_timeout_duration(options.idle_timeout);
        }
        router_.Init(server_, owners_);
    }

    /// 根路由（空中间件）。注意：成员名不能用 Router() —— 会遮蔽类型名
    /// Router（C++ 名查找陷阱），故沿用现有命名 Routes()。
    RouterGroup<>& Routes() { return router_; }

    /// 追加 Application 级中间件：返回带中间件的根 Group。
    /// 之后的注册必须通过返回值进行（Use 改变类型，无法原地生效）。
    template <typename... NewMws>
        requires(Middleware<std::decay_t<NewMws>> && ...)
    [[nodiscard]] auto Use(NewMws&&... mws) {
        return router_.Use(std::forward<NewMws>(mws)...);
    }

    /// 根分组（空中间件）。
    [[nodiscard]] auto Group(std::string_view path) const {
        return router_.Group(path);
    }

    // ---- 便捷方法：注册到无中间件根上（兼容旧 API）----

    template <typename Handler, typename... Aspects>
    void Get(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Get(path, std::forward<Handler>(handler),
                    std::forward<Aspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... Aspects>
    void Get(std::string_view path, Object* object, Method method,
             Aspects&&... aspects) {
        router_.Get(path, object, method, std::forward<Aspects>(aspects)...);
    }

    template <typename Handler, typename... Aspects>
    void Post(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Post(path, std::forward<Handler>(handler),
                     std::forward<Aspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... Aspects>
    void Post(std::string_view path, Object* object, Method method,
              Aspects&&... aspects) {
        router_.Post(path, object, method, std::forward<Aspects>(aspects)...);
    }

    template <typename Handler, typename... Aspects>
    void Put(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Put(path, std::forward<Handler>(handler),
                    std::forward<Aspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... Aspects>
    void Put(std::string_view path, Object* object, Method method,
             Aspects&&... aspects) {
        router_.Put(path, object, method, std::forward<Aspects>(aspects)...);
    }

    template <typename Handler, typename... Aspects>
    void Delete(std::string_view path, Handler&& handler,
                Aspects&&... aspects) {
        router_.Delete(path, std::forward<Handler>(handler),
                       std::forward<Aspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... Aspects>
    void Delete(std::string_view path, Object* object, Method method,
                Aspects&&... aspects) {
        router_.Delete(path, object, method, std::forward<Aspects>(aspects)...);
    }

    template <typename Handler, typename... Aspects>
    void Patch(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Patch(path, std::forward<Handler>(handler),
                      std::forward<Aspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... Aspects>
    void Patch(std::string_view path, Object* object, Method method,
               Aspects&&... aspects) {
        router_.Patch(path, object, method, std::forward<Aspects>(aspects)...);
    }

    template <typename Handler, typename... Aspects>
    void Options(std::string_view path, Handler&& handler,
                 Aspects&&... aspects) {
        router_.Options(path, std::forward<Handler>(handler),
                        std::forward<Aspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... Aspects>
    void Options(std::string_view path, Object* object, Method method,
                 Aspects&&... aspects) {
        router_.Options(path, object, method, std::forward<Aspects>(aspects)...);
    }

    template <typename Handler, typename... Aspects>
    void Head(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Head(path, std::forward<Handler>(handler),
                     std::forward<Aspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... Aspects>
    void Head(std::string_view path, Object* object, Method method,
              Aspects&&... aspects) {
        router_.Head(path, object, method, std::forward<Aspects>(aspects)...);
    }

    template <typename Handler, typename... Aspects>
    void Any(std::string_view path, Handler&& handler, Aspects&&... aspects) {
        router_.Any(path, std::forward<Handler>(handler),
                    std::forward<Aspects>(aspects)...);
    }

    template <typename Object, typename Method, typename... Aspects>
    void Any(std::string_view path, Object* object, Method method,
             Aspects&&... aspects) {
        router_.Any(path, object, method, std::forward<Aspects>(aspects)...);
    }

    /// 注册 Controller：转发到根路由。注意根路由无中间件；
    /// Controller 中间件在 Register 内部通过 r.Use(...) 声明；
    /// 若要挂到带中间件的 Group 下，用该 Group 的 AddController（Register
    /// 需写为模板接受对应 Group 类型）。
    template <typename Controller, typename... Args>
    Controller& AddController(Args&&... args) {
        return router_.AddController<Controller>(
            std::forward<Args>(args)...);
    }

    /// 注册通用模块（与 Controller 行为一致，仅语义区分）。
    template <typename Module, typename... Args>
    Module& AddModule(Args&&... args) {
        return router_.AddModule<Module>(std::forward<Args>(args)...);
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
    /// 共享的 Controller/Module 存储：声明先于 server_，保证析构顺序为
    /// server_（join 线程池）→ router_ → owners_（销毁 Controller）。
    /// 用户注入的外部依赖（构造参数）不归 Application 管，须活得比 app 久。
    std::shared_ptr<std::vector<std::shared_ptr<void>>> owners_ =
        std::make_shared<std::vector<std::shared_ptr<void>>>();
    RouterGroup<> router_;
    cinatra::coro_http_server server_;
};

} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_APPLICATION_HPP
