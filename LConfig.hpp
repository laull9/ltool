/**
 * @file LConfig.hpp
 * @brief JSON/TOML/YAML 统一配置读取、序列化和环境变量覆盖。
 *
 * 基于 reflect-cpp（rfl）的反射能力，把结构体直接映射到 JSON/TOML/YAML；
 * 再叠加 .env 环境变量覆盖绑定，适合命令行工具的配置管理。
 *
 * 主要接口：
 * - load<T>() / read<T>()   ：从文件或文本反序列化为结构体。
 * - save<T>() / write<T>()  ：把结构体序列化到文件或文本。
 * - Format                  ：json/toml/yaml/auto_detect 格式选择。
 * - Options::env()          ：把环境变量绑定到结构体字段，支持成员指针和嵌套路径。
 * - Options                 ：缺失字段策略、父目录搜索、env 覆盖开关等。
 *
 * 常用示例：
 * @code
 * struct AppConfig {
 *     std::string name;
 *     int port = 8080;
 *     std::string host = "localhost";
 * };
 *
 * // 读取 config.json，并用环境变量覆盖 host/port
 * auto cfg = LConfig::load<AppConfig>("config.json",
 *     LConfig::Options{}
 *         .env("APP_HOST", &AppConfig::host)
 *         .env("APP_PORT", &AppConfig::port));
 *
 * // 从文本读取、写入文件
 * auto cfg2 = LConfig::read<AppConfig>("{\"name\":\"demo\"}");
 * LConfig::save("out.toml", cfg2, LConfig::Format::toml);
 * @endcode
 */

#ifndef LTOOL_LCONFIG_PUBLIC_INCLUDE
#define LTOOL_LCONFIG_PUBLIC_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LConcepts.hpp"
#include "LEnv.hpp"
#include "LFile.hpp"
#include "LJson.hpp"
#include "LToml.hpp"
#include "LYaml.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <tuple>
#include <vector>

#if LTOOL_HAS_MAGIC_ENUM
#include "pkgs/magic_enum/magic_enum.hpp"
#endif

#if LTOOL_HAS_FILESYSTEM
#include <filesystem>
#endif

namespace LConfig {

/// 配置格式。
enum class Format {
    auto_detect, ///< 根据文件扩展名或默认 JSON 推断。
    json,        ///< JSON 格式。
    toml,        ///< TOML 格式。
    yaml         ///< YAML 格式。
};

/// 缺失字段的处理策略。
enum class MissingFields {
    use_defaults, ///< 缺失字段使用结构体默认值。
    strict        ///< 缺失字段报错。
};

/// 环境变量覆盖选项。
struct EnvOptions {
    bool ignore_empty = false; ///< 环境变量为空串时忽略（不覆盖）。
    bool required = false;     ///< 环境变量必须存在，否则抛异常。
};

/// EnvOptions 的别名，语义更贴近绑定场景。
using EnvBindOptions = EnvOptions;

/// 从环境变量读取并写入目标字段的模板前向声明。
template<class T>
bool load_from_env(const std::string& key, T* target,
                   EnvBindOptions options = EnvBindOptions{});

/// 从环境变量读取并写入目标引用的模板前向声明。
template<class T>
bool load_from_env(const std::string& key, T& target,
                   EnvBindOptions options = EnvBindOptions{});

/// 一条环境变量绑定：记录环境变量名、根类型和运行时应用回调。
struct EnvBinding {
    std::string key;                       ///< 环境变量名。
    std::type_index root_type;             ///< 绑定的根结构体类型。
    std::function<bool(void*)> apply;      ///< 对根对象执行覆盖的回调。
};

namespace detail {

/// 成员指针元信息：把 T C::* 拆成类类型 C 和字段类型 T。
template<class T>
struct member_pointer_traits;

template<class Class, class Field>
struct member_pointer_traits<Field Class::*> {
    using class_type = Class;  ///< 成员所属的类。
    using field_type = Field;  ///< 成员类型。
};

/// 沿成员指针链逐级解引用的声明（单段）。
template<class Obj, class Member>
decltype(auto) member_chain(Obj& obj, Member member);

/// 沿成员指针链逐级解引用的声明（多段）。
template<class Obj, class Member, class... Members>
decltype(auto) member_chain(Obj& obj, Member member, Members... members);

} // namespace detail

/// 嵌套成员路径：保存一组成员指针。
template<class... Members>
struct MemberPath {
    std::tuple<Members...> members; ///< 成员指针元组。
};

/// 从成员指针构造嵌套路径。
template<class... Members>
    LTOOL_REQUIRES(LTool::concepts::MemberObjectPointerPack<Members...>)
MemberPath<Members...> path(Members... members) {
    static_assert(sizeof...(Members) > 0, "LConfig::path requires at least one member pointer");
    static_assert(LTool::traits::all_member_object_pointers<Members...>::value,
                  "LConfig::path requires member object pointers");
    return MemberPath<Members...>{std::tuple<Members...>(members...)};
}

/// 配置加载选项：格式、缺失字段策略、父目录搜索、env 覆盖绑定。
struct Options {
    Format format = Format::auto_detect;          ///< 输入/输出格式。
    EnvOptions env_options;                       ///< 默认 env 绑定选项。
    bool allow_missing_fields = true;             ///< 缺失字段是否允许。
    bool search_parent_dirs = true;               ///< 是否向上级目录搜索配置文件。
    std::string search_start_dir;                 ///< 搜索起点目录。
    std::vector<EnvBinding> env_bindings;         ///< 已注册的 env 覆盖绑定。

