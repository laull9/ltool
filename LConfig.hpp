/**
 * @file LConfig.hpp
 * @brief JSON/TOML/YAML 配置读取、序列化和环境变量覆盖。
 */

#ifndef LTOOL_LCONFIG_PUBLIC_INCLUDE
#define LTOOL_LCONFIG_PUBLIC_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LConcepts.hpp"
#include "LEnv.hpp"
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

enum class Format {
    auto_detect,
    json,
    toml,
    yaml
};

enum class MissingFields {
    use_defaults,
    strict
};

struct EnvOptions {
    bool ignore_empty = false;
    bool required = false;
};

using EnvBindOptions = EnvOptions;

template<class T>
bool load_from_env(const std::string& key, T* target,
                   EnvBindOptions options = EnvBindOptions{});

template<class T>
bool load_from_env(const std::string& key, T& target,
                   EnvBindOptions options = EnvBindOptions{});

struct EnvBinding {
    std::string key;
    std::type_index root_type;
    std::function<bool(void*)> apply;
};

namespace detail {

template<class T>
struct member_pointer_traits;

template<class Class, class Field>
struct member_pointer_traits<Field Class::*> {
    using class_type = Class;
    using field_type = Field;
};

template<class Obj, class Member>
decltype(auto) member_chain(Obj& obj, Member member);

template<class Obj, class Member, class... Members>
decltype(auto) member_chain(Obj& obj, Member member, Members... members);

} // namespace detail

template<class... Members>
struct MemberPath {
    std::tuple<Members...> members;
};

template<class... Members>
    LTOOL_REQUIRES(LTool::concepts::MemberObjectPointerPack<Members...>)
MemberPath<Members...> path(Members... members) {
    static_assert(sizeof...(Members) > 0, "LConfig::path requires at least one member pointer");
    static_assert(LTool::traits::all_member_object_pointers<Members...>::value,
                  "LConfig::path requires member object pointers");
    return MemberPath<Members...>{std::tuple<Members...>(members...)};
}

struct Options {
    Format format = Format::auto_detect;
    EnvOptions env_options;
    bool allow_missing_fields = true;
    bool search_parent_dirs = true;
    std::string search_start_dir;
    std::vector<EnvBinding> env_bindings;

    Options& use_format(Format value) {
        format = value;
        return *this;
    }

    Options& missing_fields(MissingFields policy) {
        allow_missing_fields = policy == MissingFields::use_defaults;
        return *this;
    }

    Options& search_parents(bool enabled = true) {
        search_parent_dirs = enabled;
        return *this;
    }

    Options& search_from(std::string dir) {
        search_start_dir = std::move(dir);
        return *this;
    }

    Options& ignore_empty_env(bool enabled = true) {
        env_options.ignore_empty = enabled;
        return *this;
    }

    Options& require_env(bool enabled = true) {
        env_options.required = enabled;
        return *this;
    }

    template<class Root, class Field>
    Options& env(std::string key, Field Root::* member) {
        return env(std::move(key), member, env_options);
    }

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

    template<class First, class... Rest>
    Options& env(std::string key, MemberPath<First, Rest...> member_path) {
        return env(std::move(key), std::move(member_path), env_options);
    }

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

    template<class Root, class First, class Second, class... Rest>
        LTOOL_REQUIRES(!std::same_as<typename std::decay<Second>::type, EnvBindOptions>)
    LTOOL_CONSTRAINED_RETURN(
        Options&,
        !std::is_same<typename std::decay<Second>::type, EnvBindOptions>::value)
    env(std::string key, First Root::* first, Second second, Rest... rest) {
        return env(std::move(key), path(first, second, rest...));
    }

    template<class Root, class Accessor>
    Options& env(std::string key, Accessor accessor) {
        return env<Root>(std::move(key), std::move(accessor), env_options);
    }

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
                return LConfig::load_from_env(stored_key, target, bind_options);
            }
        });
        return *this;
    }

    template<class Root, class Field>
    Options& bind_env(std::string key, Field Root::* member) {
        return env(std::move(key), member);
    }

    template<class Root, class Field>
    Options& bind_env(std::string key, Field Root::* member,
                      EnvBindOptions bind_options) {
        return env(std::move(key), member, bind_options);
    }

    template<class Root, class First, class Second, class... Rest>
    Options& bind_env(std::string key, First Root::* first, Second second, Rest... rest) {
        return env(std::move(key), first, second, rest...);
    }

    template<class Root, class Accessor>
    Options& bind_env(std::string key, Accessor accessor) {
        return env<Root>(std::move(key), std::move(accessor));
    }

    template<class Root, class Accessor>
    Options& bind_env(std::string key, Accessor accessor,
                      EnvBindOptions bind_options) {
        return env<Root>(std::move(key), std::move(accessor), bind_options);
    }
};

