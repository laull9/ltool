#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "Locked.hpp"

namespace {

/// 赋值被删除但可移动/拷贝构造的类型（模拟含 const/引用成员的句柄类）。
struct NoAssign {
    const int value = 0;

    NoAssign() = default;
    explicit NoAssign(int v) : value(v) {}
    NoAssign(const NoAssign&) = default;
    NoAssign(NoAssign&&) = default;
    NoAssign& operator=(const NoAssign&) = delete;
    NoAssign& operator=(NoAssign&&) = delete;
};
static_assert(std::move_constructible<NoAssign>);
static_assert(!std::is_copy_assignable_v<NoAssign>);
static_assert(!std::is_move_assignable_v<NoAssign>);

/// 特定输入构造时抛异常的类型，用于验证 emplace 的异常安全。
struct ThrowDuringCtor {
    static constexpr int throw_on = 13;
    int value = 0;

    ThrowDuringCtor() = default;
    explicit ThrowDuringCtor(int v) {
        if (v == throw_on) {
            throw std::runtime_error("boom");
        }
        value = v;
    }
};

} // namespace

TEST_CASE("Locked supports one writer and multiple readers", "[Locked]") {
    Locked<int, std::shared_mutex> value{0};

    constexpr int reader_count = 8;
    constexpr int write_count = 10000;

    std::atomic<bool> start{false};
    std::atomic<bool> writer_done{false};
    std::atomic<int> read_error_count{0};

    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (int i = 1; i <= write_count; ++i) {
            value.with_write([&](int& v) {
                v = i;
            });
        }

        writer_done.store(true, std::memory_order_release);
    });

    std::vector<std::thread> readers;
    readers.reserve(reader_count);

    for (int r = 0; r < reader_count; ++r) {
        readers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            int last = -1;

            while (!writer_done.load(std::memory_order_acquire)) {
                int current = value.with_read([](const int& v) {
                    return v;
                });

                if (current < last) {
                    read_error_count.fetch_add(1, std::memory_order_relaxed);
                }

                last = current;
            }
        });
    }

    start.store(true, std::memory_order_release);

    writer.join();

    for (auto& reader : readers) {
        reader.join();
    }

    REQUIRE(read_error_count.load() == 0);

    REQUIRE(value.with_read([](const int& v) {
        return v;
    }) == write_count);
}

TEST_CASE("Locked callbacks return values after releasing their lock", "[Locked]") {
    Locked<int> value(7);
    static_assert(!std::is_reference_v<decltype(value.with_read(
        [](const int& input) -> const int& { return input; }))>);
    REQUIRE(value.with_read([](const int& input) -> const int& { return input; }) == 7);
}

TEST_CASE("Locked::emplace rebuilds non-assignable values in place", "[Locked]") {
    // 赋值被删除的类型：旧实现（构造临时 + move 赋值）无法编译。
    Locked<NoAssign> value(std::in_place, 1);
    REQUIRE(value.with_read([](const NoAssign& v) { return v.value; }) == 1);

    value.emplace(42);
    REQUIRE(value.with_read([](const NoAssign& v) { return v.value; }) == 42);

    value.emplace(7);
    REQUIRE(value.with_read([](const NoAssign& v) { return v.value; }) == 7);
}

TEST_CASE("Locked::emplace is exception-safe on a throwing constructor",
          "[Locked]") {
    Locked<ThrowDuringCtor> value(std::in_place, 1);
    REQUIRE(value.with_read([](const ThrowDuringCtor& v) { return v.value; }) == 1);

    // 参数构造抛异常：临时对象构造失败，原值不受影响。
    REQUIRE_THROWS_AS(value.emplace(ThrowDuringCtor::throw_on),
                      std::runtime_error);
    REQUIRE(value.with_read([](const ThrowDuringCtor& v) { return v.value; }) == 1);

    // 失败后仍可正常重建。
    value.emplace(9);
    REQUIRE(value.with_read([](const ThrowDuringCtor& v) { return v.value; }) == 9);
}

TEST_CASE("Locked guards are contextually convertible to bool", "[Locked]") {
    Locked<std::vector<int>> xs(std::vector<int>{1, 2, 3});
    if (auto guard = xs.rlock()) {
        REQUIRE(guard->size() == 3);
    } else {
        FAIL("read guard should own its lock");
    }
    auto guard = xs.wlock();
    REQUIRE(static_cast<bool>(guard));
}

TEST_CASE("Locked::emplace takes the write lock", "[Locked]") {
    // 读者始终应看到完整的 vector（大小与首元素一致）；emplace 不加锁时
    // 读者可能观察到正在析构/构造中的对象（TSan 下会报数据竞争）。
    Locked<std::vector<int>> value(std::vector<int>(4, 4));
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};

    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            value.with_read([&](const std::vector<int>& v) {
                if (v.empty() || static_cast<int>(v.size()) != v.front()) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
    });

    for (int i = 1; i <= 2000; ++i) {
        const int n = i % 16 + 1;
        value.emplace(static_cast<std::size_t>(n), n);
    }
    done.store(true, std::memory_order_release);
    reader.join();

    REQUIRE(torn.load() == 0);
}
