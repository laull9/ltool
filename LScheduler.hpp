/**
 * @file LScheduler.hpp
 * @brief 基于 std::chrono 的轻量定时任务调度器，纯头文件、开箱即用。
 *
 * LScheduler 的核心定位：
 * - 现代 C++（C++20+）风格，header-only，仅依赖标准库。
 * - 单调度线程 + 最小堆 + condition_variable 休眠，无忙轮询、无 detached thread。
 * - 支持一次性任务（after/at）与周期任务（every/repeat）。
 * - 所有调度接口返回轻量句柄 LScheduledTask，可安全 cancel/pause/resume。
 * - 通过可插拔 executor 把「何时执行」与「在哪个线程执行」解耦，天然配合 LThreadPool。
 *
 * 常用示例：
 * @code
 * LScheduler scheduler;
 *
 * scheduler.after(500ms, [] { fmt::println("delayed"); });
 *
 * auto heartbeat = scheduler.every(1s, [] { fmt::println("tick"); });
 * heartbeat.pause();
 * heartbeat.resume();
 * heartbeat.cancel();
 *
 * scheduler.repeat(5, 10s, [] { fmt::println("cleanup"); });
 * @endcode
 *
 * 注意：LScheduler 是普通应用级调度器，不是实时系统；实际执行精度受 OS 调度与
 * executor 影响，不承诺微秒级严格 deadline。
 */

#ifndef LTOOL_LSCHEDULER_INCLUDE
#define LTOOL_LSCHEDULER_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LConcepts.hpp"
#include <exception>

#if !LTOOL_HAS_CPP20
#error "LScheduler requires C++20 or later"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @brief 任务 ID 类型，由 Scheduler 原子递增产生。
 */
using LTaskId = std::uint64_t;

/**
 * @brief 周期任务的调度策略。
 */
enum class LScheduleMode {
    FixedDelay, ///< callback 完成后再等待 interval（默认，串行、不积压）。
    FixedRate,  ///< 锚定原始时间轴；迟到时快进跳过已错过的执行点，保持相位。
    SkipMissed, ///< 迟到时丢弃原 grid、从当前时间重新对齐（适合 heartbeat/刷新）。
};

/**
 * @brief callback 抛出异常时的处理策略。
 */
enum class LTaskErrorPolicy {
    Continue, ///< 记录异常后继续调度（默认）。
    Cancel,   ///< 记录异常后终止该任务。
};

/**
 * @brief 单次执行时的上下文，传给接受 `const LTaskContext&` 的 callback。
 */
struct LTaskContext {
    std::uint64_t run_count;                              ///< 本次是第几次执行（从 1 开始）。
    std::chrono::steady_clock::time_point scheduled_time; ///< 理论到期时间。
    std::chrono::steady_clock::time_point actual_time;    ///< 实际开始执行时间。

    /**
     * @brief 本次执行的延迟（actual_time - scheduled_time）。
     */
    [[nodiscard]] auto delay() const noexcept {
        return actual_time - scheduled_time;
    }
};

/**
 * @brief error handler 收到的任务信息快照。
 */
struct LTaskInfo {
    LTaskId id;                                   ///< 任务 ID。
    std::uint64_t run_count;                      ///< 异常发生时是第几次执行。
    std::chrono::steady_clock::duration interval; ///< 周期间隔（一次性任务为 0）。
    LScheduleMode mode;                           ///< 调度策略。
};

namespace detail {

using steady_clock = std::chrono::steady_clock;
using steady_time = steady_clock::time_point;

/**
 * @brief 可插拔执行器：接收一个已经封装好异常处理的 job，负责在某个线程上运行它。
 *
 * 空函数表示由 Scheduler 自己的调度线程内联执行。
 */
using ExecutorFn = std::function<void(std::function<void()>)>;

/// 检测类型是否具有 `post(std::function<void()>)`（用于线程池适配构造）。
template<class T, class = void>
struct has_post : std::false_type {};

template<class T>
struct has_post<T, std::void_t<decltype(std::declval<T&>().post(
                       std::declval<std::function<void()>>()))>> : std::true_type {};

/**
 * @brief 任务共享状态；由 Scheduler 与所有 LScheduledTask 句柄共同持有。
 *
 * 其中 cancelled/paused/finished/generation/run_count/next_run_ns 均为原子量，
 * 允许在任意线程读取或惰性修改；其余字段在任务创建后不可变。
 */
struct TaskState {
    LTaskId id{0};

