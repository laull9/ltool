#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "LRandom.hpp"

namespace {

bool is_hex_uuid(const std::string& value, char version) {
    if (value.size() != 36) {
        return false;
    }

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') {
                return false;
            }
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }

    return value[14] == version && (value[19] == '8' || value[19] == '9' ||
                                    value[19] == 'a' || value[19] == 'b');
}

std::uint64_t uuid_v7_timestamp_ms(const std::string& value) {
    std::uint64_t out = 0;
    for (std::size_t i = 0; i < 13; ++i) {
        if (value[i] == '-') {
            continue;
        }
        out <<= 4;
        const char ch = value[i];
        if (ch >= '0' && ch <= '9') {
            out |= static_cast<std::uint64_t>(ch - '0');
        } else {
            out |= static_cast<std::uint64_t>(ch - 'a' + 10);
        }
    }
    return out;
}

std::uint64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

} // namespace

TEST_CASE("LRandom can produce reproducible sequences", "[LRandom]") {
    LRandom a(1234);
    LRandom b(1234);

    for (int i = 0; i < 16; ++i) {
        REQUIRE(a.integer<int>(-100, 100) == b.integer<int>(-100, 100));
    }

    a.seed(7);
    b.seed(7);
    REQUIRE(a.raw() == b.raw());
}

TEST_CASE("LRandom generates bounded numbers and probabilities", "[LRandom]") {
    LRandom rng(42);

    for (int i = 0; i < 64; ++i) {
        auto value = rng.integer<int>(3, 9);
        REQUIRE(value >= 3);
        REQUIRE(value <= 9);

        auto unit = rng.real();
        REQUIRE(unit >= 0.0);
        REQUIRE(unit < 1.0);

        auto dice = rng.roll();
        REQUIRE(dice >= 1);
        REQUIRE(dice <= 6);
    }

    REQUIRE_FALSE(rng.boolean(0.0));
    REQUIRE(rng.boolean(1.0));
    REQUIRE_THROWS_AS(rng.integer<int>(5, 4), std::invalid_argument);
    REQUIRE_THROWS_AS(rng.chance(1.5), std::invalid_argument);
    REQUIRE_THROWS_AS(rng.roll(0), std::invalid_argument);
}

TEST_CASE("LRandom chooses, shuffles, samples, and uses weights", "[LRandom]") {
    LRandom rng(9);
    std::vector<int> values {1, 2, 3, 4, 5};

    auto chosen = rng.choice(values);
    REQUIRE(std::find(values.begin(), values.end(), chosen) != values.end());

    auto shuffled = rng.shuffled(values);
    std::sort(shuffled.begin(), shuffled.end());
    REQUIRE(shuffled == values);

    auto sample = rng.sample(values, 3);
    REQUIRE(sample.size() == 3);
    for (auto item : sample) {
        REQUIRE(std::find(values.begin(), values.end(), item) != values.end());
    }

    std::vector<double> weights {0.0, 0.0, 1.0};
    REQUIRE(rng.weighted_index(weights) == 2);

    std::vector<std::string> names {"a", "b", "c"};
    REQUIRE(rng.weighted_choice(names, weights) == "c");

    REQUIRE_THROWS_AS(rng.choice(std::vector<int> {}), std::runtime_error);
    REQUIRE_THROWS_AS(rng.weighted_index(std::vector<double> {-1.0}), std::invalid_argument);
}

TEST_CASE("LRandom builds random bytes, strings, hex, and UUIDs", "[LRandom]") {
    LRandom rng(100);

    auto data = rng.bytes(12);
    REQUIRE(data.size() == 12);

    std::vector<std::uint8_t> filled(4);
    rng.fill_bytes(filled.begin(), filled.end());
    REQUIRE(filled.size() == 4);

    auto text = rng.string(16, "ab");
    REQUIRE(text.size() == 16);
    REQUIRE(text.find_first_not_of("ab") == std::string::npos);

    auto hex = rng.hex(8);
    REQUIRE(hex.size() == 16);
    REQUIRE(hex.find_first_not_of(LRandom::hex_chars()) == std::string::npos);

    REQUIRE(is_hex_uuid(rng.uuid_v4(), '4'));
    REQUIRE(is_hex_uuid(rng.uuid_v7(), '7'));
    REQUIRE_THROWS_AS(rng.string(1, ""), std::runtime_error);
}

