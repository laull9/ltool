/**
 * @file LJson.hpp
 * @brief ltool 的 JSON 统一入口，按可用头文件接入多个 JSON 后端。
 *
 * LJson 以 yyjson 作为可变 DOM 后端（必选），并可选接入 nlohmann/json、
 * jsoncpp、simdjson、reflect-cpp（rfl::json）作为互操作适配层。反射序列化
 * 接口只在可用 rfl 时启用。
 *
 * 主要接口：
 * - Json 类         ：可变 JSON DOM，支持构造、索引、迭代、序列化/反序列化。
 * - parse()         ：解析 JSON 文本为 Json DOM。
 * - write()/dump()  ：把 Json 或反射结构体序列化为文本。
 * - read<T>()       ：用 rfl 反射把文本反序列化为结构体。
 * - from_file()/to_file()：文件往返（Json DOM 与反射结构体）。
 * - 下标访问        ：operator[](key/index)、at()、[] 赋值、深度查找。
 *
 * 值语义：Json 是值类型，拷贝/赋值/放入容器都得到独立副本；可写访问
 * （非 const 的 operator[]、at() 等）返回 JsonRef，写入作用于原文档；
 * const 访问返回 Json 副本。
 *
 * 常用示例：
 * @code
 * auto json = LJson::parse(R"({"name":"ltool","nums":[1,2,3]})");
 * json["name"] = "hello";
 * auto ref = json["nums"];                  // JsonRef：写回 json
 * LJson::Json copy = json["nums"];          // Json：独立副本
 * auto n = json["nums"][1].as_int();        // 2
 * auto text = json.dump();                  // 序列化
 *
 * struct Config { std::string name; std::vector<int> nums; };
 * auto cfg = LJson::read<Config>(text);     // 反射反序列化
 * auto out = LJson::write(cfg);             // 反射序列化
 * auto from_disk = LJson::from_file("cfg.json");   // Json DOM
 * auto cfg2 = LJson::from_file<Config>("cfg.json");// 反射结构体
 * LJson::to_file("out.json", from_disk);
 * LJson::to_file("out.json", cfg2);
 * @endcode
 */

#ifndef LTOOL_LJSON_INCLUDE
#define LTOOL_LJSON_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LConcepts.hpp"

#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "detail/LFmt.hpp"

#if LTOOL_HAS_CPP20 && LTOOL_HAS_INCLUDE(<format>)
#include <format>
#endif

#define LJSON_HAS_RFL_JSON LTOOL_HAS_RFL_JSON
#define LJSON_HAS_BUNDLED_RFL_JSON LTOOL_HAS_BUNDLED_RFL_JSON
#define LJSON_HAS_STATIC_REFLECTION LTOOL_HAS_RFL_JSON
#define LJSON_HAS_NLOHMANN_JSON LTOOL_HAS_NLOHMANN_JSON
#define LJSON_HAS_JSONCPP LTOOL_HAS_JSONCPP
#define LJSON_HAS_CPPJSON LTOOL_HAS_JSONCPP
#define LJSON_HAS_SIMDJSON LTOOL_HAS_SIMDJSON
#define LJSON_HAS_YYJSON LTOOL_HAS_YYJSON
#define LJSON_HAS_RFL_YYJSON LTOOL_HAS_RFL_YYJSON
#define LJSON_HAS_STD_FORMAT LTOOL_HAS_STD_FORMAT

// Json's mutable DOM is intentionally implemented on yyjson. Other JSON
// libraries below are interoperability adapters, not replacement backends.
#if !LJSON_HAS_YYJSON
#error "LJson requires its yyjson DOM backend. Do not disable LTOOL_USE_YYJSON; make yyjson available instead."
#endif

#if LJSON_HAS_NLOHMANN_JSON
#if LTOOL_HAS_NLOHMANN_JSON_HPP
#include <nlohmann/json.hpp>
#else
#include <nlohmann.hpp>
#endif
#endif

#if LJSON_HAS_JSONCPP
#include <json/json.h>
#endif

#if LJSON_HAS_SIMDJSON
#include <simdjson.h>
#endif

#if LJSON_HAS_YYJSON
#if LJSON_HAS_RFL_YYJSON
#include "pkgs/rfl/thirdparty/yyjson_impl.hpp"
#else
#include <yyjson.h>
#endif
#endif

#if LJSON_HAS_RFL_JSON
#if LJSON_HAS_BUNDLED_RFL_JSON
#include "pkgs/rfl/json.hpp"
#else
#include <rfl/json.hpp>
#endif
#endif

namespace LJson {

/// 当前可用的 JSON 后端。
enum class Backend {
    none,           ///< 无后端。
    text,           ///< 纯文本。
    nlohmann_json,  ///< nlohmann/json 后端。
    jsoncpp,        ///< jsoncpp 后端。
    simdjson,       ///< simdjson 后端。
    yyjson,         ///< yyjson 后端（DOM 主后端）。
    rfl_json        ///< reflect-cpp 反射后端。
};

class Json;
class JsonRef;

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

/// 判断类型是否为可接受的 JSON 文本来源（排除 std::string 以免歧义）。
template<class T>
struct is_json_text_source
    : std::integral_constant<bool,
                             is_text_buffer_like<remove_cvref_t<T>>::value &&
                                 !std::is_same<remove_cvref_t<T>, std::string>::value &&
                                 !std::is_base_of<Json, remove_cvref_t<T>>::value> {};

/// 判断类型是否为 Json 或 JsonRef（忽略 cv 与引用）。
template<class T>
struct is_json_value : std::is_base_of<Json, remove_cvref_t<T>> {};

#if LJSON_HAS_JSONCPP
/// 把 jsoncpp 值紧凑序列化为字符串。
inline std::string jsoncpp_to_string(const ::Json::Value& value) {
    ::Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return ::Json::writeString(builder, value);
}
#endif

#if LJSON_HAS_YYJSON
/// 接管 yyjson 分配的内存并复制为 std::string；释放原内存。
inline std::string take_yyjson_string(char* data, std::size_t size) {
    if (!data) {
        throw std::runtime_error("yyjson failed to write JSON");
    }
    std::string out(data, size);
    std::free(data);
    return out;
}

/// 序列化只读 yyjson_doc。
inline std::string yyjson_to_string(yyjson_doc* doc,
                                    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    size_t size = 0;
    return take_yyjson_string(yyjson_write(doc, flags, &size), size);
}

/// 序列化只读 yyjson_val。
inline std::string yyjson_to_string(yyjson_val* value,
                                    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    size_t size = 0;
    return take_yyjson_string(yyjson_val_write(value, flags, &size), size);
}

/// 序列化可变 yyjson_mut_doc。
inline std::string yyjson_to_string(yyjson_mut_doc* doc,
                                    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    size_t size = 0;
    return take_yyjson_string(yyjson_mut_write(doc, flags, &size), size);
}

/// 序列化可变 yyjson_mut_val。
inline std::string yyjson_to_string(yyjson_mut_val* value,
                                    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    size_t size = 0;
    return take_yyjson_string(yyjson_mut_val_write(value, flags, &size), size);
}

/// 把缩进参数映射为 yyjson 写标志；仅支持 0/2/4。
inline bool yyjson_native_write_flags(int indent, yyjson_write_flag& flags) {
    if (indent <= 0) {
        flags = YYJSON_WRITE_NOFLAG;
        return true;
    }
    if (indent == 2) {
        flags = YYJSON_WRITE_PRETTY_TWO_SPACES;
        return true;
    }
    if (indent == 4) {
        flags = YYJSON_WRITE_PRETTY;
        return true;
    }
    return false;
}
#endif

/// 追加 level 层 × indent 个空格缩进。
inline void append_json_indent(std::string& out, std::size_t level, int indent) {
    out.append(level * static_cast<std::size_t>(indent), ' ');
}

/// 对紧凑 JSON 文本做带缩进的重新格式化（纯文本处理，不解析）。
inline std::string format_json_with_indent(const std::string& text, int indent) {
    if (indent <= 0) {
        return text;
    }

    std::string out;
    out.reserve(text.size() + text.size() / 2);

    std::size_t level = 0;
    bool in_string = false;
    bool escaped = false;

    // 逐字符扫描，跳过字符串内容，在 { } [ ] , : 处排版。
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];

        if (in_string) {
            out.push_back(ch);
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        switch (ch) {
        case '"':
            in_string = true;
            out.push_back(ch);
            break;
        case '{':
        case '[': {
            out.push_back(ch);
            const char close = ch == '{' ? '}' : ']';
            if (i + 1 < text.size() && text[i + 1] != close) {
                ++level;
                out.push_back('\n');
                append_json_indent(out, level, indent);
            }
            break;
        }
        case '}':
        case ']': {
            const char open = ch == '}' ? '{' : '[';
            if (i > 0 && text[i - 1] != open) {
                if (level > 0) {
                    --level;
                }
                out.push_back('\n');
                append_json_indent(out, level, indent);
            }
            out.push_back(ch);
            break;
        }
        case ',':
            out.push_back(ch);
            out.push_back('\n');
            append_json_indent(out, level, indent);
            break;
        case ':':
            out.push_back(ch);
            out.push_back(' ');
            break;
        default:
            if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
                out.push_back(ch);
            }
            break;
        }
    }

    return out;
}

#if LJSON_HAS_YYJSON
/// 把文本作为 JSON 字符串字面量（含转义）追加到 out。
inline void append_json_string(std::string& out, const char* text, std::size_t size) {
    static constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (std::size_t i = 0; i < size; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                out += "\\u00";
                out.push_back(hex[ch >> 4]);
                out.push_back(hex[ch & 0x0f]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    out.push_back('"');
}

/// 把可变 yyjson 值递归序列化为紧凑 JSON 文本。
inline void append_mutable_json(std::string& out, yyjson_mut_val* value) {
    if (!value) {
        throw std::logic_error("LJson::Json references no value");
    }
    if (yyjson_mut_is_null(value)) {
        out += "null";
    } else if (yyjson_mut_is_bool(value)) {
        out += yyjson_mut_get_bool(value) ? "true" : "false";
    } else if (yyjson_mut_is_num(value)) {
        char number[40];
        char* end = yyjson_mut_write_number(value, number);
        if (!end) {
            throw std::runtime_error("yyjson failed to write JSON number");
        }
        out.append(number, static_cast<std::size_t>(end - number));
    } else if (yyjson_mut_is_str(value)) {
        append_json_string(out, yyjson_mut_get_str(value), yyjson_mut_get_len(value));
    } else if (yyjson_mut_is_raw(value)) {
        const char* raw = yyjson_mut_get_raw(value);
        out.append(raw, yyjson_mut_get_len(value));
    } else if (yyjson_mut_is_arr(value)) {
        out.push_back('[');
        yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(value);
        bool first = true;
        while (auto* item = yyjson_mut_arr_iter_next(&iter)) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            append_mutable_json(out, item);
        }
        out.push_back(']');
    } else if (yyjson_mut_is_obj(value)) {
        out.push_back('{');
        yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(value);
        bool first = true;
        while (auto* key = yyjson_mut_obj_iter_next(&iter)) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            append_json_string(out, yyjson_mut_get_str(key), yyjson_mut_get_len(key));
            out.push_back(':');
            append_mutable_json(out, yyjson_mut_obj_iter_get_val(key));
        }
        out.push_back('}');
    } else {
        throw std::runtime_error("yyjson contains an unsupported JSON value");
    }
}
#endif

/// 以二进制模式读取整个文件文本。
inline std::string read_file_text(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("LJson failed to open file: " + path);
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// 以二进制模式把文本整体写入文件。
inline void write_file_text(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        throw std::runtime_error("LJson failed to open file for writing: " + path);
    }
    out << text;
    out.flush();
    if (!out) {
        throw std::runtime_error("LJson failed to write file: " + path);
    }
    out.close();
    if (!out) {
        throw std::runtime_error("LJson failed to close file: " + path);
    }
}

} // namespace detail

/**
 * @brief JSON 文本的零拷贝视图；不拥有数据。
 *
 * 可隐式从 const char*、std::string、其它文本缓冲构造；生命周期需由调用方保证。
 */
class JsonView {
private:
    const char* data_ = ""; ///< 指向文本起始位置。
    std::size_t size_ = 0;  ///< 文本字节长度。

public:
    /// 空视图。
    JsonView() = default;

