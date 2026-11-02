#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

#include "LYaml.hpp"

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

enum class LYamlTestMode {
    dev,
    prod
};

struct LYamlTestDb {
    std::string host = "localhost";
    int port = 5432;
};

struct LYamlTestApp {
    std::string name;
    int port = 0;
    bool enabled = false;
    LYamlTestMode mode = LYamlTestMode::dev;
    LYamlTestDb db;
};

TEST_CASE("LYaml serializes and deserializes reflected structs", "[LYaml][rfl]") {
    LYamlTestApp app;
    app.name = "demo";
    app.port = 8080;
    app.enabled = true;
    app.mode = LYamlTestMode::prod;
    app.db.host = "db.local";
    app.db.port = 15432;

    const auto text = LYaml::write(app);
    const auto parsed = LYaml::read_or_throw<LYamlTestApp>(text);

    REQUIRE(text.find("db:") != std::string::npos);
    REQUIRE(parsed.name == "demo");
    REQUIRE(parsed.port == 8080);
    REQUIRE(parsed.enabled);
    REQUIRE(parsed.mode == LYamlTestMode::prod);
    REQUIRE(parsed.db.host == "db.local");
    REQUIRE(parsed.db.port == 15432);
}

TEST_CASE("LYaml exposes Json-style text wrappers", "[LYaml][text]") {
    LYamlTestApp app;
    app.name = "wrapped";
    app.port = 7000;
    app.db.host = "db.local";

    const auto text = LYaml::Yaml::from(app);
    const LYaml::YamlView view = text;
    const auto parsed = text.to<LYamlTestApp>();
    const auto parsed_from_free_function = LYaml::to<LYamlTestApp>(view);

    REQUIRE(LYaml::active_backend() == LYaml::Backend::rfl_yaml);
    REQUIRE(LYaml::to_string(view) == text.str());
    REQUIRE(parsed.name == "wrapped");
    REQUIRE(parsed.port == 7000);
    REQUIRE(parsed_from_free_function.db.host == "db.local");
}

TEST_CASE("LYaml supports fmt, std::format and ostream like LJson", "[LYaml][text]") {
    LYamlTestApp app;
    app.name = "formatted";
    app.port = 9000;
    app.db.host = "db.local";

    const auto yaml = LYaml::Yaml::from(app);

    // fmt::format 直接输出文本。
    const auto via_fmt = fmt::format("{}", yaml);
    REQUIRE(via_fmt == yaml.to_string());

    // ostream 流输出。
    std::ostringstream stream;
    stream << yaml;
    REQUIRE(stream.str() == yaml.to_string());

    // dump() 与 to_string() 等价（YAML 格式固定）。
    REQUIRE(yaml.dump() == yaml.to_string());
    REQUIRE(yaml.dump(2) == yaml.to_string());

    // 文本比较：Yaml 之间、与 std::string、与 C 字符串。
    const LYaml::Yaml same(yaml.to_string());
    const LYaml::Yaml other("port: 1\n");
    REQUIRE(yaml == same);
    REQUIRE(yaml != other);
    REQUIRE(yaml == yaml.to_string());
    REQUIRE(yaml.to_string() == yaml);
    REQUIRE(yaml != "port: 1\n");
    REQUIRE("port: 1\n" != yaml);
    REQUIRE(yaml == yaml.to_string().c_str());
    REQUIRE(yaml.to_string().c_str() == yaml);

    // std::string_view 比较（含跨编译器回归：避免与模板构造歧义）。
    const auto yaml_text = yaml.to_string();
    const std::string_view yaml_view = yaml_text;
    REQUIRE(yaml == yaml_view);
    REQUIRE(yaml_view == yaml);
    REQUIRE(yaml != std::string_view("port: 1\n"));
    REQUIRE(std::string_view("port: 1\n") != yaml);

#if LYAML_HAS_STD_FORMAT
    const auto via_std = std::format("{}", yaml);
    REQUIRE(via_std == yaml.to_string());
#endif
}

TEST_CASE("LYaml from_file/to_file round-trips text and reflected structs", "[LYaml][file]") {
    LYamlTestApp app;
    app.name = "fileio";
    app.port = 7070;
    app.enabled = true;
    app.db.host = "db.file";
    app.db.port = 15432;

    TempFileGuard guard("yaml");
    const std::string& tmp = guard.path;

    // 文本持有类型往返。
    const auto yaml = LYaml::Yaml::from(app);
    LYaml::to_file(tmp, yaml);
    const auto loaded = LYaml::from_file(tmp);
    REQUIRE(loaded == yaml);

    // 反射结构体往返。
    LYaml::to_file(tmp, app);
    const auto loaded_struct = LYaml::from_file<LYamlTestApp>(tmp);
    REQUIRE(loaded_struct.name == "fileio");
    REQUIRE(loaded_struct.port == 7070);
    REQUIRE(loaded_struct.enabled);
    REQUIRE(loaded_struct.db.host == "db.file");
    REQUIRE(loaded_struct.db.port == 15432);

    // 原样文本写入：to_file 排除反射模板，走文本持有版本。
    LYaml::to_file(tmp, "host: raw\n");
    const auto raw = LYaml::from_file(tmp);
    REQUIRE(raw == "host: raw\n");
    LYaml::to_file(tmp, std::string("host: raw\n"));
    const auto raw_string = LYaml::from_file(tmp);
    REQUIRE(raw_string == "host: raw\n");
}
