/**
 * @file LResult.hpp
 * @brief 轻量结果类型 LResult<T, E> 与作用域退出守卫 LScopeGuard。
 *
 * LResult 提供类似 std::expected 的值/错误二态容器（C++17+ 可用，无需 C++23），
 * 适用于不希望靠异常控制流程的场景；LScopeGuard 在离开作用域时自动执行清理回调。
 *
 * 主要接口：
 * - LResult<T, E>      ：支持 ok()/err()、has_value()/is_ok()/is_err()、value()/value_or()、
 *                        map()/and_then()/or_else()/map_error() 与 LResult<void, E> 特化。
 * - LErr<E> / LTool::err()：显式构造错误态，避免 T 与 E 同类型时产生歧义。
 * - LScopeGuard / make_scope_exit()：RAII 退出守卫，支持 dismiss() 取消触发。
 *
 * 常用示例：
 * @code
 * LResult<int, std::string> parse_port(std::string_view s) {
 *     if (s.empty()) return LTool::err(std::string("empty port"));
 *     return 8080;
 * }
 *
 * auto port = parse_port("8080")
 *     .map([](int p) { return p + 1; })
 *     .value_or(80);
 *
 * auto guard = LTool::make_scope_exit([&] { cleanup(); });
 * @endcode
 */

#ifndef LTOOL_LRESULT_INCLUDE
#define LTOOL_LRESULT_INCLUDE

#include "detail/LToolConfig.hpp"

#if !LTOOL_HAS_CPP17
#error "LResult requires C++17 or later"
#endif

#include <exception>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace LResultDetail {

/// 把错误对象转为可读异常消息。
template<class E>
inline std::string format_error_message(const E& err) {
    if constexpr (std::is_convertible_v<const E&, std::string>) {
        return std::string(err);
    } else {
        return "LResult accessed value in error state";
    }
}

/// 构造错误态时的包装判断 trait。
template<class T>
struct is_lerr : std::false_type {};

} // namespace LResultDetail

/// 显式错误值包装，用于消除 LResult<T, E> 中 T 与 E 同类型或可互转时的构造歧义。
template<class E>
struct LErr {
    E error; ///< 携带的错误对象。

    /// 从左值错误构造。
    explicit constexpr LErr(const E& value)
        : error(value) {}

    /// 从右值错误构造。
    explicit constexpr LErr(E&& value) noexcept(std::is_nothrow_move_constructible_v<E>)
        : error(std::move(value)) {}
};

namespace LResultDetail {

template<class E>
struct is_lerr<LErr<E>> : std::true_type {};

} // namespace LResultDetail

/// 构造错误包装 LErr<E> 的辅助函数。
template<class E>
inline constexpr LErr<std::decay_t<E>> make_err(E&& error) {
    return LErr<std::decay_t<E>>(std::forward<E>(error));
}

/// 表示操作成功（T）或失败（E）的值/错误二态容器。
template<class T, class E = std::string>
class LResult {
public:
    using value_type = T; ///< 成功值类型。
    using error_type = E; ///< 错误值类型。

    /// 构造包含成功值的结果。
    static LResult ok(T value) {
        return LResult(std::move(value));
    }

    /// 构造包含错误值的结果。
    static LResult err(E error) {
        return LResult(LErr<E>(std::move(error)));
    }

    /// 从可转换为 T 的值隐式构造成功态（排除 LResult / LErr）。
    template<class U = T,
             std::enable_if_t<
                 !std::is_same_v<std::decay_t<U>, LResult> &&
                 !LResultDetail::is_lerr<std::decay_t<U>>::value &&
                 std::is_convertible_v<U&&, T>, int> = 0>
    LResult(U&& value)
        : storage_(std::in_place_index<0>, std::forward<U>(value)) {}

    /// 从 LErr<E2> 构造错误态。
    template<class E2, std::enable_if_t<std::is_convertible_v<const E2&, E>, int> = 0>
    LResult(const LErr<E2>& err_val)
        : storage_(std::in_place_index<1>, err_val.error) {}

