/**
 * @file modules/ltool.cppm
 * @brief ltool 的 C++20 模块接口单元：`import ltool;`。
 *
 * 导出内容与 LTool.hpp 聚合的组件一致（同样按编译期特性条件导出），
 * 不含 LWeb 与 LImGui。实现方式：在全局模块片段中以普通头文件方式引入
 * LTool.hpp，再用 using 声明逐个导出公开名字；ltool 头文件本身无需修改，
 * 头文件用法与模块用法可以在不同翻译单元中并存。
 *
 * 模块无法导出宏：需要 LLOG_* 等日志宏时，在 import 之后包含 LLogMacros.hpp。
 *
 * 常用示例：
 * @code
 * import ltool;
 * #include "LLogMacros.hpp"
 *
 * int main() {
 *     LString name = " ltool ";
 *     auto json = LJson::parse(R"({"n":1})");
 *     json["n"] = 2;
 *     LLOG_INFO("{} {}", name.trimmed(), json);
 * }
 * @endcode
 *
 * 新增公开 API 时必须同步在此导出，否则模块用户看不到它（tests/LModule_test.cpp 覆盖主要入口）。
 */

module;

// 全局模块片段：这里的声明不属于 ltool 模块，但对导入方可达
// （模板特化、隐藏友元等随之可用）。
#include "../LTool.hpp"

export module ltool;

// ---------------------------------------------------------------------------
// LTool：版本、特性 traits/concepts 与无前缀别名
// ---------------------------------------------------------------------------

export namespace LTool {
using LTool::Version;
using LTool::current_version;
using LTool::version_string;

using LTool::Env;
using LTool::Random;
using LTool::ScopeTimer;
using LTool::Timer;
using LTool::make_pool_executor;
using LTool::ScheduleMode;
using LTool::ScheduledTask;
using LTool::Scheduler;
using LTool::TaskContext;
using LTool::TaskErrorPolicy;
using LTool::TaskId;
using LTool::TaskInfo;
#if LTOOL_HAS_THREAD_POOL
using LTool::PauseThreadPool;
using LTool::PriorityThreadPool;
using LTool::ThreadPool;
using LTool::WaitDeadlockThreadPool;
#endif
} // namespace LTool

export namespace LTool::traits {
using LTool::traits::all_member_object_pointers;
using LTool::traits::is_arithmetic;
using LTool::traits::is_const_range;
using LTool::traits::is_enum;
using LTool::traits::is_floating_point;
using LTool::traits::is_invocable;
using LTool::traits::is_iterator;
using LTool::traits::is_member_object_pointer;
using LTool::traits::is_non_bool_integral;
using LTool::traits::is_random_access_iterator;
using LTool::traits::is_range;
using LTool::traits::remove_cvref;
using LTool::traits::remove_cvref_t;
using LTool::traits::void_t;
} // namespace LTool::traits

export namespace LTool::concepts {
using LTool::concepts::Arithmetic;
using LTool::concepts::ConstRange;
using LTool::concepts::Enum;
using LTool::concepts::FloatingPoint;
using LTool::concepts::InvocableWith;
using LTool::concepts::Iterator;
using LTool::concepts::MemberObjectPointer;
using LTool::concepts::MemberObjectPointerPack;
using LTool::concepts::MemberPointer;
using LTool::concepts::NonBoolIntegral;
using LTool::concepts::RandomAccessIterator;
using LTool::concepts::Range;
} // namespace LTool::concepts

// ---------------------------------------------------------------------------
// 全局命名空间中的值类型
// ---------------------------------------------------------------------------

export {
// LString
using ::LEncoding;
using ::LString;
using ::operator""_ls;

// LEnv / LTimer / LRandom / LTable
using ::LEnv;
using ::LEnvOptions;
using ::LRandom;
using ::LScopeTimer;
using ::LTable;
using ::LTimer;

// LScheduler
using ::LScheduleMode;
using ::LScheduledTask;
using ::LScheduler;
using ::LTaskContext;
using ::LTaskErrorPolicy;
using ::LTaskId;
using ::LTaskInfo;
using ::make_pool_executor;

#if LTOOL_HAS_FILESYSTEM
// LPath / LFile
using ::LFile;
using ::LFileWriteMode;
using ::LPath;
using ::LPathEntryKind;
using ::operator""_lf;
using ::operator""_lp;
#endif

#if LTOOL_HAS_CONCEPTS
using ::Locked;
#endif

#if LTOOL_HAS_THREAD_POOL
using ::BasicLThreadPool;
using ::LPauseThreadPool;
using ::LPriorityThreadPool;
using ::LThreadPool;
using ::LWaitDeadlockThreadPool;
#endif
}