    std::function<void(const LTaskContext&)> callback; ///< 已适配好签名的回调。

    std::chrono::steady_clock::duration interval{0}; ///< 周期间隔，一次性任务为 0。
    LScheduleMode mode{LScheduleMode::FixedDelay};
    LTaskErrorPolicy error_policy{LTaskErrorPolicy::Continue};

    std::optional<std::uint64_t> max_runs; ///< 总执行次数；nullopt 表示无限（every）。
    std::atomic<std::uint64_t> run_count{0}; ///< 已执行次数。

    std::atomic<std::uint64_t> generation{0}; ///< 调度代际，用于堆的惰性失效。
    std::atomic<bool> cancelled{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> finished{false};

    std::atomic<std::int64_t> next_run_ns{0}; ///< 下次到期时间（ns since epoch），0 表示无。
};

/**
 * @brief 最小堆中的条目。
 *
 * generation 用于惰性失效：Cancel/Pause/Resume 会递增 generation 使旧条目失效，
 * 从而避免为「从堆中间删除」实现复杂的 O(n) 操作。
 */
struct HeapEntry {
    steady_time due;
    std::uint64_t generation;
    std::shared_ptr<TaskState> state;
    std::uint64_t seq; ///< 插入序号，保证 due 相同时仍为全序。

    bool operator<(const HeapEntry& rhs) const {
        if (due != rhs.due) {
            return due > rhs.due; // 最小堆：due 更早的排在顶部。
        }
        return seq > rhs.seq; // due 相同按插入顺序。
    }
};

inline std::int64_t to_ns(steady_time tp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

/**
 * @brief 判断 callback 的可调用形式：优先接受 (const LTaskContext&)，否则接受 ()。
 */
template<class F>
auto make_callback(F&& fn) {
    using D = std::decay_t<F>;
    if constexpr (std::is_invocable_v<D, const LTaskContext&>) {
        return std::function<void(const LTaskContext&)>(
            [f = std::forward<F>(fn)](const LTaskContext& ctx) {
                std::invoke(f, ctx);
            });
    } else {
        static_assert(std::is_invocable_v<D>,
                      "LScheduler callback 必须可调用为 fn() 或 fn(const LTaskContext&)");
        return std::function<void(const LTaskContext&)>(
            [f = std::forward<F>(fn)](const LTaskContext&) {
                std::invoke(f);
            });
    }
}

/**
 * @brief 计算周期任务下一次到期时间。
 *
 * 为什么这样实现：三种模式的差异只在「迟到」时如何对齐。
 * - FixedDelay 以「完成时刻」为基准，天然串行、永不积压。
 * - FixedRate 以「理论时间」为基准，快进循环保证 callback 超时也不会无限补跑
 *   （interval 恒为正，循环必然终止）。
 * - SkipMissed 迟到一次即丢弃原 grid 重新对齐，避免恢复后突发补跑。
 */
inline steady_time compute_next(const TaskState& s, steady_time scheduled, steady_time completed) {
    const auto iv = s.interval;
    switch (s.mode) {
    case LScheduleMode::FixedDelay:
        return completed + iv;
    case LScheduleMode::FixedRate: {
        auto next = scheduled + iv;
        while (next <= completed) {
            next += iv;
        }
        return next;
    }
    case LScheduleMode::SkipMissed: {
        auto next = scheduled + iv;
        if (next <= completed) {
            next = completed + iv;
        }
        return next;
    }
    }
    return completed + iv;
}

} // namespace detail

/**
 * @brief 调度器的共享内部状态。
 *
 * 之所以从 LScheduler 中拆出，是因为 LScheduledTask 句柄需要持有一个指向它的
 * 弱引用，才能安全地在任意线程调用 cancel/pause/resume（尤其 resume 需要往堆里
 * 推入新条目）。句柄仅持有 weak_ptr，因此 Scheduler 析构后句柄操作自然降级为
 * no-op，不会产生悬空指针。
 */
struct LSchedulerCore : std::enable_shared_from_this<LSchedulerCore> {
    using steady_time = detail::steady_time;

    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable in_flight_cv;

