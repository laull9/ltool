# LLog

`LLog` 是一个轻量级 header-only 日志工具，默认使用仓库内置 fmt 格式化文本。它支持运行时日志等级、编译期日志宏裁剪、自定义 sink，以及可选的调用位置输出。

基础接口兼容 C++11；C++20 环境下会使用 `std::source_location` 获取更完整的调用位置。默认 sink 会在终端中自动彩色输出，并尊重 `NO_COLOR`、`CLICOLOR=0` 和 `CLICOLOR_FORCE` 环境变量；也可以通过 `LLog::set_color_mode()` 手动控制。

## 常用能力

- `LLOG_TRACE`、`LLOG_DEBUG`、`LLOG_INFO`、`LLOG_WARN`、`LLOG_ERROR`、`LLOG_FATAL`：宏接口，支持编译期裁剪。宏与等级常量定义在只含宏的 `LLogMacros.hpp` 中（`LLog.hpp` 自动包含）；使用 `import ltool;` 时在 import 之后包含它。
- `LLog::set_level()`：设置运行时最低输出等级。
- `LLog::set_color_mode()`：控制默认终端 sink 的彩色输出。
- `LLog::writef()` / `LLog::info()` / `LLog::warn()`：函数式日志入口。
- `LTOOL_ACTIVE_LOG_LEVEL=LTOOL_LOG_LEVEL_WARN`：在预处理阶段裁剪更低等级日志宏。

## 示例

```cpp
#include "LLog.hpp"

void log_example() {
    LLog::set_level(LLog::Level::debug);
    LLog::set_color_mode(LLog::ColorMode::automatic);
    LLOG_INFO("user {} logged in", 42);
    LLog::warn("disk space is low");
}
```
