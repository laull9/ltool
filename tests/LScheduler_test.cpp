#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "LScheduler.hpp"

using namespace std::chrono_literals;

namespace {

/// 轮询等待谓词成立（用于替代固定 sleep，减少 flaky）。
template<class F>
bool wait_until(F&& pred, std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return pred();
}

} // namespace

// ---------------------------------------------------------------------------
// 一次性任务
// ---------------------------------------------------------------------------

TEST_CASE("after 正常执行且只执行一次", "[LScheduler][after]") {
    LScheduler scheduler;
    std::atomic<int> count{0};
    scheduler.after(20ms, [&] { count.fetch_add(1); });

    REQUIRE(wait_until([&] { return count.load() >= 1; }));
    std::this_thread::sleep_for(60ms);
    REQUIRE(count.load() == 1);
}

TEST_CASE("after 可取消", "[LScheduler][after]") {
    LScheduler scheduler;
    std::atomic<int> count{0};
    auto task = scheduler.after(30ms, [&] { count.fetch_add(1); });

    task.cancel();
    task.cancel(); // 重复取消应安全

    std::this_thread::sleep_for(80ms);
    REQUIRE(count.load() == 0);
    REQUIRE(task.is_cancelled());
}

TEST_CASE("at 在绝对时间执行一次", "[LScheduler][at]") {
    LScheduler scheduler;
    std::atomic<int> count{0};
    scheduler.at(std::chrono::system_clock::now() + 20ms, [&] { count.fetch_add(1); });

    REQUIRE(wait_until([&] { return count.load() >= 1; }));
    std::this_thread::sleep_for(60ms);
    REQUIRE(count.load() == 1);
}

// ---------------------------------------------------------------------------
// 周期任务
// ---------------------------------------------------------------------------

TEST_CASE("every 多次执行，cancel 后停止", "[LScheduler][every]") {
    LScheduler scheduler;
    std::atomic<int> count{0};
    auto task = scheduler.every(10ms, [&] { count.fetch_add(1); });

    REQUIRE(wait_until([&] { return count.load() >= 3; }));
    task.cancel();

    const auto snapshot = count.load();
    std::this_thread::sleep_for(80ms);
    REQUIRE(count.load() == snapshot);
    REQUIRE(task.is_cancelled());
}

TEST_CASE("pause 后停止，resume 后恢复", "[LScheduler][every]") {
    LScheduler scheduler;
    std::atomic<int> count{0};
    auto task = scheduler.every(10ms, [&] { count.fetch_add(1); });

    REQUIRE(wait_until([&] { return count.load() >= 3; }));
    task.pause();
    REQUIRE(task.is_paused());

    const auto paused_snapshot = count.load();
    std::this_thread::sleep_for(80ms);
    REQUIRE(count.load() == paused_snapshot); // 暂停期间不再执行

    task.resume();
    REQUIRE_FALSE(task.is_paused());
    REQUIRE(wait_until([&] { return count.load() > paused_snapshot; }));
    task.cancel();
}

TEST_CASE("repeat 恰好执行指定次数", "[LScheduler][repeat]") {
    LScheduler scheduler;
    std::atomic<int> count{0};
    auto task = scheduler.repeat(5, 15ms, [&] { count.fetch_add(1); });

    REQUIRE(wait_until([&] { return count.load() >= 5; }, 3000ms));
    std::this_thread::sleep_for(120ms); // 留出足够时间，若有多余执行也会发生
    REQUIRE(count.load() == 5);
    REQUIRE_FALSE(task.is_running()); // 已自然结束
    REQUIRE_FALSE(task.is_cancelled());
}

