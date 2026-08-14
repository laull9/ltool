/**
 * @file LYaml.hpp
 * @brief ltool 的 YAML 反射序列化入口。
 *
 * 基于 reflect-cpp（rfl::yaml）提供结构体反射读写，同时提供 Yaml/YamlView
 * 文本持有类型。可用依赖不足时，反射相关接口会编译期报错，文本类接口照常可用。
 *
 * 主要接口：
 * - read<T>() / read_or_throw<T>() / to<T>() ：文本/字符串 -> 结构体。
 * - write<T>() / from<T>()                  ：结构体 -> YAML 文本。
 * - load<T>() / save<T>()                   ：文件往返。
 * - Yaml / YamlView                         ：YAML 文本的持有/零拷贝视图。
 *
 * 常用示例：
 * @code
 * struct Server { int port; std::string host; };
 *
 * auto srv = LYaml::read_or_throw<Server>("host: localhost\nport: 8080\n");
 * auto text = LYaml::write(srv);
 * auto srv2 = LYaml::load<Server>("config.yaml");
 * LYaml::save("out.yaml", srv2);
 * @endcode
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

/// 让模板静态断言延迟到实例化时触发的辅助类型。
template<class>
struct dependent_false : std::false_type {};

/// 标准 void_t 别名。
template<class...>
using void_t = void;

/// 剥除 cv 和引用后的类型。
template<class T>
struct remove_cvref {
    using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<class T>
using remove_cvref_t = typename remove_cvref<T>::type;

/// 判断类型是否具备 data()/size() 文本缓冲形态。
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

/// 判断类型是否为可接受的 YAML 文本来源（排除 std::string 以免歧义）。
template<class T>
struct is_yaml_text_source
    : std::integral_constant<bool,
                             is_text_buffer_like<remove_cvref_t<T>>::value &&
                                 !std::is_same<remove_cvref_t<T>, std::string>::value> {};

/// 以二进制模式读取整个文件文本。
inline std::string read_file_text(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("LYaml failed to open file: " + path);
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// 以二进制模式把文本整体写入文件。
inline void write_file_text(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        throw std::runtime_error("LYaml failed to open file for writing: " + path);
    }
    out << text;
    out.flush();
    if (!out) {
        throw std::runtime_error("LYaml failed to write file: " + path);
    }
    out.close();
    if (!out) {
        throw std::runtime_error("LYaml failed to close file: " + path);
    }
}

} // namespace detail

/**
 * @brief YAML 文本的零拷贝视图；不拥有数据。
 *
 * 可隐式从 const char*、std::string、其它文本缓冲构造；生命周期需由调用方保证。
 */
class YamlView {
private:
    const char* data_ = ""; ///< 指向文本起始位置。
    std::size_t size_ = 0;  ///< 文本字节长度。

public:
    /// 空视图。
    YamlView() = default;

    /// 从 NUL 结尾字符串构造；nullptr 视为空视图。
    YamlView(const char* text)
        : data_(text ? text : ""), size_(text ? std::strlen(text) : 0) {}

    /// 从带显式长度的缓冲区构造；非空长度不允许 nullptr。
    YamlView(const char* text, std::size_t size)
        : data_(text ? text : ""), size_(text ? size : 0) {
        if (!text && size != 0) {
            throw std::invalid_argument("LYaml::YamlView cannot reference null data");
        }
    }

    /// 从 std::string 构造视图。
    YamlView(const std::string& text)
        : data_(text.data()), size_(text.size()) {}

    /// 从任意文本缓冲类型构造视图。
    template<class Text LTOOL_ENABLE_IF(detail::is_yaml_text_source<Text>::value)>
        LTOOL_REQUIRES(detail::is_yaml_text_source<Text>::value)
    YamlView(const Text& text)
        : data_(text.data()), size_(text.size()) {}

    /// 返回文本起始指针。
    const char* data() const noexcept {
        return data_;
    }

    /// 返回文本字节长度。
    std::size_t size() const noexcept {
        return size_;
    }

    /// 判断视图是否为空。
    bool empty() const noexcept {
        return size_ == 0;
    }

    /// 复制视图内容为 std::string。
    std::string str() const {
        return std::string(data_, size_);
    }
};

/**
 * @brief YAML 文本的持有类型；拥有数据副本。
 */
class Yaml {
private:
    std::string text_; ///< 持有的 YAML 文本。

public:
    /// 空文本。
    Yaml() = default;

