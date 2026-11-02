# C++20 模块

ltool 提供模块接口单元 `modules/ltool.cppm`，可以用 `import ltool;` 代替 `#include "LTool.hpp"`。
头文件用法不受影响，两种方式可以在同一项目的不同翻译单元中并存。

```cpp
#include <iostream>        // 标准库头文件放在 import 之前

import ltool;
#include "LLogMacros.hpp"  // 需要 LLOG_* 宏时在 import 之后包含

int main() {
    auto json = LJson::parse(R"({"name":" ltool "})");
    json["name"] = LString(json["name"].get_as<std::string>()).trimmed().str();
    LLOG_INFO("config = {}", json);
    std::cout << fmt::format("{}", LTool::version_string()) << "\n";
}
```

## 导出内容

- 与 `LTool.hpp` 聚合的组件一致，并按相同的编译期特性条件导出：`LString`、`LLog`、`LJson`、`LToml`、`LYaml`、
  `LConfig`、`LEnv`、`LTimer`、`LScheduler`、`LRandom`、`LTable`、`LPath`、`LFile`、`Locked`、`LThreadPool`、
  `LDialog`、`LProgress`、`LRange`，以及 `LTool` 命名空间中的版本函数、traits、concepts 和无前缀别名。
- 字面量运算符 `_ls`、`_lp`、`_lf`。
- ltool 自带 fmt 的常用入口：`fmt::format`、`format_to`、`print`、`println`、`join`、`runtime`、`to_string`、
  `formatter`、`format_string` 等，模块用户无需再包含 fmt 头文件。
- `fmt::formatter`、`std::hash` 等模板特化与隐藏友元运算符随类型自动可用。
- **不包含** `LWeb` 和 `LImGui`（与 `LTool.hpp` 一致），需要时仍用头文件方式引入。

## 宏

模块无法导出宏。`LLOG_TRACE`…`LLOG_FATAL`、`LTOOL_CURRENT_SOURCE_LOCATION` 和 `LTOOL_LOG_LEVEL_*`
位于只含宏的 `LLogMacros.hpp`，在 `import ltool;` 之后包含即可；`LTOOL_ACTIVE_LOG_LEVEL` 的编译期裁剪照常生效。
`LTOOL_HAS_*` 等配置宏不对模块用户提供；需要按特性分支时请使用头文件方式。

## 构建

模块接口单元通过相对路径包含 `../LTool.hpp`，只需要能编译 ltool 头文件的那些 include 路径
（使用 `LRange` 时另需 `pkgs/range-v3/include`）。编译 `ltool.cppm` 时的宏开关（如 `LTOOL_USE_EXTERNAL_FMT`）
决定模块导出的内容，使用方的翻译单元应使用相同的开关。

### xmake

```lua
target("app")
    set_languages("c++20")
    add_files("src/*.cpp", "ltool/modules/ltool.cppm")
    add_includedirs("ltool")
    set_policy("build.c++.modules", true)
```

### CMake（3.28+，Ninja 或 Visual Studio 生成器）

```cmake
set(LTOOL_BUILD_MODULE ON)
add_subdirectory(path/to/ltool)
target_link_libraries(app PRIVATE ltool::module)
```

`ltool::module` 是包含 `ltool.cppm` 的静态库，依赖头文件目标 `ltool::ltool`；安装时模块接口单元与
`CXX_MODULES_DIRECTORY` 元数据一并安装。

### 手动编译（clang）

```bash
clang++ -std=c++20 -I ltool --precompile ltool/modules/ltool.cppm -o ltool.pcm
clang++ -std=c++20 -c ltool.pcm -o ltool.o
clang++ -std=c++20 -I ltool -fmodule-file=ltool=ltool.pcm main.cpp ltool.o -o app
```

## 维护约定

- 新增公开 API 时，必须在 `modules/ltool.cppm` 中同步添加 `using` 导出，否则模块用户看不到它。
- 条件编译的 API 在模块中套用与头文件相同的 `LTOOL_HAS_*` / `LJSON_HAS_*` 条件。
- `tests/LModule_test.cpp` 通过 `import ltool;` 覆盖各组件的主要入口；新增组件时补充对应用例
  （运行：`xmake test -P . LModule_test/default`）。
