/**
 * @file LThreadPool.hpp
 * @brief BS_thread_pool 的 ltool 风格薄封装，纯头文件、轻量易用。
 *
 * LThreadPool 的核心定位：
 * - 直接复用仓库内置 pkgs/BS_thread_pool.hpp，不额外引入第三方构建依赖。
 * - 提供 submit/post/parallel_for/wait 等更短的常用入口。
 * - 保留 native() 访问底层线程池，复杂场景仍可使用 BS_thread_pool 原生 API。
 *
 * 常用示例：
 * @code
 * LThreadPool pool(4);
 * auto future = pool.submit([] { return 42; });
 * pool.parallel_for(0, 100, [](int i) { work(i); }).wait();
 * auto answer = future.get();
 * @endcode
 */

#ifndef LTOOL_LTHREAD_POOL_INCLUDE
#define LTOOL_LTHREAD_POOL_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LConcepts.hpp"

#if !LTOOL_HAS_THREAD_POOL
#error "LThreadPool requires C++17 or later"
#endif

#include "pkgs/BS_thread_pool.hpp"

#include <chrono>
#include <cstddef>
#include <future>
#include <type_traits>
#include <utility>

/**
 * @brief BS::thread_pool 的轻量包装模板。
 *
 * 默认 LThreadPool 不启用 BS_thread_pool 的可选特性，保持最快最轻。需要优先级或
 * 暂停能力时可使用 LPriorityThreadPool、LPauseThreadPool，或直接使用
 * BasicLThreadPool<...>。
 */
template<BS::tp Flags = BS::tp::none>
class BasicLThreadPool {
public:
    using native_type = BS::thread_pool<Flags>; ///< 底层 BS_thread_pool 类型。
    using priority_t = BS::priority_t;          ///< 任务优先级类型。

    /// 默认构造：按硬件并发数创建线程池。
    BasicLThreadPool()
        : pool_() {}

    /// 用指定线程数创建线程池。
    explicit BasicLThreadPool(std::size_t threads)
        : pool_(threads) {}

    /// 传入初始化回调（在线程启动时执行）创建线程池。
    template<class F
             LTOOL_ENABLE_IF(std::is_invocable<F>::value ||
                             std::is_invocable<F, std::size_t>::value)>
        LTOOL_REQUIRES(std::invocable<F> || std::invocable<F, std::size_t>)
    explicit BasicLThreadPool(F&& init)
        : pool_(std::forward<F>(init)) {}

    /// 用指定线程数和初始化回调创建线程池。
    template<class F
             LTOOL_ENABLE_IF(std::is_invocable<F>::value ||
                             std::is_invocable<F, std::size_t>::value)>
        LTOOL_REQUIRES(std::invocable<F> || std::invocable<F, std::size_t>)
    BasicLThreadPool(std::size_t threads, F&& init)
        : pool_(threads, std::forward<F>(init)) {}

    /// 禁止复制。
    BasicLThreadPool(const BasicLThreadPool&) = delete;
    BasicLThreadPool& operator=(const BasicLThreadPool&) = delete;

    /// 访问底层线程池的可写引用。
    native_type& native() noexcept {
        return pool_;
    }

    /// 访问底层线程池的只读引用。
    const native_type& native() const noexcept {
        return pool_;
    }

    /// 返回线程池工作线程数。
    std::size_t size() const noexcept {
        return pool_.get_thread_count();
    }

    /// size() 的同义函数。
    std::size_t thread_count() const noexcept {
        return pool_.get_thread_count();
    }

    /// 返回等待执行的任务数量。
    std::size_t queued() const {
        return pool_.get_tasks_queued();
    }

    /// 返回正在执行的任务数量。
    std::size_t running() const {
        return pool_.get_tasks_running();
    }

    /// 返回排队与执行中的任务总数。
    std::size_t pending() const {
        return pool_.get_tasks_total();
    }

    /**
     * @brief 提交一个有返回值或可等待的任务。
     */
    template<class F,
             class R = std::invoke_result_t<std::decay_t<F>>
             LTOOL_ENABLE_IF(std::is_invocable<std::decay_t<F>>::value)>
        LTOOL_REQUIRES(std::invocable<std::decay_t<F>>)
    std::future<R> submit(F&& fn, priority_t priority = 0) {
        return pool_.submit_task(std::forward<F>(fn), priority);
    }

