#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "LChannel.hpp"

TEST_CASE("LChannel bounded send/receive and close behavior", "[LChannel]") {
    LChannel<int> ch(2);
    REQUIRE(ch.capacity() == 2);
    REQUIRE(ch.empty());

    REQUIRE(ch.try_send(10));
    REQUIRE(ch.try_send(20));
    REQUIRE_FALSE(ch.try_send(30));
    REQUIRE(ch.size() == 2);

    std::thread producer([&] {
        REQUIRE(ch.send(30));
        ch.close();
    });

    REQUIRE(ch.receive() == 10);
    REQUIRE(ch.receive() == 20);
    REQUIRE(ch.receive() == 30);
    REQUIRE_FALSE(ch.receive().has_value());
    REQUIRE(ch.is_closed());
    REQUIRE_FALSE(ch.send(40));

    producer.join();
}

TEST_CASE("LLruCache evicts LRU entries and expires by TTL", "[LChannel]") {
    LTool::LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    REQUIRE(cache.get("a") == 1); // 访问 a，使 b 成为最久未使用
    cache.put("c", 3);            // 应淘汰 b

    REQUIRE_FALSE(cache.contains("b"));
    REQUIRE(cache.peek("a") == 1);
    REQUIRE(cache.get("c") == 3);
    REQUIRE(cache.size() == 2);

    LChannel<int> timeout_ch(1);
    REQUIRE_FALSE(timeout_ch.receive_for(std::chrono::milliseconds(15)).has_value());

    LLruCache<std::string, std::string> ttl_cache(4, std::chrono::milliseconds(30));
    ttl_cache.put("k", "v");
    REQUIRE(ttl_cache.get("k") == "v");
    std::this_thread::sleep_for(std::chrono::milliseconds(45));
    REQUIRE_FALSE(ttl_cache.get("k").has_value());
}