    /// 从 NUL 结尾字符串构造；nullptr 视为空视图。
    JsonView(const char* text)
        : data_(text ? text : ""), size_(text ? std::strlen(text) : 0) {}

    /// 从带显式长度的缓冲区构造；非空长度不允许 nullptr。
    JsonView(const char* text, std::size_t size)
        : data_(text ? text : ""), size_(text ? size : 0) {
        if (!text && size != 0) {
            throw std::invalid_argument("LJson::JsonView cannot reference null data");
        }
    }

    /// 从 std::string 构造视图。
    JsonView(const std::string& text)
        : data_(text.data()), size_(text.size()) {}

    /// 从任意文本缓冲类型构造视图。
    template<class Text LTOOL_ENABLE_IF(detail::is_json_text_source<Text>::value)>
        LTOOL_REQUIRES(detail::is_json_text_source<Text>::value)
    JsonView(const Text& text)
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
 * @brief 可变 JSON DOM 包装类。
 *
 * 内部基于 yyjson_mut_doc。每个 Json 对象要么是持有整个文档的根节点，
 * 要么是对父节点的"代理视图"（通过 key 或下标定位），对代理赋值会写回父文档。
 */
class Json {
    friend class JsonRef;

private:
    // Creating an array element at a distant index requires materializing all
    // preceding null values. Keep that convenience operation bounded.
    static constexpr std::size_t max_sparse_array_growth_ = 1024 * 1024; ///< 稀疏数组扩展上限。

    /// 当前节点与父节点的关系。
    enum class relation_kind {
        root,   ///< 文档根节点。
        object, ///< 父节点对象的某个键。
        array   ///< 父节点数组的某个下标。
    };

    /// 数组下标访问缓存，避免重复定位。
    struct array_index_cache {
        yyjson_mut_val* array = nullptr; ///< 缓存的数组节点。
        yyjson_mut_val* value = nullptr; ///< 缓存的目标值。
        std::size_t index = 0;           ///< 缓存的下标。
        std::size_t size = 0;            ///< 缓存时的数组长度。
    };

    mutable std::string text_;                              ///< 惰性同步的 JSON 文本。
    mutable std::shared_ptr<yyjson_mut_doc> doc_;           ///< 持有的 yyjson 文档。
    mutable yyjson_mut_val* value_ = nullptr;               ///< 当前值节点。
    mutable yyjson_mut_val* parent_ = nullptr;              ///< 父节点。
    mutable relation_kind relation_ = relation_kind::root;  ///< 与父节点的关系。
    mutable std::string key_;                               ///< 在父对象中的键。
    mutable std::size_t index_ = 0;                         ///< 在父数组中的下标。
    mutable std::shared_ptr<bool> text_dirty_;              ///< 文本是否已修改。
    mutable std::shared_ptr<array_index_cache> array_cache_; ///< 数组缓存。
    bool proxy_ = false;                                    ///< 是否为代理视图。
    bool valid_ = true;                                     ///< 是否引用有效值。

    /// 有符号整数带范围检查转换。
    template<class T>
    static T checked_sint_cast(std::int64_t value) {
        if constexpr (std::is_unsigned<T>::value) {
            if (value < 0 || static_cast<std::uint64_t>(value) >
                                 static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                throw std::domain_error("LJson::Json integer value is out of range");
            }
        } else if (value < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
                   value > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
            throw std::domain_error("LJson::Json integer value is out of range");
        }
        return static_cast<T>(value);
    }