// ---------------------------------------------------------------------------
// LLog（LLOG_* 宏见 LLogMacros.hpp）
// ---------------------------------------------------------------------------

export namespace LLog {
using LLog::ColorMode;
using LLog::Level;
using LLog::Logger;
using LLog::Record;
using LLog::SinkType;
using LLog::SourceLocation;
using LLog::debug;
using LLog::default_logger;
using LLog::enabled;
using LLog::env_is_set;
using LLog::env_is_zero;
using LLog::error;
using LLog::fatal;
using LLog::format_record;
using LLog::info;
using LLog::is_enabled;
using LLog::is_terminal;
using LLog::level_color;
using LLog::level_name;
using LLog::local_time;
using LLog::reset_sink;
using LLog::set_color_enabled;
using LLog::set_color_mode;
using LLog::set_level;
using LLog::set_location_visible;
using LLog::set_sink;
using LLog::should_use_color;
using LLog::trace;
using LLog::warn;
using LLog::write;
using LLog::writef;
} // namespace LLog

/// LLOG_* 与 LTOOL_CURRENT_SOURCE_LOCATION 宏展开后需要的内部入口。
export namespace LLog::detail {
using LLog::detail::log_checked;
using LLog::detail::make_location;
} // namespace LLog::detail

// ---------------------------------------------------------------------------
// LJson
// ---------------------------------------------------------------------------

export namespace LJson {
using LJson::Backend;
using LJson::Json;
using LJson::JsonRef;
using LJson::JsonView;
using LJson::active_backend;
using LJson::backend_name;
using LJson::from;
using LJson::from_file;
using LJson::has_static_reflection;
using LJson::operator<<;
using LJson::parse;
using LJson::parse_yyjson;
using LJson::read;
using LJson::read_or_throw;
using LJson::text;
using LJson::to;
using LJson::to_file;
using LJson::to_string;
using LJson::view;
using LJson::write;
using LJson::yyjson_document;
#if LJSON_HAS_NLOHMANN_JSON
using LJson::parse_nlohmann;
using LJson::to_nlohmann;
#endif
#if LJSON_HAS_JSONCPP
using LJson::parse_cppjson;
using LJson::parse_jsoncpp;
using LJson::to_cppjson;
using LJson::to_jsoncpp;
#endif
#if LJSON_HAS_SIMDJSON
using LJson::parse_simdjson;
using LJson::simdjson_document;
#endif
} // namespace LJson

// ---------------------------------------------------------------------------
// LToml / LYaml / LConfig（C++20）
// ---------------------------------------------------------------------------

#if LTOOL_HAS_CPP20
export namespace LToml {
using LToml::Backend;
using LToml::Toml;
using LToml::TomlView;
using LToml::active_backend;
using LToml::backend_name;
using LToml::from;
using LToml::from_file;
using LToml::has_static_reflection;
using LToml::load;
using LToml::read;
using LToml::read_or_throw;
using LToml::save;
using LToml::text;
using LToml::to;
using LToml::to_file;
using LToml::to_string;
using LToml::view;
using LToml::write;
} // namespace LToml

export namespace LYaml {
using LYaml::Backend;
using LYaml::Yaml;
using LYaml::YamlView;
using LYaml::active_backend;
using LYaml::backend_name;
using LYaml::from;
using LYaml::from_file;
using LYaml::has_static_reflection;
using LYaml::load;
using LYaml::read;
using LYaml::read_or_throw;
using LYaml::save;
using LYaml::text;
using LYaml::to;
using LYaml::to_file;
using LYaml::to_string;
using LYaml::view;
using LYaml::write;
} // namespace LYaml

export namespace LConfig {
using LConfig::EnvBindOptions;
using LConfig::EnvBinding;
using LConfig::EnvOptions;
using LConfig::Format;
using LConfig::MemberPath;
using LConfig::MissingFields;
using LConfig::Options;
using LConfig::detect_format;
using LConfig::find_config_file;
using LConfig::format_name;
using LConfig::load;
using LConfig::load_from_env;
using LConfig::path;
using LConfig::read;
using LConfig::save;
using LConfig::write;
} // namespace LConfig
#endif

// ---------------------------------------------------------------------------
// LDialog / LProgress / LRange（按依赖检测）
// ---------------------------------------------------------------------------