    /// 设置配置格式。
    Options& use_format(Format value) {
        format = value;
        return *this;
    }

    /// 设置缺失字段策略。
    Options& missing_fields(MissingFields policy) {
        allow_missing_fields = policy == MissingFields::use_defaults;
        return *this;
    }

    /// 开关父目录搜索。
    Options& search_parents(bool enabled = true) {
        search_parent_dirs = enabled;
        return *this;
    }

    /// 设置配置文件搜索起点目录。
    Options& search_from(std::string dir) {
        search_start_dir = std::move(dir);
        return *this;
    }

    /// 开关空环境变量的忽略行为。
    Options& ignore_empty_env(bool enabled = true) {
        env_options.ignore_empty = enabled;
        return *this;
    }

    /// 开关环境变量必填行为。
    Options& require_env(bool enabled = true) {
        env_options.required = enabled;
        return *this;
    }

    /// 把环境变量绑定到成员字段（使用默认绑定选项）。
    template<class Root, class Field>
    Options& env(std::string key, Field Root::* member) {
        return env(std::move(key), member, env_options);
    }

    /// 把环境变量绑定到成员字段，可指定绑定选项。
    template<class Root, class Field>
    Options& env(std::string key, Field Root::* member,
                 EnvBindOptions bind_options) {
        const auto stored_key = key;
        env_bindings.push_back(EnvBinding{
            std::move(key),
            std::type_index(typeid(Root)),
            [stored_key, member, bind_options](void* root) {
                return LConfig::load_from_env(
                    stored_key,
                    &(static_cast<Root*>(root)->*member),
                    bind_options
                );
            }
        });
        return *this;
    }

    /// 把环境变量绑定到嵌套成员路径（使用默认绑定选项）。
    template<class First, class... Rest>
    Options& env(std::string key, MemberPath<First, Rest...> member_path) {
        return env(std::move(key), std::move(member_path), env_options);
    }

    /// 把环境变量绑定到嵌套成员路径，可指定绑定选项。
    template<class First, class... Rest>
    Options& env(std::string key, MemberPath<First, Rest...> member_path,
                 EnvBindOptions bind_options) {
        using Root = typename detail::member_pointer_traits<First>::class_type;
        const auto stored_key = key;
        env_bindings.push_back(EnvBinding{
            std::move(key),
            std::type_index(typeid(Root)),
            [stored_key, member_path, bind_options](void* root) {
                auto& target = std::apply(
                    [root](auto... members) -> decltype(auto) {
                        return detail::member_chain(*static_cast<Root*>(root), members...);
                    },
                    member_path.members
                );
                return LConfig::load_from_env(stored_key, target, bind_options);
            }
        });
        return *this;
    }

    /// 把环境变量绑定到多段成员指针路径。
    template<class Root, class First, class Second, class... Rest>
        LTOOL_REQUIRES(!std::same_as<typename std::decay<Second>::type, EnvBindOptions>)
    LTOOL_CONSTRAINED_RETURN(
        Options&,
        !std::is_same<typename std::decay<Second>::type, EnvBindOptions>::value)
    env(std::string key, First Root::* first, Second second, Rest... rest) {
        return env(std::move(key), path(first, second, rest...));
    }

    /// 把环境变量绑定到访问器 lambda（使用默认绑定选项）。
    template<class Root, class Accessor>
    Options& env(std::string key, Accessor accessor) {
        return env<Root>(std::move(key), std::move(accessor), env_options);
    }