    /**
     * @brief 提交一个无需 future 的任务。
     */
    template<class F LTOOL_ENABLE_IF(std::is_invocable<std::decay_t<F>>::value)>
        LTOOL_REQUIRES(std::invocable<std::decay_t<F>>)
    void post(F&& fn, priority_t priority = 0) {
        pool_.detach_task(std::forward<F>(fn), priority);
    }

    /// post() 的同义函数。
    template<class F LTOOL_ENABLE_IF(std::is_invocable<std::decay_t<F>>::value)>
        LTOOL_REQUIRES(std::invocable<std::decay_t<F>>)
    void detach(F&& fn, priority_t priority = 0) {
        post(std::forward<F>(fn), priority);
    }

    /**
     * @brief 并行执行 [first, last) 范围内的每个下标，返回 multi_future<void>。
     */
    template<class T1, class T2, class F>
    BS::multi_future<void> parallel_for(T1 first, T2 last, F&& fn,
                                        std::size_t blocks = 0,
                                        priority_t priority = 0) {
        return pool_.submit_loop(first, last, std::forward<F>(fn), blocks, priority);
    }

    /**
     * @brief 按块并行执行 [first, last)，回调签名为 fn(begin, end)。
     */
    template<class T1, class T2, class F>
    auto parallel_blocks(T1 first, T2 last, F&& fn, std::size_t blocks = 0,
                         priority_t priority = 0)
        -> decltype(std::declval<native_type&>().submit_blocks(first, last, std::forward<F>(fn),
                                                                blocks, priority)) {
        return pool_.submit_blocks(first, last, std::forward<F>(fn), blocks, priority);
    }

    /**
     * @brief 等待当前线程池所有已提交任务完成。
     */
    void wait() {
        ensure_not_worker_thread();
        pool_.wait();
    }

    /// 等待任务完成，最多等待 duration；超时返回 false。
    template<class Rep, class Period>
    bool wait_for(const std::chrono::duration<Rep, Period>& duration) {
        ensure_not_worker_thread();
        return pool_.wait_for(duration);
    }

    /// 等待任务完成直到 time_point；超时返回 false。
    template<class Clock, class Duration>
    bool wait_until(const std::chrono::time_point<Clock, Duration>& time_point) {
        ensure_not_worker_thread();
        return pool_.wait_until(time_point);
    }

    /// 丢弃尚未开始执行的排队任务。
    void purge() {
        // purge() discards queued work. Do not wait on futures returned for
        // discarded tasks: BS_thread_pool cannot complete those shared states.
        pool_.purge();
    }

    /// 重置线程池（丢弃状态、等待中的任务按当前配置重新初始化）。
    void reset() {
        pool_.reset();
    }

    /// 重置线程池并调整线程数。
    void reset(std::size_t threads) {
        pool_.reset(threads);
    }

    /// 重置线程池并传入新的初始化回调。
    template<class F
             LTOOL_ENABLE_IF(std::is_invocable<F>::value ||
                             std::is_invocable<F, std::size_t>::value)>
        LTOOL_REQUIRES(std::invocable<F> || std::invocable<F, std::size_t>)
    void reset(std::size_t threads, F&& init) {
        pool_.reset(threads, std::forward<F>(init));
    }

private:
    /// 防止在工作线程内等待本线程池导致死锁；否则抛出 BS::wait_deadlock。
    void ensure_not_worker_thread() const {
        const auto current_pool = BS::this_thread::get_pool();
        if (current_pool && *current_pool ==
                                static_cast<void*>(const_cast<native_type*>(&pool_))) {
            throw BS::wait_deadlock();
        }
    }

    native_type pool_; ///< 底层 BS_thread_pool 实例。
};

/// 默认线程池：无扩展特性。
using LThreadPool = BasicLThreadPool<>;
/// 支持任务优先级的线程池。
using LPriorityThreadPool = BasicLThreadPool<BS::tp::priority>;
/// 支持暂停/恢复的线程池。
using LPauseThreadPool = BasicLThreadPool<BS::tp::pause>;
/// 启用死锁检测的线程池。
using LWaitDeadlockThreadPool = BasicLThreadPool<BS::tp::wait_deadlock_checks>;

namespace LTool {
using ThreadPool = ::LThreadPool;                         ///< 线程池别名。
using PriorityThreadPool = ::LPriorityThreadPool;         ///< 优先级线程池别名。
using PauseThreadPool = ::LPauseThreadPool;               ///< 可暂停线程池别名。
using WaitDeadlockThreadPool = ::LWaitDeadlockThreadPool; ///< 死锁检测线程池别名。
} // namespace LTool

#endif // LTOOL_LTHREAD_POOL_INCLUDE
