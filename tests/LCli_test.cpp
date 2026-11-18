#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "LCli.hpp"

TEST_CASE("LCli parses flags, values, bundled short flags, and positionals", "[LCli]") {
    LCli cli("demo", "测试工具");
    cli.version("1.2.3")
       .flag("verbose", 'v', "详细模式")
       .flag("all", 'a', "全部")
       .value_option("port", 'p', "PORT", "端口", "8080")
       .multi_option("tag", 't', "TAG", "标签")
       .positional("input", "输入文件", true);

    auto res = cli.parse(std::vector<std::string>{
        "-va", "-p9090", "-t", "alpha", "--tag=beta", "file.txt", "--", "--raw"
    });

    REQUIRE(res.ok());
    REQUIRE(res.flag("verbose"));
    REQUIRE(res.flag("all"));
    REQUIRE(res.get<int>("port") == 9090);
    REQUIRE(res.get_all("tag") == std::vector<std::string>{"alpha", "beta"});
    REQUIRE(res.positional(0) == "file.txt");
    REQUIRE(res.passthrough() == std::vector<std::string>{"--raw"});

    auto missing = cli.parse(std::vector<std::string>{});
    REQUIRE_FALSE(missing.ok());
    REQUIRE(missing.error().find("input") != std::string::npos);

    auto help = cli.parse(std::vector<std::string>{"--help"});
    REQUIRE(help.ok());
    REQUIRE(help.help_requested());
    REQUIRE(cli.help_text().find("--port <PORT>") != std::string::npos);

    auto ver = cli.parse(std::vector<std::string>{"-V"});
    REQUIRE(ver.ok());
    REQUIRE(ver.version_requested());
}

TEST_CASE("LCli supports nested subcommands and default values", "[LCli]") {
    LTool::Cli app("gitx", "示例 CLI");
    auto& remote = app.subcommand("remote", "管理远程仓库");
    auto& add = remote.subcommand("add", "添加远程");
    add.flag("force", 'f', "强制覆盖")
       .value_option("timeout", 'T', "SEC", "超时秒数", "30")
       .positional("name", "名称", true)
       .positional("url", "地址", true);

    auto res = app.parse(std::vector<std::string>{"remote", "add", "-f", "origin", "https://example.com"});
    REQUIRE(res.ok());
    REQUIRE(res.subcommand_name() == "remote");
    REQUIRE(res.subcommand_result() != nullptr);
    REQUIRE(res.subcommand_result()->subcommand_name() == "add");
    const auto* leaf = res.subcommand_result()->subcommand_result();
    REQUIRE(leaf != nullptr);
    REQUIRE(leaf->flag("force"));
    REQUIRE(leaf->get_or<int>("timeout", 0) == 30);
    REQUIRE(leaf->positional(0) == "origin");
    REQUIRE(leaf->positional(1) == "https://example.com");
}

TEST_CASE("subcommands validate parent options but allow help") {
    LCli app("tool");
    app.value_option("config", 'c', "PATH", "", "", true);
    app.subcommand("run");
    REQUIRE_FALSE(app.parse(std::vector<std::string>{"run"}).ok());
    REQUIRE(app.parse(std::vector<std::string>{"run", "--help"}).help_requested());
    REQUIRE(app.parse(std::vector<std::string>{"-c", "dev", "run"}).ok());
}