    /// 从右值 LErr<E2> 构造错误态。
    template<class E2, std::enable_if_t<std::is_convertible_v<E2&&, E>, int> = 0>
    LResult(LErr<E2>&& err_val)
        : storage_(std::in_place_index<1>, std::move(err_val.error)) {}

    /// 判断是否持有成功值。
    [[nodiscard]] bool has_value() const noexcept {
        return storage_.index() == 0;
    }

    /// has_value() 的同义函数。
    [[nodiscard]] bool is_ok() const noexcept {
        return has_value();
    }

    /// 判断是否处于错误态。
    [[nodiscard]] bool is_err() const noexcept {
        return !has_value();
    }

    /// 布尔上下文：持有成功值时为 true。
    explicit operator bool() const noexcept {
        return has_value();
    }

    /// 获取成功值左值引用；处于错误态时抛出 std::runtime_error。
    T& value() & {
        ensure_ok();
        return std::get<0>(storage_);
    }

    /// 获取成功值只读引用；处于错误态时抛出 std::runtime_error。
    const T& value() const& {
        ensure_ok();
        return std::get<0>(storage_);
    }

    /// 获取成功值右值引用；处于错误态时抛出 std::runtime_error。
    T&& value() && {
        ensure_ok();
        return std::move(std::get<0>(storage_));
    }

    /// 获取成功值，失败时返回 fallback。
    template<class U>
    T value_or(U&& fallback) const& {
        return has_value() ? std::get<0>(storage_) : static_cast<T>(std::forward<U>(fallback));
    }

    /// 获取成功值（移动），失败时返回 fallback。
    template<class U>
    T value_or(U&& fallback) && {
        return has_value() ? std::move(std::get<0>(storage_))
                           : static_cast<T>(std::forward<U>(fallback));
    }

    /// 获取错误值引用；处于成功态时抛出 std::logic_error。
    E& error() & {
        ensure_err();
        return std::get<1>(storage_);
    }

    /// 获取错误值只读引用；处于成功态时抛出 std::logic_error。
    const E& error() const& {
        ensure_err();
        return std::get<1>(storage_);
    }

    /// 获取错误值右值引用；处于成功态时抛出 std::logic_error。
    E&& error() && {
        ensure_err();
        return std::move(std::get<1>(storage_));
    }

    /// 解引用访问成功值（不做空检查，需先判断 has_value()）。
    T& operator*() & { return std::get<0>(storage_); }
    /// 解引用只读访问成功值。
    const T& operator*() const& { return std::get<0>(storage_); }
    /// 解引用移动访问成功值。
    T&& operator*() && { return std::move(std::get<0>(storage_)); }

    /// 成员访问成功值。
    T* operator->() { return &std::get<0>(storage_); }
    /// 只读成员访问成功值。
    const T* operator->() const { return &std::get<0>(storage_); }

    /// 成功时对值应用 fn(value) 并返回新 LResult<U, E>；失败时原样透传错误。
    template<class F>
    auto map(F&& fn) const& {
        using U = std::decay_t<std::invoke_result_t<F, const T&>>;
        if constexpr (std::is_void_v<U>) {
            if (has_value()) {
                std::invoke(std::forward<F>(fn), std::get<0>(storage_));
                return LResult<void, E>::ok();
            }
            return LResult<void, E>::err(std::get<1>(storage_));
        } else {
            if (has_value()) {
                return LResult<U, E>::ok(std::invoke(std::forward<F>(fn), std::get<0>(storage_)));
            }
            return LResult<U, E>::err(std::get<1>(storage_));
        }
    }

    /// 成功时对值应用 fn(std::move(value)) 并返回新 LResult<U, E>。
    template<class F>
    auto map(F&& fn) && {
        using U = std::decay_t<std::invoke_result_t<F, T&&>>;
        if constexpr (std::is_void_v<U>) {
            if (has_value()) {
                std::invoke(std::forward<F>(fn), std::move(std::get<0>(storage_)));
                return LResult<void, E>::ok();
            }
            return LResult<void, E>::err(std::move(std::get<1>(storage_)));
        } else {
            if (has_value()) {
                return LResult<U, E>::ok(
                    std::invoke(std::forward<F>(fn), std::move(std::get<0>(storage_))));
            }
            return LResult<U, E>::err(std::move(std::get<1>(storage_)));
        }
    }