#if LTOOL_HAS_PORTABLE_FILE_DIALOGS
export namespace LDialog {
using LDialog::AsyncResult;
using LDialog::Button;
using LDialog::Capabilities;
using LDialog::Choice;
using LDialog::ConfirmCallback;
using LDialog::FileFilter;
using LDialog::Filters;
using LDialog::Icon;
using LDialog::MessageAsync;
using LDialog::MessageCallback;
using LDialog::MessageOptions;
using LDialog::NotifyOptions;
using LDialog::OpenFileAsync;
using LDialog::OpenFileCallback;
using LDialog::OpenFileOptions;
using LDialog::PathAsync;
using LDialog::PathCallback;
using LDialog::SaveFileOptions;
using LDialog::SelectFolderOptions;
using LDialog::VoidAsync;
using LDialog::VoidCallback;
using LDialog::all_files;
using LDialog::available;
using LDialog::confirm;
using LDialog::confirm_async;
using LDialog::home;
using LDialog::message;
using LDialog::message_async;
using LDialog::notify;
using LDialog::notify_async;
using LDialog::open_file;
using LDialog::open_file_async;
using LDialog::open_single_file;
using LDialog::open_single_file_async;
using LDialog::rescan;
using LDialog::save_file;
using LDialog::save_file_async;
using LDialog::select_folder;
using LDialog::select_folder_async;
using LDialog::separator;
using LDialog::verbose;
} // namespace LDialog
#endif

#if LTOOL_HAS_BARKEEP
export namespace LProgress {
using LProgress::Animation;
using LProgress::AnimationConfig;
using LProgress::AnimationStyle;
using LProgress::BarParts;
using LProgress::Capabilities;
using LProgress::Composite;
using LProgress::Counter;
using LProgress::CounterConfig;
using LProgress::Display;
using LProgress::Duration;
using LProgress::IterableBar;
using LProgress::IterableBarConfig;
using LProgress::ProgressBar;
using LProgress::ProgressBarConfig;
using LProgress::ProgressBarStyle;
using LProgress::Status;
using LProgress::Strings;
using LProgress::blue;
using LProgress::cyan;
using LProgress::green;
using LProgress::magenta;
using LProgress::red;
using LProgress::reset;
using LProgress::yellow;
} // namespace LProgress
#endif

#if LTOOL_HAS_RANGE_V3
export namespace LRange {
namespace views = ranges::views;
namespace actions = ranges::actions;
namespace algorithms = ranges;

using LRange::ContainerRange;
using LRange::begin;
using LRange::end;
using LRange::range_reference_t;
using LRange::range_value_t;

using LRange::action_drop;
using LRange::action_drop_while;
using LRange::action_join;
using LRange::action_reverse;
using LRange::action_shuffle;
using LRange::action_sort;
using LRange::action_stable_sort;
using LRange::action_stride;
using LRange::action_take;
using LRange::action_take_while;
using LRange::action_unique;
using LRange::all;
using LRange::all_of;
using LRange::any;
using LRange::average;
using LRange::cache1;
using LRange::chunk;
using LRange::collect;
using LRange::concat;
using LRange::contains;
using LRange::contains_if;
using LRange::count;
using LRange::count_if;
using LRange::cycle;
using LRange::drop;
using LRange::drop_while;
using LRange::each;
using LRange::empty;
using LRange::filter;
using LRange::first;
using LRange::generate;
using LRange::generate_n;
using LRange::group_by;
using LRange::indexed;
using LRange::ints;
using LRange::iota;
using LRange::join;
using LRange::join_strings;
using LRange::keys;
using LRange::last;
using LRange::mean;
using LRange::none;
using LRange::partial_sum;
using LRange::range;
using LRange::repeat;
using LRange::repeat_n;
using LRange::replace;
using LRange::replace_if;
using LRange::reverse;
using LRange::single;
using LRange::size;
using LRange::slice;
using LRange::sliding;
using LRange::split;
using LRange::split_when;
using LRange::stride;
using LRange::sum;
using LRange::tail;
using LRange::take;
using LRange::take_while;
using LRange::to;
using LRange::to_map;
using LRange::to_set;
using LRange::to_unordered_map;
using LRange::to_unordered_set;
using LRange::to_vector;
using LRange::transform;
using LRange::trim;
using LRange::unique;
using LRange::values;
using LRange::zip;
using LRange::zip_with;
} // namespace LRange
#endif

// ---------------------------------------------------------------------------
// fmt：ltool 自带 fmt，导出常用入口，模块用户无需再包含 fmt 头文件
// ---------------------------------------------------------------------------

export namespace fmt {
using fmt::format;
using fmt::format_string;
using fmt::format_to;
using fmt::formatted_size;
using fmt::formatter;
using fmt::join;
using fmt::print;
using fmt::println;
using fmt::runtime;
using fmt::to_string;
using fmt::vformat;
} // namespace fmt
