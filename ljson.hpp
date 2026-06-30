/**
 * @file LJson.hpp
 * @brief ltool 的 JSON 统一入口，按可用头文件接入多个 JSON 后端。
 */

#ifndef LTOOL_LJSON_INCLUDE
#define LTOOL_LJSON_INCLUDE

#include "detail/LToolConfig.hpp"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
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

enum class Backend {
    none,
    text,
    nlohmann_json,
    jsoncpp,
    simdjson,
    yyjson,
    rfl_json
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
struct is_json_text_source
    : std::integral_constant<bool,
                             is_text_buffer_like<remove_cvref_t<T>>::value &&
                                 !std::is_same<remove_cvref_t<T>, std::string>::value> {};

#if LJSON_HAS_JSONCPP
inline std::string jsoncpp_to_string(const ::Json::Value& value) {
    ::Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return ::Json::writeString(builder, value);
}
#endif

#if LJSON_HAS_YYJSON
inline std::string take_yyjson_string(char* data, std::size_t size) {
    if (!data) {
        throw std::runtime_error("yyjson failed to write JSON");
    }
    std::string out(data, size);
    std::free(data);
    return out;
}

inline std::string yyjson_to_string(yyjson_doc* doc,
                                    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    size_t size = 0;
    return take_yyjson_string(yyjson_write(doc, flags, &size), size);
}

inline std::string yyjson_to_string(yyjson_val* value,
                                    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    size_t size = 0;
    return take_yyjson_string(yyjson_val_write(value, flags, &size), size);
}

inline std::string yyjson_to_string(yyjson_mut_doc* doc,
                                    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    size_t size = 0;
    return take_yyjson_string(yyjson_mut_write(doc, flags, &size), size);
}

inline std::string yyjson_to_string(yyjson_mut_val* value,
                                    yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    size_t size = 0;
    return take_yyjson_string(yyjson_mut_val_write(value, flags, &size), size);
}

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

inline void append_json_indent(std::string& out, std::size_t level, int indent) {
    out.append(level * static_cast<std::size_t>(indent), ' ');
}

inline std::string format_json_with_indent(const std::string& text, int indent) {
    if (indent <= 0) {
        return text;
    }

    std::string out;
    out.reserve(text.size() + text.size() / 2);

    std::size_t level = 0;
    bool in_string = false;
    bool escaped = false;

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

} // namespace detail

class JsonView {
private:
    const char* data_ = "";
    std::size_t size_ = 0;

public:
    JsonView() = default;

    JsonView(const char* text)
        : data_(text ? text : ""), size_(text ? std::strlen(text) : 0) {}

    JsonView(const char* text, std::size_t size)
        : data_(text ? text : ""), size_(text ? size : 0) {
        if (!text && size != 0) {
            throw std::invalid_argument("LJson::JsonView cannot reference null data");
        }
    }

    JsonView(const std::string& text)
        : data_(text.data()), size_(text.size()) {}

    template<class Text,
             typename std::enable_if<detail::is_json_text_source<Text>::value,
                                     int>::type = 0>
    JsonView(const Text& text)
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

class Json {
private:
    enum class relation_kind {
        root,
        object,
        array
    };

    struct array_index_cache {
        yyjson_mut_val* array = nullptr;
        yyjson_mut_val* value = nullptr;
        std::size_t index = 0;
        std::size_t size = 0;
    };

    mutable std::string text_;
    mutable std::shared_ptr<yyjson_mut_doc> doc_;
    mutable yyjson_mut_val* value_ = nullptr;
    mutable yyjson_mut_val* parent_ = nullptr;
    mutable relation_kind relation_ = relation_kind::root;
    mutable std::string key_;
    mutable std::size_t index_ = 0;
    mutable std::shared_ptr<bool> text_dirty_;
    mutable std::shared_ptr<array_index_cache> array_cache_;
    bool proxy_ = false;
    bool valid_ = true;

    static std::shared_ptr<yyjson_mut_doc> make_doc() {
        auto* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            throw std::bad_alloc();
        }
        return std::shared_ptr<yyjson_mut_doc>(doc, yyjson_mut_doc_free);
    }

    static yyjson_mut_val* require_value(yyjson_mut_val* value) {
        if (!value) {
            throw std::bad_alloc();
        }
        return value;
    }

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

    void init_from_value_copy(yyjson_val* value) {
        doc_ = make_doc();
        value_ = value ? yyjson_val_mut_copy(doc_.get(), value) : yyjson_mut_null(doc_.get());
        value_ = require_value(value_);
        yyjson_mut_doc_set_root(doc_.get(), value_);
        text_dirty_ = std::make_shared<bool>(true);
        array_cache_ = std::make_shared<array_index_cache>();
        valid_ = true;
    }

