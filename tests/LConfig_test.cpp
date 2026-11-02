#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "LConfig.hpp"

enum class LConfigTestMode {
    dev,
    prod
};

struct LConfigTestDb {
    std::string host = "localhost";
    int port = 5432;
};

struct LConfigTestApp {
    std::string name;
    int port = 0;
    bool enabled = false;
    LConfigTestMode mode = LConfigTestMode::dev;
    LConfigTestDb db;
};

struct LConfigOtherRoot {
    int value = 0;
};

namespace {

void write_text_file(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    out << text;
}

} // namespace

TEST_CASE("LConfig detects formats and applies registered environment overrides", "[LConfig]") {
    const std::string path = "lconfig_test.toml";
    write_text_file(path,
                    "name = 'file'\n"
                    "port = 1000\n"
                    "enabled = false\n"
                    "mode = 'dev'\n"
                    "\n"
                    "[db]\n"
                    "host = 'file-db'\n"
                    "port = 15432\n");

    LEnv::set_env("LCONFIGTEST_PORT", "9000");
    LEnv::set_env("LCONFIGTEST_ENABLED", "true");
    LEnv::set_env("LCONFIGTEST_MODE", "prod");
    LEnv::set_env("LCONFIGTEST_DB__HOST", "env-db");

    auto options = LConfig::Options{}
        .env("LCONFIGTEST_PORT", &LConfigTestApp::port)
        .env("LCONFIGTEST_ENABLED", &LConfigTestApp::enabled)
        .env("LCONFIGTEST_MODE", &LConfigTestApp::mode)
        .env("LCONFIGTEST_DB__HOST", LConfig::path(&LConfigTestApp::db, &LConfigTestDb::host));

    const auto parsed = LConfig::load<LConfigTestApp>(path, options);

    REQUIRE(parsed.name == "file");
    REQUIRE(parsed.port == 9000);
    REQUIRE(parsed.enabled);
    REQUIRE(parsed.mode == LConfigTestMode::prod);
    REQUIRE(parsed.db.host == "env-db");
    REQUIRE(parsed.db.port == 15432);

    LEnv::unset_env("LCONFIGTEST_PORT");
    LEnv::unset_env("LCONFIGTEST_ENABLED");
    LEnv::unset_env("LCONFIGTEST_MODE");
    LEnv::unset_env("LCONFIGTEST_DB__HOST");
    std::remove(path.c_str());
}

TEST_CASE("LConfig loads individual values from environment variables", "[LConfig]") {
    LConfigTestApp app;
    app.name = "file";
    app.port = 1000;
    app.enabled = false;
    app.mode = LConfigTestMode::dev;
    app.db.host = "file-db";
    app.db.port = 15432;

    LEnv::set_env("LCONFIGTEST_PORT", "9000");
    LEnv::set_env("LCONFIGTEST_ENABLED", "true");
    LEnv::set_env("LCONFIGTEST_MODE", "prod");
    LEnv::set_env("LCONFIGTEST_DB_HOST", "env-db");

    REQUIRE(LConfig::load_from_env("LCONFIGTEST_PORT", &app.port));
    REQUIRE(LConfig::load_from_env("LCONFIGTEST_ENABLED", app.enabled));
    REQUIRE(LConfig::load_from_env("LCONFIGTEST_MODE", &app.mode));
    REQUIRE(LConfig::load_from_env("LCONFIGTEST_DB_HOST", &app.db.host));
    REQUIRE_FALSE(LConfig::load_from_env("LCONFIGTEST_DB_PORT", &app.db.port));

    REQUIRE(app.name == "file");
    REQUIRE(app.port == 9000);
    REQUIRE(app.enabled);
    REQUIRE(app.mode == LConfigTestMode::prod);
    REQUIRE(app.db.host == "env-db");
    REQUIRE(app.db.port == 15432);

    LEnv::unset_env("LCONFIGTEST_PORT");
    LEnv::unset_env("LCONFIGTEST_ENABLED");
    LEnv::unset_env("LCONFIGTEST_MODE");
    LEnv::unset_env("LCONFIGTEST_DB_HOST");
}