    /// 从 NUL 结尾字符串复制；nullptr 视为空文本。
    Yaml(const char* text)
        : text_(text ? text : "") {}

    /// 从带显式长度的缓冲区复制；非空长度不允许 nullptr。
    Yaml(const char* text, std::size_t size)
        : text_(text ? std::string(text, size) : std::string()) {
        if (!text && size != 0) {
            throw std::invalid_argument("LYaml::Yaml cannot copy null data");
        }
    }

    /// 从视图复制内容。
    Yaml(YamlView text)
        : text_(text.data(), text.size()) {}

    /// 接管已有 std::string。
    Yaml(std::string text)
        : text_(std::move(text)) {}

    /// 从任意文本缓冲类型复制。
    template<class Text LTOOL_ENABLE_IF(detail::is_yaml_text_source<Text>::value)>
        LTOOL_REQUIRES(detail::is_yaml_text_source<Text>::value)
    Yaml(const Text& text)
        : text_(text.data(), text.size()) {}

    /// 返回底层文本的只读引用。
    const std::string& str() const noexcept {
        return text_;
    }

    /// 返回 NUL 结尾的文本指针。
    const char* c_str() const noexcept {
        return text_.c_str();
    }

    /// 返回文本数据指针。
    const char* data() const noexcept {
        return text_.data();
    }

    /// 返回文本字节长度。
    std::size_t size() const noexcept {
        return text_.size();
    }

    /// 判断文本是否为空。
    bool empty() const noexcept {
        return text_.empty();
    }

    /// 返回当前文本的零拷贝视图。
    YamlView view() const noexcept {
        return YamlView(data(), size());
    }

    /// 隐式转换为视图。
    operator YamlView() const noexcept {
        return view();
    }

    /// 隐式转换为底层字符串引用。
    operator const std::string&() const noexcept {
        return text_;
    }

    /// 返回文本副本。
    std::string to_string() const {
        return text_;
    }

    /// 从视图构造 Yaml（持有副本）。
    static Yaml parse(YamlView text) {
        return Yaml(text);
    }

#if LYAML_HAS_RFL_YAML
    /// 用反射把结构体序列化为 YAML 文本。
    template<class T>
    static Yaml from(const T& value) {
        return Yaml(rfl::yaml::write(value));
    }

    /// 用反射把当前文本反序列化为结构体，返回 rfl 结果对象。
    template<class T>
    auto read() const -> decltype(rfl::yaml::read<T>(std::declval<const std::string&>())) {
        return rfl::yaml::read<T>(str());
    }

    /// 反序列化为结构体；失败时抛出异常。
    template<class T>
    T to() const {
        return read<T>().value();
    }

    /// to() 的同义函数。
    template<class T>
    T to_or_throw() const {
        return to<T>();
    }
#else
    /// 反射序列化；缺少依赖时编译期报错。
    template<class T>
    static Yaml from(const T&) {
        static_assert(detail::dependent_false<T>::value,
                      "LYaml::Yaml::from<T> requires C++20 and reflect-cpp rfl/yaml");
        return Yaml();
    }

    /// 反射反序列化；缺少依赖时编译期报错。
    template<class T>
    T read() const {
        static_assert(detail::dependent_false<T>::value,
                      "LYaml::Yaml::read<T> requires C++20 and reflect-cpp rfl/yaml");
        return T();
    }

    /// 反射反序列化；缺少依赖时编译期报错。
    template<class T>
    T to() const {
        static_assert(detail::dependent_false<T>::value,
                      "LYaml::Yaml::to<T> requires C++20 and reflect-cpp rfl/yaml");
        return T();
    }

    /// to() 的同义函数。
    template<class T>
    T to_or_throw() const {
        return to<T>();
    }
#endif
};

/// 从 NUL 结尾字符串构造视图。
inline YamlView view(const char* text) {
    return YamlView(text);
}

/// 从 std::string 构造视图。
inline YamlView view(const std::string& text) {
    return YamlView(text);
}

/// 从 Yaml 持有对象构造视图。
inline YamlView view(const Yaml& text) noexcept {
    return text.view();
}

/// 从视图构造持有文本的 Yaml。
inline Yaml text(YamlView value) {
    return Yaml(value);
}

/// 返回当前实际使用的反射后端。
inline Backend active_backend() noexcept {
#if LYAML_HAS_RFL_YAML
    return Backend::rfl_yaml;
#else
    return Backend::none;
#endif
}

