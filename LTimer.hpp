/**
 * @file LTimer.hpp
 * @brief std::chrono 的轻量计时器封装，纯头文件、开箱即用。
 *
 * LTimer 的核心定位：
 * - 默认使用 std::chrono::steady_clock，避免系统时间调整影响耗时统计。
 * - 支持 start/stop/resume/reset、lap、elapsed 和 expired。
 * - 提供 ms/us/ns/seconds 等常用单位快捷函数。
 * - 可用 LScopeTimer 在作用域退出时自动回调耗时。
 *
 * 常用示例：
 * @code
 * LTimer timer;
 * do_work();
 * auto cost = timer.ms();
 *
 * LScopeTimer guard([](const LTimer& t) {
 *     fmt::println("cost {} ms", t.ms());
 * });
 * @endcode
 */

#ifndef LTOOL_LTIMER_INCLUDE
#define LTOOL_LTIMER_INCLUDE

#include "detail/LConfig.hpp"

#include <chrono>
#include <functional>
#include <thread>
#include <utility>

/**
 * @brief 基于 steady_clock 的可暂停计时器。
 */
class LTimer {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using duration = clock::duration;

    /**
     * @brief 构造计时器；默认立即开始计时。
     */
    explicit LTimer(bool start_now = true)
        : start_(clock::now()), lap_(start_), elapsed_(duration::zero()),
          running_(start_now) {}

    /**
     * @brief 创建一个已开始计时的 LTimer。
     */
    static LTimer start() {
        return LTimer(true);
    }

    /**
     * @brief 清零并开始计时。
     */
    void reset() {
        start_ = clock::now();
        lap_ = start_;
        elapsed_ = duration::zero();
        running_ = true;
    }

    /**
     * @brief 清零但不开始计时。
     */
    void clear() {
        start_ = clock::now();
        lap_ = start_;
        elapsed_ = duration::zero();
        running_ = false;
    }

    /**
     * @brief 暂停计时。
     */
    void stop() {
        if (!running_) {
            return;
        }
        const auto now = clock::now();
        elapsed_ += now - start_;
        lap_ = now;
        running_ = false;
    }

    /**
     * @brief 继续计时。
     */
    void resume() {
        if (running_) {
            return;
        }
        start_ = clock::now();
        lap_ = start_;
        running_ = true;
    }

    /**
     * @brief 返回当前是否正在计时。
     */
    bool running() const noexcept {
        return running_;
    }

    /**
     * @brief 获取已累计耗时。
     */
    duration elapsed() const {
        if (!running_) {
            return elapsed_;
        }
        return elapsed_ + (clock::now() - start_);
    }

    /**
     * @brief 获取自上次 lap()/reset()/resume() 后的耗时，并刷新 lap 起点。
     */
    duration lap() {
        const auto now = clock::now();
        const auto out = now - lap_;
        lap_ = now;
        return out;
    }

    /**
     * @brief 判断累计耗时是否已经达到指定时长。
     */
    template<class Rep, class Period>
    bool expired(const std::chrono::duration<Rep, Period>& timeout) const {
        return elapsed() >= timeout;
    }

    /**
     * @brief 按指定 duration 类型返回耗时。
     */
    template<class Duration>
    Duration elapsed_as() const {
        return std::chrono::duration_cast<Duration>(elapsed());
    }

    long long ns() const {
        return elapsed_as<std::chrono::nanoseconds>().count();
    }

    long long us() const {
        return elapsed_as<std::chrono::microseconds>().count();
    }

    long long ms() const {
        return elapsed_as<std::chrono::milliseconds>().count();
    }

    double seconds() const {
        return std::chrono::duration<double>(elapsed()).count();
    }

    double milliseconds() const {
        return std::chrono::duration<double, std::milli>(elapsed()).count();
    }

    double microseconds() const {
        return std::chrono::duration<double, std::micro>(elapsed()).count();
    }

    double nanoseconds() const {
        return std::chrono::duration<double, std::nano>(elapsed()).count();
    }

    /**
     * @brief 执行函数并返回耗时；不接管函数返回值。
     */
    template<class F>
    static duration measure(F&& fn) {
        LTimer timer;
        std::forward<F>(fn)();
        return timer.elapsed();
    }

    template<class F>
    static double measure_ms(F&& fn) {
        return std::chrono::duration<double, std::milli>(measure(std::forward<F>(fn))).count();
    }

    template<class Rep, class Period>
    static void sleep_for(const std::chrono::duration<Rep, Period>& value) {
        std::this_thread::sleep_for(value);
    }

private:
    time_point start_;
    time_point lap_;
    duration elapsed_;
    bool running_;
};

/**
 * @brief 作用域计时器，析构时把 LTimer 传给回调。
 */
class LScopeTimer {
public:
    using callback_type = std::function<void(const LTimer&)>;

    explicit LScopeTimer(callback_type callback)
        : timer_(), callback_(std::move(callback)), active_(true) {}

    LScopeTimer(const LScopeTimer&) = delete;
    LScopeTimer& operator=(const LScopeTimer&) = delete;

    LScopeTimer(LScopeTimer&& other) noexcept
        : timer_(other.timer_), callback_(std::move(other.callback_)), active_(other.active_) {
        other.active_ = false;
    }

    LScopeTimer& operator=(LScopeTimer&& other) noexcept {
        if (this != &other) {
            finish();
            timer_ = other.timer_;
            callback_ = std::move(other.callback_);
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }

    ~LScopeTimer() {
        finish();
    }

    const LTimer& timer() const noexcept {
        return timer_;
    }

    void dismiss() noexcept {
        active_ = false;
    }

    void finish() {
        if (active_) {
            active_ = false;
            if (callback_) {
                callback_(timer_);
            }
        }
    }

private:
    LTimer timer_;
    callback_type callback_;
    bool active_;
};

namespace LTool {
using Timer = ::LTimer;
using ScopeTimer = ::LScopeTimer;
} // namespace LTool

#endif // LTOOL_LTIMER_INCLUDE
