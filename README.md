# ltool

`ltool` 是一组 C++ 纯头文件小工具，目标是把日常项目里高频但琐碎的字符串、文件路径和加锁访问写得更顺手。仓库不需要单独编译库文件，直接把需要的头文件加入 include 路径即可使用。

## 特性

- 纯头文件：复制到项目中或通过 `-I` 指向本目录即可。
- 中文文本友好：`LString` 内部按 UTF-8 字节保存，边界 API 支持 GBK/GB2312、UTF-16、UTF-32、Latin1 等编码转换。
- 常用能力打包：字符串处理、文件读写、目录遍历、路径辅助、随机数、Base64、MD5、枚举名转换和线程安全包装。
- 尽量贴近标准库：类型和接口围绕 `std::string`、`std::filesystem::path`、`std::mutex` / `std::shared_mutex` 展开。
- 外部依赖随仓库提供并统一放在 `pkgs/`：`fmt/`、`magic_enum/`、`rfl/`、`rfl.hpp`、`BS_thread_pool.hpp`、`toml++` 和 `fkYAML`。

## 快速开始

```cpp
#include "LTool.hpp"

#include <iostream>
#include <vector>

int main() {
    LLOG_INFO("ltool {}", LTool::version_string());

    LString name = " ltool ";
    auto title = name.trimmed().upper_ascii();

    LFile out = "build/demo.txt";
    out.write_lines(std::vector<LString>{
        LString::format("name = {}", title),
        LString("abc").md5(),
        LString("abc").sha256(),
    });

    Locked<int> counter(0);
    counter.with_write([](int& value) {
        ++value;
    });

    std::cout << out.read_text() << "\n";
}
```

编译示例：

```bash
c++ -std=c++20 -I. demo.cpp -o demo -liconv
```

如果平台把 `iconv` 放在 C 标准库里，例如多数 Linux/glibc 环境，可以省略 `-liconv`：

```bash
c++ -std=c++20 -I. demo.cpp -o demo
```

如果只使用 `LString.hpp` 或 `LLog.hpp`，核心能力可在 C++11 下工作；`LPath.hpp` 和 `LFile.hpp` 需要 C++17 `std::filesystem`，`Locked.hpp` 使用了 C++20 concepts。

## CMake 集成

`ltool` 自己的 CMake 工程放在本目录，可以被外部项目直接作为子目录引入：

```cmake
add_subdirectory(path/to/ltool)
target_link_libraries(your_target PRIVATE LTool::ltool)
```

也可以单独配置和安装：

```bash
cmake -S path/to/ltool -B build/ltool -DLTOOL_USE_RFL_JSON=OFF
cmake --build build/ltool
cmake --install build/ltool
```

仓库根目录的 `CMakeLists.txt` 只是本地 playground/test 工程，不再承载 ltool 的包定义和安装规则。

## 头文件一览

### `LTool.hpp`

`LTool.hpp` 是常用组件总入口，会引入 `detail/LToolConfig.hpp`、`LString.hpp`、`LLog.hpp`、
`LJson.hpp`、`LEnv.hpp`、`LTimer.hpp`、`LRandom.hpp`、`LPath.hpp`、`LFile.hpp`、`Locked.hpp` 和
`LThreadPool.hpp`。其中 `LToml.hpp`、`LYaml.hpp` 和 `LConfig.hpp` 只在 C++20 起引入，
`LPath.hpp` 和 `LFile.hpp` 只在检测到 C++17 `std::filesystem` 时引入，
`LThreadPool.hpp` 只在 C++17 起引入，
`Locked.hpp` 只在检测到 C++20 concepts 时引入。

### `detail/LToolConfig.hpp`

`detail/LToolConfig.hpp` 提供 ltool 版本、C++ 标准、平台、编译器和常用特性检测宏，例如
`LTOOL_VERSION_STRING`、`LTOOL_HAS_CPP20`、`LTOOL_HAS_FILESYSTEM`、
`LTOOL_PLATFORM_WINDOWS`，并提供 `LTool::version_string()`。

