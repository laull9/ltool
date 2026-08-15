/**
 * @file LWeb.hpp
 * @brief ltool 的 Web 应用层：基于 Cinatra 的 Router/Controller/Response/
 *        Middleware/HttpClient 薄封装（聚合头）。
 *
 * 本文件为聚合入口，实现按职责拆分在 detail/LWeb/ 下：
 * @code
 * detail/LWeb/
 * ├── Types.hpp        底层别名 / Result / JSON Codec / Middleware concepts
 * ├── Error.hpp        HttpException / JsonParseError / BindingError 异常层级
 * ├── Traits.hpp       Handler 特征与同步/协程包装（异常归一化）
 * ├── Request.hpp      请求辅助 + Typed Binding（Param/Query/Header/BodyJson）
 * ├── Response.hpp     响应辅助 + 统一 JSON 错误协议 + 结果映射
 * ├── Router.hpp       路由：RouterGroup<M...> 编译期中间件组合 + 注册核心
 * ├── Application.hpp  应用入口（server 所有权 + 安全默认值 + 生命周期）
 * ├── Jwt.hpp          JWT v0.1：HS256 / Base64URL / JwtClaims / Encode/Verify
 * ├── Middleware.hpp   内置中间件（AccessLog/Cors/BearerAuth/...）
 * ├── JwtMiddleware.hpp JWT 认证中间件（JwtAuth/RequireClaim/RequireScope）
 * └── HttpClient.hpp   协程 HTTP 客户端
 * @endcode
 *
 * LWeb 不重新实现 HTTP、socket、协程、TLS 等底层能力，而是把 Cinatra 的
 * 路由注册、Controller 组织、Response/JSON、Middleware 转发整理成更顺手的
 * 形态。底层类型直接复用 Cinatra（Request/Response/Task 均为别名），并始终
 * 提供 Native() 逃生通道回到原生 API。
 *
 * 设计红线：
 * - Request/Response/Task 直接 alias，不重新造对象；
 * - 所有高级功能允许通过 Native() 回到 Cinatra；
 * - Controller/Module 不继承基类（duck typing），不强制静态 Prefix；
 * - 不做运行时 DI（依赖经构造函数/参数显式传入，生命周期由用户管理）；
 * - Middleware 是普通 C++ 类型（无虚函数、无 RTTI、无 runtime registry），
 *   Route/Group/Application 三级最终都编译期展开为 Cinatra Aspect pack，
 *   运行时唯一执行层是 Cinatra 原生 before/after fold；
 * - 请求级状态禁止存 middleware 成员（多线程共享）；
 * - 统一错误协议：HTTP 错误默认 JSON {"error":{"code","message"}}，
 *   只有显式 Text() 才返回普通文本；
 * - 异常归一化：HttpException -> 自身状态码，JsonParseError/BindingError -> 400，
 *   std::exception -> 500；
 * - JWT 是轻量认证基础设施（HS256/标准 claims/JwtAuth 中间件），不是
 *   Identity Framework：用户库/OAuth/Refresh Token/黑名单等是业务代码；
 * - JSON 使用 LJson（codec 可替换）；
 * - 不规定业务架构：Minimal API / Controller / Module / Vertical Slice 可混用。
 *
 * Middleware 执行语义（Cinatra 原生，LWeb 不重建 onion pipeline）：
 * - before 链按注册序（App 级 -> Group 级 -> Route 级），遇到 false 短路，
 *   跳过后续 before 与 Handler；
 * - after 链按注册序（与 before 相同，非逆序），且无论 before 是否中断都会
 *   执行（ok 在 handler 后被重置）；after 返回 false 跳过后续 after；
 * - 中间件不得抛异常（LWeb/Cinatra 均无法兜底）。
 *
 * 中间件设置方式（普通路由与 Group 均可）：
 * @code
 * // 1) 普通路由 + 路由级中间件（尾参，任意数量）：
 * app.Get("/profile", handler, LWeb::middleware::JwtAuth{},
 *                         LWeb::middleware::RequireClaim{"role", "admin"});
 * // 2) Group/嵌套 Group 继承：
 * auto api = app.Group("/api").Use(LWeb::middleware::JwtAuth{});
 * api.Get("/me", handler);
 * // 3) 普通路由先挂中间件再注册（Use 返回新类型，须接收返回值）：
 * auto r = app.Routes().Use(LWeb::middleware::AccessLog{});
 * r.Get("/health", handler);
 * @endcode
 *
 * 全局默认 JWT（便捷入口）：注册前配置一次，之后 JwtAuth{} 缺省使用：
 * @code
 * LWeb::SetDefaultJwt(LWeb::JwtOptions{.secret = config.jwt_secret});
 * app.Get("/me", handler, LWeb::middleware::JwtAuth{});  // 全局默认 JWT
 * auto token = LWeb::DefaultJwt().CreateToken("u-1", 2h);  // 签发
 * @endcode
 * 注意：SetDefaultJwt 必须在 Start()/Run() 之前调用（与路由注册同一阶段）。
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
 *         auto users = r.Group("/api/users")
 *                        .Use(LWeb::middleware::BearerAuth{
 *                            [](std::string_view t) { return t == "token"; }});
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
 * int main() {
 *     UserService svc;   // 依赖必须比 app 活得久
 *     LWeb::Application app{LWeb::ApplicationOptions{.port = 8080}};
 *
 *     auto root = app.Use(LWeb::middleware::AccessLog{});   // App 级中间件
 *     root.Get("/health", [](auto&, LWeb::Response& res) {  // Minimal API
 *         LWeb::Ok(res, "ok");
 *     });
 *     app.AddController<UserController>(svc);               // Controller
 *     return app.Run() ? 1 : 0;
 * }
 * @endcode
 *
 * 注意事项：
 * - 协程 Handler 支持 `[](auto&, auto&) -> LWeb::Task<>` 等泛型 lambda；
 * - 路由注册必须在 Start()/Run() 之前完成（之后注册与 in-flight 请求存在
 *   数据竞争）；
 * - Controller 生命周期由 Application 管理，析构时先停服务再销毁 Controller；
 *   但用户注入的依赖（构造参数）归用户管理，须活得比 Application 久；
 * - Header/Query/Param/Body 返回的 string_view 仅在 handler 执行期间有效，
 *   如需跨 co_await 使用请先拷贝为 std::string；
 * - HttpClient 单实例不宜并发使用（底层 cinatra::coro_http_client 不保证
 *   并发安全）；
 * - Handler 若是 mutable 有状态对象，状态会跨请求共享（cinatra 单
 *   std::function 可能被多线程并发调用），请保持 handler 无共享可变状态；
 * - Middleware 的 before/after 为同步执行；before 返回 false 中断请求后，
 *   after 链仍会执行（Cinatra 原生行为），after 不得假设 handler 已执行；
 * - Use()/Group() 返回新类型（值语义），必须接收返回值，丢弃 = 不生效。
 */

#ifndef LTOOL_LWEB_INCLUDE
#define LTOOL_LWEB_INCLUDE

#include "detail/LWeb/Error.hpp"
#include "detail/LWeb/Types.hpp"
#include "detail/LWeb/Traits.hpp"
#include "detail/LWeb/Request.hpp"
#include "detail/LWeb/Response.hpp"
#include "detail/LWeb/Router.hpp"
#include "detail/LWeb/Application.hpp"
#include "detail/LWeb/Jwt.hpp"
#include "detail/LWeb/Middleware.hpp"
#include "detail/LWeb/JwtMiddleware.hpp"
#include "detail/LWeb/HttpClient.hpp"

#endif // LTOOL_LWEB_INCLUDE
