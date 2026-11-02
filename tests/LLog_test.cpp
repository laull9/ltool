#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "LTool.hpp"

TEST_CASE("LLog filters messages and writes to custom sink", "[LLog]") {
    std::vector<LLog::Record> records;

    LLog::set_level(LLog::Level::warn);
    LLog::set_sink([&](const LLog::Record& item) {
        records.push_back(item);
    });

    LLog::info("hidden");
    LLOG_WARN("visible {}", 42);

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].severity == LLog::Level::warn);
    REQUIRE(records[0].message == "visible 42");

    LLog::reset_sink();
    LLog::set_level(LLog::Level::info);
}

TEST_CASE("LLog can format colored terminal records", "[LLog]") {
    LLog::Record item;
    item.severity = LLog::Level::error;
    item.message = "boom";

    auto plain = LLog::format_record(item, false, false);
    auto colored = LLog::format_record(item, false, true);

    REQUIRE(plain.find("\x1b[") == std::string::npos);
    REQUIRE(colored.find("\x1b[") != std::string::npos);
    REQUIRE(colored.find("[ERROR]") != std::string::npos);
}

TEST_CASE("ltool umbrella header exposes common headers", "[ltool]") {
    REQUIRE(std::string(LTool::version_string()) == "1.0.0");
}

TEST_CASE("LLog::writef without location uses default location", "[LLog]") {
    std::vector<LLog::Record> records;

    LLog::set_level(LLog::Level::info);
    LLog::set_sink([&](const LLog::Record& item) {
        records.push_back(item);
    });

    LLog::writef(LLog::Level::info, "hello {}", 42);

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].severity == LLog::Level::info);
    REQUIRE(records[0].message == "hello 42");
    // 未显式传位置时仍会记录一个默认位置（当前机制产生的来源位置）。
    REQUIRE(records[0].location.file_name() != nullptr);

    LLog::reset_sink();
}

TEST_CASE("LLog::writef with explicit trailing location", "[LLog]") {
    std::vector<LLog::Record> records;

    LLog::set_level(LLog::Level::info);
    LLog::set_sink([&](const LLog::Record& item) {
        records.push_back(item);
    });

    LLog::writef(LLog::Level::info, "hello {}", 42, LTOOL_CURRENT_SOURCE_LOCATION);

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].message == "hello 42");
    // 显式位置应指向本测试文件。
    REQUIRE(records[0].location.file_name() != nullptr);
    REQUIRE(std::string(records[0].location.file_name()).find("LLog_test") != std::string::npos);

    LLog::reset_sink();
}

TEST_CASE("LLog::writef formats string format args instead of misreading them", "[LLog]") {
    std::vector<LLog::Record> records;

    LLog::set_level(LLog::Level::warn);
    LLog::set_sink([&](const LLog::Record& item) {
        records.push_back(item);
    });

    // 曾经会因参数顺序错误而把 "user {}" 误当位置；现在应正常输出。
    LLog::writef(LLog::Level::warn, "user {}", "bob");

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].severity == LLog::Level::warn);
    REQUIRE(records[0].message == "user bob");

    LLog::reset_sink();
    LLog::set_level(LLog::Level::info);
}

TEST_CASE("LLog::writef handles plain and empty format strings", "[LLog]") {
    std::vector<LLog::Record> records;

    LLog::set_level(LLog::Level::info);
    LLog::set_sink([&](const LLog::Record& item) {
        records.push_back(item);
    });

    LLog::writef(LLog::Level::info, "plain");
    LLog::writef(LLog::Level::info, "");

    REQUIRE(records.size() == 2);
    REQUIRE(records[0].message == "plain");
    REQUIRE(records[1].message == "");

    LLog::reset_sink();
}

TEST_CASE("LLog::writef formats multiple arguments", "[LLog]") {
    std::vector<LLog::Record> records;

    LLog::set_level(LLog::Level::info);
    LLog::set_sink([&](const LLog::Record& item) {
        records.push_back(item);
    });

    LLog::writef(LLog::Level::info, "{} {} {:.1f}", 1, "two", 3.0);

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].message == "1 two 3.0");

    LLog::reset_sink();
}

TEST_CASE("LLOG macros still work and carry the call-site location", "[LLog]") {
    std::vector<LLog::Record> records;

    LLog::set_level(LLog::Level::info);
    LLog::set_sink([&](const LLog::Record& item) {
        records.push_back(item);
    });

    LLOG_INFO("value {} {}", 1, 2);

    REQUIRE(records.size() == 1);
    REQUIRE(records[0].message == "value 1 2");
    REQUIRE(records[0].location.file_name() != nullptr);
    REQUIRE(std::string(records[0].location.file_name()).find("LLog_test") != std::string::npos);

    LLog::reset_sink();
}

// ---------------------------------------------------------------------------
// 宏路径的编译期格式串校验
//
// LLOG_* 宏展开为 ::LLog::detail::log_checked(...)，其格式串参数类型为
// fmt::format_string<Args...>，由 fmt 在编译期（consteval）对格式串与参数做校验。
// 因此格式串与参数不匹配时在编译期即报错，不再等到运行时抛 fmt::format_error。
//
// Catch2 无法直接断言"编译失败"，故把负向用例放在 #if 0 块中保留为文档说明；
// 以下调用在当前实现下会编译失败（clang++ 实测）：
//
//     LLOG_INFO("{} {}", 1);              // 占位符多于参数：argument not found
//     LLOG_INFO("{:d}", "not a number");  // 格式说明符与参数类型不匹配
//
// 注：fmt 允许"额外参数"（占位符少于参数，如 LLOG_INFO("{}", 1, 2)），这与
// fmt::format 的语义一致，不属于编译期错误；该路径编译通过的证据见上面
// LLOG_INFO("value {} {}", 1, 2) 用例以及 LLog_test 的构建。
// ---------------------------------------------------------------------------
#if 0
static_assert(false, "以下调用会在编译期报错（保留为编译失败验证文档，不参与构建）：");
LLOG_INFO("{} {}", 1);
LLOG_INFO("{:d}", "not a number");
#endif