### `detail/LFmt.hpp`

`detail/LFmt.hpp` 是 ltool 内部统一的 fmt 引入入口。`LString.hpp` 和 `LLog.hpp` 都通过
它处理内置 fmt、外部 fmt 和 header-only 配置，避免每个头文件重复维护 fmt 宏。

### `LLog.hpp`

`LLog` 是一个轻量级 header-only 日志工具，默认使用仓库内置 fmt 格式化文本。
它支持运行时日志等级、编译期日志宏裁剪、自定义 sink，以及可选的调用位置输出。
基础接口兼容 C++11；C++20 环境下会使用 `std::source_location` 获取更完整的调用位置。
默认 sink 会在终端中自动彩色输出，并尊重 `NO_COLOR`、`CLICOLOR=0` 和
`CLICOLOR_FORCE` 环境变量；也可以通过 `LLog::set_color_mode()` 手动控制。

示例：

```cpp
#include "LLog.hpp"

void log_example() {
    LLog::set_level(LLog::Level::debug);
    LLog::set_color_mode(LLog::ColorMode::automatic);
    LLOG_INFO("user {} logged in", 42);
    LLog::warn("disk space is low");
}
```

### `LJson.hpp`

`LJson` 是 JSON 能力的统一入口。它不把某一个 JSON
库变成 ltool 的硬依赖，而是按头文件检测开启适配器。

核心类型：

- `LJson::JsonView`：非拥有 JSON 文本视图，字符串输入路径零拷贝。
- `LJson::Json`：拥有 JSON 文本和内建 DOM 的统一中间表示，支持随机增删改查；
  检测到外部 JSON 库时可作为转换入口/出口。

当前适配器：

- nlohmann/json：检测 `<nlohmann/json.hpp>` 或 `<nlohmann.hpp>`，宏 `LJSON_HAS_NLOHMANN_JSON`。
- JsonCpp/cppjson：检测 `<json/json.h>`，宏 `LJSON_HAS_JSONCPP` / `LJSON_HAS_CPPJSON`。
- simdjson：C++17 起检测 `<simdjson.h>`，宏 `LJSON_HAS_SIMDJSON`。
- yyjson：优先使用 ltool 内置的 `pkgs/rfl/thirdparty/yyjson.h`；如果没有，则检测
  `<yyjson.h>`，宏 `LJSON_HAS_YYJSON`。
- rfl/json：默认开启；处于 C++20 且能找到 ltool 内置的 `"pkgs/rfl/json.hpp"` 或外部
  `<rfl/json.hpp>` 时，宏 `LJSON_HAS_STATIC_REFLECTION` 会变为 1。

示例：

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

### `LToml.hpp` / `LYaml.hpp`

`LToml` 和 `LYaml` 是 reflect-cpp 的 TOML/YAML 反射入口封装。它们提供和 `LJson` 类似的文本视图、
拥有文本对象以及 `from<T>()` / `to<T>()` / `read_or_throw<T>()` / `write<T>()` 辅助函数。TOML 基于 rfl
官方适配器和内置 single-header `toml++`；YAML 底层使用内置 header-only `fkYAML`，不需要额外链接
yaml-cpp。

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

### `LConfig.hpp`

`LConfig` 统一 JSON/TOML/YAML 配置读写。`load<T>()` 会按扩展名识别 `.json`、`.toml`、`.tml`、
`.yaml` 和 `.yml`，并默认从当前目录向父目录查找配置文件；`write<T>()` / `save<T>()` 可指定输出格式。
配置文件缺少字段时默认使用结构体里的默认值；需要严格要求文件写全时，可把 `missing_fields()` 设为
`MissingFields::strict`。环境变量覆盖基于 `LEnv` 的进程环境变量 API，通过 `Options::env()` 显式注册到具体字段，
避免嵌套配置被字段名规则隐式覆盖。单独读取某个环境变量到变量时，可使用 `load_from_env()`。

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

