#pragma once

/**
 * @file Locked.hpp
 * @brief 互斥保护值包装，提供读锁/写锁安全的成员访问。
 *
 * Locked 用 shared_mutex（或传入的 Mutex）保护一个值 T，通过 RAII 守卫
 * 或回调式接口安全访问：
 * - wlock()/rlock() 返回可解引用的锁守卫，锁在守卫析构时释放。
 * - with_write()/with_read() 在锁内执行回调，避免锁逃逸。
 * - copy()/assign()/emplace() 提供常用整体读写捷径。
 *
 * 依赖 C++20 concept。非拷贝、非移动：对象必须常驻原地。
 *
 * 常用示例：
 * @code
 * Locked<std::vector<int>> xs;
 * xs.with_write([](std::vector<int>& v) { v.push_back(1); });
 * auto snapshot = xs.copy();
 * if (auto guard = xs.rlock()) { fmt::println("{}", guard->size()); }
 * @endcode
 */

#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <functional>
#include <concepts>

namespace LockedDetail {

/// 判断 Mutex 是否支持共享（读）锁。
template<class M>
concept SharedLockable = requires(M& m) {
    m.lock_shared();
    m.unlock_shared();
};

/// 读锁类型：共享锁可用时用 shared_lock，否则退回 unique_lock。
template<class M>
using ReadLock =
    std::conditional_t<SharedLockable<M>, std::shared_lock<M>, std::unique_lock<M>>;

/// 写锁类型：独占锁。
template<class M>
using WriteLock = std::unique_lock<M>;

} // namespace LockedDetail

/**
 * @brief 互斥保护的值包装。
 *
 * @tparam T     被保护的值类型。
 * @tparam Mutex 锁类型，默认 std::shared_mutex。
 */
template<class T, class Mutex = std::shared_mutex>
class Locked {
private:
    T data_;              ///< 被保护的数据。
    mutable Mutex mutex_; ///< 保护 data_ 的互斥锁。

public:
    using value_type = T;     ///< 被保护值的类型。
    using mutex_type = Mutex; ///< 锁类型。

    /// 默认构造；要求 T 可默认初始化。
    Locked() requires std::default_initializable<T>
        : data_() {}

    /// 就地构造 T，转发任意构造参数。
    template<class... Args>
    explicit Locked(std::in_place_t, Args&&... args)
        : data_(std::forward<Args>(args)...) {}

    /// 用已有值构造。
    explicit Locked(T value)
        : data_(std::move(value)) {}

    /// 禁止复制。
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;

    /// 禁止移动。
    Locked(Locked&&) = delete;
    Locked& operator=(Locked&&) = delete;

private:
    /// RAII 锁守卫：持有锁并暴露被保护值的指针/引用。
    template<bool Const, class Lock>
    class Guard {
    private:
        using Ptr = std::conditional_t<Const, const T*, T*>;

        Ptr ptr_;    ///< 指向被保护数据的指针。
#if defined(_MSC_VER)
        [[msvc::no_unique_address]] Lock lock_; ///< 持有的锁。
#else
        [[no_unique_address]] Lock lock_; ///< 持有的锁。
#endif

    public:
        /// 绑定指针和锁构造守卫。
        Guard(Ptr ptr, Lock&& lock)
            : ptr_(ptr), lock_(std::move(lock)) {}

        /// 禁止复制。
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        /// 可移动。
        Guard(Guard&&) noexcept = default;
        Guard& operator=(Guard&&) noexcept = default;

        /// 指针访问被保护数据。
        auto operator->() const {
            return ptr_;
        }

        /// 引用访问被保护数据。
        decltype(auto) operator*() const {
            return *ptr_;
        }

        /// 以引用返回被保护数据。
        decltype(auto) get() const {
            return *ptr_;
        }

        /// 判断锁当前是否被持有。
        bool owns_lock() const {
            return lock_.owns_lock();
        }
    };

public:
    using WriteGuard = Guard<false, LockedDetail::WriteLock<Mutex>>; ///< 写锁守卫。
    using ReadGuard  = Guard<true,  LockedDetail::ReadLock<Mutex>>;  ///< 读锁守卫。

    /// 获取写锁守卫；写守卫可修改被保护数据。
    WriteGuard wlock() {
        return WriteGuard(&data_, LockedDetail::WriteLock<Mutex>(mutex_));
    }

    /// 获取读锁守卫；读守卫只能读取被保护数据。
    ReadGuard rlock() const {
        return ReadGuard(&data_, LockedDetail::ReadLock<Mutex>(mutex_));
    }

    /// 在写锁内执行回调 f(data)；回调结果按值返回。
    template<class F>
    // Callback results are returned by value so references cannot outlive the lock.
    auto with_write(F&& f) {
        auto lock = LockedDetail::WriteLock<Mutex>(mutex_);
        return std::invoke(std::forward<F>(f), data_);
    }

    /// 在读锁内执行回调 f(data)；回调结果按值返回。
    template<class F>
    // Callback results are returned by value so references cannot outlive the lock.
    auto with_read(F&& f) const {
        auto lock = LockedDetail::ReadLock<Mutex>(mutex_);
        return std::invoke(std::forward<F>(f), std::as_const(data_));
    }

    /// 可调用对象形式：等价于 with_write()。
    template<class F>
    auto operator()(F&& f) {
        return with_write(std::forward<F>(f));
    }

    /// 可调用对象形式（const）：等价于 with_read()。
    template<class F>
    auto operator()(F&& f) const {
        return with_read(std::forward<F>(f));
    }

    /// 在锁内复制返回被保护数据的副本；要求 T 可复制构造。
    T copy() const requires std::copy_constructible<T> {
        return with_read([](const T& data) {
            return data;
        });
    }

    /// 在写锁内整体赋值。
    void assign(T value) {
        with_write([&](T& data) {
            data = std::move(value);
        });
    }

    /// 在写锁内就地重建被保护数据（异常安全）。
    ///
    /// 与 std::optional::emplace 的"原地构造"语义一致：先销毁旧值，再在
    /// 原内存位置直接构造新值。T 可移动/可拷贝构造时，先在临时对象中完成
    /// 参数构造——若参数构造抛异常，原值不受影响；随后销毁旧值并原地放置
    /// 新值（需要 T 可移动构造，不可移动时退化为拷贝构造）。注意：该放置
    /// 阶段（placement move/copy）本身若抛出（如移动/拷贝构造函数抛异常或
    /// bad_alloc），data_ 已处于销毁状态，调用方需自行恢复——与直接原地构造
    /// 分支的权衡一致。T 既不可移动也不可拷贝构造时退化为直接原地构造，
    /// 此时若参数构造抛异常，data_ 同样已处于销毁状态，调用方需自行处理。
    template<class... Args>
    void emplace(Args&&... args) {
        if constexpr (std::move_constructible<T> || std::copy_constructible<T>) {
            T tmp(std::forward<Args>(args)...);
            data_.~T(); // 析构不抛异常
            if constexpr (std::move_constructible<T>) {
                ::new (static_cast<void*>(&data_)) T(std::move(tmp));
            } else {
                ::new (static_cast<void*>(&data_)) T(tmp);
            }
        } else {
            data_.~T();
            ::new (static_cast<void*>(&data_)) T(std::forward<Args>(args)...);
        }
    }
};
