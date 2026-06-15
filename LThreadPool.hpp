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

#include "detail/LConfig.hpp"

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
    using native_type = BS::thread_pool<Flags>;
    using priority_t = BS::priority_t;

    BasicLThreadPool()
        : pool_() {}

    explicit BasicLThreadPool(std::size_t threads)
        : pool_(threads) {}

    template<class F>
    explicit BasicLThreadPool(F&& init)
        : pool_(std::forward<F>(init)) {}

    template<class F>
    BasicLThreadPool(std::size_t threads, F&& init)
        : pool_(threads, std::forward<F>(init)) {}

    BasicLThreadPool(const BasicLThreadPool&) = delete;
    BasicLThreadPool& operator=(const BasicLThreadPool&) = delete;

    native_type& native() noexcept {
        return pool_;
    }

    const native_type& native() const noexcept {
        return pool_;
    }

    std::size_t size() const noexcept {
        return pool_.get_thread_count();
    }

    std::size_t thread_count() const noexcept {
        return pool_.get_thread_count();
    }

    std::size_t queued() const {
        return pool_.get_tasks_queued();
    }

    std::size_t running() const {
        return pool_.get_tasks_running();
    }

    std::size_t pending() const {
        return pool_.get_tasks_total();
    }

    /**
     * @brief 提交一个有返回值或可等待的任务。
     */
    template<class F, class R = std::invoke_result_t<std::decay_t<F>>>
    std::future<R> submit(F&& fn, priority_t priority = 0) {
        return pool_.submit_task(std::forward<F>(fn), priority);
    }

    /**
     * @brief 提交一个无需 future 的任务。
     */
    template<class F>
    void post(F&& fn, priority_t priority = 0) {
        pool_.detach_task(std::forward<F>(fn), priority);
    }

    template<class F>
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
        pool_.wait();
    }

    template<class Rep, class Period>
    bool wait_for(const std::chrono::duration<Rep, Period>& duration) {
        return pool_.wait_for(duration);
    }

    template<class Clock, class Duration>
    bool wait_until(const std::chrono::time_point<Clock, Duration>& time_point) {
        return pool_.wait_until(time_point);
    }

    void purge() {
        pool_.purge();
    }

    void reset() {
        pool_.reset();
    }

    void reset(std::size_t threads) {
        pool_.reset(threads);
    }

    template<class F>
    void reset(std::size_t threads, F&& init) {
        pool_.reset(threads, std::forward<F>(init));
    }

private:
    native_type pool_;
};

using LThreadPool = BasicLThreadPool<>;
using LPriorityThreadPool = BasicLThreadPool<BS::tp::priority>;
using LPauseThreadPool = BasicLThreadPool<BS::tp::pause>;
using LWaitDeadlockThreadPool = BasicLThreadPool<BS::tp::wait_deadlock_checks>;

namespace LTool {
using ThreadPool = ::LThreadPool;
using PriorityThreadPool = ::LPriorityThreadPool;
using PauseThreadPool = ::LPauseThreadPool;
using WaitDeadlockThreadPool = ::LWaitDeadlockThreadPool;
} // namespace LTool

#endif // LTOOL_LTHREAD_POOL_INCLUDE