TEST_CASE("LRandom generates UUIDv7 timestamps and safe UUIDs", "[LRandom]") {
    LRandom rng(77);
    const auto before = now_ms();
    const auto uuid7 = rng.uuid_v7();
    const auto safe4 = LRandom::safe_uuid_v4();
    const auto safe7 = LRandom::safe_uuid_v7();
    const auto after = now_ms();

    REQUIRE(is_hex_uuid(uuid7, '7'));
    REQUIRE(is_hex_uuid(safe4, '4'));
    REQUIRE(is_hex_uuid(safe7, '7'));

    const auto uuid7_ms = uuid_v7_timestamp_ms(uuid7);
    const auto safe7_ms = uuid_v7_timestamp_ms(safe7);
    REQUIRE(uuid7_ms >= before);
    REQUIRE(uuid7_ms <= after);
    REQUIRE(safe7_ms >= before);
    REQUIRE(safe7_ms <= after);
}

TEST_CASE("LRandom static helpers use thread-local default instances", "[LRandom]") {
    for (int i = 0; i < 64; ++i) {
        auto value = LRandom::rand_int(10, 20);
        REQUIRE(value >= 10);
        REQUIRE(value <= 20);

        auto unit = LRandom::rand_real();
        REQUIRE(unit >= 0.0);
        REQUIRE(unit < 1.0);
    }

    std::vector<int> values {1, 2, 3, 4};
    REQUIRE(std::find(values.begin(), values.end(), LRandom::rand_choice(values)) != values.end());

    auto shuffled = LRandom::rand_shuffled(values);
    std::sort(shuffled.begin(), shuffled.end());
    REQUIRE(shuffled == values);

    auto sample = LRandom::rand_sample(values, 2);
    REQUIRE(sample.size() == 2);

    std::vector<double> weights {0.0, 1.0, 0.0, 0.0};
    REQUIRE(LRandom::rand_weighted_choice(values, weights) == 2);

    std::vector<std::uint8_t> filled(8);
    LRandom::rand_fill_bytes(filled.begin(), filled.end());

    REQUIRE(LRandom::rand_string(8).size() == 8);
    REQUIRE(LRandom::rand_hex(4).size() == 8);
    REQUIRE(is_hex_uuid(LRandom::rand_uuid_v4(), '4'));
    REQUIRE(is_hex_uuid(LRandom::rand_uuid_v7(), '7'));
}

TEST_CASE("LRandom static helpers can be called from multiple threads", "[LRandom]") {
    std::vector<std::thread> threads;
    std::vector<int> sums(8, 0);
    std::vector<int> ok(8, 1);

    for (std::size_t i = 0; i < sums.size(); ++i) {
        threads.emplace_back([i, &sums, &ok]() {
            int sum = 0;
            for (int j = 0; j < 256; ++j) {
                sum += LRandom::rand_int(0, 10);
                ok[i] = ok[i] && !LRandom::rand_bool(0.0);
                ok[i] = ok[i] && LRandom::rand_bool(1.0);
                ok[i] = ok[i] &&
                        LRandom::rand_string(4, "ab").find_first_not_of("ab") ==
                            std::string::npos;
            }
            sums[i] = sum;
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    for (auto sum : sums) {
        REQUIRE(sum >= 0);
        REQUIRE(sum <= 2560);
    }
    for (auto item : ok) {
        REQUIRE(item == 1);
    }
}

TEST_CASE("LRandom rejects invalid weighted distributions", "[LRandom]") {
    LRandom random(42);
    REQUIRE_THROWS_AS(random.weighted_index(std::vector<double> {0.0, 0.0}), std::invalid_argument);
    REQUIRE_THROWS_AS(random.weighted_index(std::vector<double> {std::nan(""), 1.0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(random.weighted_index(
                          std::vector<double> {std::numeric_limits<double>::infinity()}),
                      std::invalid_argument);
}
