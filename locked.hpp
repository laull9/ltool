#pragma once

#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <functional>
#include <concepts>

namespace locked_detail {

template<class M>
concept SharedLockable = requires(M& m) {
    m.lock_shared();
    m.unlock_shared();
};

template<class M>
using ReadLock =
    std::conditional_t<SharedLockable<M>, std::shared_lock<M>, std::unique_lock<M>>;

template<class M>
using WriteLock = std::unique_lock<M>;

} // namespace locked_detail

template<class T, class Mutex = std::shared_mutex>
class Locked {
private:
    T data_;
    mutable Mutex mutex_;
  
public:
    using value_type = T;
    using mutex_type = Mutex;

    Locked() requires std::default_initializable<T>
        : data_() {}

    template<class... Args>
    explicit Locked(std::in_place_t, Args&&... args)
        : data_(std::forward<Args>(args)...) {}

    explicit Locked(T value)
        : data_(std::move(value)) {}

    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;

    Locked(Locked&&) = delete;
    Locked& operator=(Locked&&) = delete;

private:
    template<bool Const, class Lock>
    class Guard {
    private:
        using Ptr = std::conditional_t<Const, const T*, T*>;

        Ptr ptr_;
        [[no_unique_address]] Lock lock_;

    public:
        Guard(Ptr ptr, Lock&& lock)
            : ptr_(ptr), lock_(std::move(lock)) {}

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        Guard(Guard&&) noexcept = default;
        Guard& operator=(Guard&&) noexcept = default;

        auto operator->() const {
            return ptr_;
        }

        decltype(auto) operator*() const {
            return *ptr_;
        }

        decltype(auto) get() const {
            return *ptr_;
        }

        bool owns_lock() const {
            return lock_.owns_lock();
        }
    };

public:
    using WriteGuard = Guard<false, locked_detail::WriteLock<Mutex>>;
    using ReadGuard  = Guard<true,  locked_detail::ReadLock<Mutex>>;

    WriteGuard wlock() {
        return WriteGuard(&data_, locked_detail::WriteLock<Mutex>(mutex_));
    }

    ReadGuard rlock() const {
        return ReadGuard(&data_, locked_detail::ReadLock<Mutex>(mutex_));
    }

    template<class F>
    decltype(auto) with_write(F&& f) {
        auto lock = locked_detail::WriteLock<Mutex>(mutex_);
        return std::invoke(std::forward<F>(f), data_);
    }

    template<class F>
    decltype(auto) with_read(F&& f) const {
        auto lock = locked_detail::ReadLock<Mutex>(mutex_);
        return std::invoke(std::forward<F>(f), std::as_const(data_));
    }

    template<class F>
    decltype(auto) operator()(F&& f) {
        return with_write(std::forward<F>(f));
    }

    template<class F>
    decltype(auto) operator()(F&& f) const {
        return with_read(std::forward<F>(f));
    }

    T copy() const requires std::copy_constructible<T> {
        return with_read([](const T& data) {
            return data;
        });
    }

    void assign(T value) {
        with_write([&](T& data) {
            data = std::move(value);
        });
    }

    template<class... Args>
    void emplace(Args&&... args) {
        with_write([&](T& data) {
            data = T(std::forward<Args>(args)...);
        });
    }
};