    std::priority_queue<detail::HeapEntry> heap;

    std::jthread thread;
    std::atomic<std::uint64_t> next_id{1};
    std::uint64_t seq{0}; ///< 堆条目插入序号，仅在 mutex 下访问。
    std::atomic<std::size_t> in_flight{0}; ///< executor 上在飞任务数。
    std::atomic<bool> stopping{false};

    detail::ExecutorFn executor; ///< 空函数表示内联执行。
    std::function<void(std::exception_ptr, const LTaskInfo&)> error_handler;
    std::atomic<LTaskErrorPolicy> error_policy{LTaskErrorPolicy::Continue};

    explicit LSchedulerCore(detail::ExecutorFn ex)
        : executor(std::move(ex)) {
        thread = std::jthread([this] { run(); });
    }

    /**
     * @brief 调度线程主循环。
     *
     * 并发正确性的关键点：
     * - wait 的谓词检查「stopping」，配合析构中「持锁置位 → notify」的顺序，
     *   保证不会丢唤醒导致悬挂。
     * - 用户 callback 在锁释放之后才执行，绝不在持锁状态运行。
     * - 堆中失效条目通过 generation 惰性丢弃，不在持锁时做 O(n) 删除。
     */
    void run() {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopping.load(std::memory_order_relaxed)) {
            if (heap.empty()) {
                cv.wait(lock, [this] { return stopping.load(std::memory_order_relaxed) || !heap.empty(); });
                continue;
            }

            const auto now = detail::steady_clock::now();
            if (heap.top().due > now) {
                cv.wait_until(lock, heap.top().due, [this] {
                    return stopping.load(std::memory_order_relaxed) || heap.empty() ||
                           heap.top().due <= detail::steady_clock::now();
                });
                continue;
            }

            auto entry = heap.top();
            heap.pop();

            // 惰性失效：generation 不匹配说明该条目已被 cancel/pause/resume 作废。
            const auto& state = entry.state;
            if (!state || entry.generation != state->generation.load(std::memory_order_relaxed)) {
                continue;
            }
            if (state->cancelled.load(std::memory_order_relaxed) ||
                state->paused.load(std::memory_order_relaxed) ||
                state->finished.load(std::memory_order_relaxed)) {
                continue;
            }

            const auto run_no = state->run_count.fetch_add(1, std::memory_order_relaxed) + 1;
            const bool is_last = state->max_runs.has_value() && run_no >= *state->max_runs;

            const LTaskContext ctx{run_no, entry.due, now};

            // 在锁内拷贝 error_handler 快照，避免与 set_error_handler 并发写产生数据竞争。
            auto handler = error_handler;
            const auto policy = state->error_policy;

            // 执行期间清空 next_run，表示「尚无已排定的下一次执行」。
            state->next_run_ns.store(0, std::memory_order_relaxed);
            in_flight.fetch_add(1, std::memory_order_relaxed);

            lock.unlock();

            // job 持有 weak_ptr：即使 Scheduler 正在析构，完成后也不会触碰已释放内存。
            std::weak_ptr<LSchedulerCore> weak = shared_from_this();
            auto job = [weak, state, ctx, scheduled = entry.due, is_last,
                        handler = std::move(handler), policy]() mutable {
                auto core = weak.lock();
                if (!core) {
                    return;
                }
                bool cancel_task = false;
                try {
                    state->callback(ctx);
                } catch (...) {
                    const auto ep = std::current_exception();
                    if (handler) {
                        handler(ep, LTaskInfo{state->id, ctx.run_count, state->interval, state->mode});
                    }
                    cancel_task = (policy == LTaskErrorPolicy::Cancel);
                }
                if (cancel_task) {
                    state->cancelled.store(true, std::memory_order_relaxed);
                }
                core->on_task_completed(state, scheduled, is_last);
            };

            if (executor) {
                executor(std::move(job));
            } else {
                job();
            }

            lock.lock();
        }
    }