    void init_from_value_copy(yyjson_mut_val* value) {
        doc_ = make_doc();
        value_ = value ? yyjson_mut_val_mut_copy(doc_.get(), value) : yyjson_mut_null(doc_.get());
        value_ = require_value(value_);
        yyjson_mut_doc_set_root(doc_.get(), value_);
        text_dirty_ = std::make_shared<bool>(true);
        array_cache_ = std::make_shared<array_index_cache>();
        valid_ = true;
    }

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

    void ensure_value() const {
        if (!valid_) {
            throw std::logic_error("LJson::Json references no value");
        }
        if (value_) {
            return;
        }

        auto* self = const_cast<Json*>(this);
        self->parse_text_now(JsonView(text_));
    }

    yyjson_mut_val* mutable_value() {
        ensure_value();
        mark_dirty();
        return value_;
    }

    yyjson_mut_val* const_value() const {
        ensure_value();
        return value_;
    }

    yyjson_mut_doc* mutable_doc() {
        ensure_value();
        return doc_.get();
    }

    yyjson_mut_doc* const_doc() const {
        ensure_value();
        return doc_.get();
    }

    void mark_dirty() {
        if (!text_dirty_) {
            text_dirty_ = std::make_shared<bool>(false);
        }
        *text_dirty_ = true;
    }

    void clear_array_cache() const noexcept {
        if (array_cache_) {
            *array_cache_ = array_index_cache{};
        }
    }

    void sync_text() const {
        if (!value_) {
            return;
        }

        const auto flags = YYJSON_WRITE_NOFLAG;
        std::size_t size = 0;
        char* data = yyjson_mut_val_write(value_, flags, &size);
        if (!data) {
            return;
        }
        text_.assign(data, size);
        std::free(data);
        if (!proxy_ && text_dirty_) {
            *text_dirty_ = false;
        }
    }

    std::string write_current(int indent = -1) const {
        auto* value = const_value();
        yyjson_write_flag flags = YYJSON_WRITE_NOFLAG;
        if (detail::yyjson_native_write_flags(indent, flags)) {
            return detail::yyjson_to_string(value, flags);
        }

        return detail::format_json_with_indent(
            detail::yyjson_to_string(value, YYJSON_WRITE_NOFLAG),
            indent
        );
    }

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

    void set_value_from(const Json& value) {
        set_value_from(value.const_value());
    }

    template<class Maker>
    void set_value(Maker&& maker) {
        if (!doc_) {
            doc_ = make_doc();
        }
        auto* value = require_value(std::forward<Maker>(maker)(doc_.get()));
        replace_current(value);
        valid_ = true;
    }

    static yyjson_mut_val* make_string(yyjson_mut_doc* doc, const std::string& value) {
        return yyjson_mut_strncpy(doc, value.data(), value.size());
    }