    /// 把环境变量绑定到访问器 lambda，可指定绑定选项。
    template<class Root, class Accessor>
    Options& env(std::string key, Accessor accessor,
                 EnvBindOptions bind_options) {
        using TargetRef = decltype(accessor(std::declval<Root&>()));
        static_assert(std::is_lvalue_reference<TargetRef>::value ||
                          std::is_pointer<typename std::decay<TargetRef>::type>::value,
                      "LConfig::Options::env accessor must return a field reference or pointer");

        const auto stored_key = key;
        env_bindings.push_back(EnvBinding{
            std::move(key),
            std::type_index(typeid(Root)),
            [stored_key, accessor, bind_options](void* root) {
                auto&& target = accessor(*static_cast<Root*>(root));
                using Target = typename std::decay<decltype(target)>::type;
                if constexpr (std::is_pointer<Target>::value) {
                    if (!target) {
                        throw std::invalid_argument(
                            "LConfig::Options::env accessor returned a null pointer");
                    }
                    return LConfig::load_from_env(stored_key, *target, bind_options);
                } else {
                    return LConfig::load_from_env(stored_key, target, bind_options);
                }
            }
        });
        return *this;
    }

    /// bind_env 系列：env() 的语义化别名（成员指针版本）。
    template<class Root, class Field>
    Options& bind_env(std::string key, Field Root::* member) {
        return env(std::move(key), member);
    }

    /// bind_env：成员指针版本，带绑定选项。
    template<class Root, class Field>
    Options& bind_env(std::string key, Field Root::* member,
                      EnvBindOptions bind_options) {
        return env(std::move(key), member, bind_options);
    }

    /// bind_env：多段成员指针版本。
    template<class Root, class First, class Second, class... Rest>
    Options& bind_env(std::string key, First Root::* first, Second second, Rest... rest) {
        return env(std::move(key), first, second, rest...);
    }

    /// bind_env：访问器版本。
    template<class Root, class Accessor>
    Options& bind_env(std::string key, Accessor accessor) {
        return env<Root>(std::move(key), std::move(accessor));
    }

    /// bind_env：访问器版本，带绑定选项。
    template<class Root, class Accessor>
    Options& bind_env(std::string key, Accessor accessor,
                      EnvBindOptions bind_options) {
        return env<Root>(std::move(key), std::move(accessor), bind_options);
    }
};

namespace detail {

/// 让模板静态断言延迟到实例化时触发的辅助类型。
template<class>
struct dependent_false : std::false_type {};

/// 沿成员指针链解引用（单段终止）。
template<class Obj, class Member>
decltype(auto) member_chain(Obj& obj, Member member) {
    return obj.*member;
}

/// 沿成员指针链解引用（递归多段）。
template<class Obj, class Member, class... Members>
decltype(auto) member_chain(Obj& obj, Member member, Members... members) {
    return member_chain(obj.*member, members...);
}

/// 判断是否为 std::optional。
template<class T>
struct is_optional : std::false_type {};

template<class T>
struct is_optional<std::optional<T>> : std::true_type {
    using value_type = T; ///< 内部值类型。
};

/// 判断是否为 std::string 特化。
template<class T>
struct is_std_string : std::false_type {};

template<class Char, class Traits, class Alloc>
struct is_std_string<std::basic_string<Char, Traits, Alloc>> : std::true_type {};

/// 判断是否为 std::vector。
template<class T>
struct is_vector : std::false_type {};

template<class T, class Alloc>
struct is_vector<std::vector<T, Alloc>> : std::true_type {};

/// 以二进制模式读取整个文件文本。
inline std::string read_file_text(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("LConfig failed to open file: " + path);
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// 原子地把文本整体写入文件：写同目录临时文件 + fsync + rename 替换，
/// 崩溃/磁盘满也不会截断原文件。
inline void write_file_text(const std::string& path, const std::string& text) {
    LFile::atomic_write_bytes(path, text);
}

/// 把字符串转为小写（逐字节）。
inline std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

/// 提取文件路径的扩展名（不含点，小写）。
inline std::string extension_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return std::string();
    }
    return lowercase(path.substr(dot + 1));
}

/// 根据文件扩展名推断配置格式；无法识别时抛异常。
inline Format detect_format_from_path(const std::string& path) {
    const auto ext = extension_of(path);
    if (ext == "json") {
        return Format::json;
    }
    if (ext == "toml" || ext == "tml") {
        return Format::toml;
    }
    if (ext == "yaml" || ext == "yml") {
        return Format::yaml;
    }
    throw std::invalid_argument("LConfig cannot detect config format from path: " + path);
}

/// 解析最终格式：显式指定时直接采用，否则按路径推断，默认 JSON。
inline Format resolve_format(Format requested, const std::string& path = std::string()) {
    if (requested != Format::auto_detect) {
        return requested;
    }
    if (!path.empty()) {
        return detect_format_from_path(path);
    }
    return Format::json;
}

/// 判断路径是否指向存在的普通文件。
inline bool file_exists(const std::string& path) {
#if LTOOL_HAS_FILESYSTEM
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
#else
    std::ifstream in(path.c_str(), std::ios::binary);
    return static_cast<bool>(in);
#endif
}

/// 从 start_dir（或当前目录）起逐级向上查找文件。
inline std::string find_file_in_parents(const std::string& path,
                                        const std::string& start_dir = std::string()) {
#if LTOOL_HAS_FILESYSTEM
    namespace fs = std::filesystem;
    const fs::path requested(path);
    if (requested.is_absolute()) {
        return requested.string();
    }

    std::error_code ec;
    fs::path current = start_dir.empty() ? fs::current_path(ec) : fs::path(start_dir);
    if (ec || current.empty()) {
        current = fs::path(".");
    }
    if (fs::is_regular_file(current, ec)) {
        current = current.parent_path();
    }

    while (true) {
        const auto candidate = current / requested;
        if (fs::is_regular_file(candidate, ec)) {
            return candidate.string();
        }

        const auto parent = current.parent_path();
        if (parent.empty() || parent == current) {
            break;
        }
        current = parent;
    }
#endif
    return path;
}

/// 根据选项决定最终加载路径（是否启用父目录搜索）。
inline std::string resolve_load_path(const std::string& path, const Options& options) {
    if (!options.search_parent_dirs) {
        return path;
    }
    return find_file_in_parents(path, options.search_start_dir);
}

/// 解析常见的布尔文本（true/false/1/0/yes/no/on/off）。
inline bool parse_bool(std::string text, bool& out) {
    text = lowercase(std::move(text));
    if (text == "1" || text == "true" || text == "yes" || text == "on") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "no" || text == "off") {
        out = false;
        return true;
    }
    return false;
}

