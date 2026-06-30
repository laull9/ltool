/**
 * @file LYaml.hpp
 * @brief ltool 的 YAML 反射序列化入口。
 */

#ifndef LTOOL_LYAML_INCLUDE
#define LTOOL_LYAML_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LConcepts.hpp"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#define LYAML_HAS_RFL_YAML LTOOL_HAS_RFL_YAML
#define LYAML_HAS_BUNDLED_RFL_YAML LTOOL_HAS_BUNDLED_RFL_YAML
#define LYAML_HAS_STATIC_REFLECTION LTOOL_HAS_RFL_YAML

#if LYAML_HAS_RFL_YAML
#if LYAML_HAS_BUNDLED_RFL_YAML
#include "pkgs/rfl/yaml.hpp"
#else
#include <rfl/yaml.hpp>
#endif
#endif

namespace LYaml {

enum class Backend {
    none,
    rfl_yaml
};

namespace detail {

template<class>
struct dependent_false : std::false_type {};

template<class...>
using void_t = void;

template<class T>
struct remove_cvref {
    using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<class T>
using remove_cvref_t = typename remove_cvref<T>::type;

template<class T, class = void>
struct is_text_buffer_like : std::false_type {};

template<class T>
struct is_text_buffer_like<T,
                           void_t<decltype(std::declval<const T&>().data()),
                                  decltype(std::declval<const T&>().size())>>
    : std::integral_constant<bool,
                             std::is_convertible<decltype(std::declval<const T&>().data()),
                                                 const char*>::value &&
                                 std::is_convertible<decltype(std::declval<const T&>().size()),
                                                     std::size_t>::value> {};

template<class T>
struct is_yaml_text_source
    : std::integral_constant<bool,
                             is_text_buffer_like<remove_cvref_t<T>>::value &&
                                 !std::is_same<remove_cvref_t<T>, std::string>::value> {};

inline std::string read_file_text(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("LYaml failed to open file: " + path);
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

inline void write_file_text(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        throw std::runtime_error("LYaml failed to open file for writing: " + path);
    }
    out << text;
}

} // namespace detail

class YamlView {
private:
    const char* data_ = "";
    std::size_t size_ = 0;

public:
    YamlView() = default;

    YamlView(const char* text)
        : data_(text ? text : ""), size_(text ? std::strlen(text) : 0) {}

    YamlView(const char* text, std::size_t size)
        : data_(text ? text : ""), size_(text ? size : 0) {
        if (!text && size != 0) {
            throw std::invalid_argument("LYaml::YamlView cannot reference null data");
        }
    }

    YamlView(const std::string& text)
        : data_(text.data()), size_(text.size()) {}

    template<class Text LTOOL_ENABLE_IF(detail::is_yaml_text_source<Text>::value)>
        LTOOL_REQUIRES(detail::is_yaml_text_source<Text>::value)
    YamlView(const Text& text)
        : data_(text.data()), size_(text.size()) {}

    const char* data() const noexcept {
        return data_;
    }

    std::size_t size() const noexcept {
        return size_;
    }

    bool empty() const noexcept {
        return size_ == 0;
    }

    std::string str() const {
        return std::string(data_, size_);
    }
};

class Yaml {
private:
    std::string text_;

public:
    Yaml() = default;

    Yaml(const char* text)
        : text_(text ? text : "") {}

    Yaml(const char* text, std::size_t size)
        : text_(text ? std::string(text, size) : std::string()) {
        if (!text && size != 0) {
            throw std::invalid_argument("LYaml::Yaml cannot copy null data");
        }
    }

    Yaml(YamlView text)
        : text_(text.data(), text.size()) {}

    Yaml(std::string text)
        : text_(std::move(text)) {}

    template<class Text LTOOL_ENABLE_IF(detail::is_yaml_text_source<Text>::value)>
        LTOOL_REQUIRES(detail::is_yaml_text_source<Text>::value)
    Yaml(const Text& text)
        : text_(text.data(), text.size()) {}

    const std::string& str() const noexcept {
        return text_;
    }

    const char* c_str() const noexcept {
        return text_.c_str();
    }

    const char* data() const noexcept {
        return text_.data();
    }

    std::size_t size() const noexcept {
        return text_.size();
    }

    bool empty() const noexcept {
        return text_.empty();
    }

    YamlView view() const noexcept {
        return YamlView(data(), size());
    }

    operator YamlView() const noexcept {
        return view();
    }

    operator const std::string&() const noexcept {
        return text_;
    }

    std::string to_string() const {
        return text_;
    }