### `LString.hpp`

`LString` 是 `std::string` 的薄封装。它不试图把字符串变成复杂的 Unicode 容器，内部始终保存 `std::string` 字节；`size()`、`find()`、`substr()`、`split()` 等基础操作保持字节语义。编码转换只在 `from_encoding()` / `to_encoding()` 这类边界函数发生。

常用能力：

- 字符串基础操作：`trimmed()`、`lower_ascii()`、`upper_ascii()`、`split()`、`join()`、`lines()`、`replace_all()`。
- 格式化：内置支持 `fmt::format` 风格的 `LString::format()` 和 `append_format()`。
- 编码转换：`from_gbk()`、`to_gbk()`、`from_encoding()`、`to_encoding()`、`from_wstring()`、`to_wstring()`。
- 正则：`regex_contains()`、`regex_find()`、`regex_find_all()`、`regex_replace()`；检测到 RE2 时优先使用 RE2，否则使用 `std::regex`。
- 数字转换：C++17 起提供 `to_int()`、`to_i64()`、`to_double()`。
- 枚举辅助：检测到 `magic_enum` 时，可把枚举值转成名称，也可用 `to_enum<T>()` 从文本解析。
- 工具函数：Base64 编解码、MD5、SHA-256、路径名辅助、`std::hash<LString>`。

示例：

```cpp
#include "LString.hpp"

enum class Color { Red, Blue };

void string_example() {
    auto parts = LString(" a, b, c ").trimmed().split(',');
    auto joined = LString::join(parts, "/");        // "a/ b/ c"

    std::string gbk_bytes = /* 从外部读取的 GBK 字节 */ "";
    auto utf8 = LString::from_gbk(gbk_bytes);
    auto gbk = utf8.to_gbk();

    auto b64 = LString("hello").base64_encoded();   // "aGVsbG8="
    auto md5 = LString("abc").md5();                // "900150983cd24fb0d6963f7d28e17f72"
    auto sha = LString("abc").sha256();             // "ba7816bf..."

    auto color = LString("blue").to_enum<Color>(false);
}
```

### `LPath.hpp`

`LPath` 是 `std::filesystem::path` 的薄封装，专注路径、目录遍历和文件系统路径操作。查询类函数通常吞掉 `filesystem_error` 并返回保守结果；变更类函数失败时会抛出异常。

常用能力：

- 目录操作：`create_directories()`、`files()`、`recursive_files()`、`directories()`、`list()`。
- 路径辅助：`filename()`、`stem()`、`extension()`、`parent_path()`、`absolute()`、`canonical()`、`relative_to()`、`normalized()`。
- 文件系统操作：`copy_to()`、`copy_file_to()`、`move_to()`、`remove()`、`remove_all()`、`permissions()`。
- 临时路径：`temp_directory()`、`temp_path()`。
- 简单通配：`glob()` / `glob_path()` 支持文件名中的 `*` 和 `?`。

示例：

```cpp
#include "LPath.hpp"

#include <iostream>

void path_example() {
    LPath src = "src";
    for (const auto& cpp : src.recursive_files(".cpp")) {
        std::cout << cpp << "\n";
    }

    auto header = src / "ltool" / "LPath.hpp";
    std::cout << header.normalized() << "\n";
}
```

### `LFile.hpp`

`LFile` 保存一个 `LPath`，但职责集中在普通文件内容读写。路径拼接、目录枚举、glob、路径规范化等能力由 `LPath` 提供。

常用能力：

