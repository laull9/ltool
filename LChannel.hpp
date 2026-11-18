/**
 * @file LChannel.hpp
 * @brief LTool 并发阻塞通道（MPMC）与线程安全 LRU/TTL 缓存（纯头文件）
 *
 * @code
 * #include "LChannel.hpp"
 *
 * LChannel<int> ch(4);
 * ch.send(42);
 * auto v = ch.receive(); // 42
 * ch.close();
 *
 * LLruCache<std::string, int> cache(128, std::chrono::seconds(60));
 * cache.put("answer", 42);
 * auto cached = cache.get("answer"); // 42
 * @endcode
 */
#ifndef LTOOL_LCHANNEL_INCLUDE
#define LTOOL_LCHANNEL_INCLUDE

#include "detail/LToolConfig.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include "LLruCache.hpp"
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

/// 线程安全的多生产者多消费者（MPMC）通信通道（支持有界背压与无界队列）
template <typename T>
class LChannel {
public:
    /// 构造通道（capacity == 0 表示无界，> 0 表示最大缓冲容量）
    explicit LChannel(std::size_t capacity = 0)
        : capacity_(capacity) {}

    LChannel(const LChannel&) = delete;
    LChannel& operator=(const LChannel&) = delete;

    /// 发送数据；若有界且已满则阻塞，通道关闭后返回 false
    bool send(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this]() {
            return closed_ || capacity_ == 0 || queue_.size() < capacity_;
        });
        if (closed_) {
            return false;
        }
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    /// 非阻塞尝试发送数据；通道已关闭或已满时返回 false
    bool try_send(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || (capacity_ > 0 && queue_.size() >= capacity_)) {
            return false;
        }
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    /// 带超时等待发送数据；超时或通道关闭时返回 false
    template <typename Rep, typename Period>
    bool send_for(T value, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ready = not_full_.wait_for(lock, timeout, [this]() {
            return closed_ || capacity_ == 0 || queue_.size() < capacity_;
        });
        if (!ready || closed_) {
            return false;
        }
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    /// 阻塞接收数据；仅当通道已关闭且队列耗尽时返回 std::nullopt
    [[nodiscard]] std::optional<T> receive() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this]() {
            return closed_ || !queue_.empty();
        });
        if (queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return item;
    }

    /// 非阻塞尝试接收数据；当前为空时返回 std::nullopt
    [[nodiscard]] std::optional<T> try_receive() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return item;
    }

    /// 带超时等待接收数据；超时或关闭且队列为空时返回 std::nullopt
    template <typename Rep, typename Period>
    [[nodiscard]] std::optional<T> receive_for(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ready = not_empty_.wait_for(lock, timeout, [this]() {
            return closed_ || !queue_.empty();
        });
        if (!ready || queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return item;
    }

    /// 关闭通道，唤醒所有阻塞中的发送者与接收者
    void close() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    /// 查询通道是否已关闭
    [[nodiscard]] bool is_closed() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    /// 查询当前队列中的元素数量
    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /// 查询当前队列是否为空
    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /// 查询通道容量（0 表示无界）
    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

private:
    std::size_t capacity_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> queue_;
    bool closed_ = false;
};

namespace LTool {

/// 并发通信通道类型别名
template <typename T>
using Channel = ::LChannel<T>;

} // namespace LTool

#endif // LTOOL_LCHANNEL_INCLUDE
