#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "LProcess.hpp"

namespace {
std::string child_path() {
    return LProcess::executable_dir() +
#if LTOOL_PLATFORM_WINDOWS
        "/ltool_process_child.exe";
#else
        "/ltool_process_child";
#endif
}
}

TEST_CASE("LProcess handles simultaneous backpressure on all three pipes", "[LProcess]") {
    LProcessOptions opts;
    opts.stdin_data.assign(262144, 'i');
    opts.timeout = std::chrono::seconds(5);
    auto result = LProcess::run(child_path(), {"duplex"}, opts);
    REQUIRE(result.ok());
    REQUIRE(result.stdout_text == std::string(131072, 'o') + opts.stdin_data);
    REQUIRE(result.stderr_text == std::string(131072, 'e'));

    opts.env["LTOOL_TEST_PROC_VAR"] = "hello_env";
    opts.stdin_data.clear();
    auto env_res = LProcess::run(child_path(), {"env"}, opts);
    REQUIRE(env_res.ok());
    REQUIRE(env_res.stdout_text == "hello_env");

    auto fail = LProcess::run(child_path(), {"exit"});
    REQUIRE(fail.started);
    REQUIRE_FALSE(fail.ok());
    REQUIRE(fail.exit_code == 42);
}

TEST_CASE("LProcess enforces timeout and provides process metadata", "[LProcess]") {
    LTool::ProcessOptions opts;
    opts.timeout = std::chrono::milliseconds(80);
    opts.stdin_data.assign(262144, 'x');
    const auto start = std::chrono::steady_clock::now();
    auto slow = LTool::Process::run(child_path(), {"idle"}, opts);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
    REQUIRE(slow.started);
    REQUIRE(slow.timed_out);
    REQUIRE_FALSE(slow.ok());

    auto missing = LProcess::run("definitely_nonexistent_binary_xyz_12345");
    REQUIRE_FALSE(missing.started);
    REQUIRE_FALSE(missing.ok());
    REQUIRE_FALSE(missing.error.empty());

    REQUIRE(LProcess::current_pid() > 0);
    REQUIRE_FALSE(LProcess::executable_path().empty());
    REQUIRE_FALSE(LProcess::executable_dir().empty());
    REQUIRE_FALSE(LProcess::hostname().empty());
}