- 文件读写：`read_bytes()`、`read_text()`、`read_lines()`、`write_bytes()`、`write_text()`、`write_lines()`、`append_text()`。
- 内容查找替换：`contains()`、`find()`、`replace_all()`、`regex_contains()`、`regex_find_all()`、`regex_replace()`；原始字节可用 `bytes_find()`、`replace_bytes_all()`。
- 流和随机访问：`open_input()`、`open_output()`、`open_stream()`、`read_bytes_at()`、`read_bytes_from()`、`write_bytes_at()`、`seekg()`、`seekp()`、`tellg()`、`tellp()`。
- 文件操作：`touch()`、`copy_to()`、`move_to()`、`remove()`。
- 哈希：`path_hash()` 按路径求标准库哈希，`sha256()` 按文件内容返回 SHA-256 十六进制摘要。
- 临时文件：`create_temp_file()`。

示例：

```cpp
#include "LFile.hpp"

void file_example() {
    LFile file = "data/hello.txt";
    file.write_text("你好\n");

    LString text = file.read_text();                // 默认自动检测输入编码
    auto lines = file.read_lines();

    auto tmp = LFile::create_temp_file("ltool-", ".txt");
    tmp.write_text("temporary text");
    auto digest = tmp.sha256();
}
```

### `LRandom.hpp`

`LRandom` 是 `std::random` 的轻量封装，默认使用 `std::mt19937_64`。对象可以用固定种子构造，适合测试、模拟和可复现实验；也可以使用 `LRandom::shared()` 或 `LRandom::rand_*()` 静态快捷函数获取线程局部默认实例，少写样板代码。需要标准库分布器时，可以通过 `engine()` 直接访问底层引擎。

常用能力：

- 数值随机：`integer()`、`real()`、`normal()`、`exponential()`、`boolean()`、`chance()`、`roll()`。
- 容器随机：`choice()`、`weighted_choice()`、`shuffle()`、`shuffled()`、`sample()`。
- 字节和文本：`bytes()`、`fill_bytes()`、`string()`、`hex()`、`uuid_v4()`、`uuid_v7()`。
- 安全 UUID：`safe_uuid_v4()`、`safe_uuid_v7()` 使用操作系统安全随机源；`uuid_v4()`、`uuid_v7()` 使用当前 `LRandom` 引擎，适合可复现流程。
- 无上下文快捷函数：`rand_int()`、`rand_real()`、`rand_choice()`、`rand_shuffle()`、`rand_string()`、`rand_uuid_v4()`、`rand_uuid_v7()` 等；它们使用 `thread_local` 默认实例，多线程调用时不会共享同一个随机引擎。

示例：

```cpp
#include "LRandom.hpp"

#include <vector>

void random_example() {
    LRandom rng(42);

    auto dice = rng.roll();
    auto score = rng.real(0.0, 100.0);
    auto token = rng.string(12);

    std::vector<int> values {1, 2, 3, 4};
    rng.shuffle(values);
    auto one = rng.choice(values);

    auto id = LRandom::shared().uuid_v4();
    auto quick = LRandom::rand_int(1, 100);
    auto quick_one = LRandom::rand_choice(values);
    auto quick_id = LRandom::rand_uuid_v7();
    auto safe_id = LRandom::safe_uuid_v4();
    auto safe_time_id = LRandom::safe_uuid_v7();
}
```

### `Locked.hpp`

`Locked<T, Mutex>` 用一个互斥量保护一个对象，适合把“拿锁、访问数据、释放锁”的重复代码集中起来。默认互斥量是 `std::shared_mutex`：读访问使用共享锁，写访问使用独占锁；如果传入的互斥量不支持共享锁，读访问也会退化为独占锁。

常用能力：

- `wlock()`：返回可写 guard，可通过 `*guard` 或 `guard->` 访问数据。
- `rlock()`：返回只读 guard。
- `with_write(fn)`：加写锁后调用函数。
- `with_read(fn)`：加读锁后调用函数。
- `operator()(fn)`：非常短的读写访问语法；非常量对象走写锁，常量对象走读锁。
- `copy()`、`assign()`、`emplace()`：常见值语义操作。

示例：

