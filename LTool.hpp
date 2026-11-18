/**
 * @file LTool.hpp
 * @brief ltool 纯头文件工具库总入口，按编译期特性自动聚合各组件。
 *
 * 本头文件是 ltool 的推荐入口。它根据 LToolConfig.hpp 检测到的编译环境和
 * 可用依赖，按条件包含下列组件；缺失组件对应的能力在源码中自动降级或报错。
 *
 * 组件清单（均位于 ltool/ 目录，可单独 include）：
 * - LString     ：std::string 薄封装，文本/编码/格式化/正则/Base64/MD5/SHA-256/HMAC。
 * - LLog        ：轻量级多级日志，支持颜色、来源位置、多 sink 与轮转文件 sink。
 * - LJson       ：JSON DOM 与反射序列化（yyjson + 可选 nlohmann/jsoncpp/simdjson/rfl）。
 * - LToml       ：TOML 反射序列化（可选 rfl::toml）。
 * - LYaml       ：YAML 反射序列化（可选 rfl::yaml）。
 * - LConfig     ：JSON/TOML/YAML 统一配置读取、写入与环境变量覆盖。
 * - LEnv        ：.env 文件解析与进程环境变量辅助。
 * - LTimer      ：chrono 计时器与作用域计时器。
 * - LDateTime   ：日历时间、ISO-8601 解析/格式化、本地/UTC 转换与时长文本工具。
 * - LScheduler  ：定时任务调度器（一次性/周期任务，可取消，需 C++20）。
 * - LRandom     ：随机数、抽样、洗牌、随机文本与 UUID。
 * - LTable      ：命令行 Unicode 表格。
 * - LPath       ：std::filesystem::path 薄封装、** 递归 glob 与 LTempDir。
 * - LFile       ：文件内容读写、字节/文本/行、正则替换与 LTempFile。
 * - LResult     ：轻量级返回值错误处理 LResult<T, E> 与作用域守卫 LScopeGuard。
 * - LCli        ：零依赖命令行参数解析器（长短选项/子命令/自动帮助）。
 * - LProcess    ：跨平台子进程执行（stdin/stdout/stderr/超时）与进程信息。
 * - LChannel    ：MPMC 阻塞通道 LChannel<T> 与线程安全 LRU/TTL 缓存 LLruCache<K, V>。
 * - Locked      ：互斥保护值包装（C++20 concept）。
 * - LThreadPool ：线程池（可选 BS_thread_pool）。
 * - LDialog     ：原生文件/目录/消息对话框（可选 portable-file-dialogs）。
 * - LProgress   ：终端等待动画/计数/进度条/状态消息（可选 barkeep）。
 * - LRange      ：range-v3 风格的 view 与聚合工具（可选 range-v3）。
 *
 * 单独使用：LWeb 基于 Cinatra，需要额外 include 路径，不在本聚合入口中，
 * 请直接 `#include "LWeb.hpp"`（详见 docs/LWeb.md）。
 *
 * 常用示例：
 * @code
 * #include "LTool.hpp"
 *
 * int main() {
 *     LLOG_INFO("hello {}", "world");                 // 日志
 *     auto s = LString("  abc  ").trimmed();          // 字符串
 *     auto n = LRandom::rand_int(1, 100);             // 随机数
 *     auto json = LJson::parse("{\"a\":1}");          // JSON DOM
 *     return 0;
 * }
 * @endcode
 */

#ifndef LTOOL_INCLUDE
#define LTOOL_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LFmt.hpp"
#include "LString.hpp"
#include "LLog.hpp"
#include "LJson.hpp"

#if LTOOL_HAS_RANGE_V3
#include "LRange.hpp"
#endif

#if LTOOL_HAS_CPP20
#include "LToml.hpp"
#include "LYaml.hpp"
#include "LConfig.hpp"
#endif

#include "LEnv.hpp"
#include "LTimer.hpp"

#if LTOOL_HAS_CPP17
#include "LResult.hpp"
#include "LDateTime.hpp"
#include "LCli.hpp"
#include "LProcess.hpp"
#include "LChannel.hpp"
#endif

#if LTOOL_HAS_CPP20
#include "LScheduler.hpp"
#endif

#include "LRandom.hpp"
#include "LTable.hpp"

#if LTOOL_HAS_PORTABLE_FILE_DIALOGS
#include "LDialog.hpp"
#endif

#if LTOOL_HAS_BARKEEP
#include "LProgress.hpp"
#endif

#if LTOOL_HAS_FILESYSTEM
#include "LPath.hpp"
#include "LFile.hpp"
#endif

#if LTOOL_HAS_CONCEPTS
#include "Locked.hpp"
#endif

#if LTOOL_HAS_THREAD_POOL
#include "LThreadPool.hpp"
#endif

// 注意：LWeb 不在聚合入口中。它需要额外的 include 路径
// （-I ltool/pkgs/cinatra），请直接 #include "LWeb.hpp" 单独使用。

#endif // LTOOL_INCLUDE