    /**
     * @brief callback 完成后（可能在 executor 线程上）的回调，负责重新调度。
     *
     * 在持锁状态下完成：判断是否继续、计算下次到期、递增 generation、推入堆。
     * 若正在析构则只做在飞计数结算，不再调度。
     */
    void on_task_completed(const std::shared_ptr<detail::TaskState>& state,
                           steady_time scheduled, bool is_last) {
        std::lock_guard<std::mutex> lock(mutex);
        in_flight.fetch_sub(1, std::memory_order_relaxed);
        in_flight_cv.notify_one();

        if (stopping.load(std::memory_order_relaxed)) {
            return;
        }
        if (is_last) {
            state->finished.store(true, std::memory_order_relaxed);
            return;
        }
        if (state->cancelled.load(std::memory_order_relaxed) ||
            state->paused.load(std::memory_order_relaxed) ||
            state->finished.load(std::memory_order_relaxed)) {
            return;
        }

        const auto completed = detail::steady_clock::now();
        const auto next = detail::compute_next(*state, scheduled, completed);

        state->generation.fetch_add(1, std::memory_order_relaxed);
        const auto gen = state->generation.load(std::memory_order_relaxed);
        state->next_run_ns.store(detail::to_ns(next), std::memory_order_relaxed);

        heap.push(detail::HeapEntry{next, gen, state, seq++});
        cv.notify_one();
    }

    /// 取消任务（幂等）；若 callback 在飞则结束后不再调度。
    void cancel(const std::shared_ptr<detail::TaskState>& state) {
        std::lock_guard<std::mutex> lock(mutex);
        if (state->cancelled.load(std::memory_order_relaxed) ||
            state->finished.load(std::memory_order_relaxed)) {
            return;
        }
        state->cancelled.store(true, std::memory_order_relaxed);
        state->generation.fetch_add(1, std::memory_order_relaxed);
        state->next_run_ns.store(0, std::memory_order_relaxed);
        cv.notify_one();
    }

    /// 暂停任务；不再开始新的 callback，正在执行的 callback 不强制终止。
    void pause(const std::shared_ptr<detail::TaskState>& state) {
        std::lock_guard<std::mutex> lock(mutex);
        if (state->cancelled.load(std::memory_order_relaxed) ||
            state->finished.load(std::memory_order_relaxed)) {
            return;
        }
        if (state->paused.exchange(true, std::memory_order_relaxed)) {
            return; // 已暂停
        }
        state->generation.fetch_add(1, std::memory_order_relaxed);
        state->next_run_ns.store(0, std::memory_order_relaxed);
        cv.notify_one();
    }

    /// 恢复任务；以当前时间重新计算下次到期，不补跑暂停期间的周期。
    void resume(const std::shared_ptr<detail::TaskState>& state) {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping.load(std::memory_order_relaxed) ||
            state->cancelled.load(std::memory_order_relaxed) ||
            state->finished.load(std::memory_order_relaxed)) {
            return;
        }
        if (!state->paused.exchange(false, std::memory_order_relaxed)) {
            return; // 未暂停
        }
        // 周期任务等待一个完整间隔；一次性任务尽快执行。
        auto due = detail::steady_clock::now();
        if (state->interval.count() > 0) {
            due += state->interval;
        }
        state->generation.fetch_add(1, std::memory_order_relaxed);
        const auto gen = state->generation.load(std::memory_order_relaxed);
        state->next_run_ns.store(detail::to_ns(due), std::memory_order_relaxed);
        heap.push(detail::HeapEntry{due, gen, state, seq++});
        cv.notify_one();
    }
};

/**
 * @brief 已调度任务的轻量句柄，可复制/移动，不拥有线程。
 *
 * 句柄内部持有一个 shared_ptr<TaskState> 与一个 weak_ptr<LSchedulerCore>，因此：
 * - Scheduler 析构后，句柄的查询与操作仍是安全的（操作降级为 no-op）。
 * - 重复 cancel() 幂等；cancel 已在执行的 callback 不中断其当前运行，但不会再调度。
 */
