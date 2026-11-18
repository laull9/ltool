#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "LProcess.hpp"

TEST_CASE("LProcess captures stdout, stderr, stdin, and exit code", "[LProcess]") {
    auto ok = LProcess::shell("printf 'out_msg'; printf 'err_msg' >&2");
    REQUIRE(ok.started);
    REQUIRE(ok.ok());
    REQUIRE(ok.exit_code == 0);
    REQUIRE(ok.stdout_text == "out_msg");
    REQUIRE(ok.stderr_text == "err_msg");

    LProcessOptions in_opts;
    in_opts.stdin_data = "ping_from_stdin";
    auto echo_in = LProcess::shell("cat", in_opts);
    REQUIRE(echo_in.ok());
    REQUIRE(echo_in.stdout_text == "ping_from_stdin");

    LProcessOptions env_opts;
    env_opts.env["LTOOL_TEST_PROC_VAR"] = "hello_env";
    auto env_res = LProcess::shell("printf \"%s\" \"$LTOOL_TEST_PROC_VAR\"", env_opts);
    REQUIRE(env_res.ok());
    REQUIRE(env_res.stdout_text == "hello_env");

    auto fail = LProcess::shell("exit 42");
    REQUIRE(fail.started);
    REQUIRE_FALSE(fail.ok());
    REQUIRE(fail.exit_code == 42);
}

TEST_CASE("LProcess enforces timeout and provides process metadata", "[LProcess]") {
    LTool::ProcessOptions opts;
    opts.timeout = std::chrono::milliseconds(80);
    auto slow = LTool::Process::shell("sleep 3", opts);
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