TEST_CASE("LConfig uses struct defaults for missing config fields by default", "[LConfig]") {
    const std::string path = "lconfig_partial.toml";
    write_text_file(path,
                    "name = 'partial'\n"
                    "\n"
                    "[db]\n"
                    "host = 'partial-db'\n");

    const auto parsed = LConfig::load<LConfigTestApp>(path);

    REQUIRE(parsed.name == "partial");
    REQUIRE(parsed.port == 0);
    REQUIRE_FALSE(parsed.enabled);
    REQUIRE(parsed.mode == LConfigTestMode::dev);
    REQUIRE(parsed.db.host == "partial-db");
    REQUIRE(parsed.db.port == 5432);

    auto strict = LConfig::Options{}.missing_fields(LConfig::MissingFields::strict);
    REQUIRE_THROWS(LConfig::load<LConfigTestApp>(path, strict));

    std::remove(path.c_str());
}

TEST_CASE("LConfig searches parent directories by default", "[LConfig]") {
    namespace fs = std::filesystem;
    const fs::path root = "lconfig_parent_test";
    const fs::path child = root / "child" / "grandchild";
    const fs::path config = root / "app.toml";

    fs::create_directories(child);
    write_text_file(config.string(),
                    "name = 'parent'\n"
                    "port = 4321\n"
                    "enabled = true\n"
                    "mode = 'prod'\n"
                    "\n"
                    "[db]\n"
                    "host = 'parent-db'\n"
                    "port = 15432\n");

    LConfig::Options options;
    options.search_start_dir = child.string();

    const auto found = LConfig::find_config_file("app.toml", options);
    const auto parsed = LConfig::load<LConfigTestApp>("app.toml", options);

    REQUIRE(fs::equivalent(fs::path(found), config));
    REQUIRE(parsed.name == "parent");
    REQUIRE(parsed.port == 4321);
    REQUIRE(parsed.db.host == "parent-db");

    fs::remove_all(root);
}

TEST_CASE("LConfig writes requested formats", "[LConfig]") {
    LConfigTestApp app;
    app.name = "demo";
    app.port = 8080;
    app.enabled = true;
    app.mode = LConfigTestMode::prod;
    app.db.host = "db.local";

    REQUIRE(LConfig::write(app, LConfig::Format::json).find("\"demo\"") != std::string::npos);
    REQUIRE(LConfig::write(app, LConfig::Format::toml).find("[db]") != std::string::npos);
    REQUIRE(LConfig::write(app, LConfig::Format::yaml).find("db:") != std::string::npos);
}

TEST_CASE("LConfig bindings are type scoped and trim numeric values", "[LConfig]") {
    LConfigTestApp app;
    LConfigOtherRoot other;
    LEnv::set_env("LCONFIGTEST_TRIMMED", " 9000 \t");

    const auto options = LConfig::Options{}
        .env("LCONFIGTEST_TRIMMED", &LConfigTestApp::port);
    REQUIRE(LConfig::load_from_env("LCONFIGTEST_TRIMMED", app.port));
    REQUIRE(app.port == 9000);
    LConfig::detail::apply_env_bindings(other, options);
    REQUIRE(other.value == 0);
    LEnv::unset_env("LCONFIGTEST_TRIMMED");
}

TEST_CASE("LConfig saves config files atomically and loads them back", "[LConfig]") {
    const std::string path = "lconfig_save_test.toml";

    LConfigTestApp app;
    app.name = "saved";
    app.port = 7070;
    app.enabled = true;
    app.mode = LConfigTestMode::prod;
    app.db.host = "save-db";
    app.db.port = 17070;

    LConfig::save(path, app, LConfig::Format::toml);

    const auto loaded = LConfig::load<LConfigTestApp>(path);
    REQUIRE(loaded.name == "saved");
    REQUIRE(loaded.port == 7070);
    REQUIRE(loaded.enabled);
    REQUIRE(loaded.mode == LConfigTestMode::prod);
    REQUIRE(loaded.db.host == "save-db");
    REQUIRE(loaded.db.port == 17070);

    // save 使用原子写（临时文件 + rename），目录中不应残留 .tmp 临时文件
    namespace fs = std::filesystem;
    bool has_temp = false;
    std::error_code ec;
    for (auto it = fs::directory_iterator(fs::current_path(), ec),
                end = fs::directory_iterator();
         !ec && it != end; ++it) {
        const auto name = it->path().filename().string();
        if (name.rfind("lconfig_save_test.toml.", 0) == 0 &&
            name.size() >= 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) {
            has_temp = true;
            break;
        }
    }
    REQUIRE_FALSE(has_temp);

    std::remove(path.c_str());
}