/// 解析数值文本；整数走 from_chars，浮点走流式解析。
template<class T>
bool parse_number(const std::string& text, T& out) {
    const auto first = text.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos) {
        return false;
    }
    const auto last = text.find_last_not_of(" \t\r\n\f\v");
    const std::string trimmed = text.substr(first, last - first + 1);
    if constexpr (std::is_integral<T>::value) {
        T value{};
        const auto* begin = trimmed.data();
        const auto* end = trimmed.data() + trimmed.size();
        const auto res = std::from_chars(begin, end, value);
        if (res.ec == std::errc() && res.ptr == end) {
            out = value;
            return true;
        }
        return false;
    } else {
        std::istringstream in(trimmed);
        T value{};
        in >> value;
        if (in && in.eof()) {
            out = value;
            return true;
        }
        return false;
    }
}

/// 把环境变量字符串解析并写入目标字段。
template<class T>
bool assign_from_env_string(T& out, const std::string& text) {
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same<Value, std::string>::value) {
        out = text;
        return true;
    } else if constexpr (std::is_same<Value, bool>::value) {
        return parse_bool(text, out);
    } else if constexpr (std::is_enum<Value>::value) {
#if LTOOL_HAS_MAGIC_ENUM
        if (auto value = magic_enum::enum_cast<Value>(text)) {
            out = *value;
            return true;
        }
#endif
        using Underlying = typename std::underlying_type<Value>::type;
        Underlying value{};
        if (parse_number(text, value)) {
            out = static_cast<Value>(value);
            return true;
        }
        return false;
    } else if constexpr (std::is_arithmetic<Value>::value) {
        return parse_number(text, out);
    } else if constexpr (is_optional<Value>::value) {
        using Inner = typename is_optional<Value>::value_type;
        if (text.empty() || lowercase(text) == "null") {
            out.reset();
            return true;
        }
        Inner value{};
        if (assign_from_env_string(value, text)) {
            out = std::move(value);
            return true;
        }
        return false;
    } else {
        return false;
    }
}

/// 读取环境变量并应用到目标字段；受 required/ignore_empty 选项控制。
template<class T>
bool apply_bound_env(const std::string& key, T& target, const EnvBindOptions& options) {
    std::string text;
    if (!LEnv::try_get_env(key, text)) {
        if (options.required) {
            throw std::runtime_error("LConfig missing required environment variable " + key);
        }
        return false;
    }
    if (options.ignore_empty && text.empty()) {
        return true;
    }
    if (!assign_from_env_string(target, text)) {
        throw std::runtime_error("LConfig failed to parse environment variable " + key);
    }
    return true;
}