class LScheduledTask {
public:
    using shared_state = std::shared_ptr<detail::TaskState>;

    /// 默认构造：空句柄（等价于已取消任务）。
    LScheduledTask() = default;

    LScheduledTask(shared_state state, std::weak_ptr<LSchedulerCore> core)
        : state_(std::move(state)), core_(std::move(core)) {}

    /// 句柄是否指向真实任务。
    [[nodiscard]] bool valid() const noexcept {
        return state_ != nullptr;
    }

    /// 任务 ID；空句柄返回 0。
    [[nodiscard]] LTaskId id() const noexcept {
        return state_ ? state_->id : 0;
    }

    /// 取消任务；幂等。
    void cancel() {
        if (!state_) {
            return;
        }
        if (auto core = core_.lock()) {
            core->cancel(state_);
        } else {
            // Scheduler 已析构：直接置位，保证句柄状态一致。
            state_->cancelled.store(true, std::memory_order_relaxed);
            state_->generation.fetch_add(1, std::memory_order_relaxed);
            state_->next_run_ns.store(0, std::memory_order_relaxed);
        }
    }

    /// 暂停周期任务。
    void pause() {
        if (!state_) {
            return;
        }
        if (auto core = core_.lock()) {
            core->pause(state_);
        } else {
            state_->paused.store(true, std::memory_order_relaxed);
            state_->generation.fetch_add(1, std::memory_order_relaxed);
            state_->next_run_ns.store(0, std::memory_order_relaxed);
        }
    }

    /// 恢复任务；Scheduler 已析构时为 no-op。
    void resume() {
        if (!state_) {
            return;
        }
        if (auto core = core_.lock()) {
            core->resume(state_);
        }
    }

    /// 任务是否仍处于活动状态（未被取消且未自然结束；暂停不影响此状态）。
    /// Scheduler 已析构时视为不再运行。
    [[nodiscard]] bool is_running() const noexcept {
        return state_ && !core_.expired() &&
               !state_->cancelled.load(std::memory_order_relaxed) &&
               !state_->finished.load(std::memory_order_relaxed);
    }

    /// 任务是否处于暂停状态。
    [[nodiscard]] bool is_paused() const noexcept {
        return state_ && state_->paused.load(std::memory_order_relaxed);
    }

    /// 任务是否已被取消（空句柄视为已取消）。
    [[nodiscard]] bool is_cancelled() const noexcept {
        return !state_ || state_->cancelled.load(std::memory_order_relaxed);
    }

    /**
     * @brief 下次到期时间；若任务已取消/已结束/已暂停或无下次执行，返回 nullopt。
     */
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_run() const {
        if (!state_) {
            return std::nullopt;
        }
        const auto ns = state_->next_run_ns.load(std::memory_order_relaxed);
        if (ns == 0) {
            return std::nullopt;
        }
        return std::chrono::steady_clock::time_point{std::chrono::nanoseconds{ns}};
    }

private:
    shared_state state_;
    std::weak_ptr<LSchedulerCore> core_;
};

/**
 * @brief 轻量定时任务调度器。
 *
 * 内部只维护一个调度线程（std::jthread），任务按下次到期时间放入最小堆；
 * 调度线程通过 condition_variable::wait_until 精确休眠到最近到期点。
 *
 * 线程模型约定：
 * - 所有堆/状态变更都短暂持有内部互斥量；用户 callback 绝不在持锁状态下执行。
 * - callback 内再次调用 Scheduler、或 cancel/pause 自己，均不会死锁。
 * - 析构时停止接收新任务、唤醒并 join 调度线程，再等待 executor 上在飞任务排空。
 */
class LScheduler {
public:
    /// 默认构造：callback 由调度线程内联执行。
    LScheduler();

    /**
     * @brief 用外部 executor 构造；executor 接收一个 std::function<void()>。
     *
     * 示例：
     * @code
     * LScheduler scheduler([](std::function<void()> job) { pool.post(std::move(job)); });
     * @endcode
     */
    explicit LScheduler(detail::ExecutorFn executor);

