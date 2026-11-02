// 验证 `import ltool;` 的导出面：主要组件的公开 API、模板特化（fmt formatter）、
// ADL（operator<<）、隐藏友元（==）、字面量运算符，以及 import 后包含宏头文件。
// 会弹窗或写终端的接口（LDialog、LProgress）只放在未调用的 lambda 中检查名字可见性。

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

import ltool;

#include "LLogMacros.hpp"

using namespace std::chrono_literals;

namespace {

/// LConfig/LToml/LYaml 反射用的配置结构。
struct ServerConfig {
    std::string host = "localhost";
    int port = 8080;
};

} // namespace

TEST_CASE("模块导出 LString 与 fmt 入口", "[LModule]") {
    LString name = "  ltool  ";
    REQUIRE(name.trimmed() == "ltool");
    REQUIRE(LString::format("{}-{}", "a", 1) == "a-1");
    REQUIRE(fmt::format("[{}]", name.trimmed()) == "[ltool]"); // formatter<LString> 特化可达
    auto literal = "abc"_ls;
    REQUIRE(literal.md5() == "900150983cd24fb0d6963f7d28e17f72");
    REQUIRE(LTool::current_version().major >= 1);
    REQUIRE_FALSE(std::string(LTool::version_string()).empty());
}

TEST_CASE("模块导出 LJson 值语义与引用", "[LModule]") {
    auto root = LJson::parse(R"({"x":1,"arr":[1,2]})");
    auto ref = root["x"];
    ref = 5;
    LJson::Json copy = root["arr"];
    copy.push_back(3);
    REQUIRE(root["x"] == 5);                 // 隐藏友元 operator==
    REQUIRE(root["arr"].dump() == "[1,2]");

    std::ostringstream out;
    out << root["x"];                        // LJson::operator<<（ADL）
    REQUIRE(out.str() == "5");
    REQUIRE(fmt::format("{}", root["arr"]) == "[1,2]");
}

TEST_CASE("模块导出 LLog，宏头文件可在 import 后使用", "[LModule]") {
    std::vector<std::string> lines;
    LLog::set_sink([&](const LLog::Record& record) {
        lines.push_back(record.message + "@" + std::to_string(record.location.line()));
    });
    LLOG_INFO("hello {}", 42);
    const auto expected_line = __LINE__ - 1;
    LLog::writef(LLog::Level::warn, "w {}", 1, LTOOL_CURRENT_SOURCE_LOCATION);
    LLog::reset_sink();

    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "hello 42@" + std::to_string(expected_line));
    REQUIRE(lines[1].rfind("w 1@", 0) == 0);
}

TEST_CASE("模块导出 Locked、LTimer、LRandom、LTable、LEnv", "[LModule]") {
    Locked<std::vector<int>> values;
    values.with_write([](std::vector<int>& v) { v.push_back(1); });
    if (auto guard = values.rlock()) {
        REQUIRE(guard->size() == 1);
    }

    LTimer timer;
    REQUIRE(timer.ms() >= 0);

    LRandom rng(42);
    REQUIRE(rng.real(0.0, 1.0) < 1.0);
    const auto n = LRandom::rand_int(1, 6);
    REQUIRE(n >= 1);
    REQUIRE(n <= 6);
    REQUIRE(LRandom::shared().uuid_v4().size() == 36);

    LTable table;
    table.header({"k", "v"}).add_values("a", 1);
    REQUIRE(table.markdown().find("| a") != std::string::npos);

    REQUIRE(LEnv::get_env("LTOOL_MODULE_TEST_UNSET_VAR", "fallback") == "fallback");
}

TEST_CASE("模块导出 LPath 与 LFile", "[LModule]") {
    auto dir = LPath(std::filesystem::temp_directory_path()) / "ltool_module_test";
    std::filesystem::create_directories(dir.path());
    LFile file = dir / "a.txt";
    file.write_text("module");
    REQUIRE(file.read_text() == "module");
    REQUIRE((dir / "a.txt") == LPath(file.path()));
    REQUIRE("x/y.txt"_lp.filename() == "y.txt"_lp);
    std::filesystem::remove_all(dir.path());
}

TEST_CASE("模块导出 LScheduler 与 LThreadPool", "[LModule]") {
    LThreadPool pool(2);
    LScheduler scheduler(LTool::make_pool_executor(pool));
    std::atomic<int> count{0};
    scheduler.after(1ms, [&] { count.fetch_add(1); });
    for (int i = 0; i < 500 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(2ms);
    }
    REQUIRE(count.load() == 1);
    REQUIRE(pool.submit([] { return 7; }).get() == 7);
}

TEST_CASE("模块导出 LConfig、LToml、LYaml", "[LModule]") {
    auto cfg = LConfig::read<ServerConfig>(R"({"host":"example.org","port":9000})");
    REQUIRE(cfg.host == "example.org");
    REQUIRE(cfg.port == 9000);
    REQUIRE(LToml::read_or_throw<ServerConfig>(LToml::write(cfg)).port == 9000);
    REQUIRE(LYaml::read_or_throw<ServerConfig>(LYaml::write(cfg)).host == "example.org");
    REQUIRE(LJson::read_or_throw<ServerConfig>(LJson::write(cfg)).port == 9000);
}

TEST_CASE("模块导出 LRange", "[LModule]") {
    auto squares = LRange::range(1, 6)
                 | LRange::filter([](int x) { return x % 2 == 1; })
                 | LRange::transform([](int x) { return x * x; })
                 | LRange::to_vector();
    REQUIRE(squares == std::vector<int>{1, 9, 25});
    REQUIRE(LRange::join_strings(std::vector<std::string>{"a", "b"}, ",") == "a,b");
}

TEST_CASE("模块导出 LDialog 与 LProgress 的名字", "[LModule]") {
    // 只检查名字可见与可编译，不实际调用。
    [[maybe_unused]] auto dialog = [] {
        LDialog::message("title", "text");
        return LDialog::open_file("pick", ".");
    };
    [[maybe_unused]] auto progress = [] {
        std::atomic<int> work{0};
        LProgress::Counter counter(&work);
        return LProgress::red;
    };
    SUCCEED();
}
