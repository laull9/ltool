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
    LTool::String name = "  ltool  ";
    LTool::Encoding enc = LTool::Encoding::Utf8;
    REQUIRE(enc == LEncoding::Utf8);
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

    LTool::Table table;
    table.header({"k", "v"}).add_values("a", 1);
    REQUIRE(table.markdown().find("| a") != std::string::npos);

    LTool::EnvOptions env_opts;
    REQUIRE_FALSE(env_opts.overwrite);
    REQUIRE(LEnv::get_env("LTOOL_MODULE_TEST_UNSET_VAR", "fallback") == "fallback");
}

TEST_CASE("模块导出 LPath 与 LFile", "[LModule]") {
    auto dir = LTool::Path(std::filesystem::temp_directory_path()) / "ltool_module_test";
    std::filesystem::create_directories(dir.path());
    LTool::File file = dir / "a.txt";
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

TEST_CASE("模块导出 LResult、LDateTime、LCli、LProcess、LChannel、LTempDir 与 LTempFile", "[LModule]") {
    LTool::Result<int, std::string> ok = LTool::Result<int, std::string>::ok(10);
    REQUIRE(ok.value() == 10);
    auto err = LTool::Result<int, std::string>(LTool::err(std::string("bad")));
    REQUIRE_FALSE(err.has_value());

    int cleaned = 0;
    {
        auto g = LTool::make_scope_exit([&] { ++cleaned; });
        REQUIRE(g.active());
    }
    REQUIRE(cleaned == 1);

    auto dt = LTool::DateTime::parse_iso8601("2026-11-18T12:00:00Z");
    REQUIRE(dt.to_iso8601() == "2026-11-18T12:00:00Z");

    LTool::Cli cli("mod_demo");
    cli.flag("flag", 'f', "标志");
    auto parsed = cli.parse(std::vector<std::string>{"-f"});
    REQUIRE(parsed.ok());
    REQUIRE(parsed.flag("flag"));

    REQUIRE(LTool::Process::current_pid() > 0);

    LTool::Channel<int> ch(2);
    REQUIRE(ch.try_send(42));
    REQUIRE(ch.try_receive() == 42);

    LTool::LruCache<std::string, int> cache(2);
    cache.put("k", 7);
    REQUIRE(cache.get("k") == 7);

    std::filesystem::path tmp_dir_path;
    {
        LTool::TempDir td("lmod_");
        tmp_dir_path = td.path().path();
        REQUIRE(std::filesystem::exists(tmp_dir_path));
    }
    REQUIRE_FALSE(std::filesystem::exists(tmp_dir_path));

    std::filesystem::path tmp_file_path;
    {
        LTool::TempFile tf("lmod_", ".tmp");
        tmp_file_path = tf.path();
        REQUIRE(std::filesystem::exists(tmp_file_path));
    }
    REQUIRE_FALSE(std::filesystem::exists(tmp_file_path));

    auto rot_sink = LLog::make_rotating_file_sink;
    (void)rot_sink;
    SUCCEED();
}

TEST_CASE("模块用户可以整理 JSON、借用遍历与配置行输出") {
    auto root = LJson::parse("[1,2]");
    root.compact();
    int count = 0;
    root.for_each_view([&](const LJson::Json&) { ++count; });
    REQUIRE(count == 3);
    LTempFile temp;
    LFile::WriteLinesOptions options;
    options.final_newline = false;
    temp.file().write_lines(std::vector<LString>{"one"}, options);
    REQUIRE(temp.file().read_text() == "one");
}