    static Yaml parse(YamlView text) {
        return Yaml(text);
    }

#if LYAML_HAS_RFL_YAML
    template<class T>
    static Yaml from(const T& value) {
        return Yaml(rfl::yaml::write(value));
    }

    template<class T>
    auto read() const -> decltype(rfl::yaml::read<T>(std::declval<const std::string&>())) {
        return rfl::yaml::read<T>(str());
    }

    template<class T>
    T to() const {
        return read<T>().value();
    }

    template<class T>
    T to_or_throw() const {
        return to<T>();
    }
#else
    template<class T>
    static Yaml from(const T&) {
        static_assert(detail::dependent_false<T>::value,
                      "LYaml::Yaml::from<T> requires C++20 and reflect-cpp rfl/yaml");
        return Yaml();
    }

    template<class T>
    T read() const {
        static_assert(detail::dependent_false<T>::value,
                      "LYaml::Yaml::read<T> requires C++20 and reflect-cpp rfl/yaml");
        return T();
    }

    template<class T>
    T to() const {
        static_assert(detail::dependent_false<T>::value,
                      "LYaml::Yaml::to<T> requires C++20 and reflect-cpp rfl/yaml");
        return T();
    }

    template<class T>
    T to_or_throw() const {
        return to<T>();
    }
#endif
};

inline YamlView view(const char* text) {
    return YamlView(text);
}

inline YamlView view(const std::string& text) {
    return YamlView(text);
}

inline YamlView view(const Yaml& text) noexcept {
    return text.view();
}

inline Yaml text(YamlView value) {
    return Yaml(value);
}

inline Backend active_backend() noexcept {
#if LYAML_HAS_RFL_YAML
    return Backend::rfl_yaml;
#else
    return Backend::none;
#endif
}

inline const char* backend_name(Backend value) noexcept {
    switch (value) {
    case Backend::rfl_yaml:
        return LYAML_HAS_BUNDLED_RFL_YAML ? "pkgs/rfl/yaml" : "rfl/yaml";
    case Backend::none:
    default:
        return "none";
    }
}

inline bool has_static_reflection() noexcept {
    return LYAML_HAS_STATIC_REFLECTION != 0;
}

inline const char* backend_name() noexcept {
    return backend_name(active_backend());
}

inline std::string to_string(YamlView value) {
    return value.str();
}

inline std::string to_string(const Yaml& value) {
    return value.str();
}

#if LYAML_HAS_RFL_YAML

template<class T>
inline std::string write(const T& value) {
    return rfl::yaml::write(value);
}

template<class T>
inline Yaml from(const T& value) {
    return Yaml::from(value);
}

template<class T>
inline auto read(const std::string& text) -> decltype(rfl::yaml::read<T>(text)) {
    return rfl::yaml::read<T>(text);
}

template<class T>
inline auto read(YamlView text) -> decltype(rfl::yaml::read<T>(std::string())) {
    return rfl::yaml::read<T>(text.str());
}

template<class T>
inline auto read(const Yaml& text) -> decltype(rfl::yaml::read<T>(text.str())) {
    return rfl::yaml::read<T>(text.str());
}

template<class T>
inline auto read(const char* text) -> decltype(rfl::yaml::read<T>(std::string())) {
    return rfl::yaml::read<T>(std::string(text ? text : ""));
}

template<class T>
inline T read_or_throw(const std::string& text) {
    return read<T>(text).value();
}

template<class T>
inline T read_or_throw(YamlView text) {
    return read<T>(text).value();
}

template<class T>
inline T read_or_throw(const Yaml& text) {
    return read<T>(text).value();
}

template<class T>
inline T read_or_throw(const char* text) {
    return read<T>(text).value();
}

template<class T>
inline T to(const std::string& text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T to(YamlView text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T to(const Yaml& text) {
    return text.template to<T>();
}

template<class T>
inline T to(const char* text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T load(const std::string& path) {
    return read_or_throw<T>(detail::read_file_text(path));
}

template<class T>
inline void save(const std::string& path, const T& value) {
    detail::write_file_text(path, write(value));
}

#else

template<class T>
inline std::string write(const T&) {
    static_assert(detail::dependent_false<T>::value,
                  "LYaml::write<T> requires C++20 and reflect-cpp rfl/yaml");
    return std::string();
}

template<class T>
inline T read_or_throw(const std::string&) {
    static_assert(detail::dependent_false<T>::value,
                  "LYaml::read_or_throw<T> requires C++20 and reflect-cpp rfl/yaml");
    return T();
}

#endif

} // namespace LYaml

#endif // LTOOL_LYAML_INCLUDE
