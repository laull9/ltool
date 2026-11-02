# ltool — Agent 指南

ltool 是纯头文件 C++ 工具库（C++20 为主，部分模块兼容 C++11/17）。本文件只写读代码不易推出的约定与陷阱；
各模块用法以头文件顶部 `@file` 注释中的 `@code` 示例为准，`docs/*.md` 为补充。

## 硬性约定

- **纯头文件**：不新增 `.cpp`；所有非模板自由函数必须 `inline`。
- **只用相对 include**：使用方只需 `-I ltool`。ltool 内部引用一律 `"detail/..."`、`"pkgs/..."`、`"../pkgs/..."`，
  不要求使用方额外添加 include 路径（LWeb/Cinatra 已按此改写，见 `pkgs/README_LTOOL.md`）。
- **`pkgs/` 只读**：第三方代码只放这里，不在其中写 ltool 逻辑。确需改第三方代码时，必须同步记录到
  `pkgs/README_LTOOL.md`（升级时要重做）。
- **`detail/` 放实现细节**，公开入口是 ltool 根目录的 `LXxx.hpp`。
- **注释用中文**：每个模块头文件顶部有 `@file`/`@brief`、能力清单和 `@code` 示例；每个函数、类、宏、成员
  前有一行简短 `///` 注释。新增 API 时同步更新头部示例。
- **标准与特性判断只用 `detail/LToolConfig.hpp` 的宏**：`LTOOL_HAS_CPP17/20/23`、`LTOOL_HAS_FILESYSTEM`、
  `LTOOL_HAS_CONCEPTS` 等。不要直接写 `__cplusplus`（MSVC 未开 `/Zc:__cplusplus` 时恒为 199711L）。
  可选依赖用 `LTOOL_USE_X`（用户开关，默认 1）+ `LTOOL_HAS_X`（检测结果）两层宏。
- **fmt 统一从 `detail/LFmt.hpp` 引入**，不直接 include `pkgs/fmt/*`。
- **新增公开 API 必须同步导出到 `modules/ltool.cppm`**（`using` 声明，条件编译的 API 套用相同宏条件），
  并在 `tests/LModule_test.cpp` 中通过 `import ltool;` 覆盖；否则模块用户看不到它。
- **用户可见的宏放进只含宏的头文件**（如 `LLogMacros.hpp`），宏展开后引用的内部函数要在模块中导出；
  宏本身不能依赖 `LTOOL_HAS_*` 等配置宏，因为模块用户看不到它们。

## 命名与组织

| 形态 | 规则 | 例子 |
| --- | --- | --- |
| 值类型/对象 | 全局命名空间 `LXxx` 类，并在 `namespace LTool` 提供无前缀别名 | `LString`、`LPath`、`LFile`、`LTimer`、`LRandom`、`LEnv`、`LTable`、`LScheduler`；`LTool::Timer` |
| 函数集合 | `namespace LXxx { ... }` 自由函数 | `LLog::info`、`LJson::parse`、`LConfig::load<T>`、`LDialog::open_file` |
| 实现细节 | `LXxxDetail` 或 `LXxx::detail` | `LStringDetail`、`LLog::detail` |
| 方法名 | snake_case；返回新值的变换用过去分词，原地修改用动词 | `trimmed()` vs `trim()` |
| 例外 | **LWeb 使用 PascalCase**（`Get`/`Use`/`Group`/`CreateToken`），贴近 ASP.NET 风格，改 LWeb 时保持其风格 | |

## 模块地图

| 头文件 | 能力 | 最低标准 / 依赖 |
| --- | --- | --- |
| `LTool.hpp` | 聚合入口，按宏条件引入下列模块（不含 LWeb、LImGui） | — |
| `LString.hpp` | `std::string` 薄封装：trim/split/join、编码转换、正则、Base64/MD5/SHA-256、枚举名 | C++11；macOS/BSD 需 `-liconv` |
| `LLog.hpp` | `LLOG_INFO(...)` 等宏、级别、颜色、自定义 sink | C++11 |
| `LJson.hpp` | `LJson::Json` 值类型 DOM + `LJson::JsonRef` 可写引用（yyjson 后端，必选）；`LJson::read<T>/write` 反射（rfl） | 反射需 C++20 |
| `LToml.hpp` / `LYaml.hpp` | 反射读写 TOML/YAML（rfl + toml++/fkYAML） | C++20 |
| `LConfig.hpp` | JSON/TOML/YAML 统一 `load<T>/save`，`Options::env()` 环境变量覆盖 | C++20 |
| `LEnv.hpp` | `.env` 解析、进程环境变量 | C++11 |
| `LPath.hpp` / `LFile.hpp` | `std::filesystem::path` 封装；文本/字节/行读写、临时文件 | C++17 filesystem |
| `LTimer.hpp` | `LTimer`、`LScopeTimer` | C++11 |
| `LScheduler.hpp` | `after/at/every/repeat` 定时任务，可取消/暂停 | C++20（`std::jthread`） |
| `LRandom.hpp` | 随机数、抽样、洗牌、UUID v4/v7；`LRandom::rand_*` 线程局部快捷函数 | C++11 |
| `LTable.hpp` | 终端 Unicode 表格、Markdown 输出 | C++17 |
| `LThreadPool.hpp` | BS_thread_pool 封装 | C++17 |
| `Locked.hpp` | `Locked<T, Mutex>` 互斥保护值 | C++20 concepts |
| `LRange.hpp` | range-v3 风格 view 与聚合 | range-v3（`pkgs/range-v3/include`） |
| `LProgress.hpp` | 终端进度条/动画/计数（barkeep，已本地修改） | C++20 |
| `LDialog.hpp` | 原生文件/消息对话框（portable-file-dialogs） | — |
| `LWeb.hpp` | Cinatra 上的 Router/Middleware/JWT/HttpClient，需单独 include | C++20 协程；需 OpenSSL 时链接 |
| `LImGui.hpp` | HelloImGui/字体/主题 facade，需单独 include | 检测到 imgui 时启用 |
| `modules/ltool.cppm` | C++20 模块 `import ltool;`，导出与 `LTool.hpp` 相同的组件（不含 LWeb/LImGui） | C++20；见 `docs/Modules.md` |
| `LLogMacros.hpp` | 只含 `LLOG_*`、`LTOOL_LOG_LEVEL_*`、`LTOOL_CURRENT_SOURCE_LOCATION` 宏；模块用户在 import 后包含 | — |

