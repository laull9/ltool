/**
 * @file detail/LWeb/Traits.hpp
 * @brief LWeb Handler 特征与包装：协程判定、返回值式 Handler 适配、
 *        同步/协程包装、异常兜底。
 *
 * 依赖：Types.hpp（Request/Response/Result 等）。
 * 说明：detail::WriteResult 仅在声明，定义在 Response.hpp
 * （定义依赖响应辅助函数，故按文件顺序后置）。
 */

#ifndef LTOOL_LWEB_DETAIL_TRAITS_HPP
#define LTOOL_LWEB_DETAIL_TRAITS_HPP

#include <string>
#include <type_traits>
#include <utility>

#include "detail/LWeb/Types.hpp"

namespace LWeb {
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

// WriteResult 前置声明（定义在 Response.hpp）：
// 供 WrapSync/WrapCoro 在包装层调用，定义位于响应辅助函数之后。
inline void WriteResult(Response& res, Result::NoContent);
inline void WriteResult(Response& res, Result::NotFound not_found);
inline void WriteResult(Response& res, Result::BadRequest bad_request);
inline void WriteResult(Response& res, Result::Unauthorized unauthorized);
inline void WriteResult(Response& res, Result::Forbidden forbidden);
inline void WriteResult(Response& res, Result::Conflict conflict);
template <typename T>
void WriteResult(Response& res, Result::Created<T> created);
template <typename... Ts>
void WriteResult(Response& res, std::variant<Ts...> variant);
template <typename T>
void WriteResult(Response& res, T&& value);

// 统一错误响应写入（定义在 Response.hpp）：
// 包装层把 HttpException / 内部异常映射为 JSON 错误体。
inline void WriteErrorResponse(Response& res, const HttpException& e);
inline void WriteInternalError(Response& res, std::string_view message);

/// 把 Handler 包装为类型化同步 lambda（消除泛型 lambda 对 function_traits
/// 的依赖）。统一异常归一化：cinatra 对不同路由类型（exact / :param / regex）
/// 的异常捕获路径不一致，这里统一映射：
///   HttpException -> 自身 status（JSON 错误体）
///   std::exception -> 500（JSON 错误体）
///   unknown        -> 500
/// Handler 可返回 void（直接操作 res），也可返回结果类型（Result::* /
/// 普通类型）由框架自动映射状态码。
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
        catch (const HttpException& e) {
            detail::WriteErrorResponse(res, e);
        }
        catch (const std::exception& e) {
            detail::WriteInternalError(res, e.what());
        }
        catch (...) {
            detail::WriteInternalError(res, "unknown exception");
        }
    };
}

/// 把返回 Lazy<T> 的 Handler 包装为 Lazy<void> 协程 lambda。
/// 与 WrapSync 相同：统一异常归一化（见 WrapSync 注释），并支持协程
/// 返回值（Task<T> / Task<Result>）自动映射。
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
        catch (const HttpException& e) {
            detail::WriteErrorResponse(res, e);
        }
        catch (const std::exception& e) {
            detail::WriteInternalError(res, e.what());
        }
        catch (...) {
            detail::WriteInternalError(res, "unknown exception");
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
} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_TRAITS_HPP
