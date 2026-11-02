#include <catch2/catch_test_macros.hpp>

#include <map>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "LRange.hpp"

TEST_CASE("LRange exposes range-v3 view pipelines", "[LRange]") {
    std::vector<int> values {1, 2, 3, 4, 5, 6};

    auto out = values
             | LRange::filter([](int value) { return value % 2 == 0; })
             | LRange::transform([](int value) { return value * value; })
             | LRange::reverse;

    REQUIRE(LRange::to_vector(out) == std::vector<int> {36, 16, 4});

    auto from_range = LRange::range(1, 7)
                    | LRange::filter([](int value) { return value % 2 == 0; })
                    | LRange::transform([](int value) { return value * value; });
    REQUIRE(LRange::to_vector(from_range) == std::vector<int> {4, 16, 36});
}

TEST_CASE("LRange collects ranges into standard containers", "[LRange]") {
    auto values = LRange::ints(1, 6) | LRange::transform([](int value) { return value % 3; });

    REQUIRE(LRange::to_vector(values) == std::vector<int> {1, 2, 0, 1, 2});
    REQUIRE(LRange::to_set(values) == std::set<int> {0, 1, 2});
    REQUIRE(LRange::to_unordered_set(values).size() == 3);

    std::vector<std::pair<std::string, int>> pairs {{"one", 1}, {"two", 2}};
    REQUIRE(LRange::to_map<std::string, int>(pairs) == std::map<std::string, int> {{"one", 1}, {"two", 2}});
}

TEST_CASE("LRange collection helpers can be used as pipe terminals", "[LRange]") {
    auto squares = LRange::range(1, 5)
                 | LRange::transform([](int value) { return value * value; })
                 | LRange::to<std::vector<int>>();
    REQUIRE(squares == std::vector<int> {1, 4, 9, 16});

    auto inferred_vector = LRange::range(1, 4) | LRange::to_vector();
    REQUIRE(inferred_vector == std::vector<int> {1, 2, 3});

    auto explicit_vector = LRange::range(1, 4) | LRange::to_vector<long>();
    REQUIRE(explicit_vector == std::vector<long> {1, 2, 3});

    auto set_values = LRange::range(1, 6)
                    | LRange::transform([](int value) { return value % 3; })
                    | LRange::to_set();
    REQUIRE(set_values == std::set<int> {0, 1, 2});

    auto explicit_set = LRange::range(1, 4) | LRange::to_set<long>();
    REQUIRE(explicit_set == std::set<long> {1, 2, 3});

    auto unordered_values = LRange::range(1, 6)
                          | LRange::transform([](int value) { return value % 3; })
                          | LRange::to_unordered_set();
    REQUIRE(unordered_values.size() == 3);

    std::vector<std::pair<std::string, int>> pairs {{"one", 1}, {"two", 2}};
    auto map_values = pairs | LRange::to_map<std::string, int>();
    REQUIRE(map_values == std::map<std::string, int> {{"one", 1}, {"two", 2}});

    auto unordered_map_values = pairs | LRange::to_unordered_map<std::string, int>();
    REQUIRE(unordered_map_values.at("one") == 1);
    REQUIRE(unordered_map_values.at("two") == 2);

    auto collected = LRange::range(3) | LRange::collect();
    REQUIRE(collected == std::vector<int> {0, 1, 2});
}

TEST_CASE("LRange provides query and aggregation helpers", "[LRange]") {
    std::vector<int> values {2, 4, 6, 8};

    REQUIRE(LRange::contains(values, 6));
    REQUIRE_FALSE(LRange::contains(values, 7));
    REQUIRE(LRange::contains_if(values, [](int value) { return value > 6; }));
    REQUIRE(LRange::count_if(values, [](int value) { return value % 4 == 0; }) == 2);
    REQUIRE(LRange::any(values, [](int value) { return value == 8; }));
    REQUIRE(LRange::all_of(values, [](int value) { return value % 2 == 0; }));
    REQUIRE(LRange::none(values, [](int value) { return value < 0; }));
    REQUIRE(LRange::first(values) == 2);
    REQUIRE(LRange::last(values) == 8);
    REQUIRE(LRange::sum(values) == 20);
    REQUIRE(LRange::average(values).value() == 5.0);

    std::vector<int> empty;
    REQUIRE_FALSE(LRange::first(empty).has_value());
    REQUIRE_FALSE(LRange::last(empty).has_value());
    REQUIRE_FALSE(LRange::average(empty).has_value());
}

TEST_CASE("LRange adds indexed and string joining helpers", "[LRange]") {
    std::vector<std::string> words {"alpha", "beta", "gamma"};
    std::vector<std::pair<int, std::string>> indexed;

    for (auto [index, value] : LRange::indexed(words, 10)) {
        indexed.emplace_back(index, value);
    }

    REQUIRE(indexed == std::vector<std::pair<int, std::string>> {
        {10, "alpha"},
        {11, "beta"},
        {12, "gamma"},
    });
    REQUIRE(LRange::join_strings(words, "/") == "alpha/beta/gamma");
}

TEST_CASE("LRange range behaves like Python range", "[LRange]") {
    REQUIRE(LRange::to_vector(LRange::range(5)) == std::vector<int> {0, 1, 2, 3, 4});
    REQUIRE(LRange::to_vector(LRange::range(-3)).empty());
    REQUIRE(LRange::to_vector(LRange::range(2, 7)) == std::vector<int> {2, 3, 4, 5, 6});
    REQUIRE(LRange::to_vector(LRange::range(8, 2, -2)) == std::vector<int> {8, 6, 4});
    REQUIRE_THROWS_AS(LRange::range(8u, 2u, -2), std::invalid_argument);
    REQUIRE(LRange::to_vector(LRange::range(2, 8, -1)).empty());
    REQUIRE_THROWS_AS(LRange::range(1, 5, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(LRange::range(0.0, 1.0, std::numeric_limits<double>::quiet_NaN()),
                      std::invalid_argument);

    auto floats = LRange::to_vector(LRange::range(0.0, 1.0, 0.25));
    REQUIRE(floats == std::vector<double> {0.0, 0.25, 0.5, 0.75});

    auto mixed = LRange::to_vector(LRange::range(1, 2.0, 0.5));
    REQUIRE(mixed == std::vector<double> {1.0, 1.5});
}

TEST_CASE("LRange avoids overflow and owns temporary indexed ranges", "[LRange]") {
    const auto near_limit = LRange::to_vector(
        LRange::range(std::numeric_limits<int>::max() - 1, std::numeric_limits<int>::max(), 2));
    REQUIRE(near_limit == std::vector<int> {std::numeric_limits<int>::max() - 1});

    std::vector<std::pair<std::ptrdiff_t, int>> values;
    for (auto [index, value] : LRange::indexed(std::vector<int> {4, 5}, 3)) {
        values.emplace_back(index, value);
    }
    REQUIRE(values == std::vector<std::pair<std::ptrdiff_t, int>> {{3, 4}, {4, 5}});
}