    /**
     * @brief 用任意线程池构造（要求其具有 post(std::function<void()>) 方法）。
     */
    template<class Pool LTOOL_ENABLE_IF(detail::has_post<Pool>::value)>
        LTOOL_REQUIRES(detail::has_post<Pool>::value)
    explicit LScheduler(Pool& pool)
        : LScheduler([&pool](std::function<void()> job) {
              pool.post(std::move(job));
          }) {}

    LScheduler(const LScheduler&) = delete;
    LScheduler& operator=(const LScheduler&) = delete;

    /// 析构：停止调度、唤醒并 join 调度线程，等待在飞任务排空。
    ~LScheduler();

    /**
     * @brief 延迟指定时长后执行一次。
     */
    template<class Rep, class Period, class F>
    LScheduledTask after(const std::chrono::duration<Rep, Period>& delay, F&& callback) {
        auto state = make_state(std::forward<F>(callback));
        state->max_runs = 1;
        const auto due = detail::steady_clock::now() +
                         std::chrono::duration_cast<detail::steady_clock::duration>(delay);
        return schedule(std::move(state), due);
    }

    /**
     * @brief 在指定绝对时间（system_clock）执行一次。
     *
     * 时间换算在调度时刻一次性完成：此后 system_clock 被调整不会移动该 deadline。
     * 若目标时间已过去，则尽快执行。
     */
    template<class F>
    LScheduledTask at(std::chrono::system_clock::time_point when, F&& callback) {
        auto state = make_state(std::forward<F>(callback));
        state->max_runs = 1;
        const auto delay = when - std::chrono::system_clock::now();
        const auto due = detail::steady_clock::now() +
                         std::chrono::duration_cast<detail::steady_clock::duration>(delay);
        return schedule(std::move(state), due);
    }

    /**
     * @brief 周期性执行，直到被 cancel。
     */
    template<class Rep, class Period, class F>
    LScheduledTask every(const std::chrono::duration<Rep, Period>& interval, F&& callback,
                         LScheduleMode mode = LScheduleMode::FixedDelay) {
        auto state = make_state(std::forward<F>(callback));
        const auto iv = std::chrono::duration_cast<detail::steady_clock::duration>(interval);
        validate_interval(iv);
        state->interval = iv;
        state->mode = mode;
        const auto due = detail::steady_clock::now() + iv;
        return schedule(std::move(state), due);
    }

    /**
     * @brief 周期性执行，共执行 count 次后自动结束。
     */
    template<class Rep, class Period, class F>
    LScheduledTask repeat(std::uint64_t count, const std::chrono::duration<Rep, Period>& interval,
                          F&& callback, LScheduleMode mode = LScheduleMode::FixedDelay) {
        auto state = make_state(std::forward<F>(callback));
        const auto iv = std::chrono::duration_cast<detail::steady_clock::duration>(interval);
        validate_interval(iv);
        state->interval = iv;
        state->mode = mode;
        state->max_runs = count;
        if (count == 0) {
            // 零次：创建即结束，不进入调度队列。
            state->finished.store(true, std::memory_order_relaxed);
            return LScheduledTask(std::move(state), core_);
        }
        const auto due = detail::steady_clock::now() + iv;
        return schedule(std::move(state), due);
    }

    /// after() 的同义函数。
    template<class Rep, class Period, class F>
    LScheduledTask once(const std::chrono::duration<Rep, Period>& delay, F&& callback) {
        return after(delay, std::forward<F>(callback));
    }

    /**
     * @brief 注册 Scheduler 级异常处理器；callback 抛异常时会收到异常与任务信息。
     *
     * 建议在调度任何任务前配置；未注册时异常仍会被捕获并安全吞掉。
     */
    void set_error_handler(std::function<void(std::exception_ptr, const LTaskInfo&)> handler) {
        std::lock_guard<std::mutex> lock(core_->mutex);
        core_->error_handler = std::move(handler);
    }

    /**
     * @brief 设置默认异常处理策略（仅影响之后创建的任务）。
     */
    void set_error_policy(LTaskErrorPolicy policy) {
        core_->error_policy.store(policy, std::memory_order_relaxed);
    }

