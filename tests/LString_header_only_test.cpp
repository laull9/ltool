#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "LString.hpp"

TEST_CASE("LString uses bundled fmt as header-only by default", "[LString][fmt]") {
    REQUIRE(LString::format("{}:{}", "local", 42) == "local:42");

    LString value = "prefix";
    value.append_format("-{}", 7);
    REQUIRE(value == "prefix-7");
    REQUIRE(fmt::format("{}", LString("ok")) == "ok");
    REQUIRE(LString(std::vector<int> {1, 2}) == fmt::format("{}", std::vector<int> {1, 2}));
}
