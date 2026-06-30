/**
 * @file LConfig.hpp
 * @brief JSON/TOML/YAML 配置读取、序列化和环境变量覆盖。
 */

#ifndef LTOOL_LCONFIG_PUBLIC_INCLUDE
#define LTOOL_LCONFIG_PUBLIC_INCLUDE

#include "detail/LToolConfig.hpp"
#include "LEnv.hpp"
#include "LJson.hpp"
#include "LToml.hpp"
#include "LYaml.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
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

struct EnvOptions {
    bool enabled = false;
    std::string prefix;
    std::string prefix_separator = "_";
    std::string separator = "__";
    bool uppercase = true;
    bool ignore_empty = false;
};

struct Options {
    Format format = Format::auto_detect;
    EnvOptions env;
    bool search_parent_dirs = true;
    std::string search_start_dir;
};

struct EnvBindOptions {
    bool ignore_empty = false;
};

namespace detail {

template<class>
struct dependent_false : std::false_type {};

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

inline std::string normalize_env_part(std::string part, bool uppercase) {
    for (char& ch : part) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) == 0) {
            ch = '_';
        } else if (uppercase) {
            ch = static_cast<char>(std::toupper(uch));
        }
    }
    return part;
}

inline std::string make_env_key(const EnvOptions& options,
                                const std::vector<std::string>& path) {
    std::string key = options.prefix;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!key.empty()) {
            key += (i == 0 && !options.prefix.empty()) ? options.prefix_separator
                                                       : options.separator;
        }
        key += normalize_env_part(path[i], options.uppercase);
    }
    if (options.uppercase) {
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
    }
    return key;
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
void apply_env(T& value, const EnvOptions& options, std::vector<std::string>& path);

template<class T>
void apply_env_field(T& value, const EnvOptions& options, std::vector<std::string>& path) {
    const auto key = make_env_key(options, path);
    std::string text;
    if (LEnv::try_get_env(key, text)) {
        if (!options.ignore_empty || !text.empty()) {
            if (!assign_from_env_string(value, text)) {
                throw std::runtime_error("LConfig failed to parse environment variable " + key);
            }
        }
    }

    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_class<Value>::value &&
                  !is_std_string<Value>::value &&
                  !is_optional<Value>::value &&
                  !is_vector<Value>::value) {
        apply_env(value, options, path);
    }
}

template<class T>
void apply_env(T& value, const EnvOptions& options, std::vector<std::string>& path) {
    if (!options.enabled) {
        return;
    }
    auto view = rfl::to_view(value);
    view.apply([&](auto field) {
        using Field = std::remove_cv_t<std::remove_reference_t<decltype(field)>>;
        auto* ptr = field.value();
        path.push_back(std::string(Field::name()));
        apply_env_field(*ptr, options, path);
        path.pop_back();
    });
}

template<class T>
T parse_text(const std::string& text, Format format) {
    switch (format) {
    case Format::json:
        return LJson::read_or_throw<T>(text);
    case Format::toml:
        return LToml::read_or_throw<T>(text);
    case Format::yaml:
        return LYaml::read_or_throw<T>(text);
    case Format::auto_detect:
    default:
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
    T value = detail::parse_text<T>(text, format);
    std::vector<std::string> path;
    detail::apply_env(value, options.env, path);
    return value;
}

template<class T>
T load(const std::string& path, Options options = Options{}) {
    const auto resolved_path = detail::resolve_load_path(path, options);
    const auto format = detail::resolve_format(options.format, resolved_path);
    T value = detail::parse_text<T>(detail::read_file_text(resolved_path), format);
    std::vector<std::string> env_path;
    detail::apply_env(value, options.env, env_path);
    return value;
}

template<class T>
T load_with_env(const std::string& path, std::string prefix = std::string(),
                Format format = Format::auto_detect) {
    Options options;
    options.format = format;
    options.env.enabled = true;
    options.env.prefix = std::move(prefix);
    return load<T>(path, options);
}

template<class T>
bool bind_env(const std::string& key, T* target, EnvBindOptions options = EnvBindOptions{}) {
    if (!target) {
        throw std::invalid_argument("LConfig::bind_env target cannot be null");
    }
    return detail::apply_bound_env(key, *target, options);
}

template<class T>
bool bind_env(const std::string& key, T& target, EnvBindOptions options = EnvBindOptions{}) {
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
