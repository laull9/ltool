/**
 * @file LToml.hpp
 * @brief ltool 的 TOML 反射序列化入口。
 */

#ifndef LTOOL_LTOML_INCLUDE
#define LTOOL_LTOML_INCLUDE

#include "detail/LToolConfig.hpp"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#define LTOML_HAS_RFL_TOML LTOOL_HAS_RFL_TOML
#define LTOML_HAS_BUNDLED_RFL_TOML LTOOL_HAS_BUNDLED_RFL_TOML
#define LTOML_HAS_STATIC_REFLECTION LTOOL_HAS_RFL_TOML

#if LTOML_HAS_RFL_TOML
#if LTOML_HAS_BUNDLED_RFL_TOML
#include "pkgs/rfl/toml.hpp"
#else
#include <rfl/toml.hpp>
#endif
#endif

namespace LToml {

enum class Backend {
    none,
    rfl_toml
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
struct is_toml_text_source
    : std::integral_constant<bool,
                             is_text_buffer_like<remove_cvref_t<T>>::value &&
                                 !std::is_same<remove_cvref_t<T>, std::string>::value> {};

inline std::string read_file_text(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("LToml failed to open file: " + path);
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

inline void write_file_text(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        throw std::runtime_error("LToml failed to open file for writing: " + path);
    }
    out << text;
}

} // namespace detail

class TomlView {
private:
    const char* data_ = "";
    std::size_t size_ = 0;

public:
    TomlView() = default;

    TomlView(const char* text)
        : data_(text ? text : ""), size_(text ? std::strlen(text) : 0) {}

    TomlView(const char* text, std::size_t size)
        : data_(text ? text : ""), size_(text ? size : 0) {
        if (!text && size != 0) {
            throw std::invalid_argument("LToml::TomlView cannot reference null data");
        }
    }

    TomlView(const std::string& text)
        : data_(text.data()), size_(text.size()) {}

    template<class Text,
             typename std::enable_if<detail::is_toml_text_source<Text>::value,
                                     int>::type = 0>
    TomlView(const Text& text)
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

class Toml {
private:
    std::string text_;

public:
    Toml() = default;

    Toml(const char* text)
        : text_(text ? text : "") {}

    Toml(const char* text, std::size_t size)
        : text_(text ? std::string(text, size) : std::string()) {
        if (!text && size != 0) {
            throw std::invalid_argument("LToml::Toml cannot copy null data");
        }
    }

    Toml(TomlView text)
        : text_(text.data(), text.size()) {}

    Toml(std::string text)
        : text_(std::move(text)) {}

    template<class Text,
             typename std::enable_if<detail::is_toml_text_source<Text>::value,
                                     int>::type = 0>
    Toml(const Text& text)
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

    TomlView view() const noexcept {
        return TomlView(data(), size());
    }

    operator TomlView() const noexcept {
        return view();
    }

    operator const std::string&() const noexcept {
        return text_;
    }

    std::string to_string() const {
        return text_;
    }

    static Toml parse(TomlView text) {
        return Toml(text);
    }

#if LTOML_HAS_RFL_TOML
    template<class T>
    static Toml from(const T& value) {
        return Toml(rfl::toml::write(value));
    }

    template<class T>
    auto read() const -> decltype(rfl::toml::read<T>(std::declval<const std::string&>())) {
        return rfl::toml::read<T>(str());
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
    static Toml from(const T&) {
        static_assert(detail::dependent_false<T>::value,
                      "LToml::Toml::from<T> requires C++20 and reflect-cpp rfl/toml");
        return Toml();
    }

    template<class T>
    T read() const {
        static_assert(detail::dependent_false<T>::value,
                      "LToml::Toml::read<T> requires C++20 and reflect-cpp rfl/toml");
        return T();
    }

    template<class T>
    T to() const {
        static_assert(detail::dependent_false<T>::value,
                      "LToml::Toml::to<T> requires C++20 and reflect-cpp rfl/toml");
        return T();
    }

    template<class T>
    T to_or_throw() const {
        return to<T>();
    }
#endif
};

inline TomlView view(const char* text) {
    return TomlView(text);
}

inline TomlView view(const std::string& text) {
    return TomlView(text);
}

inline TomlView view(const Toml& text) noexcept {
    return text.view();
}

inline Toml text(TomlView value) {
    return Toml(value);
}

inline Backend active_backend() noexcept {
#if LTOML_HAS_RFL_TOML
    return Backend::rfl_toml;
#else
    return Backend::none;
#endif
}

inline const char* backend_name(Backend value) noexcept {
    switch (value) {
    case Backend::rfl_toml:
        return LTOML_HAS_BUNDLED_RFL_TOML ? "pkgs/rfl/toml" : "rfl/toml";
    case Backend::none:
    default:
        return "none";
    }
}

inline bool has_static_reflection() noexcept {
    return LTOML_HAS_STATIC_REFLECTION != 0;
}

inline const char* backend_name() noexcept {
    return backend_name(active_backend());
}

inline std::string to_string(TomlView value) {
    return value.str();
}

inline std::string to_string(const Toml& value) {
    return value.str();
}

#if LTOML_HAS_RFL_TOML

template<class T>
inline std::string write(const T& value) {
    return rfl::toml::write(value);
}

template<class T>
inline Toml from(const T& value) {
    return Toml::from(value);
}

template<class T>
inline auto read(const std::string& text) -> decltype(rfl::toml::read<T>(text)) {
    return rfl::toml::read<T>(text);
}

template<class T>
inline auto read(TomlView text) -> decltype(rfl::toml::read<T>(std::string())) {
    return rfl::toml::read<T>(text.str());
}

template<class T>
inline auto read(const Toml& text) -> decltype(rfl::toml::read<T>(text.str())) {
    return rfl::toml::read<T>(text.str());
}

template<class T>
inline auto read(const char* text) -> decltype(rfl::toml::read<T>(std::string())) {
    return rfl::toml::read<T>(std::string(text ? text : ""));
}

template<class T>
inline T read_or_throw(const std::string& text) {
    return read<T>(text).value();
}

template<class T>
inline T read_or_throw(TomlView text) {
    return read<T>(text).value();
}

template<class T>
inline T read_or_throw(const Toml& text) {
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
inline T to(TomlView text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T to(const Toml& text) {
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
                  "LToml::write<T> requires C++20 and reflect-cpp rfl/toml");
    return std::string();
}

template<class T>
inline T read_or_throw(const std::string&) {
    static_assert(detail::dependent_false<T>::value,
                  "LToml::read_or_throw<T> requires C++20 and reflect-cpp rfl/toml");
    return T();
}

#endif

} // namespace LToml

#endif // LTOOL_LTOML_INCLUDE