TEST_CASE("repeat(0) 不执行", "[LScheduler][repeat]") {
    LScheduler scheduler;
    std::atomic<int> count{0};
    auto task = scheduler.repeat(0, 10ms, [&] { count.fetch_add(1); });
    std::this_thread::sleep_for(50ms);
    REQUIRE(count.load() == 0);
    REQUIRE_FALSE(task.is_running());
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

TEST_CASE("Scheduler 立即析构且含长延迟任务", "[LScheduler][lifecycle]") {
    {
        LScheduler scheduler;
        scheduler.after(10s, [] {});
        scheduler.every(1s, [] {});
        scheduler.repeat(3, 500ms, [] {});
    }
    SUCCEED(); // 不悬挂、不崩溃即为通过
}

TEST_CASE("句柄在 Scheduler 析构后仍存在且操作无 UB", "[LScheduler][lifecycle]") {
    LScheduledTask outlived;
    {
        LScheduler scheduler;
        outlived = scheduler.every(50ms, [] {});
    }
    // Scheduler 已析构，这些操作应安全降级，不产生未定义行为。
    outlived.cancel();
    outlived.pause();
    outlived.resume();
    REQUIRE(outlived.is_cancelled()); // cancel 后状态一致
}

// ---------------------------------------------------------------------------
// 并发
// ---------------------------------------------------------------------------

TEST_CASE("多个线程同时创建任务", "[LScheduler][concurrency]") {
    LScheduler scheduler;
    std::atomic<int> total{0};

    std::vector<std::thread> creators;
    for (int i = 0; i < 4; ++i) {
        creators.emplace_back([&, i] {
            for (int j = 0; j < 20; ++j) {
                scheduler.after(5ms + std::chrono::milliseconds(i), [&] { total.fetch_add(1); });
            }
        });
    }
    for (auto& t : creators) {
        t.join();
    }
    REQUIRE(wait_until([&] { return total.load() >= 80; }, 3000ms));
}

TEST_CASE("多个线程同时 cancel", "[LScheduler][concurrency]") {
    LScheduler scheduler;
    std::atomic<int> count{0};

    std::vector<LScheduledTask> tasks;
    for (int i = 0; i < 100; ++i) {
        tasks.push_back(scheduler.every(2ms, [&] { count.fetch_add(1); }));
    }

    std::vector<std::thread> cancellers;
    for (auto& t : tasks) {
        cancellers.emplace_back([t]() mutable { t.cancel(); });
    }
    for (auto& t : cancellers) {
        t.join();
    }

    const auto snapshot = count.load();
    std::this_thread::sleep_for(40ms);
    REQUIRE(count.load() == snapshot); // 全部取消后不再增长
}

TEST_CASE("callback 内创建新任务", "[LScheduler][concurrency]") {
    LScheduler scheduler;
    std::atomic<int> count{0};

    scheduler.every(20ms, [&](const LTaskContext& ctx) {
        if (ctx.run_count == 1) {
            scheduler.after(10ms, [&] { count.fetch_add(1); });
        }
    });

    REQUIRE(wait_until([&] { return count.load() >= 1; }, 3000ms));
}

TEST_CASE("callback 内取消自己", "[LScheduler][concurrency]") {
    LScheduler scheduler;
    std::atomic<int> count{0};

    // 句柄在 every() 返回后才赋值，而 callback 可能已在调度线程上运行，
    // 因此用互斥锁发布句柄，避免读写竞争。
    std::mutex handle_mutex;
    LScheduledTask handle;
    {
        std::lock_guard<std::mutex> lock(handle_mutex);
        handle = scheduler.every(5ms, [&] {
            count.fetch_add(1);
            LScheduledTask self;
            {
                std::lock_guard<std::mutex> inner(handle_mutex);
                self = handle;
            }
            self.cancel();
        });
    }

    REQUIRE(wait_until([&] { return count.load() >= 1; }));
    const auto snapshot = count.load();
    std::this_thread::sleep_for(60ms);
    REQUIRE(count.load() == snapshot); // 自取消后不再执行
}

// ---------------------------------------------------------------------------
// 时间行为（调度策略语义）
// ---------------------------------------------------------------------------

TEST_CASE("compute_next 三种模式的到期计算", "[LScheduler][timing]") {
    const auto t0 = std::chrono::steady_clock::now();

    // FixedDelay：从完成时刻起算。
    {
        detail::TaskState s;
        s.interval = 10ms;
        s.mode = LScheduleMode::FixedDelay;
        REQUIRE(detail::compute_next(s, t0, t0 + 300ms) == t0 + 310ms);
    }
    // FixedRate：迟到时快进到下一个未来 grid 槽位，不积压补跑。
    {
        detail::TaskState s;
        s.interval = 10ms;
        s.mode = LScheduleMode::FixedRate;
        // 阻塞到 355ms（第 35 个间隔与第 36 个之间），下一个合法执行点是 360ms。
        REQUIRE(detail::compute_next(s, t0, t0 + 355ms) == t0 + 360ms);
        // 未迟到时与理论时间一致。
        REQUIRE(detail::compute_next(s, t0, t0) == t0 + 10ms);
    }
    // SkipMissed：迟到即丢弃原 grid，从当前时间重新对齐。
    {
        detail::TaskState s;
        s.interval = 10ms;
        s.mode = LScheduleMode::SkipMissed;
        REQUIRE(detail::compute_next(s, t0, t0 + 355ms) == t0 + 365ms);
    }
}

TEST_CASE("callback 执行时间大于 interval 时不会无限补任务", "[LScheduler][timing]") {
    // 用 inline executor 的阻塞任务占住调度线程，模拟 Scheduler 被阻塞 120ms。
    LScheduler scheduler;
    std::atomic<int> rate_count{0};
    std::atomic<int> slow_count{0};

    auto rate = scheduler.every(10ms, [&] { rate_count.fetch_add(1); }, LScheduleMode::FixedRate);
    scheduler.after(50ms, [&] {
        slow_count.fetch_add(1);
        std::this_thread::sleep_for(120ms); // 阻塞调度线程
    });

    std::this_thread::sleep_for(320ms);
    rate.cancel();

    REQUIRE(slow_count.load() == 1);
    // 阻塞 120ms 内错过的周期不应被突发补跑，总执行次数应远小于「每个间隔都补」的数量。
    REQUIRE(rate_count.load() < 25);
    REQUIRE(rate_count.load() >= 8);
}

// ---------------------------------------------------------------------------
// 异常安全
// ---------------------------------------------------------------------------

TEST_CASE("callback 抛异常后 Scheduler 仍然存活（Continue 策略）", "[LScheduler][exception]") {
    LScheduler scheduler;
    std::atomic<int> count{0};

    auto task = scheduler.every(10ms, [&] {
        count.fetch_add(1);
        throw std::runtime_error("boom");
    });

    REQUIRE(wait_until([&] { return count.load() >= 3; })); // 继续调度
    task.cancel();
    REQUIRE(task.is_cancelled());
}

TEST_CASE("Cancel 策略下异常终止任务", "[LScheduler][exception]") {
    LScheduler scheduler;
    scheduler.set_error_policy(LTaskErrorPolicy::Cancel);
    std::atomic<int> count{0};

    auto task = scheduler.every(10ms, [&] {
        count.fetch_add(1);
        throw std::runtime_error("boom");
    });

    REQUIRE(wait_until([&] { return count.load() >= 1; }));
    const auto snapshot = count.load();
    std::this_thread::sleep_for(80ms);
    REQUIRE(count.load() == snapshot); // 已终止，不再调度
    REQUIRE(task.is_cancelled());
}

TEST_CASE("error handler 被调用", "[LScheduler][exception]") {
    LScheduler scheduler;
    std::atomic<int> handled{0};
    LTaskInfo captured_info{0, 0, 0ns, LScheduleMode::FixedDelay};

    scheduler.set_error_handler([&](std::exception_ptr ep, const LTaskInfo& info) {
        REQUIRE(ep != nullptr);
        captured_info = info;
        handled.fetch_add(1);
    });

    auto task = scheduler.every(10ms, [] { throw std::runtime_error("boom"); });
    REQUIRE(wait_until([&] { return handled.load() >= 1; }));
    task.cancel();

    REQUIRE(captured_info.id == task.id());
    REQUIRE(captured_info.run_count >= 1);
}

TEST_CASE("无效 interval 抛异常", "[LScheduler][exception]") {
    LScheduler scheduler;
    REQUIRE_THROWS_AS(scheduler.every(0ms, [] {}), std::invalid_argument);
    REQUIRE_THROWS_AS(scheduler.repeat(3, -1ms, [] {}), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// 上下文与执行器
// ---------------------------------------------------------------------------

TEST_CASE("callback 接收 LTaskContext", "[LScheduler][context]") {
    LScheduler scheduler;
    std::atomic<int> count{0};
    std::atomic<bool> saw_delay{false};

    auto task = scheduler.every(10ms, [&](const LTaskContext& ctx) {
        count.fetch_add(1);
        if (ctx.run_count >= 1 && ctx.delay() >= 0ns) {
            saw_delay.store(true);
        }
    });

    REQUIRE(wait_until([&] { return count.load() >= 2; }));
    task.cancel();
    REQUIRE(saw_delay.load());
}

TEST_CASE("新插入更早的任务会提前唤醒调度线程", "[LScheduler][timing]") {
    LScheduler scheduler;
    std::atomic<int> early{0};
    auto late = scheduler.after(2s, [] {});
    std::this_thread::sleep_for(20ms); // 让调度线程进入针对 2s 截止时间的等待
    scheduler.after(10ms, [&] { early.fetch_add(1); });
    REQUIRE(wait_until([&] { return early.load() == 1; }, 500ms));
    late.cancel();
}