    /// 当前在飞（executor 上执行中）的任务数，仅用于观测。
    [[nodiscard]] std::size_t pending() const noexcept {
        return core_->in_flight.load(std::memory_order_relaxed);
    }

    /// 调度线程是否仍在运行（未被停止）。
    [[nodiscard]] bool running() const noexcept {
        return core_ && !core_->stopping.load(std::memory_order_relaxed);
    }

private:
    template<class F>
    std::shared_ptr<detail::TaskState> make_state(F&& callback);

    LScheduledTask schedule(std::shared_ptr<detail::TaskState> state, detail::steady_time due);

    static void validate_interval(detail::steady_clock::duration iv);

    std::shared_ptr<LSchedulerCore> core_;
};

/**
 * @brief 构造线程池 executor 的辅助函数（不引入对 LThreadPool.hpp 的依赖）。
 *
 * 用法：`LScheduler scheduler(LTool::make_pool_executor(pool));`
 */
template<class Pool>
auto make_pool_executor(Pool& pool) {
    return [&pool](std::function<void()> job) {
        pool.post(std::move(job));
    };
}

// ---------------------------------------------------------------------------
// 实现
// ---------------------------------------------------------------------------

inline LScheduler::LScheduler()
    : core_(std::make_shared<LSchedulerCore>(detail::ExecutorFn{})) {}

inline LScheduler::LScheduler(detail::ExecutorFn executor)
    : core_(std::make_shared<LSchedulerCore>(std::move(executor))) {}

inline LScheduler::~LScheduler() {
    if (!core_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(core_->mutex);
        core_->stopping.store(true, std::memory_order_relaxed);
    }
    core_->cv.notify_all();
    core_->thread.request_stop();
    core_->thread.join();

    // 等待 executor 上在飞任务排空，避免 callback 完成后回调访问已析构的状态。
    {
        std::unique_lock<std::mutex> lock(core_->mutex);
        core_->in_flight_cv.wait(lock, [this] {
            return core_->in_flight.load(std::memory_order_relaxed) == 0;
        });
    }
}

template<class F>
std::shared_ptr<detail::TaskState> LScheduler::make_state(F&& callback) {
    auto state = std::make_shared<detail::TaskState>();
    state->id = core_->next_id.fetch_add(1, std::memory_order_relaxed);
    state->callback = detail::make_callback(std::forward<F>(callback));
    state->error_policy = core_->error_policy.load(std::memory_order_relaxed);
    return state;
}

inline LScheduledTask LScheduler::schedule(std::shared_ptr<detail::TaskState> state,
                                           detail::steady_time due) {
    const auto gen = state->generation.load(std::memory_order_relaxed);
    state->next_run_ns.store(detail::to_ns(due), std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(core_->mutex);
        if (core_->stopping.load(std::memory_order_relaxed)) {
            state->cancelled.store(true, std::memory_order_relaxed);
            state->next_run_ns.store(0, std::memory_order_relaxed);
        } else {
            core_->heap.push(detail::HeapEntry{due, gen, state, core_->seq++});
            core_->cv.notify_one();
        }
    }
    return LScheduledTask(std::move(state), core_);
}

inline void LScheduler::validate_interval(detail::steady_clock::duration iv) {
    if (iv.count() <= 0) {
        throw std::invalid_argument("LScheduler: interval 必须为正数");
    }
}

namespace LTool {
using Scheduler = ::LScheduler;             ///< 调度器别名。
using ScheduledTask = ::LScheduledTask;     ///< 任务句柄别名。
using TaskContext = ::LTaskContext;         ///< 执行上下文别名。
using TaskInfo = ::LTaskInfo;               ///< 任务信息别名。
using TaskId = ::LTaskId;                   ///< 任务 ID 别名。
using ScheduleMode = ::LScheduleMode;       ///< 调度策略别名。
using TaskErrorPolicy = ::LTaskErrorPolicy; ///< 错误策略别名。
} // namespace LTool

#endif // LTOOL_LSCHEDULER_INCLUDE
