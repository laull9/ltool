/**
 * @file LLruCache.hpp
 * @brief 支持容量限制与可选 TTL 的线程安全缓存。
 * @code
 * LLruCache<int, int> cache(16);
 * cache.put(1, 2);
 * @endcode
 */
#pragma once
#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

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
/// 线程安全 LRU/TTL 缓存类型别名
template <
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
using LruCache = ::LLruCache<Key, Value, Hash, KeyEqual>;

} // namespace LTool
