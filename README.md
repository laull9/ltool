# ltool

`ltool` 是一组 C++ 纯头文件小工具，目标是把日常项目里高频但琐碎的字符串、文件路径、配置、日志、随机数、加锁访问和可选 ImGui 启动流程写得更顺手。仓库不需要单独编译库文件，直接把需要的头文件加入 include 路径即可使用。

## 特性概览

- 纯头文件：复制到项目中或通过 `-I` 指向本目录即可。
- 中文文本友好：`LString` 内部按 UTF-8 字节保存，边界 API 支持 GBK/GB2312、UTF-16、UTF-32、Latin1 等编码转换。
- 常用能力打包：字符串处理、文件读写、目录遍历、路径辅助、配置读写、日志、随机数、Base64、MD5、SHA-256、枚举名转换和线程安全包装。
- JSON DOM：`LJson::Json` 以随库提供的 yyjson 作为高性能强制后端；nlohmann/json、JsonCpp、simdjson 和 rfl 仅提供互操作接口。
- 原生对话框：基于内置 portable-file-dialogs 的 `LDialog`，可打开文件、目录、消息框和桌面通知。
- 终端进度显示：基于内置 barkeep 的 `LProgress`，提供等待动画、计数、进度条、状态消息与组合显示；进度条按终端宽度自适应（可设上限），默认显示已运行时间、速度与预估剩余时间。
- Web 应用层：基于内置 Cinatra 的 `LWeb`，提供 Router/Controller/Response/JSON/Middleware 薄封装与 `Native()` 逃生通道（需 C++20，单独引入）。
- 可选 ImGui facade：检测到 Dear ImGui 后，可通过 `LImGui.hpp` 快速接入 HelloImGui、字体、主题和常用小组件。
- 尽量贴近标准库：类型和接口围绕 `std::string`、`std::filesystem::path`、`std::mutex` / `std::shared_mutex` 展开。
- 外部依赖随仓库提供并统一放在 `pkgs/`：`fmt/`、`magic_enum/`、`rfl/`、`rfl.hpp`、`BS_thread_pool.hpp`、`portable-file-dialogs`、`barkeep.h`、`toml++` 和 `fkYAML`。

## 项目约定

- 项目使用 C++20 标准，部分模块可在 C++11 或 C++17 下工作。
- pkgs/ 存放第三方依赖，ltool 内部使用 `detail/` 目录存放实现细节。
- 纯Headonly，不引入cpp源文件
- 每个模块头文件前有模块说明和使用示例
- 每个函数、类、宏前有一行简短注释 

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

```bash
c++ -std=c++20 -I. demo.cpp -o demo -liconv
```

多数 Linux/glibc 环境可以省略 `-liconv`。如果只使用 `LString.hpp` 或 `LLog.hpp`，核心能力可在 C++11 下工作；`LPath.hpp` 和 `LFile.hpp` 需要 C++17 `std::filesystem`，`Locked.hpp` 使用了 C++20 concepts。

## 文档索引

| 模块 | 文档 | 入口头文件 |
| --- | --- | --- |
| 字符串、编码、哈希、格式化 | [LString](docs/LString.md) | `LString.hpp` |
| 日志 | [LLog](docs/LLog.md) | `LLog.hpp` |
| JSON/TOML/YAML 与配置 | [LJson 和 LConfig](docs/LJson-LConfig.md) | `LJson.hpp`、`LToml.hpp`、`LYaml.hpp`、`LConfig.hpp` |
| 路径 | [LPath](docs/LPath.md) | `LPath.hpp` |
| 文件读写 | [LFile](docs/LFile.md) | `LFile.hpp` |
| 随机数和 UUID | [LRandom](docs/LRandom.md) | `LRandom.hpp` |
| 原生对话框 | [LDialog](docs/LDialog.md) | `LDialog.hpp` |
| 加锁访问 | [Locked](docs/Locked.md) | `Locked.hpp` |
| Web 应用层 | [LWeb](docs/LWeb.md) | `LWeb.hpp` |
| ImGui facade | [LImGui](docs/LImGui.md) | `LImGui.hpp` |
| 集成、依赖和开关 | [集成说明](docs/Integration.md) | `LTool.hpp`、`detail/LToolConfig.hpp`、`detail/LFmt.hpp` |

## 头文件入口

- `LTool.hpp`：常用组件总入口，会按 C++ 标准和头文件检测引入可用模块。
- `detail/LToolConfig.hpp`：版本、平台、编译器、C++ 标准和特性检测宏。
- `detail/LFmt.hpp`：ltool 内部统一的 fmt 引入入口。
- 单模块头文件：需要更小 include 面时，可直接包含 `LString.hpp`、`LFile.hpp`、`LImGui.hpp` 等模块入口。

## CMake 集成

```cmake
add_subdirectory(path/to/ltool)
target_link_libraries(your_target PRIVATE LTool::ltool)
```

更多编译、依赖、宏开关和目录结构说明见 [docs/Integration.md](docs/Integration.md)。

## 建议用法

- 小项目可以直接复制需要的 `.hpp` 和对应依赖目录。
- 大项目建议把本仓库作为子模块或 vendored 目录，并通过 `target_include_directories()` 暴露 include 路径。
- 如果项目已有自己的 `fmt` 版本，建议定义 `LTOOL_USE_EXTERNAL_FMT=1`，避免重复携带多份 fmt。