    /// 无符号整数带范围检查转换。
    template<class T>
    static T checked_uint_cast(std::uint64_t value) {
        if constexpr (std::is_signed<T>::value) {
            if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                throw std::domain_error("LJson::Json integer value is out of range");
            }
        } else if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
            throw std::domain_error("LJson::Json integer value is out of range");
        }
        return static_cast<T>(value);
    }

    /// 浮点数转整数带范围与有限性检查。
    template<class T>
    static T checked_real_cast(double value) {
        if (!std::isfinite(value)) {
            throw std::domain_error("LJson::Json number is not finite");
        }
        const double upper = std::ldexp(1.0, std::numeric_limits<T>::digits);
        if constexpr (std::is_unsigned<T>::value) {
            if (value < 0.0 || value >= upper) {
                throw std::domain_error("LJson::Json number is out of integer range");
            }
        } else if (value < -upper || value >= upper) {
            throw std::domain_error("LJson::Json number is out of integer range");
        }
        return static_cast<T>(value);
    }

    /// 创建空的可变 yyjson 文档。
    static std::shared_ptr<yyjson_mut_doc> make_doc() {
        auto* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            throw std::bad_alloc();
        }
        return std::shared_ptr<yyjson_mut_doc>(doc, yyjson_mut_doc_free);
    }

    /// 校验值节点非空；空则抛 bad_alloc。
    static yyjson_mut_val* require_value(yyjson_mut_val* value) {
        if (!value) {
            throw std::bad_alloc();
        }
        return value;
    }

    /// JsonRef 共享构造标记。
    struct share_tag {};

    /// 共享构造：复制全部成员（含代理关系），新对象与 other 指向同一节点。
    Json(const Json& other, share_tag)
        : text_(other.text_),
          doc_(other.doc_),
          value_(other.value_),
          parent_(other.parent_),
          relation_(other.relation_),
          key_(other.key_),
          index_(other.index_),
          text_dirty_(other.text_dirty_),
          array_cache_(other.array_cache_),
          proxy_(other.proxy_),
          valid_(other.valid_) {}

    /// 用 other 当前值的深拷贝初始化自身（构造期使用，自身为默认状态）。
    void copy_value_from(const Json& other) {
        text_ = other.str();
        if (!other.valid_) {
            valid_ = false;
            return;
        }
        if (other.value_) {
            init_from_value_copy(other.value_);
            if (text_dirty_) {
                *text_dirty_ = false;
            }
        }
    }

    /// 内部构造：初始化所有成员以建立代理关系。
    Json(std::shared_ptr<yyjson_mut_doc> doc,
         yyjson_mut_val* value,
         yyjson_mut_val* parent,
         relation_kind relation,
         std::string key,
         std::size_t index,
         std::shared_ptr<bool> text_dirty,
         std::shared_ptr<array_index_cache> array_cache,
         bool proxy)
        : doc_(std::move(doc)),
          value_(value),
          parent_(parent),
          relation_(relation),
          key_(std::move(key)),
          index_(index),
          text_dirty_(std::move(text_dirty)),
          array_cache_(std::move(array_cache)),
          proxy_(proxy) {}

    /// 用工厂回调创建根节点值。
    template<class Maker>
    void init_root(Maker&& maker) {
        doc_ = make_doc();
        value_ = require_value(std::forward<Maker>(maker)(doc_.get()));
        yyjson_mut_doc_set_root(doc_.get(), value_);
        parent_ = nullptr;
        relation_ = relation_kind::root;
        text_dirty_ = std::make_shared<bool>(true);
        array_cache_ = std::make_shared<array_index_cache>();
        proxy_ = false;
        valid_ = true;
    }

    /// 从已有可变文档深拷贝初始化。
    void init_from_mut_doc_copy(yyjson_mut_doc* doc) {
        if (!doc || !yyjson_mut_doc_get_root(doc)) {
            init_root([](yyjson_mut_doc* target) { return yyjson_mut_null(target); });
            return;
        }

        auto* copied = yyjson_mut_doc_mut_copy(doc, nullptr);
        if (!copied) {
            throw std::bad_alloc();
        }
        doc_ = std::shared_ptr<yyjson_mut_doc>(copied, yyjson_mut_doc_free);
        value_ = yyjson_mut_doc_get_root(doc_.get());
        parent_ = nullptr;
        relation_ = relation_kind::root;
        text_dirty_ = std::make_shared<bool>(true);
        array_cache_ = std::make_shared<array_index_cache>();
        proxy_ = false;
        valid_ = true;
    }

    /// 从只读文档深拷贝初始化。
    void init_from_doc_copy(yyjson_doc* doc) {
        if (!doc || !yyjson_doc_get_root(doc)) {
            init_root([](yyjson_mut_doc* target) { return yyjson_mut_null(target); });
            return;
        }

        auto* copied = yyjson_doc_mut_copy(doc, nullptr);
        if (!copied) {
            throw std::bad_alloc();
        }
        doc_ = std::shared_ptr<yyjson_mut_doc>(copied, yyjson_mut_doc_free);
        value_ = yyjson_mut_doc_get_root(doc_.get());
        parent_ = nullptr;
        relation_ = relation_kind::root;
        text_dirty_ = std::make_shared<bool>(true);
        array_cache_ = std::make_shared<array_index_cache>();
        proxy_ = false;
        valid_ = true;
    }

    /// 从只读值节点深拷贝初始化。
    void init_from_value_copy(yyjson_val* value) {
        doc_ = make_doc();
        value_ = value ? yyjson_val_mut_copy(doc_.get(), value) : yyjson_mut_null(doc_.get());
        value_ = require_value(value_);
        yyjson_mut_doc_set_root(doc_.get(), value_);
        text_dirty_ = std::make_shared<bool>(true);
        array_cache_ = std::make_shared<array_index_cache>();
        valid_ = true;
    }

    /// 从可变值节点深拷贝初始化。
    void init_from_value_copy(yyjson_mut_val* value) {
        doc_ = make_doc();
        value_ = value ? yyjson_mut_val_mut_copy(doc_.get(), value) : yyjson_mut_null(doc_.get());
        value_ = require_value(value_);
        yyjson_mut_doc_set_root(doc_.get(), value_);
        text_dirty_ = std::make_shared<bool>(true);
        array_cache_ = std::make_shared<array_index_cache>();
        valid_ = true;
    }

    /// 立即解析 JSON 文本；空文本创建 null 根。
    void parse_text_now(JsonView text) {
        if (text.empty()) {
            init_root([](yyjson_mut_doc* target) { return yyjson_mut_null(target); });
            if (text_dirty_) {
                *text_dirty_ = false;
            }
            return;
        }

        yyjson_read_err err{};
        auto* parsed = yyjson_read_opts(const_cast<char*>(text.data()), text.size(),
                                        YYJSON_READ_NOFLAG, nullptr, &err);
        if (!parsed) {
            throw std::runtime_error("LJson parse error at byte " +
                                     std::to_string(err.pos) + ": " +
                                     (err.msg ? err.msg : "yyjson failed to parse JSON"));
        }

        auto* copied = yyjson_doc_mut_copy(parsed, nullptr);
        yyjson_doc_free(parsed);
        if (!copied) {
            throw std::bad_alloc();
        }

        doc_ = std::shared_ptr<yyjson_mut_doc>(copied, yyjson_mut_doc_free);
        value_ = yyjson_mut_doc_get_root(doc_.get());
        if (!value_) {
            value_ = require_value(yyjson_mut_null(doc_.get()));
            yyjson_mut_doc_set_root(doc_.get(), value_);
        }
        parent_ = nullptr;
        relation_ = relation_kind::root;
        text_dirty_ = std::make_shared<bool>(false);
        array_cache_ = std::make_shared<array_index_cache>();
        proxy_ = false;
        valid_ = true;
    }

    /// 惰性解析：确保存在有效值节点。
    void ensure_value() const {
        if (!valid_) {
            throw std::logic_error("LJson::Json references no value");
        }
        // 根节点可能已被指向根的 JsonRef（如 pointer("")）整体替换，
        // 以文档当前根为准，避免缓存的 value_ 过期。
        if (relation_ == relation_kind::root && doc_) {
            if (auto* root = yyjson_mut_doc_get_root(doc_.get())) {
                value_ = root;
            }
        }
        if (value_) {
            return;
        }

        auto* self = const_cast<Json*>(this);
        self->parse_text_now(JsonView(text_));
    }

    /// 返回可写值节点并标记文本脏。
    yyjson_mut_val* mutable_value() {
        ensure_value();
        mark_dirty();
        return value_;
    }

    /// 返回只读值节点。
    yyjson_mut_val* const_value() const {
        ensure_value();
        return value_;
    }

    /// 返回可写文档指针。
    yyjson_mut_doc* mutable_doc() {
        ensure_value();
        return doc_.get();
    }

    /// 返回只读文档指针。
    yyjson_mut_doc* const_doc() const {
        ensure_value();
        return doc_.get();
    }

    /// 标记文本已脏（下次读取需重新序列化）。
    void mark_dirty() {
        if (!text_dirty_) {
            text_dirty_ = std::make_shared<bool>(false);
        }
        *text_dirty_ = true;
    }

    /// 清空数组缓存。
    void clear_array_cache() const noexcept {
        if (array_cache_) {
            *array_cache_ = array_index_cache{};
        }
    }

    /// 把值节点序列化为紧凑文本。
    std::string write_value(yyjson_mut_val* value) const {
        std::string text;
        detail::append_mutable_json(text, value);
        return text;
    }

    /// 同步文本缓存为当前值序列化结果。
    void sync_text() const {
        if (!value_) {
            return;
        }

        text_ = write_value(value_);
        if (!proxy_ && text_dirty_) {
            *text_dirty_ = false;
        }
    }

    /// 序列化当前节点；indent>0 时格式化。
    std::string write_current(int indent = -1) const {
        const auto text = write_value(const_value());
        return indent > 0 ? detail::format_json_with_indent(text, indent) : text;
    }

    /// 用新值替换当前节点（根节点或代理写回父节点）。
    void replace_current(yyjson_mut_val* value) {
        value = require_value(value);
        if (!proxy_ || relation_ == relation_kind::root) {
            clear_array_cache();
            yyjson_mut_doc_set_root(doc_.get(), value);
            value_ = value;
            parent_ = nullptr;
            relation_ = relation_kind::root;
            proxy_ = false;
            mark_dirty();
            return;
        }

        if (relation_ == relation_kind::object) {
            auto* key = require_value(yyjson_mut_strncpy(doc_.get(), key_.data(), key_.size()));
            if (!yyjson_mut_obj_replace(parent_, key, value)) {
                if (!yyjson_mut_obj_add(parent_, key, value)) {
                    throw std::runtime_error("yyjson failed to replace object value");
                }
                value_ = value;
            }
        } else {
            clear_array_cache();
            if (!yyjson_mut_arr_replace(parent_, index_, value)) {
                throw std::runtime_error("yyjson failed to replace array value");
            }
            value_ = value;
        }
        mark_dirty();
    }

    /// 从值节点复制并设置为当前值。
    void set_value_from(yyjson_mut_val* value) {
        if (proxy_) {
            ensure_value();
            auto* copied = require_value(yyjson_mut_val_mut_copy(doc_.get(), value));
            replace_current(copied);
            return;
        }

        doc_ = make_doc();
        value_ = require_value(yyjson_mut_val_mut_copy(doc_.get(), value));
        yyjson_mut_doc_set_root(doc_.get(), value_);
        parent_ = nullptr;
        relation_ = relation_kind::root;
        text_dirty_ = std::make_shared<bool>(true);
        array_cache_ = std::make_shared<array_index_cache>();
        valid_ = true;
    }

    /// 从另一个 Json 复制值。
    void set_value_from(const Json& value) {
        set_value_from(value.const_value());
    }

    /// 用工厂回调创建新值并替换当前值。
    template<class Maker>
    void set_value(Maker&& maker) {
        if (!doc_) {
            doc_ = make_doc();
        }
        auto* value = require_value(std::forward<Maker>(maker)(doc_.get()));
        replace_current(value);
        valid_ = true;
    }

    /// 创建字符串值节点。
    static yyjson_mut_val* make_string(yyjson_mut_doc* doc, const std::string& value) {
        return yyjson_mut_strncpy(doc, value.data(), value.size());
    }

    /// 创建 C 字符串值节点；nullptr 创建 null。
    static yyjson_mut_val* make_string(yyjson_mut_doc* doc, const char* value) {
        return value ? yyjson_mut_strcpy(doc, value) : yyjson_mut_null(doc);
    }

    /// 创建 int 数值节点。
    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, int value) {
        return yyjson_mut_sint(doc, value);
    }

    /// 创建 unsigned int 数值节点。
    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, unsigned int value) {
        return yyjson_mut_uint(doc, value);
    }

    /// 创建 long 数值节点。
    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, long value) {
        return yyjson_mut_sint(doc, static_cast<int64_t>(value));
    }

    /// 创建 unsigned long 数值节点。
    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, unsigned long value) {
        return yyjson_mut_uint(doc, static_cast<uint64_t>(value));
    }

    /// 创建 long long 数值节点。
    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, long long value) {
        return yyjson_mut_sint(doc, static_cast<int64_t>(value));
    }

    /// 创建 unsigned long long 数值节点。
    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, unsigned long long value) {
        return yyjson_mut_uint(doc, static_cast<uint64_t>(value));
    }

    /// 创建 double 数值节点。
    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, double value) {
        return yyjson_mut_real(doc, value);
    }

    /// 把算术值（不含 bool）转成 JSON 数值节点。
    template<class T
             LTOOL_ENABLE_IF(std::is_arithmetic<detail::remove_cvref_t<T>>::value &&
                             !std::is_same<detail::remove_cvref_t<T>, bool>::value)>
        LTOOL_REQUIRES(LTool::concepts::Arithmetic<detail::remove_cvref_t<T>> &&
                       !std::same_as<detail::remove_cvref_t<T>, bool>)
    yyjson_mut_val* make_json_value(T value) {
        return make_number(mutable_doc(), value);
    }

    /// 把 nullptr 转成 JSON null 节点。
    yyjson_mut_val* make_json_value(std::nullptr_t) {
        return yyjson_mut_null(mutable_doc());
    }

    /// 把 bool 转成 JSON 布尔节点。
    yyjson_mut_val* make_json_value(bool value) {
        return yyjson_mut_bool(mutable_doc(), value);
    }

    /// 把 C 字符串转成 JSON 字符串节点。
    yyjson_mut_val* make_json_value(const char* value) {
        return make_string(mutable_doc(), value);
    }

    /// 把 C 字符串转成 JSON 字符串节点。
    yyjson_mut_val* make_json_value(char* value) {
        return make_string(mutable_doc(), value);
    }

    /// 把 std::string 转成 JSON 字符串节点。
    yyjson_mut_val* make_json_value(const std::string& value) {
        return make_string(mutable_doc(), value);
    }

    /// 把右值 std::string 转成 JSON 字符串节点。
    yyjson_mut_val* make_json_value(std::string&& value) {
        return make_string(mutable_doc(), value);
    }

    /// 深拷贝另一个 Json 的值作为当前值。
    yyjson_mut_val* make_json_value(const Json& value) {
        return yyjson_mut_val_mut_copy(mutable_doc(), value.const_value());
    }

    /// 确保当前值为对象；null 自动转为空对象。
    yyjson_mut_val* ensure_object_value() {
        auto* value = mutable_value();
        if (yyjson_mut_is_null(value)) {
            replace_current(require_value(yyjson_mut_obj(doc_.get())));
            value = value_;
        }
        if (!yyjson_mut_is_obj(value)) {
            throw std::domain_error("LJson::Json value is not an object");
        }
        return value;
    }

    /// 确保当前值为数组；null 自动转为空数组。
    yyjson_mut_val* ensure_array_value() {
        auto* value = mutable_value();
        if (yyjson_mut_is_null(value)) {
            replace_current(require_value(yyjson_mut_arr(doc_.get())));
            value = value_;
        }
        if (!yyjson_mut_is_arr(value)) {
            throw std::domain_error("LJson::Json value is not an array");
        }
        return value;
    }

    /// 取对象子节点；不存在时创建 null 子节点（可写场景）。
    static yyjson_mut_val* object_child(yyjson_mut_doc* doc,
                                        yyjson_mut_val* value,
                                        const std::string& key) {
        auto* child = yyjson_mut_obj_getn(value, key.data(), key.size());
        if (child) {
            return child;
        }

        auto* key_value = require_value(yyjson_mut_strncpy(doc, key.data(), key.size()));
        auto* null_value = require_value(yyjson_mut_null(doc));
        if (!yyjson_mut_obj_add(value, key_value, null_value)) {
            throw std::runtime_error("yyjson failed to add object key");
        }
        return null_value;
    }

    /// 取对象子节点（只读）；键不存在抛 out_of_range。
    static yyjson_mut_val* object_child_const(yyjson_mut_val* value,
                                              const std::string& key) {
        if (!yyjson_mut_is_obj(value)) {
            throw std::domain_error("LJson::Json value is not an object");
        }
        auto* child = yyjson_mut_obj_getn(value, key.data(), key.size());
        if (!child) {
            throw std::out_of_range("LJson::Json object key not found: " + key);
        }
        return child;
    }

    /// 取数组子节点；越界时自动用 null 扩充（有限制）。
    yyjson_mut_val* array_child(yyjson_mut_val* value, std::size_t index) {
        const auto size = yyjson_mut_arr_size(value);
        if (index >= size && index - size >= max_sparse_array_growth_) {
            throw std::length_error("LJson::Json sparse array growth exceeds the supported limit");
        }
        while (yyjson_mut_arr_size(value) <= index) {
            clear_array_cache();
            auto* item = require_value(yyjson_mut_null(doc_.get()));
            if (!yyjson_mut_arr_append(value, item)) {
                throw std::runtime_error("yyjson failed to grow array");
            }
        }
        return array_child_unchecked(value, index);
    }

    /// 取数组子节点（只读）；越界抛 out_of_range。
    yyjson_mut_val* array_child_const(yyjson_mut_val* value, std::size_t index) const {
        if (!yyjson_mut_is_arr(value)) {
            throw std::domain_error("LJson::Json value is not an array");
        }
        auto* child = array_child_unchecked(value, index);
        if (!child) {
            throw std::out_of_range("LJson::Json array index out of range");
        }
        return child;
    }

    /// 取数组子节点（不检查越界），带简单访问缓存。
    yyjson_mut_val* array_child_unchecked(yyjson_mut_val* value, std::size_t index) const {
        const auto size = yyjson_mut_arr_size(value);
        if (index >= size) {
            return nullptr;
        }

        if (!array_cache_) {
            array_cache_ = std::make_shared<array_index_cache>();
        }

        // 命中缓存时从缓存位置顺延遍历，避免每次都从头查找。
        auto& cache = *array_cache_;
        yyjson_mut_val* child = nullptr;
        if (cache.array == value && cache.value && cache.size == size) {
            if (index == cache.index) {
                child = cache.value;
            } else if (index > cache.index) {
                child = cache.value;
                for (auto i = cache.index; i < index; ++i) {
                    child = child->next;
                }
            }
        }

        if (!child) {
            if (index == 0) {
                child = yyjson_mut_arr_get_first(value);
            } else if (index + 1 == size) {
                child = yyjson_mut_arr_get_last(value);
            } else {
                child = yyjson_mut_arr_get(value, index);
            }
        }

        cache.array = value;
        cache.value = child;
        cache.index = index;
        cache.size = size;
        return child;
    }

    /// 判断对象是否包含指定键。
    static bool object_contains(yyjson_mut_val* value, const std::string& key) {
        return yyjson_mut_is_obj(value) &&
               yyjson_mut_obj_getn(value, key.data(), key.size()) != nullptr;
    }

    /// 解析 JSON Pointer 路径（/a/b/0 形态，支持 ~0 ~1 转义）。
    static std::vector<std::string> parse_pointer(const std::string& path) {
        std::vector<std::string> parts;
        if (path.empty()) {
            return parts;
        }
        if (path[0] != '/') {
            throw std::invalid_argument("JSON pointer must start with '/'");
        }
        std::string part;
        for (std::size_t i = 1; i < path.size(); ++i) {
            const char ch = path[i];
            if (ch == '/') {
                parts.push_back(part);
                part.clear();
            } else if (ch == '~') {
                if (++i >= path.size()) {
                    throw std::invalid_argument("invalid JSON pointer escape");
                }
                if (path[i] == '0') {
                    part.push_back('~');
                } else if (path[i] == '1') {
                    part.push_back('/');
                } else {
                    throw std::invalid_argument("invalid JSON pointer escape");
                }
            } else {
                part.push_back(ch);
            }
        }
        parts.push_back(part);
        return parts;
    }

    /// 判断文本是否为合法数组下标。
    static bool is_array_index(const std::string& text) {
        if (text.empty() || (text.size() > 1 && text.front() == '0')) {
            return false;
        }
        for (char ch : text) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return false;
            }
        }
        return true;
    }

    /// 把文本解析为数组下标；非法抛异常。
    static std::size_t parse_index(const std::string& text) {
        if (!is_array_index(text)) {
            throw std::invalid_argument("invalid JSON array index: " + text);
        }
        return static_cast<std::size_t>(std::stoull(text));
    }

    /// 创建指向指定值的 JsonRef（写回父节点）。
    JsonRef make_proxy(yyjson_mut_val* value,
                       yyjson_mut_val* parent,
                       relation_kind relation,
                       std::string key = std::string(),
                       std::size_t index = 0) const;

    /// 把整数下标转为 size_t；负数抛 std::out_of_range。
    template<class Index>
    static std::size_t checked_index(Index index) {
        if (std::is_signed<Index>::value && index < Index(0)) {
            throw std::out_of_range("LJson::Json array index is negative: " +
                                    std::to_string(static_cast<long long>(index)));
        }
        return static_cast<std::size_t>(index);
    }

    /// 创建独立持有该值副本的 Json。
    Json make_detached_copy(yyjson_mut_val* value) const {
        Json out;
        out.init_from_value_copy(value);
        if (out.text_dirty_) {
            *out.text_dirty_ = true;
        }
        return out;
    }

    /// 按 JSON Pointer 定位（可写）；create 为 true 时自动创建缺失节点。
    JsonRef pointer_value(const std::string& path, bool create);

    /// 按 JSON Pointer 定位（只读），返回独立副本。
    Json pointer_value(const std::string& path) const {
        auto parts = parse_pointer(path);
        auto* current = const_value();
        for (const auto& part : parts) {
            if (yyjson_mut_is_arr(current)) {
                if (part == "-") {
                    throw std::out_of_range("LJson::Json array index not found: -");
                }
                current = array_child_const(current, parse_index(part));
            } else {
                current = object_child_const(current, part);
            }
        }
        return make_detached_copy(current);
    }

    /// 深度优先查找键（可写版）；返回 JsonRef 或无效引用。
    JsonRef find_recursive_impl(yyjson_mut_val* value,
                                yyjson_mut_val* parent,
                                relation_kind relation,
                                const std::string& key,
                                std::size_t index,
                                const std::string& target);

    /// 深度优先查找键（只读版）；返回独立副本或无效 Json。
    Json find_recursive_impl(yyjson_mut_val* value, const std::string& target) const {
        if (yyjson_mut_is_obj(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* obj_key = nullptr;
            yyjson_mut_val* obj_value = nullptr;
            yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
                std::string current_key(yyjson_mut_get_str(obj_key),
                                        yyjson_mut_get_len(obj_key));
                if (current_key == target) {
                    return make_detached_copy(obj_value);
                }
            }
            yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
                auto found = find_recursive_impl(obj_value, target);
                if (found.exists()) {
                    return found;
                }
            }
        } else if (yyjson_mut_is_arr(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* item = nullptr;
            yyjson_mut_arr_foreach(value, idx, max, item) {
                auto found = find_recursive_impl(item, target);
                if (found.exists()) {
                    return found;
                }
            }
        }
        Json out;
        out.valid_ = false;
        return out;
    }

    /// 深度优先收集所有匹配 target 键的值（递归实现）。
    void collect_recursive_impl(yyjson_mut_val* value, const std::string& target,
                                std::vector<Json>& out) const {
        if (yyjson_mut_is_obj(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* obj_key = nullptr;
            yyjson_mut_val* obj_value = nullptr;
            yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
                std::string current_key(yyjson_mut_get_str(obj_key),
                                        yyjson_mut_get_len(obj_key));
                if (current_key == target) {
                    out.push_back(make_detached_copy(obj_value));
                }
            }
            yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
                collect_recursive_impl(obj_value, target, out);
            }
        } else if (yyjson_mut_is_arr(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* item = nullptr;
            yyjson_mut_arr_foreach(value, idx, max, item) {
                collect_recursive_impl(item, target, out);
            }
        }
    }

    /// 深度优先删除所有匹配 key 的对象键；返回删除数量。
    static std::size_t erase_recursive_into(yyjson_mut_val* value, const std::string& key) {
        std::size_t count = 0;
        if (yyjson_mut_is_obj(value)) {
            yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(value);
            yyjson_mut_val* obj_key = nullptr;
            while ((obj_key = yyjson_mut_obj_iter_next(&iter))) {
                auto* obj_value = yyjson_mut_obj_iter_get_val(obj_key);
                std::string current_key(yyjson_mut_get_str(obj_key),
                                        yyjson_mut_get_len(obj_key));
                if (current_key == key) {
                    yyjson_mut_obj_iter_remove(&iter);
                    ++count;
                } else {
                    count += erase_recursive_into(obj_value, key);
                }
            }
        } else if (yyjson_mut_is_arr(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* item = nullptr;
            yyjson_mut_arr_foreach(value, idx, max, item) {
                count += erase_recursive_into(item, key);
            }
        }
        return count;
    }

    /// 深度优先遍历每个节点并调用 visitor（可写版，传入 JsonRef）。
    template<class Visitor>
    void for_each_recursive_impl(yyjson_mut_val* value, Visitor&& visitor);

    /// 深度优先遍历每个节点并调用 visitor（只读版，传入独立副本）。
    template<class Visitor>
    void for_each_recursive_impl(yyjson_mut_val* value, Visitor&& visitor) const {
        visitor(make_detached_copy(value));
        if (yyjson_mut_is_obj(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* obj_key = nullptr;
            yyjson_mut_val* obj_value = nullptr;
            yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
                for_each_recursive_impl(obj_value, visitor);
            }
        } else if (yyjson_mut_is_arr(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* item = nullptr;
            yyjson_mut_arr_foreach(value, idx, max, item) {
                for_each_recursive_impl(item, visitor);
            }
        }
    }

public:
    /// 默认构造：惰性 null 值。
    Json() = default;

    /// 复制构造：总是深拷贝值；源为 JsonRef 时同样得到独立副本。
    Json(const Json& other) {
        copy_value_from(other);
    }

    /// 移动构造：接管根节点文档；源为 JsonRef 时退化为深拷贝，避免变成别名。
    ///
    /// 保持 noexcept 以便 std::vector<Json> 扩容走移动路径；JsonRef 源的深拷贝
    /// 仅在内存耗尽时抛出，此时 std::terminate。
    Json(Json&& other) noexcept {
        if (other.proxy_) {
            copy_value_from(other);
            return;
        }
        text_ = std::move(other.text_);
        doc_ = std::move(other.doc_);
        value_ = other.value_;
        parent_ = other.parent_;
        relation_ = other.relation_;
        key_ = std::move(other.key_);
        index_ = other.index_;
        text_dirty_ = std::move(other.text_dirty_);
        array_cache_ = std::move(other.array_cache_);
        valid_ = other.valid_;
        other.value_ = nullptr;
        other.parent_ = nullptr;
        other.valid_ = true;
    }

    /// 复制赋值：JsonRef 写回所引用节点；Json 整体替换为 other 的深拷贝。
    Json& operator=(const Json& other) {
        if (this == &other) {
            return *this;
        }
        if (proxy_) {
            set_value_from(other);
            return *this;
        }
        text_ = other.str();
        if (!other.valid_) {
            doc_.reset();
            value_ = nullptr;
            array_cache_.reset();
            valid_ = false;
            return *this;
        }
        if (other.value_) {
            init_from_value_copy(other.value_);
            if (text_dirty_) {
                *text_dirty_ = false;
            }
        } else {
            doc_.reset();
            value_ = nullptr;
            text_dirty_.reset();
            array_cache_.reset();
        }
        parent_ = nullptr;
        relation_ = relation_kind::root;
        key_.clear();
        index_ = 0;
        proxy_ = false;
        valid_ = other.valid_;
        return *this;
    }

    /// 移动赋值：JsonRef 写回所引用节点；源为 JsonRef 时深拷贝；否则整体接管。
    Json& operator=(Json&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (proxy_) {
            set_value_from(other);
            return *this;
        }
        if (other.proxy_) {
            return *this = static_cast<const Json&>(other);
        }
        text_ = std::move(other.text_);
        doc_ = std::move(other.doc_);
        value_ = other.value_;
        parent_ = other.parent_;
        relation_ = other.relation_;
        key_ = std::move(other.key_);
        index_ = other.index_;
        text_dirty_ = std::move(other.text_dirty_);
        array_cache_ = std::move(other.array_cache_);
        proxy_ = other.proxy_;
        valid_ = other.valid_;
        other.value_ = nullptr;
        other.parent_ = nullptr;
        other.proxy_ = false;
        other.valid_ = true;
        return *this;
    }

    /// 从 C 字符串（JSON 文本）构造。
    Json(const char* text)
        : text_(text ? text : "") {}

    /// 从带长度的缓冲区（JSON 文本）构造。
    Json(const char* text, std::size_t size)
        : text_(text ? std::string(text, size) : std::string()) {
        if (!text && size != 0) {
            throw std::invalid_argument("LJson::Json cannot copy null data");
        }
    }

    /// 从视图（JSON 文本）构造。
    Json(JsonView text)
        : text_(text.data(), text.size()) {}

    /// 从 std::string（JSON 文本）构造。
    Json(std::string text)
        : text_(std::move(text)) {}

    /// 从任意文本缓冲类型构造。
    template<class Text LTOOL_ENABLE_IF(detail::is_json_text_source<Text>::value)>
        LTOOL_REQUIRES(detail::is_json_text_source<Text>::value)
    Json(const Text& text)
        : text_(text.data(), text.size()) {}

#if LJSON_HAS_NLOHMANN_JSON
    /// 从 nlohmann::json 构造（经文本序列化）。
    Json(const nlohmann::json& value)
        : Json(value.dump()) {}

    /// 从右值 nlohmann::json 构造。
    Json(nlohmann::json&& value)
        : Json(value.dump()) {}
#endif

    /// 构造 JSON null。
    Json(std::nullptr_t) {
        init_root([](yyjson_mut_doc* doc) { return yyjson_mut_null(doc); });
    }

    /// 构造 JSON 布尔。
    Json(bool value) {
        init_root([&](yyjson_mut_doc* doc) { return yyjson_mut_bool(doc, value); });
    }

    /// 构造 JSON 整数。
    Json(int value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    /// 构造 JSON 整数。
    Json(unsigned int value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    /// 构造 JSON 整数。
    Json(long value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    /// 构造 JSON 整数。
    Json(unsigned long value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    /// 构造 JSON 整数。
    Json(long long value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    /// 构造 JSON 整数。
    Json(unsigned long long value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    /// 构造 JSON 浮点数。
    Json(double value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    /// 赋值为 JSON null。
    Json& operator=(std::nullptr_t) {
        set_value([](yyjson_mut_doc* doc) { return yyjson_mut_null(doc); });
        return *this;
    }

    /// 赋值为 JSON 布尔。
    Json& operator=(bool value) {
        set_value([&](yyjson_mut_doc* doc) { return yyjson_mut_bool(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 整数。
    Json& operator=(int value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 整数。
    Json& operator=(unsigned int value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 整数。
    Json& operator=(long value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 整数。
    Json& operator=(unsigned long value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 整数。
    Json& operator=(long long value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 整数。
    Json& operator=(unsigned long long value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 浮点数。
    Json& operator=(double value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 字符串。
    Json& operator=(const char* value) {
        set_value([&](yyjson_mut_doc* doc) { return make_string(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 字符串。
    Json& operator=(const std::string& value) {
        set_value([&](yyjson_mut_doc* doc) { return make_string(doc, value); });
        return *this;
    }

    /// 赋值为 JSON 字符串（右值版本）。
    Json& operator=(std::string&& value) {
        set_value([&](yyjson_mut_doc* doc) { return make_string(doc, value); });
        return *this;
    }

#if LJSON_HAS_JSONCPP
    /// 从 jsoncpp Value 构造。
    Json(const ::Json::Value& value)
        : Json(detail::jsoncpp_to_string(value)) {}
#endif

#if LJSON_HAS_SIMDJSON
    /// 从 simdjson dom::element 构造。
    Json(simdjson::dom::element value)
        : Json(simdjson::to_string(value)) {}

    /// 从 simdjson dom::document 构造。
    Json(simdjson::dom::document& value)
        : Json(simdjson::to_string(value.root())) {}
#endif

    /// 从只读 yyjson 文档深拷贝构造。
    Json(yyjson_doc* doc) {
        init_from_doc_copy(doc);
    }

    /// 从只读 yyjson 值深拷贝构造。
    Json(yyjson_val* value) {
        init_from_value_copy(value);
    }

    /// 从可变 yyjson 文档深拷贝构造。
    Json(yyjson_mut_doc* doc) {
        init_from_mut_doc_copy(doc);
    }

    /// 从可变 yyjson 值深拷贝构造。
    Json(yyjson_mut_val* value) {
        init_from_value_copy(value);
    }

    // Best-effort borrowed text. It remains valid only until this Json is
    // modified or destroyed; use to_string() when an owning result is needed.
    /// 返回惰性同步的 JSON 文本引用；修改后可能失效。
    const std::string& str() const noexcept {
        try {
            sync_text();
        } catch (...) {
        }
        return text_;
    }

    /// 返回 NUL 结尾的文本指针。
    const char* c_str() const noexcept {
        return str().c_str();
    }

    /// 返回文本数据指针。
    const char* data() const noexcept {
        return str().data();
    }

    /// 返回文本字节长度。
    std::size_t size() const noexcept {
        return str().size();
    }

    /// 判断文本是否为空。
    bool empty() const noexcept {
        return str().empty();
    }

    /// 返回当前文本的零拷贝视图。
    JsonView view() const noexcept {
        return JsonView(data(), size());
    }

    /// 隐式转换为视图。
    operator JsonView() const noexcept {
        return view();
    }

    /// 隐式转换为底层字符串引用。
    operator const std::string&() const noexcept {
        return str();
    }

    // Returns an owning serialized snapshot and propagates serialization errors.
    /// 返回拥有的序列化快照；序列化错误会抛出。
    std::string to_string() const {
        return write_current();
    }

    /// 创建空 JSON 对象。
    static Json object() {
        Json out;
        out.init_root([](yyjson_mut_doc* doc) { return yyjson_mut_obj(doc); });
        return out;
    }

    /// 创建空 JSON 数组。
    static Json array() {
        Json out;
        out.init_root([](yyjson_mut_doc* doc) { return yyjson_mut_arr(doc); });
        return out;
    }

    /// 解析 JSON 文本为 Json；语法错误抛异常。
    static Json parse(JsonView text) {
        Json out;
        out.parse_text_now(text);
        return out;
    }

    /// 序列化为文本；indent>0 时格式化输出。
    std::string dump(int indent = -1) const {
        if (!value_) {
            return indent > 0 ? detail::format_json_with_indent(text_, indent) : text_;
        }
        return write_current(indent);
    }

#if LJSON_HAS_NLOHMANN_JSON
    /// 转换为 nlohmann::json。
    nlohmann::json to_nlohmann() const {
        return nlohmann::json::parse(to_string());
    }

    /// 隐式转换为 nlohmann::json。
    operator nlohmann::json() const {
        return to_nlohmann();
    }
#endif

    /// 按对象键访问（可写 JsonRef）；键不存在自动创建 null。
    JsonRef operator[](const std::string& key);

    /// 按对象键访问（只读）；键不存在抛异常。
    Json operator[](const std::string& key) const {
        return make_detached_copy(object_child_const(const_value(), key));
    }

    /// 按 C 字符串键访问（可写 JsonRef）。
    JsonRef operator[](const char* key);

    /// 按 C 字符串键访问（只读）。
    Json operator[](const char* key) const {
        return (*this)[std::string(key ? key : "")];
    }

    /// 按数组下标访问（可写 JsonRef）；越界自动用 null 扩充。
    JsonRef operator[](std::size_t index);

    /// 按数组下标访问（只读）；越界抛异常。
    Json operator[](std::size_t index) const {
        return make_detached_copy(array_child_const(const_value(), index));
    }

    /// 按任意整数下标访问（可写 JsonRef）；消除字面量 0 与 const char* 的重载
    /// 歧义，并让 int/unsigned/long 等下标精确匹配。负数抛 std::out_of_range。
    template<class Index LTOOL_ENABLE_IF(std::is_integral<Index>::value &&
                                         !std::is_same<Index, bool>::value &&
                                         !std::is_same<Index, std::size_t>::value)>
        LTOOL_REQUIRES(std::is_integral<Index>::value && !std::is_same<Index, bool>::value &&
                       !std::is_same<Index, std::size_t>::value)
    JsonRef operator[](Index index);

    /// 按任意整数下标访问（只读）；负数或越界抛异常。
    template<class Index LTOOL_ENABLE_IF(std::is_integral<Index>::value &&
                                         !std::is_same<Index, bool>::value &&
                                         !std::is_same<Index, std::size_t>::value)>
        LTOOL_REQUIRES(std::is_integral<Index>::value && !std::is_same<Index, bool>::value &&
                       !std::is_same<Index, std::size_t>::value)
    Json operator[](Index index) const {
        return (*this)[checked_index(index)];
    }

    /// 按对象键访问（边界检查，可写 JsonRef）。
    JsonRef at(const std::string& key);

    /// 按对象键访问（边界检查，只读）。
    Json at(const std::string& key) const {
        return (*this)[key];
    }

    /// 按数组下标访问（边界检查，可写 JsonRef）。
    JsonRef at(std::size_t index);

    /// 按数组下标访问（边界检查，只读）。
    Json at(std::size_t index) const {
        return (*this)[index];
    }

    /// 按 JSON Pointer 访问（可写 JsonRef，不创建缺失节点）。
    JsonRef at_pointer(const std::string& path);

    /// 按 JSON Pointer 访问（只读，不创建）。
    Json at_pointer(const std::string& path) const {
        return pointer_value(path);
    }

    /// 按 JSON Pointer 访问（可写 JsonRef，自动创建缺失节点）。
    JsonRef pointer(const std::string& path);

    /// 按 JSON Pointer 访问（只读）。
    Json pointer(const std::string& path) const {
        return pointer_value(path);
    }

    /// 按 JSON Pointer 设置值（Json 版本）。
    void set_pointer(const std::string& path, Json value);

    /// 按 JSON Pointer 设置值（任意 JSON 可转类型版本）。
    template<class T LTOOL_ENABLE_IF(!detail::is_json_value<T>::value)>
        LTOOL_REQUIRES(!detail::is_json_value<T>::value)
    void set_pointer(const std::string& path, T&& value);

    /// 判断对象是否包含指定键。
    bool contains(const std::string& key) const {
        return object_contains(const_value(), key);
    }

    /// 统计指定键数量（对象场景为 0 或 1）。
    std::size_t count(const std::string& key) const {
        return contains(key) ? 1u : 0u;
    }

    /// 判断 JSON Pointer 是否可达。
    bool contains_pointer(const std::string& path) const {
        try {
            (void)pointer_value(path);
            return true;
        } catch (...) {
            return false;
        }
    }

    /// 读取对象键值；不存在时返回 default_value。
    template<class T>
    T value(const std::string& key, T default_value) const {
        if (!contains(key)) {
            return default_value;
        }
        return at(key).template get_as<T>();
    }

    /// 读取 JSON Pointer 指向的值；不可达时返回 default_value。
    template<class T>
    T value_pointer(const std::string& path, T default_value) const {
        if (!contains_pointer(path)) {
            return default_value;
        }
        return at_pointer(path).template get_as<T>();
    }

    /// 删除对象键；返回是否删除成功。
    std::size_t erase(const std::string& key) {
        auto* value = mutable_value();
        if (!yyjson_mut_is_obj(value)) {
            return 0;
        }
        auto* removed = yyjson_mut_obj_remove_keyn(value, key.data(), key.size());
        if (removed) {
            clear_array_cache();
            mark_dirty();
            return 1;
        }
        return 0;
    }

    /// 删除数组元素；越界抛异常。
    void erase(std::size_t index) {
        auto* value = mutable_value();
        if (!yyjson_mut_is_arr(value) || index >= yyjson_mut_arr_size(value)) {
            throw std::out_of_range("LJson::Json array index out of range");
        }
        clear_array_cache();
        yyjson_mut_arr_remove(value, index);
        mark_dirty();
    }

    /// 按 JSON Pointer 删除节点；返回是否删除成功。
    bool erase_pointer(const std::string& path) {
        auto parts = parse_pointer(path);
        if (parts.empty()) {
            *this = nullptr;
            return true;
        }

        // 先定位父节点。
        auto* current = mutable_value();
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            try {
                if (yyjson_mut_is_arr(current)) {
                    current = array_child_const(current, parse_index(parts[i]));
                } else {
                    current = object_child_const(current, parts[i]);
                }
            } catch (...) {
                return false;
            }
        }

        // 删除叶子节点。
        const auto& leaf = parts.back();
        if (yyjson_mut_is_arr(current)) {
            if (!is_array_index(leaf)) {
                return false;
            }
            const auto index = parse_index(leaf);
            if (index >= yyjson_mut_arr_size(current)) {
                return false;
            }
            clear_array_cache();
            yyjson_mut_arr_remove(current, index);
            mark_dirty();
            return true;
        }
        if (yyjson_mut_is_obj(current)) {
            auto* removed = yyjson_mut_obj_remove_keyn(current, leaf.data(), leaf.size());
            if (removed) {
                clear_array_cache();
                mark_dirty();
                return true;
            }
        }
        return false;
    }

    /// 清空当前值：数组/对象清空元素，字符串置空，布尔置 false，数字置 0。
    void clear() {
        auto* value = mutable_value();
        clear_array_cache();
        if (yyjson_mut_is_arr(value)) {
            yyjson_mut_arr_clear(value);
        } else if (yyjson_mut_is_obj(value)) {
            yyjson_mut_obj_clear(value);
        } else if (yyjson_mut_is_str(value)) {
            replace_current(require_value(yyjson_mut_strncpy(doc_.get(), "", 0)));
        } else if (yyjson_mut_is_bool(value)) {
            replace_current(require_value(yyjson_mut_false(doc_.get())));
        } else if (yyjson_mut_is_num(value)) {
            replace_current(require_value(yyjson_mut_sint(doc_.get(), 0)));
        }
        mark_dirty();
    }

    /// 向数组追加一个 Json（深拷贝其值）。
    void push_back(Json value) {
        auto* target = ensure_array_value();
        auto* copied = require_value(yyjson_mut_val_mut_copy(doc_.get(), value.const_value()));
        clear_array_cache();
        if (!yyjson_mut_arr_append(target, copied)) {
            throw std::runtime_error("yyjson failed to append array value");
        }
        mark_dirty();
    }

    /// 向数组追加任意 JSON 可转类型值。
    template<class T LTOOL_ENABLE_IF(!detail::is_json_value<T>::value)>
        LTOOL_REQUIRES(!detail::is_json_value<T>::value)
    void push_back(T&& value) {
        auto* target = ensure_array_value();
        auto* item = require_value(make_json_value(std::forward<T>(value)));
        clear_array_cache();
        if (!yyjson_mut_arr_append(target, item)) {
            throw std::runtime_error("yyjson failed to append array value");
        }
        mark_dirty();
    }

    /// 就地构造并追加一个元素，返回其 JsonRef。
    template<class... Args>
    JsonRef emplace_back(Args&&... args);

    /// 用另一个对象的键值合并覆盖当前对象。
    void update(Json other) {
        auto* target = ensure_object_value();
        auto* source = other.const_value();
        if (!yyjson_mut_is_obj(source)) {
            throw std::domain_error("LJson::Json update requires objects");
        }

        std::size_t idx = 0;
        std::size_t max = 0;
        yyjson_mut_val* key = nullptr;
        yyjson_mut_val* value = nullptr;
        yyjson_mut_obj_foreach(source, idx, max, key, value) {
            std::string key_text(yyjson_mut_get_str(key), yyjson_mut_get_len(key));
            auto* copied_key = require_value(yyjson_mut_strncpy(doc_.get(),
                                                                key_text.data(),
                                                                key_text.size()));
            auto* copied_value = require_value(yyjson_mut_val_mut_copy(doc_.get(), value));
            if (!yyjson_mut_obj_replace(target, copied_key, copied_value)) {
                if (!yyjson_mut_obj_add(target, copied_key, copied_value)) {
                    throw std::runtime_error("yyjson failed to update object");
                }
            }
        }
        clear_array_cache();
        mark_dirty();
    }

    /// 返回数组/对象/字符串的元素数量；标量返回 0。
    std::size_t json_size() const {
        auto* value = const_value();
        if (yyjson_mut_is_arr(value)) {
            return yyjson_mut_arr_size(value);
        }
        if (yyjson_mut_is_obj(value)) {
            return yyjson_mut_obj_size(value);
        }
        if (yyjson_mut_is_str(value)) {
            return yyjson_mut_get_len(value);
        }
        return 0;
    }

    /// 判断是否为 JSON null。
    bool is_null() const {
        return yyjson_mut_is_null(const_value());
    }

    /// 判断是否为 JSON 布尔。
    bool is_boolean() const {
        return yyjson_mut_is_bool(const_value());
    }

    /// 判断是否为 JSON 数字。
    bool is_number() const {
        return yyjson_mut_is_num(const_value());
    }

    /// 判断是否为 JSON 字符串。
    bool is_string() const {
        return yyjson_mut_is_str(const_value());
    }

    /// 判断是否为 JSON 数组。
    bool is_array() const {
        return yyjson_mut_is_arr(const_value());
    }

    /// 判断是否为 JSON 对象。
    bool is_object() const {
        return yyjson_mut_is_obj(const_value());
    }

    /// 类型化取值；支持 string/bool/整型/浮点，类型不符抛异常。
    template<class T>
    T get_as() const {
        auto* value = const_value();
        if constexpr (std::is_same<T, std::string>::value) {
            if (!yyjson_mut_is_str(value)) {
                throw std::domain_error("LJson::Json value is not a string");
            }
            return std::string(yyjson_mut_get_str(value), yyjson_mut_get_len(value));
        } else if constexpr (std::is_same<T, bool>::value) {
            if (!yyjson_mut_is_bool(value)) {
                throw std::domain_error("LJson::Json value is not a boolean");
            }
            return yyjson_mut_get_bool(value);
        } else if constexpr (std::is_integral<T>::value) {
            if (!yyjson_mut_is_num(value)) {
                throw std::domain_error("LJson::Json value is not a number");
            }
            if constexpr (std::is_unsigned<T>::value) {
                if (yyjson_mut_is_uint(value)) {
                    return checked_uint_cast<T>(yyjson_mut_get_uint(value));
                }
                if (yyjson_mut_is_int(value)) {
                    return checked_sint_cast<T>(yyjson_mut_get_sint(value));
                }
                return checked_real_cast<T>(yyjson_mut_get_num(value));
            } else {
                if (yyjson_mut_is_int(value)) {
                    return checked_sint_cast<T>(yyjson_mut_get_sint(value));
                }
                if (yyjson_mut_is_uint(value)) {
                    return checked_uint_cast<T>(yyjson_mut_get_uint(value));
                }
                return checked_real_cast<T>(yyjson_mut_get_num(value));
            }
        } else if constexpr (std::is_floating_point<T>::value) {
            if (!yyjson_mut_is_num(value)) {
                throw std::domain_error("LJson::Json value is not a number");
            }
            return static_cast<T>(yyjson_mut_get_num(value));
        } else {
            static_assert(detail::dependent_false<T>::value,
                          "LJson::Json::get_as<T> supports string, bool, and numeric types");
        }
    }

    /// 取为 std::string。
    std::string as_string() const {
        return get_as<std::string>();
    }

    /// 取为 int。
    int as_int() const {
        return get_as<int>();
    }

    /// 取为 long long。
    long long as_i64() const {
        return get_as<long long>();
    }

    /// 取为 double。
    double as_double() const {
        return get_as<double>();
    }

    /// 取为 bool。
    bool as_bool() const {
        return get_as<bool>();
    }

    /// 判断该 Json 是否引用有效值（无效时访问会抛异常）。
    bool exists() const noexcept {
        return valid_;
    }

    /// 深度优先查找第一个匹配键（可写版，返回 JsonRef）。
    JsonRef find_recursive(const std::string& key);

    /// 深度优先查找第一个匹配键（只读版）。
    Json find_recursive(const std::string& key) const {
        return find_recursive_impl(const_value(), key);
    }

    /// 深度优先收集所有匹配键（可写版）。
    std::vector<Json> find_all_recursive(const std::string& key) {
        std::vector<Json> out;
        collect_recursive_impl(mutable_value(), key, out);
        return out;
    }

    /// 深度优先收集所有匹配键（只读版）。
    std::vector<Json> find_all_recursive(const std::string& key) const {
        std::vector<Json> out;
        collect_recursive_impl(const_value(), key, out);
        return out;
    }

    /// 深度优先删除所有匹配键；返回删除数量。
    std::size_t erase_recursive(const std::string& key) {
        auto count = erase_recursive_into(mutable_value(), key);
        if (count != 0) {
            clear_array_cache();
            mark_dirty();
        }
        return count;
    }

    /// 深度优先遍历每个节点（可写版，visitor 收到 JsonRef）。
    template<class Visitor>
    void for_each_recursive(Visitor&& visitor) {
        for_each_recursive_impl(mutable_value(), std::forward<Visitor>(visitor));
    }

    /// 深度优先遍历每个节点（只读版，visitor 收到独立副本）。
    template<class Visitor>
    void for_each_recursive(Visitor&& visitor) const {
        for_each_recursive_impl(const_value(), std::forward<Visitor>(visitor));
    }

    /// 两个 Json 深度相等比较。
    friend bool operator==(const Json& lhs, const Json& rhs) {
        return yyjson_mut_equals(lhs.const_value(), rhs.const_value());
    }

    /// 两个 Json 深度不等比较。
    friend bool operator!=(const Json& lhs, const Json& rhs) {
        return !(lhs == rhs);
    }

    /// 与 C 字符串比较（仅当为字符串时）。
    friend bool operator==(const Json& lhs, const char* rhs) {
        return lhs.is_string() && lhs.as_string() == (rhs ? rhs : "");
    }

    /// 与 C 字符串比较（反向）。
    friend bool operator==(const char* lhs, const Json& rhs) {
        return rhs == lhs;
    }

    /// 与 C 字符串不等比较。
    friend bool operator!=(const Json& lhs, const char* rhs) {
        return !(lhs == rhs);
    }

    /// 与 C 字符串不等比较（反向）。
    friend bool operator!=(const char* lhs, const Json& rhs) {
        return !(rhs == lhs);
    }

    /// 与 std::string 比较。
    friend bool operator==(const Json& lhs, const std::string& rhs) {
        return lhs.is_string() && lhs.as_string() == rhs;
    }

    /// 与 std::string 比较（反向）。
    friend bool operator==(const std::string& lhs, const Json& rhs) {
        return rhs == lhs;
    }

    /// 与 std::string 不等比较。
    friend bool operator!=(const Json& lhs, const std::string& rhs) {
        return !(lhs == rhs);
    }

    /// 与 std::string 不等比较（反向）。
    friend bool operator!=(const std::string& lhs, const Json& rhs) {
        return !(rhs == lhs);
    }

    /// 与算术值比较。
    template<class T
             LTOOL_ENABLE_IF(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value)>
        LTOOL_REQUIRES(LTool::concepts::Arithmetic<T> && !std::same_as<T, bool>)
    friend bool operator==(const Json& lhs, T rhs) {
        if (!lhs.is_number()) {
            return false;
        }
        auto* value = lhs.const_value();
        if constexpr (std::is_integral<T>::value) {
            if (yyjson_mut_is_int(value)) {
                const auto number = yyjson_mut_get_sint(value);
                if constexpr (std::is_signed<T>::value) {
                    return number == static_cast<std::int64_t>(rhs);
                } else {
                    return number >= 0 && static_cast<std::uint64_t>(number) ==
                                              static_cast<std::uint64_t>(rhs);
                }
            }
            if (yyjson_mut_is_uint(value)) {
                const auto number = yyjson_mut_get_uint(value);
                if constexpr (std::is_signed<T>::value) {
                    return rhs >= 0 && number == static_cast<std::uint64_t>(rhs);
                } else {
                    return number == static_cast<std::uint64_t>(rhs);
                }
            }
        }
        return lhs.as_double() == static_cast<double>(rhs);
    }

    /// 与算术值比较（反向）。
    template<class T
             LTOOL_ENABLE_IF(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value)>
        LTOOL_REQUIRES(LTool::concepts::Arithmetic<T> && !std::same_as<T, bool>)
    friend bool operator==(T lhs, const Json& rhs) {
        return rhs == lhs;
    }

    /// 与算术值不等比较。
    template<class T
             LTOOL_ENABLE_IF(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value)>
        LTOOL_REQUIRES(LTool::concepts::Arithmetic<T> && !std::same_as<T, bool>)
    friend bool operator!=(const Json& lhs, T rhs) {
        return !(lhs == rhs);
    }

    /// 与算术值不等比较（反向）。
    template<class T
             LTOOL_ENABLE_IF(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value)>
        LTOOL_REQUIRES(LTool::concepts::Arithmetic<T> && !std::same_as<T, bool>)
    friend bool operator!=(T lhs, const Json& rhs) {
        return !(rhs == lhs);
    }

    /// 与 bool 比较。
    friend bool operator==(const Json& lhs, bool rhs) {
        return lhs.is_boolean() && lhs.as_bool() == rhs;
    }

    /// 与 bool 比较（反向）。
    friend bool operator==(bool lhs, const Json& rhs) {
        return rhs == lhs;
    }

    /// 与 bool 不等比较。
    friend bool operator!=(const Json& lhs, bool rhs) {
        return !(lhs == rhs);
    }

    /// 与 bool 不等比较（反向）。
    friend bool operator!=(bool lhs, const Json& rhs) {
        return !(rhs == lhs);
    }

#if LJSON_HAS_RFL_JSON
    /// 用反射把结构体序列化为 Json。
    template<class T>
    static Json from(const T& value) {
        return Json(rfl::json::write(value));
    }

    /// 用反射把当前 Json 反序列化为结构体，返回 rfl 结果对象。
    template<class T>
    auto read() const -> decltype(rfl::json::read<T>(std::declval<const std::string&>())) {
        return rfl::json::read<T>(str());
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
    static Json from(const T&) {
        static_assert(detail::dependent_false<T>::value,
                      "LJson::Json::from<T> requires C++20 and reflect-cpp rfl/json");
        return Json();
    }

    /// 反射反序列化；缺少依赖时编译期报错。
    template<class T>
    T read() const {
        static_assert(detail::dependent_false<T>::value,
                      "LJson::Json::read<T> requires C++20 and reflect-cpp rfl/json");
        return T();
    }

    /// 反射反序列化；缺少依赖时编译期报错。
    template<class T>
    T to() const {
        static_assert(detail::dependent_false<T>::value,
                      "LJson::Json::to<T> requires C++20 and reflect-cpp rfl/json");
        return T();
    }

    /// to() 的同义函数。
    template<class T>
    T to_or_throw() const {
        return to<T>();
    }
#endif
};

/**
 * @brief Json 子节点的可写引用。
 *
 * 由可写访问接口（operator[]、at()、at_pointer()、pointer()、find_recursive()、
 * emplace_back()、for_each_recursive()）返回，读写都作用于所属文档：
 * @code
 * auto ref = root["name"];       // JsonRef：引用 root 中的节点
 * ref = "ltool";                 // 写回 root
 * LJson::Json copy = root["x"];  // Json：独立副本，修改不影响 root
 * @endcode
 *
 * 复制 JsonRef 得到指向同一节点的另一个引用；对 JsonRef 赋值总是写入值，
 * 从不改变引用目标。JsonRef 不延长父节点的有效性：父节点被整体替换后，
 * 旧引用不再反映文档内容，不应长期保存。
 */
class JsonRef : public Json {
public:
    /// 复制引用：指向同一节点，不复制值。
    JsonRef(const JsonRef& other)
        : Json(other, share_tag{}) {}

    /// 移动引用：指向同一节点。
    JsonRef(JsonRef&& other)
        : Json(other, share_tag{}) {}

    /// 把 other 引用的值写入当前节点。
    JsonRef& operator=(const JsonRef& other) {
        Json::operator=(static_cast<const Json&>(other));
        return *this;
    }

    /// 把 other 引用的值写入当前节点。
    JsonRef& operator=(JsonRef&& other) {
        Json::operator=(static_cast<const Json&>(other));
        return *this;
    }

    /// 继承 Json 的全部赋值重载（标量、字符串、Json），均写回所引用节点。
    using Json::operator=;

private:
    friend class Json;

    /// 共享构造：仅供 Json 内部创建引用。
    JsonRef(const Json& source, share_tag tag)
        : Json(source, tag) {}
};

/// 创建指向指定值的 JsonRef（写回父节点）。
inline JsonRef Json::make_proxy(yyjson_mut_val* value,
                                yyjson_mut_val* parent,
                                relation_kind relation,
                                std::string key,
                                std::size_t index) const {
    Json proxy(doc_, value, parent, relation, std::move(key), index,
               text_dirty_, array_cache_, true);
    return JsonRef(proxy, share_tag{});
}

/// 按 JSON Pointer 定位（可写）；create 为 true 时自动创建缺失节点。
inline JsonRef Json::pointer_value(const std::string& path, bool create) {
    auto parts = parse_pointer(path);
    auto* current = mutable_value();
    auto* parent = parent_;
    auto relation = relation_;
    std::string key = key_;
    std::size_t index = index_;

    // 逐段下钻：数组用下标（"-" 表示追加），对象用键。
    for (const auto& part : parts) {
        if (yyjson_mut_is_arr(current)) {
            parent = current;
            relation = relation_kind::array;
            if (part == "-") {
                if (!create) {
                    throw std::out_of_range("LJson::Json array index not found: -");
                }
                clear_array_cache();
                auto* item = require_value(yyjson_mut_null(doc_.get()));
                if (!yyjson_mut_arr_append(current, item)) {
                    throw std::runtime_error("yyjson failed to append array value");
                }
                current = item;
                index = yyjson_mut_arr_size(parent) - 1;
            } else {
                index = parse_index(part);
                current = create ? array_child(current, index)
                                 : array_child_const(current, index);
            }
            key.clear();
        } else if (create) {
            // 创建模式下 null 自动转对象。
            if (yyjson_mut_is_null(current)) {
                if (relation == relation_kind::root) {
                    replace_current(require_value(yyjson_mut_obj(doc_.get())));
                    current = value_;
                } else {
                    auto* obj = require_value(yyjson_mut_obj(doc_.get()));
                    JsonRef holder = make_proxy(current, parent, relation, key, index);
                    holder.replace_current(obj);
                    current = holder.value_;
                }
            }
            if (!yyjson_mut_is_obj(current)) {
                throw std::domain_error("LJson::Json value is not an object");
            }
            parent = current;
            relation = relation_kind::object;
            key = part;
            current = object_child(doc_.get(), current, part);
        } else {
            parent = current;
            relation = relation_kind::object;
            key = part;
            current = object_child_const(current, part);
        }
    }

    return make_proxy(current, parent, relation, key, index);
}


/// 深度优先查找键（可写版）；返回 JsonRef 或无效引用。
inline JsonRef Json::find_recursive_impl(yyjson_mut_val* value,
                                        yyjson_mut_val* parent,
                                        relation_kind relation,
                                        const std::string& key,
                                        std::size_t index,
                                        const std::string& target) {
    if (yyjson_mut_is_obj(value)) {
        // 先查当前对象直接键，再递归子对象。
        std::size_t idx = 0;
        std::size_t max = 0;
        yyjson_mut_val* obj_key = nullptr;
        yyjson_mut_val* obj_value = nullptr;
        yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
            std::string current_key(yyjson_mut_get_str(obj_key),
                                    yyjson_mut_get_len(obj_key));
            if (current_key == target) {
                return make_proxy(obj_value, value, relation_kind::object, current_key, 0);
            }
        }
        yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
            std::string current_key(yyjson_mut_get_str(obj_key),
                                    yyjson_mut_get_len(obj_key));
            auto found = find_recursive_impl(obj_value, value, relation_kind::object,
                                             current_key, 0, target);
            if (found.exists()) {
                return found;
            }
        }
    } else if (yyjson_mut_is_arr(value)) {
        std::size_t idx = 0;
        std::size_t max = 0;
        yyjson_mut_val* item = nullptr;
        yyjson_mut_arr_foreach(value, idx, max, item) {
            auto found = find_recursive_impl(item, value, relation_kind::array,
                                             std::string(), idx, target);
            if (found.exists()) {
                return found;
            }
        }
    }
    LTOOL_UNUSED(parent);
    LTOOL_UNUSED(relation);
    LTOOL_UNUSED(key);
    LTOOL_UNUSED(index);
    Json out;
    out.valid_ = false;
    return JsonRef(out, share_tag{});
}


/// 深度优先遍历每个节点并调用 visitor（可写版，传入 JsonRef）。
template<class Visitor>
inline void Json::for_each_recursive_impl(yyjson_mut_val* value, Visitor&& visitor) {
    visitor(make_proxy(value, parent_, relation_, key_, index_));
    if (yyjson_mut_is_obj(value)) {
        std::size_t idx = 0;
        std::size_t max = 0;
        yyjson_mut_val* obj_key = nullptr;
        yyjson_mut_val* obj_value = nullptr;
        yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
            JsonRef child = make_proxy(obj_value, value, relation_kind::object,
                                       std::string(yyjson_mut_get_str(obj_key),
                                                yyjson_mut_get_len(obj_key)),
                                    0);
            child.for_each_recursive_impl(obj_value, visitor);
        }
    } else if (yyjson_mut_is_arr(value)) {
        std::size_t idx = 0;
        std::size_t max = 0;
        yyjson_mut_val* item = nullptr;
        yyjson_mut_arr_foreach(value, idx, max, item) {
            JsonRef child = make_proxy(item, value, relation_kind::array, std::string(), idx);
            child.for_each_recursive_impl(item, visitor);
        }
    }
}


inline JsonRef Json::operator[](const std::string& key) {
    auto* object = ensure_object_value();
    auto* child = object_child(doc_.get(), object, key);
    return make_proxy(child, object, relation_kind::object, key, 0);
}

inline JsonRef Json::operator[](const char* key) {
    return (*this)[std::string(key ? key : "")];
}

inline JsonRef Json::operator[](std::size_t index) {
    auto* array = ensure_array_value();
    auto* child = array_child(array, index);
    return make_proxy(child, array, relation_kind::array, std::string(), index);
}

#if LTOOL_HAS_CONCEPTS
template<class Index>
    requires(std::is_integral<Index>::value && !std::is_same<Index, bool>::value &&
             !std::is_same<Index, std::size_t>::value)
#else
template<class Index,
         typename std::enable_if<(std::is_integral<Index>::value &&
                                  !std::is_same<Index, bool>::value &&
                                  !std::is_same<Index, std::size_t>::value),
                                 int>::type>
#endif
inline JsonRef Json::operator[](Index index) {
    return (*this)[checked_index(index)];
}

inline JsonRef Json::at(const std::string& key) {
    auto* value = const_value();
    auto* child = object_child_const(value, key);
    return make_proxy(child, value, relation_kind::object, key, 0);
}

inline JsonRef Json::at(std::size_t index) {
    auto* value = const_value();
    auto* child = array_child_const(value, index);
    return make_proxy(child, value, relation_kind::array, std::string(), index);
}

inline JsonRef Json::at_pointer(const std::string& path) {
    return pointer_value(path, false);
}

inline JsonRef Json::pointer(const std::string& path) {
    return pointer_value(path, true);
}

inline void Json::set_pointer(const std::string& path, Json value) {
    auto target = pointer_value(path, true);
    target.set_value_from(value);
}

#if LTOOL_HAS_CONCEPTS
template<class T>
#else
template<class T, typename std::enable_if<(!detail::is_json_value<T>::value), int>::type>
#endif
    LTOOL_REQUIRES(!detail::is_json_value<T>::value)
inline void Json::set_pointer(const std::string& path, T&& value) {
    auto target = pointer_value(path, true);
    target.replace_current(require_value(target.make_json_value(std::forward<T>(value))));
}

/// 就地构造并追加一个元素，返回其 JsonRef。
template<class... Args>
inline JsonRef Json::emplace_back(Args&&... args) {
    push_back(Json(std::forward<Args>(args)...));
    auto* target = ensure_array_value();
    const auto index = yyjson_mut_arr_size(target) - 1;
    return make_proxy(yyjson_mut_arr_get_last(target), target,
                      relation_kind::array, std::string(), index);
}


inline JsonRef Json::find_recursive(const std::string& key) {
    return find_recursive_impl(mutable_value(), parent_, relation_, key_, index_, key);
}

/// 从 NUL 结尾字符串构造视图。
inline JsonView view(const char* text) {
    return JsonView(text);
}

/// 从 std::string 构造视图。
inline JsonView view(const std::string& text) {
    return JsonView(text);
}

/// 从 Json 持有对象构造视图。
inline JsonView view(const Json& text) noexcept {
    return text.view();
}

/// 从视图构造持有文本的 Json。
inline Json text(JsonView value) {
    return Json(value);
}

/// 返回当前实际使用的 JSON 后端。
inline Backend active_backend() noexcept {
#if LJSON_HAS_RFL_JSON
    return Backend::rfl_json;
#elif LJSON_HAS_YYJSON
    return Backend::yyjson;
#elif LJSON_HAS_SIMDJSON
    return Backend::simdjson;
#elif LJSON_HAS_NLOHMANN_JSON
    return Backend::nlohmann_json;
#elif LJSON_HAS_JSONCPP
    return Backend::jsoncpp;
#else
    return Backend::text;
#endif
}

inline const char* backend_name(Backend value) noexcept {
    switch (value) {
    case Backend::text:
        return "text";
    case Backend::nlohmann_json:
        return "nlohmann/json";
    case Backend::jsoncpp:
        return "jsoncpp";
    case Backend::simdjson:
        return "simdjson";
    case Backend::yyjson:
        return LJSON_HAS_RFL_YYJSON ? "pkgs/rfl/thirdparty/yyjson" : "yyjson";
    case Backend::rfl_json:
        return "pkgs/rfl/json";
    case Backend::none:
    default:
        return "none";
    }
}

/// 返回当前实际后端的显示名称。
inline const char* backend_name() noexcept {
    return backend_name(active_backend());
}

/// 判断当前是否启用了静态反射后端。
inline bool has_static_reflection() noexcept {
    return LJSON_HAS_STATIC_REFLECTION != 0;
}

/// 把视图内容转为字符串。
inline std::string to_string(JsonView value) {
    return value.str();
}

/// 把 Json 持有对象序列化为字符串。
inline std::string to_string(const Json& value) {
    return value.str();
}

/// 解析 JSON 文本为 Json DOM；等价于 Json::parse()。
inline Json parse(JsonView text) {
    return Json::parse(text);
}

/// 从文件读取 JSON 文本并解析为 Json DOM。
inline Json from_file(const std::string& path) {
    return Json::parse(detail::read_file_text(path));
}

/// 把 Json DOM 序列化后写入文件。
inline void to_file(const std::string& path, const Json& value) {
    detail::write_file_text(path, value.to_string());
}

#if LJSON_HAS_NLOHMANN_JSON

/// 用 nlohmann 解析 JSON 文本。
inline nlohmann::json parse_nlohmann(JsonView text) {
    return nlohmann::json::parse(text.data(), text.data() + text.size());
}

/// parse_nlohmann 的别名。
inline nlohmann::json to_nlohmann(JsonView text) {
    return parse_nlohmann(text);
}

/// 把 Json 转换为 nlohmann::json。
inline nlohmann::json to_nlohmann(const Json& value) {
    return value.to_nlohmann();
}

/// 把 nlohmann::json 序列化为文本。
inline std::string to_string(const nlohmann::json& value) {
    return value.dump();
}

#endif

#if LJSON_HAS_JSONCPP

/// 用 jsoncpp 解析 JSON 文本；解析失败抛异常。
inline ::Json::Value parse_jsoncpp(JsonView text) {
    ::Json::CharReaderBuilder builder;
    ::Json::Value root;
    std::string errors;
    std::unique_ptr<::Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors)) {
        throw std::runtime_error(errors.empty() ? "JsonCpp failed to parse JSON" : errors);
    }
    return root;
}

/// parse_jsoncpp 的兼容别名（cppjson 命名）。
inline ::Json::Value parse_cppjson(JsonView text) {
    return parse_jsoncpp(text);
}

/// parse_jsoncpp 的别名。
inline ::Json::Value to_jsoncpp(JsonView text) {
    return parse_jsoncpp(text);
}

/// parse_jsoncpp 的兼容别名（cppjson 命名）。
inline ::Json::Value to_cppjson(JsonView text) {
    return parse_jsoncpp(text);
}

/// 把 jsoncpp Value 序列化为文本。
inline std::string to_string(const ::Json::Value& value) {
    return detail::jsoncpp_to_string(value);
}

#endif

#if LJSON_HAS_SIMDJSON

/**
 * @brief 持有 simdjson 解析结果的生命周期包装。
 *
 * simdjson 的 dom::element 依赖 parser 的缓冲区存活；此类统一持有两者，
 * 避免悬垂引用。
 */
class simdjson_document {
private:
    std::unique_ptr<simdjson::dom::parser> parser_; ///< 底层解析器（持有缓冲区）。
    simdjson::dom::element root_;                   ///< 解析得到的根元素。

public:
    /// 创建空解析器。
    simdjson_document()
        : parser_(new simdjson::dom::parser()) {}

    /// 解析 JSON 文本；解析失败抛异常。
    explicit simdjson_document(JsonView text)
        : parser_(new simdjson::dom::parser()) {
        auto result = parser_->parse(text.data(), text.size());
        if (result.error()) {
            throw simdjson::simdjson_error(result.error());
        }
        root_ = result.value();
    }

    /// 可移动。
    simdjson_document(simdjson_document&&) noexcept = default;
    simdjson_document& operator=(simdjson_document&&) noexcept = default;

    /// 禁止复制。
    simdjson_document(const simdjson_document&) = delete;
    simdjson_document& operator=(const simdjson_document&) = delete;

    /// 返回根元素。
    simdjson::dom::element root() const noexcept {
        return root_;
    }

    /// 转换为 LJson::Json。
    Json text() const {
        return Json(simdjson::to_string(root_));
    }

    /// 隐式转换为 LJson::Json。
    operator Json() const {
        return text();
    }
};

/// 用 simdjson 解析 JSON 文本。
inline simdjson_document parse_simdjson(JsonView text) {
    return simdjson_document(text);
}

/// 序列化 simdjson 元素。
inline std::string to_string(simdjson::dom::element value) {
    return simdjson::to_string(value);
}

/// 序列化 simdjson 文档。
inline std::string to_string(simdjson::dom::document& value) {
    return simdjson::to_string(value.root());
}

/// 序列化 simdjson_document。
inline std::string to_string(const simdjson_document& value) {
    return value.text().str();
}

#endif

#if LJSON_HAS_YYJSON

/**
 * @brief 持有 yyjson 只读文档的生命周期包装。
 *
 * 负责 yyjson_doc 的创建与释放；doc_ 所有权由本对象独占。
 */
class yyjson_document {
private:
    yyjson_doc* doc_ = nullptr; ///< 底层只读文档。

public:
    /// 空文档。
    yyjson_document() = default;

    /// 解析 JSON 文本；解析失败抛异常。
    explicit yyjson_document(JsonView text, yyjson_read_flag flags = YYJSON_READ_NOFLAG) {
        yyjson_read_err err;
        doc_ = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), flags, nullptr, &err);
        if (!doc_) {
            throw std::runtime_error(err.msg ? err.msg : "yyjson failed to parse JSON");
        }
    }

    /// 接管外部已创建的文档所有权。
    explicit yyjson_document(yyjson_doc* doc) noexcept
        : doc_(doc) {}

    /// 析构时释放文档。
    ~yyjson_document() {
        reset();
    }

    /// 移动构造。
    yyjson_document(yyjson_document&& other) noexcept
        : doc_(other.doc_) {
        other.doc_ = nullptr;
    }

    /// 移动赋值。
    yyjson_document& operator=(yyjson_document&& other) noexcept {
        if (this != &other) {
            reset();
            doc_ = other.doc_;
            other.doc_ = nullptr;
        }
        return *this;
    }

    /// 禁止复制。
    yyjson_document(const yyjson_document&) = delete;
    yyjson_document& operator=(const yyjson_document&) = delete;

    /// 返回底层文档指针。
    yyjson_doc* get() const noexcept {
        return doc_;
    }

    /// 返回根值节点。
    yyjson_val* root() const noexcept {
        return doc_ ? yyjson_doc_get_root(doc_) : nullptr;
    }

    /// 释放所有权并返回文档指针（调用方负责释放）。
    yyjson_doc* release() noexcept {
        yyjson_doc* out = doc_;
        doc_ = nullptr;
        return out;
    }

    /// 释放旧文档并接管新文档。
    void reset(yyjson_doc* doc = nullptr) noexcept {
        if (doc_) {
            yyjson_doc_free(doc_);
        }
        doc_ = doc;
    }

    /// 转换为 LJson::Json。
    Json text() const {
        return Json(detail::yyjson_to_string(doc_));
    }

    /// 隐式转换为 LJson::Json。
    operator Json() const {
        return text();
    }
};

/// 用 yyjson 解析 JSON 文本。
inline yyjson_document parse_yyjson(JsonView text,
                                    yyjson_read_flag flags = YYJSON_READ_NOFLAG) {
    return yyjson_document(text, flags);
}

/// 序列化只读文档。
inline std::string to_string(yyjson_doc* doc,
                             yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    return detail::yyjson_to_string(doc, flags);
}

/// 序列化只读值。
inline std::string to_string(yyjson_val* value,
                             yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    return detail::yyjson_to_string(value, flags);
}

/// 序列化可变文档。
inline std::string to_string(yyjson_mut_doc* doc,
                             yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    return detail::yyjson_to_string(doc, flags);
}

/// 序列化可变值。
inline std::string to_string(yyjson_mut_val* value,
                             yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    return detail::yyjson_to_string(value, flags);
}

/// 序列化 yyjson_document。
inline std::string to_string(const yyjson_document& value) {
    return to_string(value.get());
}

#endif

#if LJSON_HAS_RFL_JSON

/// 用反射把结构体序列化为 JSON 文本。
template<class T>
inline std::string write(const T& value) {
    return rfl::json::write(value);
}

/// 用反射把结构体序列化为 Json。
template<class T>
inline Json from(const T& value) {
    return Json::from(value);
}

/// 从 std::string 反序列化为结构体，返回 rfl 结果对象。
template<class T>
inline auto read(const std::string& text) -> decltype(rfl::json::read<T>(text)) {
    return rfl::json::read<T>(text);
}

/// 从视图反序列化为结构体，返回 rfl 结果对象。
template<class T>
inline auto read(JsonView text) -> decltype(rfl::json::read<T>(std::string())) {
    return rfl::json::read<T>(text.str());
}

/// 从 Json 持有对象反序列化为结构体，返回 rfl 结果对象。
template<class T>
inline auto read(const Json& text) -> decltype(rfl::json::read<T>(text.str())) {
    return rfl::json::read<T>(text.str());
}

/// 从 NUL 结尾字符串反序列化为结构体，返回 rfl 结果对象。
template<class T>
inline auto read(const char* text) -> decltype(rfl::json::read<T>(std::string())) {
    return rfl::json::read<T>(std::string(text ? text : ""));
}

/// 从 std::string 反序列化为结构体；失败时抛出异常。
template<class T>
inline T read_or_throw(const std::string& text) {
    return read<T>(text).value();
}

/// 从视图反序列化为结构体；失败时抛出异常。
template<class T>
inline T read_or_throw(JsonView text) {
    return read<T>(text).value();
}

/// 从 Json 持有对象反序列化为结构体；失败时抛出异常。
template<class T>
inline T read_or_throw(const Json& text) {
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
inline T to(JsonView text) {
    return read_or_throw<T>(text);
}

/// read_or_throw 的简写（持有对象版本）。
template<class T>
inline T to(const Json& text) {
    return text.template to<T>();
}

/// read_or_throw 的简写（字符串版本）。
template<class T>
inline T to(const char* text) {
    return read_or_throw<T>(text);
}

/// 从文件读取 JSON 文本并反序列化为结构体；失败时抛出异常。
template<class T>
inline T from_file(const std::string& path) {
    return read_or_throw<T>(detail::read_file_text(path));
}

/// 用反射序列化结构体并写入文件。
template<class T
         LTOOL_ENABLE_IF(!std::is_array<detail::remove_cvref_t<T>>::value &&
                         !std::is_same<detail::remove_cvref_t<T>, std::string>::value &&
                         !std::is_same<detail::remove_cvref_t<T>, const char*>::value)>
    LTOOL_REQUIRES(!std::is_array_v<detail::remove_cvref_t<T>> &&
                   !std::same_as<detail::remove_cvref_t<T>, std::string> &&
                   !std::same_as<detail::remove_cvref_t<T>, const char*>)
inline void to_file(const std::string& path, const T& value) {
    detail::write_file_text(path, write(value));
}

#else

/// 反射序列化；缺少依赖时编译期报错。
template<class T>
inline std::string write(const T&) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::write<T> requires C++20 and reflect-cpp rfl/json");
    return std::string();
}

/// 反射序列化；缺少依赖时编译期报错。
template<class T>
inline Json from(const T&) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::from<T> requires C++20 and reflect-cpp rfl/json");
    return Json();
}

/// 反射反序列化；缺少依赖时编译期报错。
template<class T>
inline T read(const std::string&) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

/// 反射反序列化；缺少依赖时编译期报错。
template<class T>
inline T read(JsonView) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

/// 反射反序列化；缺少依赖时编译期报错。
template<class T>
inline T read(const Json&) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

/// 反射反序列化；缺少依赖时编译期报错。
template<class T>
inline T read(const char*) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

/// read_or_throw；缺少依赖时编译期报错。
template<class T>
inline T read_or_throw(const std::string& text) {
    return read<T>(text);
}

/// read_or_throw（视图版本）。
template<class T>
inline T read_or_throw(JsonView text) {
    return read<T>(text);
}

/// read_or_throw（持有对象版本）。
template<class T>
inline T read_or_throw(const Json& text) {
    return read<T>(text);
}

/// read_or_throw（字符串版本）。
template<class T>
inline T read_or_throw(const char* text) {
    return read<T>(text);
}

/// to 简写。
template<class T>
inline T to(const std::string& text) {
    return read_or_throw<T>(text);
}

/// to 简写（视图版本）。
template<class T>
inline T to(JsonView text) {
    return read_or_throw<T>(text);
}

/// to 简写（持有对象版本）。
template<class T>
inline T to(const Json& text) {
    return text.template to<T>();
}

/// to 简写（字符串版本）。
template<class T>
inline T to(const char* text) {
    return read_or_throw<T>(text);
}

/// 从文件读取 JSON 文本并反序列化为结构体；缺少依赖时编译期报错。
template<class T>
inline T from_file(const std::string& path) {
    LTOOL_UNUSED(path);
    static_assert(detail::dependent_false<T>::value,
                  "LJson::from_file<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

/// 用反射序列化结构体并写入文件；缺少依赖时编译期报错。
template<class T>
inline void to_file(const std::string& path, const T& value) {
    LTOOL_UNUSED(path);
    LTOOL_UNUSED(value);
    static_assert(detail::dependent_false<T>::value,
                  "LJson::to_file<T> requires C++20 and reflect-cpp rfl/json");
}

#endif

/// 把 Json 序列化后写入输出流。
inline std::ostream& operator<<(std::ostream& os, const Json& value) {
    return os << value.to_string();
}

} // namespace LJson

/// fmt 格式化器：LJson::Json 按序列化文本输出。
template<>
struct fmt::formatter<LJson::Json> : fmt::formatter<fmt::string_view> {
    template<class FormatContext>
    typename FormatContext::iterator format(const LJson::Json& value, FormatContext& ctx) const {
        const auto text = value.to_string();
        return fmt::formatter<fmt::string_view>::format(
            fmt::string_view(text.data(), text.size()), ctx);
    }
};

/// fmt 格式化器：LJson::JsonRef 与 Json 相同，输出所引用节点的文本。
template<>
struct fmt::formatter<LJson::JsonRef> : fmt::formatter<LJson::Json> {};

#if LJSON_HAS_STD_FORMAT
/// std::format 格式化器：LJson::Json 按序列化文本输出。
template<>
struct std::formatter<LJson::Json, char> : std::formatter<std::string_view, char> {
    template<class FormatContext>
    auto format(const LJson::Json& value, FormatContext& ctx) const {
        const auto text = value.to_string();
        return std::formatter<std::string_view, char>::format(std::string_view(text), ctx);
    }
};

/// std::format 格式化器：LJson::JsonRef 与 Json 相同。
template<>
struct std::formatter<LJson::JsonRef, char> : std::formatter<LJson::Json, char> {};
#endif

#endif // LTOOL_LJSON_INCLUDE
