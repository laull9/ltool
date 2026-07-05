# 集成说明

## CMake

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

## 入口头文件

- `LTool.hpp`：常用组件总入口，会引入 `detail/LToolConfig.hpp`、`LString.hpp`、`LLog.hpp`、`LJson.hpp`、`LEnv.hpp`、`LTimer.hpp`、`LRandom.hpp`、`LDialog.hpp`、`LPath.hpp`、`LFile.hpp`、`Locked.hpp` 和 `LThreadPool.hpp`。
- `detail/LToolConfig.hpp`：提供 ltool 版本、C++ 标准、平台、编译器和常用特性检测宏，例如 `LTOOL_VERSION_STRING`、`LTOOL_HAS_CPP20`、`LTOOL_HAS_FILESYSTEM`、`LTOOL_PLATFORM_WINDOWS`，并提供 `LTool::version_string()`。
- `detail/LFmt.hpp`：ltool 内部统一的 fmt 引入入口，处理内置 fmt、外部 fmt 和 header-only 配置。

`LToml.hpp`、`LYaml.hpp` 和 `LConfig.hpp` 只在 C++20 起引入；`LPath.hpp` 和 `LFile.hpp` 只在检测到 C++17 `std::filesystem` 时引入；`LThreadPool.hpp` 只在 C++17 起引入；`Locked.hpp` 只在检测到 C++20 concepts 时引入。

## 外部依赖

仓库包含以下第三方头文件，默认直接从本仓库引用：

- `pkgs/fmt/`：`LString`、`LLog` 和 `LImGui::Text` 的格式化能力依赖它，默认以 header-only 方式使用。
- `pkgs/magic_enum/`：C++17 起用于枚举名和枚举值互转。
- `pkgs/BS_thread_pool.hpp`：线程池头文件。
- `pkgs/rfl.hpp`、`pkgs/rfl/`：reflect-cpp 头文件，供 `LJson` 的 rfl/json 后端使用。
- `pkgs/portable-file-dialogs/`：portable-file-dialogs 单头文件，供 `LDialog` 使用。
- `nlohmann/json`、JsonCpp、simdjson、yyjson、`rfl/json`：`LJson` 可选适配；检测到头文件和启用宏时才编译对应代码。
- `imgui`、`GLFW`、`SDL2`、`SFML`、`Vulkan`：`LImGui` 的可选图形依赖；只有检测到 Dear ImGui 和对应 backend 头文件时才启用相关路径。

## 可用宏

- `LSTRING_USE_EXTERNAL_FMT=1`：改为包含系统或包管理器提供的 `<fmt/...>`。
- `LTOOL_USE_EXTERNAL_FMT=1`：让 `LLog.hpp`、`LString.hpp` 和 `LImGui.hpp` 一起使用外部 fmt。
- `LSTRING_USE_MAGIC_ENUM=0`：关闭 `magic_enum` 相关能力。
- `LTOOL_USE_RFL_JSON=1`：尝试启用 `LJson` 的 reflect-cpp `rfl/json` 后端。
- `LTOOL_USE_NLOHMANN_JSON=0`、`LTOOL_USE_JSONCPP=0`、`LTOOL_USE_SIMDJSON=0`、`LTOOL_USE_YYJSON=0`：关闭对应 `LJson` 适配器检测。
- `LTOOL_USE_PORTABLE_FILE_DIALOGS=0`：关闭 `LDialog` / portable-file-dialogs 检测和 `LTool.hpp` 中的自动引入。
- `LTOOL_HAS_IMGUI`、`LTOOL_HAS_IMGUI_BACKEND_GLFW_OPENGL3`、`LTOOL_HAS_IMGUI_BACKEND_GLFW_VULKAN`：`detail/LToolConfig.hpp` 自动检测出的 `LImGui` 能力宏，通常不需要手动设置。
- `LTOOL_HAS_PORTABLE_FILE_DIALOGS`：`detail/LToolConfig.hpp` 自动检测出的 `LDialog` 能力宏。
- `LTOOL_ACTIVE_LOG_LEVEL=LTOOL_LOG_LEVEL_WARN`：在预处理阶段裁剪更低等级的 `LLOG_TRACE`、`LLOG_DEBUG`、`LLOG_INFO` 日志宏。
- `LLog::set_color_mode(LLog::ColorMode::never)`：运行时关闭默认 sink 的彩色输出。

## 目录结构

```text
.
├── LTool.hpp
├── detail/LToolConfig.hpp
├── detail/LFmt.hpp
├── LImGui.hpp
├── LDialog.hpp
├── detail/LImGui.hpp
├── detail/LDialog.hpp
├── detail/LHelloImGui.hpp
├── detail/LImGuiBundle.hpp
├── LLog.hpp
├── LJson.hpp
├── LEnv.hpp
├── LTimer.hpp
├── LRandom.hpp
├── LString.hpp
├── LPath.hpp
├── LFile.hpp
├── Locked.hpp
├── docs/
└── pkgs/
    ├── BS_thread_pool.hpp
    ├── fmt/
    ├── magic_enum/
    ├── portable-file-dialogs/
    ├── rfl.hpp
    └── rfl/
```