/// 返回后端显示名称。
inline const char* backend_name(Backend value) noexcept {
    switch (value) {
    case Backend::rfl_yaml:
        return LYAML_HAS_BUNDLED_RFL_YAML ? "pkgs/rfl/yaml" : "rfl/yaml";
    case Backend::none:
    default:
        return "none";
    }
}

/// 判断当前是否启用了静态反射后端。
inline bool has_static_reflection() noexcept {
    return LYAML_HAS_STATIC_REFLECTION != 0;
}

/// 返回当前实际后端的显示名称。
inline const char* backend_name() noexcept {
    return backend_name(active_backend());
}

/// 把视图内容转为字符串。
inline std::string to_string(YamlView value) {
    return value.str();
}

/// 把持有文本转为字符串。
inline std::string to_string(const Yaml& value) {
    return value.str();
}

#if LYAML_HAS_RFL_YAML

/// 用反射把结构体序列化为 YAML 文本。
template<class T>
inline std::string write(const T& value) {
    return rfl::yaml::write(value);
}

/// 用反射把结构体序列化为持有文本的 Yaml。
template<class T>
inline Yaml from(const T& value) {
    return Yaml::from(value);
}

/// 从 std::string 反序列化为结构体，返回 rfl 结果对象。
template<class T>
inline auto read(const std::string& text) -> decltype(rfl::yaml::read<T>(text)) {
    return rfl::yaml::read<T>(text);
}

/// 从视图反序列化为结构体，返回 rfl 结果对象。
template<class T>
inline auto read(YamlView text) -> decltype(rfl::yaml::read<T>(std::string())) {
    return rfl::yaml::read<T>(text.str());
}

/// 从 Yaml 持有对象反序列化为结构体，返回 rfl 结果对象。
template<class T>
inline auto read(const Yaml& text) -> decltype(rfl::yaml::read<T>(text.str())) {
    return rfl::yaml::read<T>(text.str());
}

/// 从 NUL 结尾字符串反序列化为结构体，返回 rfl 结果对象。
template<class T>
inline auto read(const char* text) -> decltype(rfl::yaml::read<T>(std::string())) {
    return rfl::yaml::read<T>(std::string(text ? text : ""));
}

/// 从 std::string 反序列化为结构体；失败时抛出异常。
template<class T>
inline T read_or_throw(const std::string& text) {
    return read<T>(text).value();
}

/// 从视图反序列化为结构体；失败时抛出异常。
template<class T>
inline T read_or_throw(YamlView text) {
    return read<T>(text).value();
}

/// 从 Yaml 持有对象反序列化为结构体；失败时抛出异常。
template<class T>
inline T read_or_throw(const Yaml& text) {
    return read<T>(text).value();
}

/// 从 NUL 结尾字符串反序列化为结构体；失败时抛出异常。
template<class T>
inline T read_or_throw(const char* text) {
    return read<T>(text).value();
}

/// read_or_throw 的简写。
template<class T>
inline T to(const std::string& text) {
    return read_or_throw<T>(text);
}

/// read_or_throw 的简写（视图版本）。
template<class T>
inline T to(YamlView text) {
    return read_or_throw<T>(text);
}

/// read_or_throw 的简写（持有对象版本）。
template<class T>
inline T to(const Yaml& text) {
    return text.template to<T>();
}

/// read_or_throw 的简写（字符串版本）。
template<class T>
inline T to(const char* text) {
    return read_or_throw<T>(text);
}

/// 从文件加载并反序列化为结构体；失败时抛出异常。
template<class T>
inline T load(const std::string& path) {
    return read_or_throw<T>(detail::read_file_text(path));
}

/// 序列化结构体并写入文件。
template<class T>
inline void save(const std::string& path, const T& value) {
    detail::write_file_text(path, write(value));
}

#else

/// 反射序列化；缺少依赖时编译期报错。
template<class T>
inline std::string write(const T&) {
    static_assert(detail::dependent_false<T>::value,
                  "LYaml::write<T> requires C++20 and reflect-cpp rfl/yaml");
    return std::string();
}

/// 反射反序列化；缺少依赖时编译期报错。
template<class T>
inline T read_or_throw(const std::string&) {
    static_assert(detail::dependent_false<T>::value,
                  "LYaml::read_or_throw<T> requires C++20 and reflect-cpp rfl/yaml");
    return T();
}

#endif

} // namespace LYaml

#endif // LTOOL_LYAML_INCLUDE