```cpp
#include "Locked.hpp"

#include <string>
#include <vector>

void locked_example() {
    Locked<std::vector<std::string>> messages;

    messages.with_write([](auto& list) {
        list.push_back("hello");
    });

    auto size = messages.with_read([](const auto& list) {
        return list.size();
    });

    auto guard = messages.wlock();
    guard->push_back("world");
}
```

## 外部依赖与开关

仓库包含以下第三方头文件，默认直接从本仓库引用：

- `pkgs/fmt/`：`LString` 格式化能力依赖它，默认以 header-only 方式使用。
- `pkgs/magic_enum/`：C++17 起用于枚举名和枚举值互转。
- `pkgs/BS_thread_pool.hpp`：线程池头文件，当前 README 只列出依赖，具体接口请参考该头文件本身。
- `pkgs/rfl.hpp`、`pkgs/rfl/`：reflect-cpp 头文件，供 `LJson` 的 rfl/json 后端使用。
- `nlohmann/json`、JsonCpp、simdjson、yyjson、`rfl/json`：`LJson` 可选适配；
  检测到头文件和启用宏时才编译对应代码。

可用宏：

- `LSTRING_USE_EXTERNAL_FMT=1`：改为包含系统或包管理器提供的 `<fmt/...>`。
- `LTOOL_USE_EXTERNAL_FMT=1`：让 `LLog.hpp` 和 `LString.hpp` 一起使用外部 fmt。
- `LSTRING_USE_MAGIC_ENUM=0`：关闭 `magic_enum` 相关能力。
- `LTOOL_USE_RFL_JSON=1`：尝试启用 `LJson` 的 reflect-cpp `rfl/json` 后端。
- `LTOOL_USE_NLOHMANN_JSON=0`、`LTOOL_USE_JSONCPP=0`、`LTOOL_USE_SIMDJSON=0`、
  `LTOOL_USE_YYJSON=0`：关闭对应 `LJson` 适配器检测。
- `LTOOL_ACTIVE_LOG_LEVEL=LTOOL_LOG_LEVEL_WARN`：在预处理阶段裁剪更低等级的
  `LLOG_TRACE`、`LLOG_DEBUG`、`LLOG_INFO` 日志宏。
- `LLog::set_color_mode(LLog::ColorMode::never)`：运行时关闭默认 sink 的彩色输出。

GBK/GB2312 转换在 Windows 上使用系统代码页 API；在 Unix-like 平台上使用 `iconv`。如果平台没有可用 `iconv`，相关转换函数会抛出异常；如果链接时报 `iconv`、`iconv_open` 或 `iconv_close` 未定义，请在编译命令中追加 `-liconv`。

## 编码约定

`LString` 的设计重点是清晰的边界：

- 内部存储：始终是 `std::string` 字节，通常按 UTF-8 文本理解。
- 基础字符串操作：按字节或 ASCII 分隔符处理，不自动按 Unicode 码点切分。
- 文件文本读写：`LFile` 通过 `LString` 的编码 API 把外部字节转换为内部 UTF-8。
- 严格模式：大多数编码转换默认 `strict = true`，遇到非法字节会抛异常；设置为 `false` 时会尽量用替换字符继续。

## 目录结构

```text
.
├── LTool.hpp
├── detail/LToolConfig.hpp
├── detail/LFmt.hpp
├── LLog.hpp
├── LJson.hpp
├── LEnv.hpp
├── LTimer.hpp
├── LRandom.hpp
├── LString.hpp
├── LPath.hpp
├── LFile.hpp
├── Locked.hpp
└── pkgs/
    ├── BS_thread_pool.hpp
    ├── fmt/
    ├── magic_enum/
    ├── rfl.hpp
    └── rfl/
```

## 建议用法

- 小项目可以直接复制需要的 `.hpp` 和对应依赖目录。
- 大项目建议把本仓库作为子模块或 vendored 目录，并通过 `target_include_directories()` 暴露 include 路径。
- 如果项目已有自己的 `fmt` 版本，建议定义 `LSTRING_USE_EXTERNAL_FMT=1`，避免重复携带多份 fmt。
