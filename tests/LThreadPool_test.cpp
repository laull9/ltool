#include <catch2/catch_test_macros.hpp>

#include <atomic>

#include "LThreadPool.hpp"

TEST_CASE("LThreadPool submits tasks and waits", "[LThreadPool]") {
    LThreadPool pool(2);

    auto answer = pool.submit([] {
        return 21 * 2;
    });

    std::atomic<int> sum {0};
    auto work = pool.parallel_for(0, 8, [&](int i) {
        sum += i;
    });
    work.wait();

    REQUIRE(answer.get() == 42);
    REQUIRE(sum.load() == 28);

    pool.post([&] {
        sum += 10;
    });
    pool.wait();
    REQUIRE(sum.load() == 38);
    REQUIRE(pool.pending() == 0);
}

TEST_CASE("LThreadPool rejects waiting for itself from a worker", "[LThreadPool]") {
    LThreadPool pool(1);
    auto result = pool.submit([&] { pool.wait(); });
    REQUIRE_THROWS_AS(result.get(), BS::wait_deadlock);
}
