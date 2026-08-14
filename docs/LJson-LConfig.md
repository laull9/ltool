# LJson 和 LConfig

`LJson` 是 JSON 能力的统一入口。`LJson::Json` 的可变 DOM 有意以 yyjson 作为高性能强制后端；其他 JSON 库按头文件检测提供互操作适配器，不替代 DOM 后端。

`LToml` 和 `LYaml` 是 reflect-cpp 的 TOML/YAML 反射入口封装。`LConfig` 在它们之上提供按扩展名识别的配置读写。

## LJson

核心类型：

- `LJson::JsonView`：非拥有 JSON 文本视图，字符串输入路径零拷贝；仅可在源 `Json` 存活且未修改期间使用。
- `LJson::Json`：拥有 JSON 文本和内建 DOM 的统一中间表示，支持随机增删改查；检测到外部 JSON 库时可作为转换入口/出口。

当前适配器：

- nlohmann/json：检测 `<nlohmann/json.hpp>` 或 `<nlohmann.hpp>`，宏 `LJSON_HAS_NLOHMANN_JSON`。
- JsonCpp/cppjson：检测 `<json/json.h>`，宏 `LJSON_HAS_JSONCPP` / `LJSON_HAS_CPPJSON`。
- simdjson：C++17 起检测 `<simdjson.h>`，宏 `LJSON_HAS_SIMDJSON`。
- yyjson：`Json` DOM 的强制后端。优先使用 ltool 内置的 `pkgs/rfl/thirdparty/yyjson.h`；如果没有，则必须提供 `<yyjson.h>`，否则 `LJson.hpp` 会给出编译错误。
- rfl/json：默认开启；处于 C++20 且能找到 ltool 内置的 `"pkgs/rfl/json.hpp"` 或外部 `<rfl/json.hpp>` 时，宏 `LJSON_HAS_STATIC_REFLECTION` 会变为 1。

```cpp
#include "LJson.hpp"

#include <string>

void json_example() {
    LJson::Json text = R"({"name":"Ada","age":37})";

    text["age"] = 38;
    text["skills"].push_back("math");
    text.set_pointer("/profile/city", "London");

    auto age = text.value("age", 0);
    auto city = text.value_pointer<std::string>("/profile/city", "");
    auto first_name = text.find_recursive("name");
    text.erase_pointer("/skills/0");

#if LJSON_HAS_NLOHMANN_JSON
    auto nlohmann_value = LJson::parse_nlohmann(text);
    LJson::Json from_nlohmann = nlohmann_value;
#endif

#if LJSON_HAS_JSONCPP
    auto jsoncpp_value = LJson::parse_cppjson(text);
    LJson::Json from_jsoncpp = jsoncpp_value;
#endif

#if LJSON_HAS_SIMDJSON
    auto simdjson_doc = LJson::parse_simdjson(text);
    LJson::Json from_simdjson = simdjson_doc;
#endif

#if LJSON_HAS_YYJSON
    auto yyjson_doc = LJson::parse_yyjson(text);
    LJson::Json from_yyjson = yyjson_doc;
#endif

#if LJSON_HAS_STATIC_REFLECTION
    struct User {
        std::string name;
        int age = 0;
    };

    auto rfl_text = LJson::Json::from(User{"Ada", 37});
    auto user = rfl_text.to<User>();

    auto same_text = LJson::from(User{"Grace", 41});
    auto same_user = LJson::to<User>(same_text);
#endif
}
```

## LToml / LYaml

`LToml` 和 `LYaml` 提供和 `LJson` 类似的文本视图、拥有文本对象以及 `from<T>()` / `to<T>()` / `read_or_throw<T>()` / `write<T>()` 辅助函数。TOML 基于 rfl 官方适配器和内置 single-header `toml++`；YAML 底层使用内置 header-only `fkYAML`，不需要额外链接 yaml-cpp。

```cpp
#include "LToml.hpp"
#include "LYaml.hpp"

struct ServerConfig {
    std::string host = "127.0.0.1";
    int port = 8080;
};

void config_format_example() {
    auto toml = LToml::Toml::from(ServerConfig{});
    auto from_toml = toml.to<ServerConfig>();
    LToml::TomlView toml_view = toml;

    auto yaml = LYaml::Yaml::from(ServerConfig{});
    auto from_yaml = LYaml::to<ServerConfig>(yaml);
    LYaml::YamlView yaml_view = yaml;
}
```

## LConfig

`LConfig` 统一 JSON/TOML/YAML 配置读写。`load<T>()` 会按扩展名识别 `.json`、`.toml`、`.tml`、`.yaml` 和 `.yml`，并默认从当前目录向父目录查找配置文件；`write<T>()` / `save<T>()` 可指定输出格式。

配置文件缺少字段时默认使用结构体里的默认值；需要严格要求文件写全时，可把 `missing_fields()` 设为 `MissingFields::strict`。环境变量覆盖基于 `LEnv` 的进程环境变量 API，通过 `Options::env()` 显式注册到具体字段，避免嵌套配置被字段名规则隐式覆盖。单独读取某个环境变量到变量时，可使用 `load_from_env()`。

```cpp
#include "LConfig.hpp"

struct Database {
    std::string host = "localhost";
    int port = 5432;
};

struct AppConfig {
    int port = 8080;
    Database database;
};

void load_config_example() {
    auto cfg = LConfig::load<AppConfig>(
        "config.toml",
        LConfig::Options{}
            .env("APP_PORT", &AppConfig::port)
            .env("APP_DATABASE_HOST", &AppConfig::database, &Database::host)
    );
}
```
