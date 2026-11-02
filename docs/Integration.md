# 集成说明

## CMake

`ltool` 自己的 CMake 工程放在本目录，可以被外部项目直接作为子目录引入：

```cmake
add_subdirectory(path/to/ltool)
target_link_libraries(your_target PRIVATE ltool::ltool)
```

也可以单独配置和安装：

```bash
cmake -S path/to/ltool -B build/ltool -DLTOOL_USE_RFL_JSON=OFF
cmake --build build/ltool
cmake --install build/ltool
```

开启 `-DLTOOL_BUILD_MODULE=ON`（CMake 3.28+，Ninja/Visual Studio 生成器）会额外提供 C++20 模块目标 `ltool::module`，使用方即可 `import ltool;`，见 [Modules](Modules.md)。

根目录的 `xmake.lua` 只用于构建和运行 `tests/` 下的测试，使用方无需引入；在 ltool 目录执行 `xmake test -P .` 即可（`-P .` 避免被外层 xmake 工程接管，详见 [AGENTS.md](../AGENTS.md)）。

## 入口头文件

- `LTool.hpp`：常用组件总入口。始终引入 `LString.hpp`、`LLog.hpp`、`LJson.hpp`、`LEnv.hpp`、`LTimer.hpp`、`LRandom.hpp`、`LTable.hpp`；其余组件按条件引入（见下表）。
- `detail/LToolConfig.hpp`：提供 ltool 版本、C++ 标准、平台、编译器和常用特性检测宏，例如 `LTOOL_VERSION_STRING`、`LTOOL_HAS_CPP20`、`LTOOL_HAS_FILESYSTEM`、`LTOOL_PLATFORM_WINDOWS`，并提供 `LTool::version_string()`。
- `detail/LFmt.hpp`：ltool 内部统一的 fmt 引入入口，处理内置 fmt、外部 fmt 和 header-only 配置。

| 组件 | `LTool.hpp` 引入条件 |
| --- | --- |
| `LToml.hpp`、`LYaml.hpp`、`LConfig.hpp`、`LScheduler.hpp` | `LTOOL_HAS_CPP20` |
| `LPath.hpp`、`LFile.hpp` | `LTOOL_HAS_FILESYSTEM`（C++17 `std::filesystem`） |
| `Locked.hpp` | `LTOOL_HAS_CONCEPTS`（C++20 concepts） |
| `LThreadPool.hpp` | `LTOOL_HAS_THREAD_POOL`（C++17） |
| `LRange.hpp` | `LTOOL_HAS_RANGE_V3` |
| `LDialog.hpp` | `LTOOL_HAS_PORTABLE_FILE_DIALOGS` |
| `LProgress.hpp` | `LTOOL_HAS_BARKEEP` |
| `LWeb.hpp`、`LImGui.hpp` | 不在聚合入口中，需单独 `#include` |

## 外部依赖

仓库包含以下第三方头文件，默认直接从本仓库引用：

- `pkgs/fmt/`：`LString`、`LLog` 和 `LImGui::Text` 的格式化能力依赖它，默认以 header-only 方式使用。
- `pkgs/magic_enum/`：C++17 起用于枚举名和枚举值互转。
- `pkgs/BS_thread_pool.hpp`：线程池头文件。
- `pkgs/rfl.hpp`、`pkgs/rfl/`：reflect-cpp 头文件，供 `LJson` 的 rfl/json 后端使用。
- `pkgs/portable-file-dialogs/`：portable-file-dialogs 单头文件，供 `LDialog` 使用。
- `nlohmann/json`、JsonCpp、simdjson、`rfl/json`：`LJson` 的可选互操作适配；检测到头文件和启用宏时才编译对应代码。yyjson 是 `LJson::Json` 的强制 DOM 后端，随仓库提供。
- `imgui`、`GLFW`、`SDL2`、`SFML`、`Vulkan`：`LImGui` 的可选图形依赖；只有检测到 Dear ImGui 和对应 backend 头文件时才启用相关路径。

## 可用宏

- `LSTRING_USE_EXTERNAL_FMT=1`：改为包含系统或包管理器提供的 `<fmt/...>`。
- `LTOOL_USE_EXTERNAL_FMT=1`：让 `LLog.hpp`、`LString.hpp` 和 `LImGui.hpp` 一起使用外部 fmt。
- `LSTRING_USE_MAGIC_ENUM=0`：关闭 `magic_enum` 相关能力。
- `LTOOL_USE_RFL_JSON=1`：尝试启用 `LJson` 的 reflect-cpp `rfl/json` 后端。
- `LTOOL_USE_NLOHMANN_JSON=0`、`LTOOL_USE_JSONCPP=0`、`LTOOL_USE_SIMDJSON=0`：关闭对应 `LJson` 互操作适配器检测。不要设置 `LTOOL_USE_YYJSON=0`，因为 `LJson::Json` 依赖 yyjson DOM。
- `LTOOL_USE_PORTABLE_FILE_DIALOGS=0`：关闭 `LDialog` / portable-file-dialogs 检测和 `LTool.hpp` 中的自动引入。
- `LTOOL_HAS_IMGUI`、`LTOOL_HAS_IMGUI_BACKEND_GLFW_OPENGL3`、`LTOOL_HAS_IMGUI_BACKEND_GLFW_VULKAN`：`detail/LToolConfig.hpp` 自动检测出的 `LImGui` 能力宏，通常不需要手动设置。
- `LTOOL_HAS_PORTABLE_FILE_DIALOGS`：`detail/LToolConfig.hpp` 自动检测出的 `LDialog` 能力宏。
- `LTOOL_ACTIVE_LOG_LEVEL=LTOOL_LOG_LEVEL_WARN`：在预处理阶段裁剪更低等级的 `LLOG_TRACE`、`LLOG_DEBUG`、`LLOG_INFO` 日志宏。
- `LLog::set_color_mode(LLog::ColorMode::never)`：运行时关闭默认 sink 的彩色输出。

## 目录结构

```text
.
├── LTool.hpp                聚合入口
├── LString.hpp  LLog.hpp  LJson.hpp  LToml.hpp  LYaml.hpp  LConfig.hpp  LEnv.hpp
├── LPath.hpp  LFile.hpp  LTimer.hpp  LScheduler.hpp  LRandom.hpp  LTable.hpp
├── LThreadPool.hpp  Locked.hpp  LRange.hpp  LProgress.hpp  LDialog.hpp
├── LWeb.hpp  LImGui.hpp     需单独引入
├── LLogMacros.hpp           只含日志宏，供 import ltool 后包含
├── modules/ltool.cppm       C++20 模块接口单元（import ltool;）
├── detail/                  实现细节（LToolConfig/LFmt/LBase64/LMd5/LSha256/LImGui/LDialog…）
│   └── LWeb/                LWeb 按职责拆分的实现
├── docs/                    模块文档
├── tests/                   Catch2 测试（每个 *_test.cpp 一个目标）
├── xmake.lua                测试工程（xmake test -P .）
└── pkgs/                    第三方依赖（只读，本地修改见 pkgs/README_LTOOL.md）
    ├── fmt/  magic_enum/  rfl/  rfl.hpp  range-v3/  cinatra/
    ├── portable-file-dialogs/  imgui_bundle/  hello_imgui/
    ├── BS_thread_pool.hpp  barkeep.h  tabulate.hpp
    └── rfl/thirdparty/      yyjson、toml++（toml.hpp）、fkYAML、ctre、enchantum
```
