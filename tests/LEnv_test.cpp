#include <catch2/catch_test_macros.hpp>

#include <string>

#include "LEnv.hpp"

TEST_CASE("LEnv parses dotenv text", "[LEnv]") {
    LEnv env;
    env.parse(
        "A=hello\n"
        "B=${A}/world\n"
        "C=\"line\\ntext\"\n"
        "D='literal # value'\n"
        "E=plain value # comment\n"
        "export F=ok\n");

    REQUIRE(env.get("A") == "hello");
    REQUIRE(env.get("B") == "hello/world");
    REQUIRE(env.get("C") == std::string("line\ntext"));
    REQUIRE(env.get("D") == "literal # value");
    REQUIRE(env.get("E") == "plain value");
    REQUIRE(env.get("F") == "ok");
    REQUIRE(env.get("MISSING", "fallback") == "fallback");
}

TEST_CASE("LEnv applies process environment without overwriting by default", "[LEnv]") {
    const std::string key = "LTOOL_LENV_TEST_VALUE";
    LEnv::unset_env(key);

    REQUIRE(LEnv::set_env(key, "origin"));

    LEnv env;
    env.set(key, "from-env");
    env.apply();
    REQUIRE(LEnv::get_env(key) == "origin");

    env.apply(true);
    REQUIRE(LEnv::get_env(key) == "from-env");

    REQUIRE(LEnv::unset_env(key));
}