/// 对根对象应用所有匹配其类型的 env 绑定。
template<class T>
void apply_env_bindings(T& value, const Options& options) {
    for (const auto& binding : options.env_bindings) {
        if (binding.root_type != std::type_index(typeid(T))) {
            continue;
        }
        binding.apply(&value);
    }
}

/// 按格式把文本反序列化为结构体。
template<class T>
T parse_text(const std::string& text, Format format, bool allow_missing_fields) {
    switch (format) {
    case Format::json:
#if LTOOL_HAS_RFL_JSON
        if (allow_missing_fields) {
            return rfl::json::read<T, rfl::DefaultIfMissing>(text).value();
        }
#endif
        return LJson::read_or_throw<T>(text);
    case Format::toml:
#if LTOOL_HAS_RFL_TOML
        if (allow_missing_fields) {
            return rfl::toml::read<T, rfl::DefaultIfMissing>(text).value();
        }
#endif
        return LToml::read_or_throw<T>(text);
    case Format::yaml:
#if LTOOL_HAS_RFL_YAML
        if (allow_missing_fields) {
            return rfl::yaml::read<T, rfl::DefaultIfMissing>(text).value();
        }
#endif
        return LYaml::read_or_throw<T>(text);
    case Format::auto_detect:
    default:
#if LTOOL_HAS_RFL_JSON
        if (allow_missing_fields) {
            return rfl::json::read<T, rfl::DefaultIfMissing>(text).value();
        }
#endif
        return LJson::read_or_throw<T>(text);
    }
}

/// 按格式把结构体序列化为文本。
template<class T>
std::string write_text(const T& value, Format format) {
    switch (format) {
    case Format::json:
        return LJson::write(value);
    case Format::toml:
        return LToml::write(value);
    case Format::yaml:
        return LYaml::write(value);
    case Format::auto_detect:
    default:
        return LJson::write(value);
    }
}

} // namespace detail

/// 根据文件路径扩展名推断配置格式。
inline Format detect_format(const std::string& path) {
    return detail::detect_format_from_path(path);
}

/// 查找配置文件的实际路径（支持父目录搜索）。
inline std::string find_config_file(const std::string& path,
                                    const Options& options = Options{}) {
    return detail::resolve_load_path(path, options);
}

/// 返回配置格式的稳定显示名称。
inline const char* format_name(Format format) noexcept {
    switch (format) {
    case Format::json:
        return "json";
    case Format::toml:
        return "toml";
    case Format::yaml:
        return "yaml";
    case Format::auto_detect:
    default:
        return "auto";
    }
}

/// 从文本反序列化为结构体，并应用 env 覆盖。
template<class T>
T read(const std::string& text, Options options = Options{}) {
    const auto format = detail::resolve_format(options.format);
    T value = detail::parse_text<T>(text, format, options.allow_missing_fields);
    detail::apply_env_bindings(value, options);
    return value;
}

/// 从文件加载并反序列化为结构体，并应用 env 覆盖。
template<class T>
T load(const std::string& path, Options options = Options{}) {
    const auto resolved_path = detail::resolve_load_path(path, options);
    const auto format = detail::resolve_format(options.format, resolved_path);
    T value = detail::parse_text<T>(
        detail::read_file_text(resolved_path),
        format,
        options.allow_missing_fields
    );
    detail::apply_env_bindings(value, options);
    return value;
}

/// 从环境变量读取并写入目标指针指向的字段。
template<class T>
bool load_from_env(const std::string& key, T* target, EnvBindOptions options) {
    if (!target) {
        throw std::invalid_argument("LConfig::load_from_env target cannot be null");
    }
    return detail::apply_bound_env(key, *target, options);
}

/// 从环境变量读取并写入目标引用。
template<class T>
bool load_from_env(const std::string& key, T& target, EnvBindOptions options) {
    return detail::apply_bound_env(key, target, options);
}

/// 把结构体序列化为配置文本。
template<class T>
std::string write(const T& value, Format format = Format::json) {
    return detail::write_text(value, detail::resolve_format(format));
}

/// 把结构体序列化并原子写入文件（临时文件 + rename，避免截断写丢数据）。
template<class T>
void save(const std::string& path, const T& value, Format format = Format::auto_detect) {
    detail::write_file_text(path, write(value, detail::resolve_format(format, path)));
}

} // namespace LConfig

#endif // LTOOL_LCONFIG_PUBLIC_INCLUDE