## 必须知道的陷阱

- **`LJson::Json` 是值类型，`LJson::JsonRef` 是引用类型**：可写访问（非 const 的 `operator[]`、`at()`、
  `at_pointer()`、`pointer()`、`find_recursive()`、`emplace_back()`、`for_each_recursive()`）返回 `JsonRef`，
  所以 `auto x = root["x"]; x = 1;` 会写回 `root`；声明为 `LJson::Json y = root["y"];` 则得到独立副本。
  const 访问返回 `Json` 副本。`JsonRef` 之间赋值写入的是值，不会改变引用目标。不要长期保存 `JsonRef`：
  父节点被整体替换后它就不再反映文档内容。按类型判断 Json 时用 `LJson::detail::is_json_value<T>` 或
  `std::derived_from<T, LJson::Json>`，不要写 `std::is_same<T, LJson::Json>`，否则会漏掉 `JsonRef`；
  为 Json 特化 fmt/std::formatter、hash 等时也要同时特化 `JsonRef`。
- **`Json` 内存只增不减**：yyjson 可变文档按 arena 分配，反复覆写同一节点不会释放旧值。长生命周期对象
  若频繁修改，定期 `LJson::parse(json.dump())` 重建。
- **`Locked<T>` 不可拷贝、不可移动**；回调接口 `with_read/with_write` 按值返回结果，防止引用逃出锁。
  `emplace()` 对不可赋值的 T 走“析构 + 原地构造”，此时构造抛异常会 `std::terminate`。
- **日志位置**：只有 `LLOG_*` 宏能记录调用方位置；直接调用 `LLog::info()` 或 `writef()` 时，位置是函数默认参数处或 LLog 内部。
- **`LString` 是字节语义**：`size()`、`operator[]`、`substr()` 都按 UTF-8 字节计算，不按码点；编码只在
  `from_*`/`to_*` 边界处理。
- **`LScheduler` 默认内联执行回调**：回调在调度线程上执行，阻塞会推迟所有任务；
  耗时任务请传入线程池（`LScheduler(pool)`）。
- **LWeb 中间件不得抛异常**；请求级状态不要存在中间件成员里（多线程共享）。`SetDefaultJwt` 必须在
  `Start()/Run()` 之前调用。
- 公开头文件在 Windows 下会引入 `<windows.h>`（并定义 `NOMINMAX`/`WIN32_LEAN_AND_MEAN`），新增代码注意 `min/max` 宏冲突。

## 构建与测试

ltool 本身无需编译。根目录的 `xmake.lua` 是只用于测试的独立工程，在本目录执行：

```bash
xmake f -P . -y                     # 首次配置（-P . 见下方说明）
xmake build -P .                    # 编译全部测试（tests/*.cpp 每个文件一个目标）
xmake test -P .                     # 运行全部测试
xmake run -P . LJson_test           # 运行单个测试（Catch2，可附加过滤参数）
```

- **始终带 `-P .`**：ltool 常作为子目录嵌在使用方的 xmake 工程里，xmake 会向上找到已配置的外层工程并
  构建它（同时改动它的配置）。单独克隆 ltool 时可省略。
- macOS 未指定 `--toolchain` 时使用 PATH 中的 LLVM clang：Xcode 的 Apple clang 无法完成模块依赖扫描，
  其 libc++ 也与自带 range-v3 冲突。编译器不支持模块扫描时用 `xmake f -P . --modules=n` 跳过 `LModule_test`。
- 新增测试：在 `tests/` 添加 `<模块>_test.cpp` 即自动生成目标；需要额外包时在根目录 `xmake.lua` 中
  `add_requires`，并在 `tests/xmake.lua` 中按目标名追加 `add_packages`。
- 修改某模块后至少运行对应 `<模块>_test`；改动 `detail/LToolConfig.hpp`、`detail/LFmt.hpp`、`LString.hpp`
  等基础头文件时运行全部测试。
- 修改头部 `@code` 示例或 `README.md` 示例时，确保示例能实际编译（曾出现文档引用不存在 API 的问题）。
- 涉及线程的代码建议额外用 `-fsanitize=thread` 编译对应测试验证。
- 独立集成用 CMake：`add_subdirectory(ltool)` + `target_link_libraries(x PRIVATE ltool::ltool)`；
  模块用法开启 `LTOOL_BUILD_MODULE` 并链接 `ltool::module`。
- 修改 `modules/ltool.cppm`、`LLogMacros.hpp` 或任何公开 API 后运行 `LModule_test`。

## 改动检查清单

1. 新 API 有一行中文注释，模块头部 `@code` 示例与 `docs/` 已同步，并已在 `modules/ltool.cppm` 导出。
2. 只用 `LTOOL_HAS_*` 判断特性；可选依赖缺失时能降级或给出清晰 `#error`/`static_assert`。
3. 非模板函数 `inline`，无新增 `.cpp`，include 均为相对路径。
4. 对应测试通过；新增行为有测试覆盖。
5. 若改了 `pkgs/`，已更新 `pkgs/README_LTOOL.md`。