    /// 失败时对错误应用 fn(error) 转换为新错误类型。
    template<class F>
    auto map_error(F&& fn) const& {
        using E2 = std::decay_t<std::invoke_result_t<F, const E&>>;
        if (has_value()) {
            return LResult<T, E2>::ok(std::get<0>(storage_));
        }
        return LResult<T, E2>::err(std::invoke(std::forward<F>(fn), std::get<1>(storage_)));
    }

    /// 成功时链式调用返回 LResult<U, E> 的函数。
    template<class F>
    auto and_then(F&& fn) const& {
        using R = std::decay_t<std::invoke_result_t<F, const T&>>;
        if (has_value()) {
            return std::invoke(std::forward<F>(fn), std::get<0>(storage_));
        }
        return R::err(std::get<1>(storage_));
    }

    /// 成功时链式调用返回 LResult<U, E> 的函数（移动值）。
    template<class F>
    auto and_then(F&& fn) && {
        using R = std::decay_t<std::invoke_result_t<F, T&&>>;
        if (has_value()) {
            return std::invoke(std::forward<F>(fn), std::move(std::get<0>(storage_)));
        }
        return R::err(std::move(std::get<1>(storage_)));
    }

    /// 失败时链式恢复：调用返回 LResult<T, E2> 的函数。
    template<class F>
    auto or_else(F&& fn) const& {
        using R = std::decay_t<std::invoke_result_t<F, const E&>>;
        if (has_value()) {
            return R::ok(std::get<0>(storage_));
        }
        return std::invoke(std::forward<F>(fn), std::get<1>(storage_));
    }

private:
    void ensure_ok() const {
        if (!has_value()) {
            throw std::runtime_error(LResultDetail::format_error_message(std::get<1>(storage_)));
        }
    }

    void ensure_err() const {
        if (has_value()) {
            throw std::logic_error("LResult accessed error in ok state");
        }
    }

    std::variant<T, E> storage_; ///< 值/错误存储。
};

/// void 成功值的 LResult 特化。
template<class E>
class LResult<void, E> {
public:
    using value_type = void; ///< 成功值类型（void）。
    using error_type = E;    ///< 错误值类型。

    /// 默认构造成功态。
    LResult() = default;

    /// 构造成功态。
    static LResult ok() {
        return LResult();
    }

    /// 构造错误态。
    static LResult err(E error) {
        return LResult(LErr<E>(std::move(error)));
    }

    /// 从 LErr<E2> 构造错误态。
    template<class E2, std::enable_if_t<std::is_convertible_v<const E2&, E>, int> = 0>
    LResult(const LErr<E2>& err_val)
        : error_(err_val.error) {}

    /// 从右值 LErr<E2> 构造错误态。
    template<class E2, std::enable_if_t<std::is_convertible_v<E2&&, E>, int> = 0>
    LResult(LErr<E2>&& err_val)
        : error_(std::move(err_val.error)) {}

    /// 判断是否成功。
    [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }
    /// has_value() 的同义函数。
    [[nodiscard]] bool is_ok() const noexcept { return has_value(); }
    /// 判断是否失败。
    [[nodiscard]] bool is_err() const noexcept { return error_.has_value(); }
    /// 布尔上下文：成功时为 true。
    explicit operator bool() const noexcept { return has_value(); }

    /// 校验处于成功态；失败时抛出 std::runtime_error。
    void value() const {
        if (error_) {
            throw std::runtime_error(LResultDetail::format_error_message(*error_));
        }
    }

    /// 获取错误值引用；成功时抛出 std::logic_error。
    E& error() & {
        ensure_err();
        return *error_;
    }