namespace detail {

template<class>
struct dependent_false : std::false_type {};

template<class Obj, class Member>
decltype(auto) member_chain(Obj& obj, Member member) {
    return obj.*member;
}

template<class Obj, class Member, class... Members>
decltype(auto) member_chain(Obj& obj, Member member, Members... members) {
    return member_chain(obj.*member, members...);
}

template<class T>
struct is_optional : std::false_type {};

template<class T>
struct is_optional<std::optional<T>> : std::true_type {
    using value_type = T;
};

template<class T>
struct is_std_string : std::false_type {};

template<class Char, class Traits, class Alloc>
struct is_std_string<std::basic_string<Char, Traits, Alloc>> : std::true_type {};

template<class T>
struct is_vector : std::false_type {};

template<class T, class Alloc>
struct is_vector<std::vector<T, Alloc>> : std::true_type {};

inline std::string read_file_text(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("LConfig failed to open file: " + path);
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

inline void write_file_text(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        throw std::runtime_error("LConfig failed to open file for writing: " + path);
    }
    out << text;
}

inline std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

inline std::string extension_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return std::string();
    }
    return lowercase(path.substr(dot + 1));
}

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

inline Format resolve_format(Format requested, const std::string& path = std::string()) {
    if (requested != Format::auto_detect) {
        return requested;
    }
    if (!path.empty()) {
        return detect_format_from_path(path);
    }
    return Format::json;
}

inline bool file_exists(const std::string& path) {
#if LTOOL_HAS_FILESYSTEM
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
#else
    std::ifstream in(path.c_str(), std::ios::binary);
    return static_cast<bool>(in);
#endif
}

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

inline std::string resolve_load_path(const std::string& path, const Options& options) {
    if (!options.search_parent_dirs) {
        return path;
    }
    return find_file_in_parents(path, options.search_start_dir);
}

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

template<class T>
bool parse_number(const std::string& text, T& out) {
    if constexpr (std::is_integral<T>::value) {
        T value{};
        const auto* begin = text.data();
        const auto* end = text.data() + text.size();
        const auto res = std::from_chars(begin, end, value);
        if (res.ec == std::errc() && res.ptr == end) {
            out = value;
            return true;
        }
        return false;
    } else {
        std::istringstream in(text);
        T value{};
        in >> value;
        if (in && in.eof()) {
            out = value;
            return true;
        }
        return false;
    }
}

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

template<class T>
void apply_env_bindings(T& value, const Options& options) {
    for (const auto& binding : options.env_bindings) {
        if (binding.root_type != std::type_index(typeid(T))) {
            throw std::invalid_argument("LConfig environment binding root type does not match load<T>()");
        }
        binding.apply(&value);
    }
}

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

inline Format detect_format(const std::string& path) {
    return detail::detect_format_from_path(path);
}

inline std::string find_config_file(const std::string& path,
                                    const Options& options = Options{}) {
    return detail::resolve_load_path(path, options);
}

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

template<class T>
T read(const std::string& text, Options options = Options{}) {
    const auto format = detail::resolve_format(options.format);
    T value = detail::parse_text<T>(text, format, options.allow_missing_fields);
    detail::apply_env_bindings(value, options);
    return value;
}

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

template<class T>
bool load_from_env(const std::string& key, T* target, EnvBindOptions options) {
    if (!target) {
        throw std::invalid_argument("LConfig::load_from_env target cannot be null");
    }
    return detail::apply_bound_env(key, *target, options);
}

template<class T>
bool load_from_env(const std::string& key, T& target, EnvBindOptions options) {
    return detail::apply_bound_env(key, target, options);
}

template<class T>
std::string write(const T& value, Format format = Format::json) {
    return detail::write_text(value, detail::resolve_format(format));
}

template<class T>
void save(const std::string& path, const T& value, Format format = Format::auto_detect) {
    detail::write_file_text(path, write(value, detail::resolve_format(format, path)));
}

} // namespace LConfig

#endif // LTOOL_LCONFIG_PUBLIC_INCLUDE
