#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

#include "LToml.hpp"

namespace {

/// 生成唯一的临时文件路径，并在析构时删除（RAII 清理）。
struct TempFileGuard {
    std::string path;

    TempFileGuard(const char* suffix) {
        static std::atomic<unsigned long> counter{0};
        const auto stamp = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        const auto name = "ltool_" + std::string(suffix) + "_" +
                          std::to_string(stamp) + "_" +
                          std::to_string(counter.fetch_add(1));
        path = (std::filesystem::temp_directory_path() / name).string();
    }

    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

} // namespace

enum class LTomlTestMode {
    dev,
    prod
};

struct LTomlTestDb {
    std::string host = "localhost";
    int port = 5432;
};

struct LTomlTestApp {
    std::string name;
    int port = 0;
    bool enabled = false;
    LTomlTestMode mode = LTomlTestMode::dev;
    LTomlTestDb db;
};

TEST_CASE("LToml serializes and deserializes reflected structs", "[LToml][rfl]") {
    LTomlTestApp app;
    app.name = "demo";
    app.port = 8080;
    app.enabled = true;
    app.mode = LTomlTestMode::prod;
    app.db.host = "db.local";
    app.db.port = 15432;

    const auto text = LToml::write(app);
    const auto parsed = LToml::read_or_throw<LTomlTestApp>(text);

    REQUIRE(text.find("[db]") != std::string::npos);
    REQUIRE(parsed.name == "demo");
    REQUIRE(parsed.port == 8080);
    REQUIRE(parsed.enabled);
    REQUIRE(parsed.mode == LTomlTestMode::prod);
    REQUIRE(parsed.db.host == "db.local");
    REQUIRE(parsed.db.port == 15432);
}

TEST_CASE("LToml exposes Json-style text wrappers", "[LToml][text]") {
    LTomlTestApp app;
    app.name = "wrapped";
    app.port = 7000;
    app.db.host = "db.local";

    const auto text = LToml::Toml::from(app);
    const LToml::TomlView view = text;
    const auto parsed = text.to<LTomlTestApp>();
    const auto parsed_from_free_function = LToml::to<LTomlTestApp>(view);

    REQUIRE(LToml::active_backend() == LToml::Backend::rfl_toml);
    REQUIRE(LToml::to_string(view) == text.str());
    REQUIRE(parsed.name == "wrapped");
    REQUIRE(parsed.port == 7000);
    REQUIRE(parsed_from_free_function.db.host == "db.local");
}

TEST_CASE("LToml supports fmt, std::format and ostream like LJson", "[LToml][text]") {
    LTomlTestApp app;
    app.name = "formatted";
    app.port = 9000;
    app.db.host = "db.local";

    const auto toml = LToml::Toml::from(app);

    // fmt::format 直接输出文本。
    const auto via_fmt = fmt::format("{}", toml);
    REQUIRE(via_fmt == toml.to_string());

    // ostream 流输出。
    std::ostringstream stream;
    stream << toml;
    REQUIRE(stream.str() == toml.to_string());

    // dump() 与 to_string() 等价（TOML 格式固定）。
    REQUIRE(toml.dump() == toml.to_string());
    REQUIRE(toml.dump(2) == toml.to_string());

    // 文本比较：Toml 之间、与 std::string、与 C 字符串。
    const LToml::Toml same(toml.to_string());
    const LToml::Toml other("port = 1");
    REQUIRE(toml == same);
    REQUIRE(toml != other);
    REQUIRE(toml == toml.to_string());
    REQUIRE(toml.to_string() == toml);
    REQUIRE(toml != "port = 1");
    REQUIRE("port = 1" != toml);
    REQUIRE(toml == toml.to_string().c_str());
    REQUIRE(toml.to_string().c_str() == toml);

    // std::string_view 比较（含跨编译器回归：避免与模板构造歧义）。
    const auto toml_text = toml.to_string();
    const std::string_view toml_view = toml_text;
    REQUIRE(toml == toml_view);
    REQUIRE(toml_view == toml);
    REQUIRE(toml != std::string_view("port = 1"));
    REQUIRE(std::string_view("port = 1") != toml);

#if LTOML_HAS_STD_FORMAT
    const auto via_std = std::format("{}", toml);
    REQUIRE(via_std == toml.to_string());
#endif
}

TEST_CASE("LToml from_file/to_file round-trips text and reflected structs", "[LToml][file]") {
    LTomlTestApp app;
    app.name = "fileio";
    app.port = 7070;
    app.enabled = true;
    app.db.host = "db.file";
    app.db.port = 15432;

    TempFileGuard guard("toml");
    const std::string& tmp = guard.path;

    // 文本持有类型往返。
    const auto toml = LToml::Toml::from(app);
    LToml::to_file(tmp, toml);
    const auto loaded = LToml::from_file(tmp);
    REQUIRE(loaded == toml);

    // 反射结构体往返。
    LToml::to_file(tmp, app);
    const auto loaded_struct = LToml::from_file<LTomlTestApp>(tmp);
    REQUIRE(loaded_struct.name == "fileio");
    REQUIRE(loaded_struct.port == 7070);
    REQUIRE(loaded_struct.enabled);
    REQUIRE(loaded_struct.db.host == "db.file");
    REQUIRE(loaded_struct.db.port == 15432);

    // 原样文本写入：to_file 排除反射模板，走文本持有版本。
    LToml::to_file(tmp, "host = 'raw'\n");
    const auto raw = LToml::from_file(tmp);
    REQUIRE(raw == "host = 'raw'\n");
    LToml::to_file(tmp, std::string("host = 'raw'\n"));
    const auto raw_string = LToml::from_file(tmp);
    REQUIRE(raw_string == "host = 'raw'\n");
}