    /// 获取错误值只读引用；成功时抛出 std::logic_error。
    const E& error() const& {
        ensure_err();
        return *error_;
    }

    /// 获取错误值右值引用；成功时抛出 std::logic_error。
    E&& error() && {
        ensure_err();
        return std::move(*error_);
    }

    /// 成功时调用 fn() 并返回新 LResult<U, E>。
    template<class F>
    auto map(F&& fn) const {
        using U = std::decay_t<std::invoke_result_t<F>>;
        if constexpr (std::is_void_v<U>) {
            if (has_value()) {
                std::invoke(std::forward<F>(fn));
                return LResult<void, E>::ok();
            }
            return LResult<void, E>::err(*error_);
        } else {
            if (has_value()) {
                return LResult<U, E>::ok(std::invoke(std::forward<F>(fn)));
            }
            return LResult<U, E>::err(*error_);
        }
    }

    /// 成功时链式调用返回 LResult<U, E> 的无参函数。
    template<class F>
    auto and_then(F&& fn) const {
        using R = std::decay_t<std::invoke_result_t<F>>;
        if (has_value()) {
            return std::invoke(std::forward<F>(fn));
        }
        return R::err(*error_);
    }

    /// 失败时链式恢复。
    template<class F>
    auto or_else(F&& fn) const {
        using R = std::decay_t<std::invoke_result_t<F, const E&>>;
        if (has_value()) {
            return R::ok();
        }
        return std::invoke(std::forward<F>(fn), *error_);
    }

private:
    void ensure_err() const {
        if (!error_) {
            throw std::logic_error("LResult accessed error in ok state");
        }
    }

    std::optional<E> error_; ///< 错误存储；为空表示成功。
};

/// 作用域退出守卫：析构时自动执行清理回调，除非调用 dismiss() 取消。
template<class F>
class LScopeGuard {
public:
    /// 用清理回调构造并激活守卫。
    explicit LScopeGuard(F fn) noexcept(std::is_nothrow_move_constructible_v<F>)
        : fn_(std::move(fn)), active_(true) {}

    /// 禁止复制。
    LScopeGuard(const LScopeGuard&) = delete;
    LScopeGuard& operator=(const LScopeGuard&) = delete;

    /// 移动构造：接管所有权并将原守卫设为非激活。
    LScopeGuard(LScopeGuard&& other) noexcept(std::is_nothrow_move_constructible_v<F>)
        : fn_(std::move(other.fn_)), active_(other.active_) {
        other.active_ = false;
    }

    /// 禁止移动赋值。
    LScopeGuard& operator=(LScopeGuard&&) = delete;

    /// 析构时若仍处于激活状态则执行清理回调。
    ~LScopeGuard() noexcept {
        if (active_) {
            try {
                fn_();
            } catch (...) {
            }
        }
    }

    /// 取消清理回调执行。
    void dismiss() noexcept {
        active_ = false;
    }

    /// 判断守卫当前是否仍激活。
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

private:
    F fn_;        ///< 清理回调。
    bool active_; ///< 是否在析构时触发。
};

/// 构造作用域退出守卫 LScopeGuard。
template<class F>
[[nodiscard]] inline LScopeGuard<std::decay_t<F>> make_scope_exit(F&& fn) {
    return LScopeGuard<std::decay_t<F>>(std::forward<F>(fn));
}

namespace LTool {
template<class T, class E = std::string>
using Result = ::LResult<T, E>; ///< 结果类型别名。
template<class E>
using Err = ::LErr<E>;          ///< 错误包装别名。
template<class F>
using ScopeGuard = ::LScopeGuard<F>; ///< 作用域守卫别名。
using ::make_err;                    ///< 构造 LErr 辅助函数。
using ::make_scope_exit;             ///< 构造作用域守卫辅助函数。

/// 构造 LErr<E> 的简短别名函数。
template<class E>
inline constexpr LErr<std::decay_t<E>> err(E&& error) {
    return ::make_err(std::forward<E>(error));
}
} // namespace LTool

#endif // LTOOL_LRESULT_INCLUDE
