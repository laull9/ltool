#include <catch2/catch_test_macros.hpp>

#include <string>

#include "LResult.hpp"

namespace {

LResult<int, std::string> parse_positive(int x) {
    if (x <= 0) {
        return make_err(std::string("must be positive"));
    }
    return LResult<int, std::string>::ok(x);
}

LResult<void, std::string> validate_even(int x) {
    if (x % 2 != 0) {
        return make_err(std::string("must be even"));
    }
    return LResult<void, std::string>::ok();
}

} // namespace

TEST_CASE("LResult ok, err, map, and_then, or_else", "[LResult]") {
    auto ok = parse_positive(21)
                  .map([](int v) { return v * 2; })
                  .and_then([](int v) { return LResult<int, std::string>::ok(v + 1); });
    REQUIRE(ok.has_value());
    REQUIRE(static_cast<bool>(ok));
    REQUIRE(ok.value() == 43);
    REQUIRE(*ok == 43);

    auto bad = parse_positive(-3);
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error() == "must be positive");
    REQUIRE(bad.value_or(99) == 99);
    REQUIRE_THROWS(bad.value());

    auto mapped_err = bad.map_error([](const std::string& e) { return "err: " + e; });
    REQUIRE_FALSE(mapped_err.has_value());
    REQUIRE(mapped_err.error() == "err: must be positive");

    auto recovered = bad.or_else([](const std::string&) {
        return LTool::Result<int, std::string>::ok(7);
    });
    REQUIRE(recovered.has_value());
    REQUIRE(recovered.value() == 7);
}

TEST_CASE("LResult<void, E> works for status-only operations", "[LResult]") {
    auto ok = validate_even(4);
    REQUIRE(ok.has_value());

    auto bad = validate_even(3);
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error() == "must be even");
    REQUIRE_THROWS(bad.value());

    LTool::Result<void> def_ok = LTool::err(std::string("failed"));
    REQUIRE_FALSE(def_ok.has_value());
    REQUIRE(def_ok.error() == "failed");
}

TEST_CASE("LScopeGuard runs on scope exit unless dismissed", "[LResult]") {
    int count = 0;
    {
        auto guard = make_scope_exit([&] { ++count; });
        REQUIRE(guard.active());
    }
    REQUIRE(count == 1);

    {
        auto guard = LTool::make_scope_exit([&] { ++count; });
        guard.dismiss();
        REQUIRE_FALSE(guard.active());
    }
    REQUIRE(count == 1);
}
