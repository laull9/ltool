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
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
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

/// 线程安全的 LRU 缓存，支持可选 TTL 过期时间
template <
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
class LLruCache {
public:
    using Clock = std::chrono::steady_clock;

    /// 构造缓存（capacity 必须大于 0；ttl 为 std::nullopt 表示永不过期）
    explicit LLruCache(
        std::size_t capacity,
        std::optional<std::chrono::milliseconds> ttl = std::nullopt
    )
        : capacity_(capacity)
        , ttl_(ttl) {
        if (capacity_ == 0) {
            throw std::invalid_argument("LLruCache capacity must be positive");
        }
    }

    LLruCache(const LLruCache&) = delete;
    LLruCache& operator=(const LLruCache&) = delete;

    /// 插入或更新缓存条目，并刷新其最近使用顺序与过期时间
    void put(Key key, Value value) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();
        auto expiry = ttl_.has_value() ? std::optional<Clock::time_point>(now + *ttl_) : std::nullopt;

        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->value = std::move(value);
            it->second->expires_at = expiry;
            order_.splice(order_.begin(), order_, it->second);
            return;
        }

        if (order_.size() >= capacity_) {
            const Key& victim = order_.back().key;
            map_.erase(victim);
            order_.pop_back();
        }

        order_.push_front(Entry{std::move(key), std::move(value), expiry});
        map_.emplace(order_.front().key, order_.begin());
    }

    /// 查询并刷新条目热度；未命中或已过期返回 std::nullopt
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }
        const auto now = Clock::now();
        if (is_expired(*it->second, now)) {
            order_.erase(it->second);
            map_.erase(it);
            return std::nullopt;
        }
        order_.splice(order_.begin(), order_, it->second);
        return it->second->value;
    }

    /// 只读查看条目（不刷新热度、不删除过期条目）
    [[nodiscard]] std::optional<Value> peek(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }
        if (is_expired(*it->second, Clock::now())) {
            return std::nullopt;
        }
        return it->second->value;
    }

    /// 判断键是否存在且未过期
    [[nodiscard]] bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        return !is_expired(*it->second, Clock::now());
    }

    /// 删除指定键，返回是否确实删除了条目
    bool erase(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        order_.erase(it->second);
        map_.erase(it);
        return true;
    }

    /// 主动清理所有已过期条目，返回清理数量
    std::size_t purge_expired() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ttl_.has_value()) {
            return 0;
        }
        const auto now = Clock::now();
        std::size_t removed = 0;
        for (auto it = order_.begin(); it != order_.end();) {
            if (is_expired(*it, now)) {
                map_.erase(it->key);
                it = order_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

    /// 清空所有缓存条目
    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
        order_.clear();
    }

    /// 查询当前条目数（含尚未惰性淘汰的条目）
    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_.size();
    }

    /// 查询缓存容量上限
    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    /// 查询缓存是否为空
    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_.empty();
    }

private:
    struct Entry {
        Key key;
        Value value;
        std::optional<Clock::time_point> expires_at;
    };

    static bool is_expired(const Entry& e, Clock::time_point now) noexcept {
        return e.expires_at.has_value() && now >= *e.expires_at;
    }

    std::size_t capacity_ = 0;
    std::optional<std::chrono::milliseconds> ttl_;
    mutable std::mutex mutex_;
    std::list<Entry> order_;
    std::unordered_map<Key, typename std::list<Entry>::iterator, Hash, KeyEqual> map_;
};

namespace LTool {

/// 并发通信通道类型别名
template <typename T>
using Channel = ::LChannel<T>;

/// 线程安全 LRU/TTL 缓存类型别名
template <
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
using LruCache = ::LLruCache<Key, Value, Hash, KeyEqual>;

} // namespace LTool

#endif // LTOOL_LCHANNEL_INCLUDE