    static yyjson_mut_val* make_string(yyjson_mut_doc* doc, const char* value) {
        return value ? yyjson_mut_strcpy(doc, value) : yyjson_mut_null(doc);
    }

    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, int value) {
        return yyjson_mut_sint(doc, value);
    }

    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, unsigned int value) {
        return yyjson_mut_uint(doc, value);
    }

    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, long value) {
        return yyjson_mut_sint(doc, static_cast<int64_t>(value));
    }

    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, unsigned long value) {
        return yyjson_mut_uint(doc, static_cast<uint64_t>(value));
    }

    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, long long value) {
        return yyjson_mut_sint(doc, static_cast<int64_t>(value));
    }

    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, unsigned long long value) {
        return yyjson_mut_uint(doc, static_cast<uint64_t>(value));
    }

    static yyjson_mut_val* make_number(yyjson_mut_doc* doc, double value) {
        return yyjson_mut_real(doc, value);
    }

    template<class T,
             typename std::enable_if<std::is_arithmetic<detail::remove_cvref_t<T>>::value &&
                                         !std::is_same<detail::remove_cvref_t<T>, bool>::value,
                                     int>::type = 0>
    yyjson_mut_val* make_json_value(T value) {
        return make_number(mutable_doc(), value);
    }

    yyjson_mut_val* make_json_value(std::nullptr_t) {
        return yyjson_mut_null(mutable_doc());
    }

    yyjson_mut_val* make_json_value(bool value) {
        return yyjson_mut_bool(mutable_doc(), value);
    }

    yyjson_mut_val* make_json_value(const char* value) {
        return make_string(mutable_doc(), value);
    }

    yyjson_mut_val* make_json_value(char* value) {
        return make_string(mutable_doc(), value);
    }

    yyjson_mut_val* make_json_value(const std::string& value) {
        return make_string(mutable_doc(), value);
    }

    yyjson_mut_val* make_json_value(std::string&& value) {
        return make_string(mutable_doc(), value);
    }

    yyjson_mut_val* make_json_value(const Json& value) {
        return yyjson_mut_val_mut_copy(mutable_doc(), value.const_value());
    }

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

    yyjson_mut_val* array_child(yyjson_mut_val* value, std::size_t index) {
        while (yyjson_mut_arr_size(value) <= index) {
            clear_array_cache();
            auto* item = require_value(yyjson_mut_null(doc_.get()));
            if (!yyjson_mut_arr_append(value, item)) {
                throw std::runtime_error("yyjson failed to grow array");
            }
        }
        return array_child_unchecked(value, index);
    }

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

    yyjson_mut_val* array_child_unchecked(yyjson_mut_val* value, std::size_t index) const {
        const auto size = yyjson_mut_arr_size(value);
        if (index >= size) {
            return nullptr;
        }

        if (!array_cache_) {
            array_cache_ = std::make_shared<array_index_cache>();
        }

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

    static bool object_contains(yyjson_mut_val* value, const std::string& key) {
        return yyjson_mut_is_obj(value) &&
               yyjson_mut_obj_getn(value, key.data(), key.size()) != nullptr;
    }

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

    static bool is_array_index(const std::string& text) {
        if (text.empty()) {
            return false;
        }
        for (char ch : text) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return false;
            }
        }
        return true;
    }

    static std::size_t parse_index(const std::string& text) {
        if (!is_array_index(text)) {
            throw std::invalid_argument("invalid JSON array index: " + text);
        }
        return static_cast<std::size_t>(std::stoull(text));
    }

    Json make_proxy(yyjson_mut_val* value,
                    yyjson_mut_val* parent,
                    relation_kind relation,
                    std::string key = std::string(),
                    std::size_t index = 0) const {
        return Json(doc_, value, parent, relation, std::move(key), index,
                    text_dirty_, array_cache_, true);
    }

    Json make_detached_copy(yyjson_mut_val* value) const {
        Json out;
        out.init_from_value_copy(value);
        if (out.text_dirty_) {
            *out.text_dirty_ = true;
        }
        return out;
    }

    Json pointer_value(const std::string& path, bool create) {
        auto parts = parse_pointer(path);
        auto* current = mutable_value();
        auto* parent = parent_;
        auto relation = relation_;
        std::string key = key_;
        std::size_t index = index_;

        for (const auto& part : parts) {
            if (yyjson_mut_is_arr(current)) {
                parent = current;
                relation = relation_kind::array;
                if (part == "-") {
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
                if (yyjson_mut_is_null(current)) {
                    if (relation == relation_kind::root) {
                        replace_current(require_value(yyjson_mut_obj(doc_.get())));
                        current = value_;
                    } else {
                        auto* obj = require_value(yyjson_mut_obj(doc_.get()));
                        Json holder = make_proxy(current, parent, relation, key, index);
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

    Json pointer_value(const std::string& path) const {
        auto parts = parse_pointer(path);
        auto* current = const_value();
        for (const auto& part : parts) {
            if (yyjson_mut_is_arr(current)) {
                current = array_child_const(current, parse_index(part));
            } else {
                current = object_child_const(current, part);
            }
        }
        return make_detached_copy(current);
    }

    Json find_recursive_impl(yyjson_mut_val* value,
                             yyjson_mut_val* parent,
                             relation_kind relation,
                             const std::string& key,
                             std::size_t index,
                             const std::string& target) {
        if (yyjson_mut_is_obj(value)) {
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
        return out;
    }

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

    void collect_recursive_impl(yyjson_mut_val* value,
                                const std::string& target,
                                std::vector<Json>& out) {
        if (yyjson_mut_is_obj(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* obj_key = nullptr;
            yyjson_mut_val* obj_value = nullptr;
            yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
                std::string current_key(yyjson_mut_get_str(obj_key),
                                        yyjson_mut_get_len(obj_key));
                if (current_key == target) {
                    out.push_back(make_proxy(obj_value, value, relation_kind::object,
                                             current_key, 0));
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

    void collect_recursive_impl(yyjson_mut_val* value,
                                const std::string& target,
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

    template<class Visitor>
    void for_each_recursive_impl(yyjson_mut_val* value, Visitor&& visitor) {
        visitor(make_proxy(value, parent_, relation_, key_, index_));
        if (yyjson_mut_is_obj(value)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_mut_val* obj_key = nullptr;
            yyjson_mut_val* obj_value = nullptr;
            yyjson_mut_obj_foreach(value, idx, max, obj_key, obj_value) {
                Json child = make_proxy(obj_value, value, relation_kind::object,
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
                Json child = make_proxy(item, value, relation_kind::array, std::string(), idx);
                child.for_each_recursive_impl(item, visitor);
            }
        }
    }

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
    Json() = default;

    Json(const Json& other)
        : text_(other.str()) {
        if (!other.valid_) {
            valid_ = false;
            return;
        }
        if (other.proxy_) {
            doc_ = other.doc_;
            value_ = other.value_;
            parent_ = other.parent_;
            relation_ = other.relation_;
            key_ = other.key_;
            index_ = other.index_;
            text_dirty_ = other.text_dirty_;
            array_cache_ = other.array_cache_;
            proxy_ = true;
            valid_ = other.valid_;
            return;
        }
        if (other.value_) {
            init_from_value_copy(other.value_);
            if (text_dirty_) {
                *text_dirty_ = false;
            }
        } else {
            valid_ = other.valid_;
        }
    }

    Json(Json&& other) noexcept
        : text_(std::move(other.text_)),
          doc_(std::move(other.doc_)),
          value_(other.value_),
          parent_(other.parent_),
          relation_(other.relation_),
          key_(std::move(other.key_)),
          index_(other.index_),
          text_dirty_(std::move(other.text_dirty_)),
          array_cache_(std::move(other.array_cache_)),
          proxy_(other.proxy_),
          valid_(other.valid_) {
        other.value_ = nullptr;
        other.parent_ = nullptr;
        other.proxy_ = false;
        other.valid_ = true;
    }

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

    Json& operator=(Json&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (proxy_) {
            set_value_from(other);
            return *this;
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

    Json(const char* text)
        : text_(text ? text : "") {}

    Json(const char* text, std::size_t size)
        : text_(text ? std::string(text, size) : std::string()) {
        if (!text && size != 0) {
            throw std::invalid_argument("LJson::Json cannot copy null data");
        }
    }

    Json(JsonView text)
        : text_(text.data(), text.size()) {}

    Json(std::string text)
        : text_(std::move(text)) {}

    template<class Text,
             typename std::enable_if<detail::is_json_text_source<Text>::value,
                                     int>::type = 0>
    Json(const Text& text)
        : text_(text.data(), text.size()) {}

#if LJSON_HAS_NLOHMANN_JSON
    Json(const nlohmann::json& value)
        : Json(value.dump()) {}

    Json(nlohmann::json&& value)
        : Json(value.dump()) {}
#endif

    Json(std::nullptr_t) {
        init_root([](yyjson_mut_doc* doc) { return yyjson_mut_null(doc); });
    }

    Json(bool value) {
        init_root([&](yyjson_mut_doc* doc) { return yyjson_mut_bool(doc, value); });
    }

    Json(int value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    Json(unsigned int value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    Json(long value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    Json(unsigned long value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    Json(long long value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    Json(unsigned long long value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    Json(double value) {
        init_root([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
    }

    Json& operator=(std::nullptr_t) {
        set_value([](yyjson_mut_doc* doc) { return yyjson_mut_null(doc); });
        return *this;
    }

    Json& operator=(bool value) {
        set_value([&](yyjson_mut_doc* doc) { return yyjson_mut_bool(doc, value); });
        return *this;
    }

    Json& operator=(int value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    Json& operator=(unsigned int value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    Json& operator=(long value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    Json& operator=(unsigned long value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    Json& operator=(long long value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    Json& operator=(unsigned long long value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    Json& operator=(double value) {
        set_value([&](yyjson_mut_doc* doc) { return make_number(doc, value); });
        return *this;
    }

    Json& operator=(const char* value) {
        set_value([&](yyjson_mut_doc* doc) { return make_string(doc, value); });
        return *this;
    }

    Json& operator=(const std::string& value) {
        set_value([&](yyjson_mut_doc* doc) { return make_string(doc, value); });
        return *this;
    }

    Json& operator=(std::string&& value) {
        set_value([&](yyjson_mut_doc* doc) { return make_string(doc, value); });
        return *this;
    }

#if LJSON_HAS_JSONCPP
    Json(const ::Json::Value& value)
        : Json(detail::jsoncpp_to_string(value)) {}
#endif

#if LJSON_HAS_SIMDJSON
    Json(simdjson::dom::element value)
        : Json(simdjson::to_string(value)) {}

    Json(simdjson::dom::document& value)
        : Json(simdjson::to_string(value.root())) {}
#endif

    Json(yyjson_doc* doc) {
        init_from_doc_copy(doc);
    }

    Json(yyjson_val* value) {
        init_from_value_copy(value);
    }

    Json(yyjson_mut_doc* doc) {
        init_from_mut_doc_copy(doc);
    }

    Json(yyjson_mut_val* value) {
        init_from_value_copy(value);
    }

    const std::string& str() const noexcept {
        try {
            sync_text();
        } catch (...) {
        }
        return text_;
    }

    const char* c_str() const noexcept {
        return str().c_str();
    }

    const char* data() const noexcept {
        return str().data();
    }

    std::size_t size() const noexcept {
        return str().size();
    }

    bool empty() const noexcept {
        return str().empty();
    }

    JsonView view() const noexcept {
        return JsonView(data(), size());
    }

    operator JsonView() const noexcept {
        return view();
    }

    operator const std::string&() const noexcept {
        return str();
    }

    std::string to_string() const {
        return str();
    }

    static Json object() {
        Json out;
        out.init_root([](yyjson_mut_doc* doc) { return yyjson_mut_obj(doc); });
        return out;
    }

    static Json array() {
        Json out;
        out.init_root([](yyjson_mut_doc* doc) { return yyjson_mut_arr(doc); });
        return out;
    }

    static Json parse(JsonView text) {
        Json out;
        out.parse_text_now(text);
        return out;
    }

    std::string dump(int indent = -1) const {
        return write_current(indent);
    }

#if LJSON_HAS_NLOHMANN_JSON
    nlohmann::json to_nlohmann() const {
        return nlohmann::json::parse(to_string());
    }

    operator nlohmann::json() const {
        return to_nlohmann();
    }
#endif

    Json operator[](const std::string& key) {
        auto* object = ensure_object_value();
        auto* child = object_child(doc_.get(), object, key);
        return make_proxy(child, object, relation_kind::object, key, 0);
    }

    Json operator[](const std::string& key) const {
        return make_detached_copy(object_child_const(const_value(), key));
    }

    Json operator[](const char* key) {
        return (*this)[std::string(key ? key : "")];
    }

    Json operator[](const char* key) const {
        return (*this)[std::string(key ? key : "")];
    }

    Json operator[](std::size_t index) {
        auto* array = ensure_array_value();
        auto* child = array_child(array, index);
        return make_proxy(child, array, relation_kind::array, std::string(), index);
    }

    Json operator[](std::size_t index) const {
        return make_detached_copy(array_child_const(const_value(), index));
    }

    Json at(const std::string& key) {
        return (*this)[key];
    }

    Json at(const std::string& key) const {
        return (*this)[key];
    }

    Json at(std::size_t index) {
        return (*this)[index];
    }

    Json at(std::size_t index) const {
        return (*this)[index];
    }

    Json at_pointer(const std::string& path) {
        return pointer_value(path, false);
    }

    Json at_pointer(const std::string& path) const {
        return pointer_value(path);
    }

    Json pointer(const std::string& path) {
        return pointer_value(path, true);
    }

    Json pointer(const std::string& path) const {
        return pointer_value(path);
    }

    void set_pointer(const std::string& path, Json value) {
        auto target = pointer_value(path, true);
        target.set_value_from(value);
    }

    template<class T,
             typename std::enable_if<!std::is_same<detail::remove_cvref_t<T>, Json>::value,
                                     int>::type = 0>
    void set_pointer(const std::string& path, T&& value) {
        auto target = pointer_value(path, true);
        target.replace_current(require_value(target.make_json_value(std::forward<T>(value))));
    }

    bool contains(const std::string& key) const {
        return object_contains(const_value(), key);
    }

    std::size_t count(const std::string& key) const {
        return contains(key) ? 1u : 0u;
    }

    bool contains_pointer(const std::string& path) const {
        try {
            (void)pointer_value(path);
            return true;
        } catch (...) {
            return false;
        }
    }

    template<class T>
    T value(const std::string& key, T default_value) const {
        if (!contains(key)) {
            return default_value;
        }
        return at(key).template get_as<T>();
    }

    template<class T>
    T value_pointer(const std::string& path, T default_value) const {
        if (!contains_pointer(path)) {
            return default_value;
        }
        return at_pointer(path).template get_as<T>();
    }

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

    void erase(std::size_t index) {
        auto* value = mutable_value();
        if (!yyjson_mut_is_arr(value) || index >= yyjson_mut_arr_size(value)) {
            throw std::out_of_range("LJson::Json array index out of range");
        }
        clear_array_cache();
        yyjson_mut_arr_remove(value, index);
        mark_dirty();
    }

    bool erase_pointer(const std::string& path) {
        auto parts = parse_pointer(path);
        if (parts.empty()) {
            *this = nullptr;
            return true;
        }

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

    void push_back(Json value) {
        auto* target = ensure_array_value();
        auto* copied = require_value(yyjson_mut_val_mut_copy(doc_.get(), value.const_value()));
        clear_array_cache();
        if (!yyjson_mut_arr_append(target, copied)) {
            throw std::runtime_error("yyjson failed to append array value");
        }
        mark_dirty();
    }

    template<class T,
             typename std::enable_if<!std::is_same<detail::remove_cvref_t<T>, Json>::value,
                                     int>::type = 0>
    void push_back(T&& value) {
        auto* target = ensure_array_value();
        auto* item = require_value(make_json_value(std::forward<T>(value)));
        clear_array_cache();
        if (!yyjson_mut_arr_append(target, item)) {
            throw std::runtime_error("yyjson failed to append array value");
        }
        mark_dirty();
    }

    template<class... Args>
    Json emplace_back(Args&&... args) {
        push_back(Json(std::forward<Args>(args)...));
        auto* target = ensure_array_value();
        const auto index = yyjson_mut_arr_size(target) - 1;
        return make_proxy(yyjson_mut_arr_get_last(target), target,
                          relation_kind::array, std::string(), index);
    }

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

    bool is_null() const {
        return yyjson_mut_is_null(const_value());
    }

    bool is_boolean() const {
        return yyjson_mut_is_bool(const_value());
    }

    bool is_number() const {
        return yyjson_mut_is_num(const_value());
    }

    bool is_string() const {
        return yyjson_mut_is_str(const_value());
    }

    bool is_array() const {
        return yyjson_mut_is_arr(const_value());
    }

    bool is_object() const {
        return yyjson_mut_is_obj(const_value());
    }

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
                return static_cast<T>(yyjson_mut_is_uint(value)
                                          ? yyjson_mut_get_uint(value)
                                          : static_cast<uint64_t>(yyjson_mut_get_num(value)));
            } else {
                return static_cast<T>(yyjson_mut_is_int(value)
                                          ? yyjson_mut_get_sint(value)
                                          : static_cast<int64_t>(yyjson_mut_get_num(value)));
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

    std::string as_string() const {
        return get_as<std::string>();
    }

    int as_int() const {
        return get_as<int>();
    }

    long long as_i64() const {
        return get_as<long long>();
    }

    double as_double() const {
        return get_as<double>();
    }

    bool as_bool() const {
        return get_as<bool>();
    }

    bool exists() const noexcept {
        return valid_;
    }

    Json find_recursive(const std::string& key) {
        return find_recursive_impl(mutable_value(), parent_, relation_, key_, index_, key);
    }

    Json find_recursive(const std::string& key) const {
        return find_recursive_impl(const_value(), key);
    }

    std::vector<Json> find_all_recursive(const std::string& key) {
        std::vector<Json> out;
        collect_recursive_impl(mutable_value(), key, out);
        return out;
    }

    std::vector<Json> find_all_recursive(const std::string& key) const {
        std::vector<Json> out;
        collect_recursive_impl(const_value(), key, out);
        return out;
    }

    std::size_t erase_recursive(const std::string& key) {
        auto count = erase_recursive_into(mutable_value(), key);
        if (count != 0) {
            clear_array_cache();
            mark_dirty();
        }
        return count;
    }

    template<class Visitor>
    void for_each_recursive(Visitor&& visitor) {
        for_each_recursive_impl(mutable_value(), std::forward<Visitor>(visitor));
    }

    template<class Visitor>
    void for_each_recursive(Visitor&& visitor) const {
        for_each_recursive_impl(const_value(), std::forward<Visitor>(visitor));
    }

    friend bool operator==(const Json& lhs, const Json& rhs) {
        return yyjson_mut_equals(lhs.const_value(), rhs.const_value());
    }

    friend bool operator!=(const Json& lhs, const Json& rhs) {
        return !(lhs == rhs);
    }

    friend bool operator==(const Json& lhs, const char* rhs) {
        return lhs.is_string() && lhs.as_string() == (rhs ? rhs : "");
    }

    friend bool operator==(const char* lhs, const Json& rhs) {
        return rhs == lhs;
    }

    friend bool operator!=(const Json& lhs, const char* rhs) {
        return !(lhs == rhs);
    }

    friend bool operator!=(const char* lhs, const Json& rhs) {
        return !(rhs == lhs);
    }

    friend bool operator==(const Json& lhs, const std::string& rhs) {
        return lhs.is_string() && lhs.as_string() == rhs;
    }

    friend bool operator==(const std::string& lhs, const Json& rhs) {
        return rhs == lhs;
    }

    friend bool operator!=(const Json& lhs, const std::string& rhs) {
        return !(lhs == rhs);
    }

    friend bool operator!=(const std::string& lhs, const Json& rhs) {
        return !(rhs == lhs);
    }

    template<class T,
             typename std::enable_if<std::is_arithmetic<T>::value &&
                                         !std::is_same<T, bool>::value,
                                     int>::type = 0>
    friend bool operator==(const Json& lhs, T rhs) {
        return lhs.is_number() && lhs.as_double() == static_cast<double>(rhs);
    }

    template<class T,
             typename std::enable_if<std::is_arithmetic<T>::value &&
                                         !std::is_same<T, bool>::value,
                                     int>::type = 0>
    friend bool operator==(T lhs, const Json& rhs) {
        return rhs == lhs;
    }

    template<class T,
             typename std::enable_if<std::is_arithmetic<T>::value &&
                                         !std::is_same<T, bool>::value,
                                     int>::type = 0>
    friend bool operator!=(const Json& lhs, T rhs) {
        return !(lhs == rhs);
    }

    template<class T,
             typename std::enable_if<std::is_arithmetic<T>::value &&
                                         !std::is_same<T, bool>::value,
                                     int>::type = 0>
    friend bool operator!=(T lhs, const Json& rhs) {
        return !(rhs == lhs);
    }

    friend bool operator==(const Json& lhs, bool rhs) {
        return lhs.is_boolean() && lhs.as_bool() == rhs;
    }

    friend bool operator==(bool lhs, const Json& rhs) {
        return rhs == lhs;
    }

    friend bool operator!=(const Json& lhs, bool rhs) {
        return !(lhs == rhs);
    }

    friend bool operator!=(bool lhs, const Json& rhs) {
        return !(rhs == lhs);
    }

#if LJSON_HAS_RFL_JSON
    template<class T>
    static Json from(const T& value) {
        return Json(rfl::json::write(value));
    }

    template<class T>
    auto read() const -> decltype(rfl::json::read<T>(std::declval<const std::string&>())) {
        return rfl::json::read<T>(str());
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
    static Json from(const T&) {
        static_assert(detail::dependent_false<T>::value,
                      "LJson::Json::from<T> requires C++20 and reflect-cpp rfl/json");
        return Json();
    }

    template<class T>
    T read() const {
        static_assert(detail::dependent_false<T>::value,
                      "LJson::Json::read<T> requires C++20 and reflect-cpp rfl/json");
        return T();
    }

    template<class T>
    T to() const {
        static_assert(detail::dependent_false<T>::value,
                      "LJson::Json::to<T> requires C++20 and reflect-cpp rfl/json");
        return T();
    }

    template<class T>
    T to_or_throw() const {
        return to<T>();
    }
#endif
};

inline JsonView view(const char* text) {
    return JsonView(text);
}

inline JsonView view(const std::string& text) {
    return JsonView(text);
}

inline JsonView view(const Json& text) noexcept {
    return text.view();
}

inline Json text(JsonView value) {
    return Json(value);
}

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

inline const char* backend_name() noexcept {
    return backend_name(active_backend());
}

inline bool has_static_reflection() noexcept {
    return LJSON_HAS_STATIC_REFLECTION != 0;
}

inline std::string to_string(JsonView value) {
    return value.str();
}

inline std::string to_string(const Json& value) {
    return value.str();
}

#if LJSON_HAS_NLOHMANN_JSON

inline nlohmann::json parse_nlohmann(JsonView text) {
    return nlohmann::json::parse(text.data(), text.data() + text.size());
}

inline nlohmann::json to_nlohmann(JsonView text) {
    return parse_nlohmann(text);
}

inline nlohmann::json to_nlohmann(const Json& value) {
    return value.to_nlohmann();
}

inline std::string to_string(const nlohmann::json& value) {
    return value.dump();
}

#endif

#if LJSON_HAS_JSONCPP

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

inline ::Json::Value parse_cppjson(JsonView text) {
    return parse_jsoncpp(text);
}

inline ::Json::Value to_jsoncpp(JsonView text) {
    return parse_jsoncpp(text);
}

inline ::Json::Value to_cppjson(JsonView text) {
    return parse_jsoncpp(text);
}

inline std::string to_string(const ::Json::Value& value) {
    return detail::jsoncpp_to_string(value);
}

#endif

#if LJSON_HAS_SIMDJSON

class simdjson_document {
private:
    std::unique_ptr<simdjson::dom::parser> parser_;
    simdjson::dom::element root_;

public:
    simdjson_document()
        : parser_(new simdjson::dom::parser()) {}

    explicit simdjson_document(JsonView text)
        : parser_(new simdjson::dom::parser()) {
        auto result = parser_->parse(text.data(), text.size());
        if (result.error()) {
            throw simdjson::simdjson_error(result.error());
        }
        root_ = result.value();
    }

    simdjson_document(simdjson_document&&) noexcept = default;
    simdjson_document& operator=(simdjson_document&&) noexcept = default;

    simdjson_document(const simdjson_document&) = delete;
    simdjson_document& operator=(const simdjson_document&) = delete;

    simdjson::dom::element root() const noexcept {
        return root_;
    }

    Json text() const {
        return Json(simdjson::to_string(root_));
    }

    operator Json() const {
        return text();
    }
};

inline simdjson_document parse_simdjson(JsonView text) {
    return simdjson_document(text);
}

inline std::string to_string(simdjson::dom::element value) {
    return simdjson::to_string(value);
}

inline std::string to_string(simdjson::dom::document& value) {
    return simdjson::to_string(value.root());
}

inline std::string to_string(const simdjson_document& value) {
    return value.text().str();
}

#endif

#if LJSON_HAS_YYJSON

class yyjson_document {
private:
    yyjson_doc* doc_ = nullptr;

public:
    yyjson_document() = default;

    explicit yyjson_document(JsonView text, yyjson_read_flag flags = YYJSON_READ_NOFLAG) {
        yyjson_read_err err;
        doc_ = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), flags, nullptr, &err);
        if (!doc_) {
            throw std::runtime_error(err.msg ? err.msg : "yyjson failed to parse JSON");
        }
    }

    explicit yyjson_document(yyjson_doc* doc) noexcept
        : doc_(doc) {}

    ~yyjson_document() {
        reset();
    }

    yyjson_document(yyjson_document&& other) noexcept
        : doc_(other.doc_) {
        other.doc_ = nullptr;
    }

    yyjson_document& operator=(yyjson_document&& other) noexcept {
        if (this != &other) {
            reset();
            doc_ = other.doc_;
            other.doc_ = nullptr;
        }
        return *this;
    }

    yyjson_document(const yyjson_document&) = delete;
    yyjson_document& operator=(const yyjson_document&) = delete;

    yyjson_doc* get() const noexcept {
        return doc_;
    }

    yyjson_val* root() const noexcept {
        return doc_ ? yyjson_doc_get_root(doc_) : nullptr;
    }

    yyjson_doc* release() noexcept {
        yyjson_doc* out = doc_;
        doc_ = nullptr;
        return out;
    }

    void reset(yyjson_doc* doc = nullptr) noexcept {
        if (doc_) {
            yyjson_doc_free(doc_);
        }
        doc_ = doc;
    }

    Json text() const {
        return Json(detail::yyjson_to_string(doc_));
    }

    operator Json() const {
        return text();
    }
};

inline yyjson_document parse_yyjson(JsonView text,
                                    yyjson_read_flag flags = YYJSON_READ_NOFLAG) {
    return yyjson_document(text, flags);
}

inline std::string to_string(yyjson_doc* doc,
                             yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    return detail::yyjson_to_string(doc, flags);
}

inline std::string to_string(yyjson_val* value,
                             yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    return detail::yyjson_to_string(value, flags);
}

inline std::string to_string(yyjson_mut_doc* doc,
                             yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    return detail::yyjson_to_string(doc, flags);
}

inline std::string to_string(yyjson_mut_val* value,
                             yyjson_write_flag flags = YYJSON_WRITE_NOFLAG) {
    return detail::yyjson_to_string(value, flags);
}

inline std::string to_string(const yyjson_document& value) {
    return to_string(value.get());
}

#endif

#if LJSON_HAS_RFL_JSON

template<class T>
inline std::string write(const T& value) {
    return rfl::json::write(value);
}

template<class T>
inline Json from(const T& value) {
    return Json::from(value);
}

template<class T>
inline auto read(const std::string& text) -> decltype(rfl::json::read<T>(text)) {
    return rfl::json::read<T>(text);
}

template<class T>
inline auto read(JsonView text) -> decltype(rfl::json::read<T>(std::string())) {
    return rfl::json::read<T>(text.str());
}

template<class T>
inline auto read(const Json& text) -> decltype(rfl::json::read<T>(text.str())) {
    return rfl::json::read<T>(text.str());
}

template<class T>
inline auto read(const char* text) -> decltype(rfl::json::read<T>(std::string())) {
    return rfl::json::read<T>(std::string(text ? text : ""));
}

template<class T>
inline T read_or_throw(const std::string& text) {
    return read<T>(text).value();
}

template<class T>
inline T read_or_throw(JsonView text) {
    return read<T>(text).value();
}

template<class T>
inline T read_or_throw(const Json& text) {
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
inline T to(JsonView text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T to(const Json& text) {
    return text.template to<T>();
}

template<class T>
inline T to(const char* text) {
    return read_or_throw<T>(text);
}

#else

template<class T>
inline std::string write(const T&) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::write<T> requires C++20 and reflect-cpp rfl/json");
    return std::string();
}

template<class T>
inline Json from(const T&) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::from<T> requires C++20 and reflect-cpp rfl/json");
    return Json();
}

template<class T>
inline T read(const std::string&) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

template<class T>
inline T read(JsonView) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

template<class T>
inline T read(const Json&) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

template<class T>
inline T read(const char*) {
    static_assert(detail::dependent_false<T>::value,
                  "LJson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

template<class T>
inline T read_or_throw(const std::string& text) {
    return read<T>(text);
}

template<class T>
inline T read_or_throw(JsonView text) {
    return read<T>(text);
}

template<class T>
inline T read_or_throw(const Json& text) {
    return read<T>(text);
}

template<class T>
inline T read_or_throw(const char* text) {
    return read<T>(text);
}

template<class T>
inline T to(const std::string& text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T to(JsonView text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T to(const Json& text) {
    return text.template to<T>();
}

template<class T>
inline T to(const char* text) {
    return read_or_throw<T>(text);
}

#endif

inline std::ostream& operator<<(std::ostream& os, const Json& value) {
    return os << value.to_string();
}

} // namespace LJson

template<>
struct fmt::formatter<LJson::Json> : fmt::formatter<fmt::string_view> {
    template<class FormatContext>
    typename FormatContext::iterator format(const LJson::Json& value, FormatContext& ctx) const {
        const auto text = value.to_string();
        return fmt::formatter<fmt::string_view>::format(
            fmt::string_view(text.data(), text.size()), ctx);
    }
};

#if LJSON_HAS_STD_FORMAT
template<>
struct std::formatter<LJson::Json, char> : std::formatter<std::string_view, char> {
    template<class FormatContext>
    auto format(const LJson::Json& value, FormatContext& ctx) const {
        const auto text = value.to_string();
        return std::formatter<std::string_view, char>::format(std::string_view(text), ctx);
    }
};
#endif

#endif // LTOOL_LJSON_INCLUDE
