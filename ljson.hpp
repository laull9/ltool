/**
 * @file LJson.hpp
 * @brief ltool 的 JSON 统一入口，按可用头文件接入多个 JSON 后端。
 */

#ifndef LTOOL_LJSON_INCLUDE
#define LTOOL_LJSON_INCLUDE

#include "detail/LConfig.hpp"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
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
#endif

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
    enum class kind {
        null_value,
        boolean,
        number,
        string,
        array,
        object
    };

    struct node_type {
        kind type = kind::null_value;
        bool bool_value = false;
        std::string text;
        std::vector<node_type> array_values;
        std::vector<std::pair<std::string, node_type>> object_values;
    };

    mutable std::string text_;
    mutable std::shared_ptr<node_type> root_;
    mutable node_type* node_ = nullptr;
    mutable std::shared_ptr<bool> text_dirty_;
    bool proxy_ = false;
    bool valid_ = true;

    Json(std::shared_ptr<node_type> root,
         node_type* node,
         std::shared_ptr<bool> text_dirty,
         bool proxy)
        : root_(std::move(root)),
          node_(node),
          text_dirty_(std::move(text_dirty)),
          proxy_(proxy) {}

    class parser {
    private:
        const char* data_ = nullptr;
        std::size_t size_ = 0;
        std::size_t pos_ = 0;

        bool eof() const noexcept {
            return pos_ >= size_;
        }

        char peek() const {
            return eof() ? '\0' : data_[pos_];
        }

        char take() {
            if (eof()) {
                fail("unexpected end of JSON");
            }
            return data_[pos_++];
        }

        [[noreturn]] void fail(const std::string& message) const {
            throw std::runtime_error("LJson parse error at byte " +
                                     std::to_string(pos_) + ": " + message);
        }

        void skip_ws() {
            while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) {
                ++pos_;
            }
        }

        bool consume(char value) {
            skip_ws();
            if (peek() == value) {
                ++pos_;
                return true;
            }
            return false;
        }

        void expect(char value) {
            skip_ws();
            if (take() != value) {
                fail(std::string("expected '") + value + "'");
            }
        }

        bool match_literal(const char* literal) {
            const auto start = pos_;
            for (const char* p = literal; *p; ++p) {
                if (eof() || data_[pos_++] != *p) {
                    pos_ = start;
                    return false;
                }
            }
            return true;
        }

        static void append_utf8(std::string& out, unsigned value) {
            if (value <= 0x7f) {
                out.push_back(static_cast<char>(value));
            } else if (value <= 0x7ff) {
                out.push_back(static_cast<char>(0xc0 | (value >> 6)));
                out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            } else if (value <= 0xffff) {
                out.push_back(static_cast<char>(0xe0 | (value >> 12)));
                out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            } else {
                out.push_back(static_cast<char>(0xf0 | (value >> 18)));
                out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            }
        }

        unsigned parse_hex4() {
            unsigned value = 0;
            for (int i = 0; i < 4; ++i) {
                const char ch = take();
                value <<= 4;
                if (ch >= '0' && ch <= '9') {
                    value += static_cast<unsigned>(ch - '0');
                } else if (ch >= 'a' && ch <= 'f') {
                    value += static_cast<unsigned>(10 + ch - 'a');
                } else if (ch >= 'A' && ch <= 'F') {
                    value += static_cast<unsigned>(10 + ch - 'A');
                } else {
                    fail("invalid unicode escape");
                }
            }
            return value;
        }

        std::string parse_string_text() {
            expect('"');
            std::string out;
            while (true) {
                if (eof()) {
                    fail("unterminated string");
                }
                const char ch = take();
                if (ch == '"') {
                    return out;
                }
                if (static_cast<unsigned char>(ch) < 0x20) {
                    fail("control character in string");
                }
                if (ch != '\\') {
                    out.push_back(ch);
                    continue;
                }

                const char esc = take();
                switch (esc) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(esc);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    unsigned code = parse_hex4();
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (take() != '\\' || take() != 'u') {
                            fail("missing low surrogate");
                        }
                        const unsigned low = parse_hex4();
                        if (low < 0xdc00 || low > 0xdfff) {
                            fail("invalid low surrogate");
                        }
                        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    }
                    append_utf8(out, code);
                    break;
                }
                default:
                    fail("invalid escape");
                }
            }
        }

        node_type parse_array() {
            expect('[');
            node_type out;
            out.type = kind::array;
            skip_ws();
            if (consume(']')) {
                return out;
            }

            while (true) {
                out.array_values.push_back(parse_value());
                skip_ws();
                if (consume(']')) {
                    return out;
                }
                expect(',');
            }
        }

        node_type parse_object() {
            expect('{');
            node_type out;
            out.type = kind::object;
            skip_ws();
            if (consume('}')) {
                return out;
            }

            while (true) {
                skip_ws();
                if (peek() != '"') {
                    fail("expected object key");
                }
                auto key = parse_string_text();
                expect(':');
                out.object_values.push_back(std::make_pair(std::move(key), parse_value()));
                skip_ws();
                if (consume('}')) {
                    return out;
                }
                expect(',');
            }
        }

        node_type parse_number() {
            const std::size_t start = pos_;
            if (peek() == '-') {
                ++pos_;
            }
            if (peek() == '0') {
                ++pos_;
            } else if (std::isdigit(static_cast<unsigned char>(peek()))) {
                while (std::isdigit(static_cast<unsigned char>(peek()))) {
                    ++pos_;
                }
            } else {
                fail("invalid number");
            }
            if (peek() == '.') {
                ++pos_;
                if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                    fail("invalid fraction");
                }
                while (std::isdigit(static_cast<unsigned char>(peek()))) {
                    ++pos_;
                }
            }
            if (peek() == 'e' || peek() == 'E') {
                ++pos_;
                if (peek() == '+' || peek() == '-') {
                    ++pos_;
                }
                if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                    fail("invalid exponent");
                }
                while (std::isdigit(static_cast<unsigned char>(peek()))) {
                    ++pos_;
                }
            }

            node_type out;
            out.type = kind::number;
            out.text.assign(data_ + start, pos_ - start);
            return out;
        }

    public:
        explicit parser(JsonView text)
            : data_(text.data()), size_(text.size()) {}

        node_type parse_value() {
            skip_ws();
            switch (peek()) {
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"': {
                node_type out;
                out.type = kind::string;
                out.text = parse_string_text();
                return out;
            }
            case 't': {
                if (!match_literal("true")) {
                    fail("invalid literal");
                }
                node_type out;
                out.type = kind::boolean;
                out.bool_value = true;
                return out;
            }
            case 'f': {
                if (!match_literal("false")) {
                    fail("invalid literal");
                }
                node_type out;
                out.type = kind::boolean;
                out.bool_value = false;
                return out;
            }
            case 'n': {
                if (!match_literal("null")) {
                    fail("invalid literal");
                }
                return node_type();
            }
            default:
                if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek()))) {
                    return parse_number();
                }
                fail("expected JSON value");
            }
        }

        node_type parse_document() {
            auto out = parse_value();
            skip_ws();
            if (!eof()) {
                fail("trailing characters");
            }
            return out;
        }
    };

    static node_type make_null() {
        return node_type();
    }

    static node_type make_bool(bool value) {
        node_type out;
        out.type = kind::boolean;
        out.bool_value = value;
        return out;
    }

    static node_type make_string(std::string value) {
        node_type out;
        out.type = kind::string;
        out.text = std::move(value);
        return out;
    }

    template<class T>
    static node_type make_number(T value) {
        node_type out;
        out.type = kind::number;
        out.text = number_to_string(value);
        return out;
    }

    static node_type make_array() {
        node_type out;
        out.type = kind::array;
        return out;
    }

    static node_type make_object() {
        node_type out;
        out.type = kind::object;
        return out;
    }

    static node_type make_json_value(const Json& value) {
        return value.const_value();
    }

    static node_type make_json_value(std::nullptr_t) {
        return make_null();
    }

    static node_type make_json_value(bool value) {
        return make_bool(value);
    }

    static node_type make_json_value(const char* value) {
        return value ? make_string(value) : make_null();
    }

    static node_type make_json_value(char* value) {
        return value ? make_string(value) : make_null();
    }

    static node_type make_json_value(const std::string& value) {
        return make_string(value);
    }

    static node_type make_json_value(std::string&& value) {
        return make_string(std::move(value));
    }

    template<class T,
             typename std::enable_if<std::is_arithmetic<detail::remove_cvref_t<T>>::value &&
                                         !std::is_same<detail::remove_cvref_t<T>, bool>::value,
                                     int>::type = 0>
    static node_type make_json_value(T value) {
        return make_number(value);
    }

    template<class T>
    static std::string number_to_string(T value) {
        std::ostringstream os;
        os << std::setprecision(17) << value;
        return os.str();
    }

    static std::string number_to_string(int value) {
        return std::to_string(value);
    }

    static std::string number_to_string(unsigned int value) {
        return std::to_string(value);
    }

    static std::string number_to_string(long value) {
        return std::to_string(value);
    }

    static std::string number_to_string(unsigned long value) {
        return std::to_string(value);
    }

    static std::string number_to_string(long long value) {
        return std::to_string(value);
    }

    static std::string number_to_string(unsigned long long value) {
        return std::to_string(value);
    }

    static node_type parse_node(JsonView text) {
        if (text.empty()) {
            return node_type();
        }
        return parser(text).parse_document();
    }

    static void append_escaped_string(const std::string& value, std::string& out) {
        static const char* hex = "0123456789abcdef";
        out.push_back('"');
        for (unsigned char ch : value) {
            switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[ch >> 4]);
                    out.push_back(hex[ch & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
            }
        }
        out.push_back('"');
    }

    static void dump_node(const node_type& value, std::string& out, int indent, int depth) {
        switch (value.type) {
        case kind::null_value:
            out += "null";
            break;
        case kind::boolean:
            out += value.bool_value ? "true" : "false";
            break;
        case kind::number:
            out += value.text.empty() ? "0" : value.text;
            break;
        case kind::string:
            append_escaped_string(value.text, out);
            break;
        case kind::array:
            dump_array(value, out, indent, depth);
            break;
        case kind::object:
            dump_object(value, out, indent, depth);
            break;
        }
    }

    static void append_indent(std::string& out, int count) {
        for (int i = 0; i < count; ++i) {
            out.push_back(' ');
        }
    }

    static void dump_array(const node_type& value, std::string& out, int indent, int depth) {
        out.push_back('[');
        for (std::size_t i = 0; i < value.array_values.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            if (indent >= 0) {
                out.push_back('\n');
                append_indent(out, (depth + 1) * indent);
            }
            dump_node(value.array_values[i], out, indent, depth + 1);
        }
        if (indent >= 0 && !value.array_values.empty()) {
            out.push_back('\n');
            append_indent(out, depth * indent);
        }
        out.push_back(']');
    }

    static void dump_object(const node_type& value, std::string& out, int indent, int depth) {
        out.push_back('{');
        for (std::size_t i = 0; i < value.object_values.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            if (indent >= 0) {
                out.push_back('\n');
                append_indent(out, (depth + 1) * indent);
            }
            append_escaped_string(value.object_values[i].first, out);
            out.push_back(':');
            if (indent >= 0) {
                out.push_back(' ');
            }
            dump_node(value.object_values[i].second, out, indent, depth + 1);
        }
        if (indent >= 0 && !value.object_values.empty()) {
            out.push_back('\n');
            append_indent(out, depth * indent);
        }
        out.push_back('}');
    }

    node_type& mutable_value() {
        ensure_value();
        mark_dirty();
        return *node_;
    }

    const node_type& const_value() const {
        ensure_value();
        return *node_;
    }

    void ensure_value() const {
        if (!valid_) {
            throw std::logic_error("LJson::Json references no value");
        }
        if (node_) {
            return;
        }

        auto* self = const_cast<Json*>(this);
        self->root_ = std::make_shared<node_type>(parse_node(JsonView(text_)));
        self->node_ = self->root_.get();
        self->text_dirty_ = std::make_shared<bool>(false);
        self->proxy_ = false;
    }

    void set_value(node_type value) {
        if (proxy_) {
            ensure_value();
            *node_ = std::move(value);
            mark_dirty();
            return;
        }

        root_ = std::make_shared<node_type>(std::move(value));
        node_ = root_.get();
        text_dirty_ = std::make_shared<bool>(true);
        valid_ = true;
    }

    void sync_text() const {
        if (!node_) {
            return;
        }
        text_ = dump_node_to_string(*node_);
        if (!proxy_ && text_dirty_) {
            *text_dirty_ = false;
        }
    }

    void mark_dirty() {
        if (!text_dirty_) {
            text_dirty_ = std::make_shared<bool>(false);
        }
        *text_dirty_ = true;
    }

    static std::string dump_node_to_string(const node_type& value, int indent = -1) {
        std::string out;
        dump_node(value, out, indent, 0);
        return out;
    }

    static node_type& object_child(node_type& value, const std::string& key) {
        if (value.type == kind::null_value) {
            value = make_object();
        }
        if (value.type != kind::object) {
            throw std::domain_error("LJson::Json value is not an object");
        }
        for (auto& item : value.object_values) {
            if (item.first == key) {
                return item.second;
            }
        }
        value.object_values.push_back(std::make_pair(key, node_type()));
        return value.object_values.back().second;
    }

    static const node_type& object_child(const node_type& value, const std::string& key) {
        if (value.type != kind::object) {
            throw std::domain_error("LJson::Json value is not an object");
        }
        for (const auto& item : value.object_values) {
            if (item.first == key) {
                return item.second;
            }
        }
        throw std::out_of_range("LJson::Json object key not found: " + key);
    }

    static node_type& array_child(node_type& value, std::size_t index) {
        if (value.type == kind::null_value) {
            value = make_array();
        }
        if (value.type != kind::array) {
            throw std::domain_error("LJson::Json value is not an array");
        }
        if (index >= value.array_values.size()) {
            value.array_values.resize(index + 1);
        }
        return value.array_values[index];
    }

    static const node_type& array_child(const node_type& value, std::size_t index) {
        if (value.type != kind::array) {
            throw std::domain_error("LJson::Json value is not an array");
        }
        if (index >= value.array_values.size()) {
            throw std::out_of_range("LJson::Json array index out of range");
        }
        return value.array_values[index];
    }

    static bool object_contains(const node_type& value, const std::string& key) {
        if (value.type != kind::object) {
            return false;
        }
        for (const auto& item : value.object_values) {
            if (item.first == key) {
                return true;
            }
        }
        return false;
    }

    static std::size_t object_erase(node_type& value, const std::string& key) {
        if (value.type != kind::object) {
            return 0;
        }
        for (auto it = value.object_values.begin(); it != value.object_values.end(); ++it) {
            if (it->first == key) {
                value.object_values.erase(it);
                return 1;
            }
        }
        return 0;
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

    node_type& pointer_value(const std::string& path, bool create) {
        auto parts = parse_pointer(path);
        auto* current = &mutable_value();
        for (const auto& part : parts) {
            if (current->type == kind::array) {
                if (part == "-") {
                    current->array_values.push_back(node_type());
                    current = &current->array_values.back();
                } else {
                    current = &array_child(*current, parse_index(part));
                }
            } else if (create) {
                current = &object_child(*current, part);
            } else {
                current = const_cast<node_type*>(&object_child(*current, part));
            }
        }
        return *current;
    }

    const node_type& pointer_value(const std::string& path) const {
        auto parts = parse_pointer(path);
        auto* current = &const_value();
        for (const auto& part : parts) {
            if (current->type == kind::array) {
                current = &array_child(*current, parse_index(part));
            } else {
                current = &object_child(*current, part);
            }
        }
        return *current;
    }

    static node_type* find_recursive_impl(node_type& value, const std::string& key) {
        if (value.type == kind::object) {
            for (auto& item : value.object_values) {
                if (item.first == key) {
                    return &item.second;
                }
            }
            for (auto& item : value.object_values) {
                if (auto* found = find_recursive_impl(item.second, key)) {
                    return found;
                }
            }
        } else if (value.type == kind::array) {
            for (auto& item : value.array_values) {
                if (auto* found = find_recursive_impl(item, key)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    static const node_type* find_recursive_impl(const node_type& value,
                                                const std::string& key) {
        if (value.type == kind::object) {
            for (const auto& item : value.object_values) {
                if (item.first == key) {
                    return &item.second;
                }
            }
            for (const auto& item : value.object_values) {
                if (auto* found = find_recursive_impl(item.second, key)) {
                    return found;
                }
            }
        } else if (value.type == kind::array) {
            for (const auto& item : value.array_values) {
                if (auto* found = find_recursive_impl(item, key)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    static void collect_recursive_impl(std::shared_ptr<node_type>& root,
                                       std::shared_ptr<bool>& dirty,
                                       node_type& value,
                                       const std::string& key,
                                       std::vector<Json>& out) {
        if (value.type == kind::object) {
            for (auto& item : value.object_values) {
                if (item.first == key) {
                    out.push_back(Json(root, &item.second, dirty, true));
                }
            }
            for (auto& item : value.object_values) {
                collect_recursive_impl(root, dirty, item.second, key, out);
            }
        } else if (value.type == kind::array) {
            for (auto& item : value.array_values) {
                collect_recursive_impl(root, dirty, item, key, out);
            }
        }
    }

    static void collect_recursive_impl(const node_type& value,
                                       const std::string& key,
                                       std::vector<Json>& out) {
        if (value.type == kind::object) {
            for (const auto& item : value.object_values) {
                if (item.first == key) {
                    out.push_back(Json(item.second));
                }
            }
            for (const auto& item : value.object_values) {
                collect_recursive_impl(item.second, key, out);
            }
        } else if (value.type == kind::array) {
            for (const auto& item : value.array_values) {
                collect_recursive_impl(item, key, out);
            }
        }
    }

    explicit Json(const node_type& value)
        : root_(std::make_shared<node_type>(value)),
          node_(root_.get()),
          text_dirty_(std::make_shared<bool>(true)) {}

public:
    Json() = default;

    Json(const Json& other)
        : text_(other.str()) {
        if (other.proxy_) {
            root_ = other.root_;
            node_ = other.node_;
            text_dirty_ = other.text_dirty_;
            proxy_ = true;
            valid_ = other.valid_;
        } else if (other.node_) {
            root_ = std::make_shared<node_type>(other.const_value());
            node_ = root_.get();
            text_dirty_ = std::make_shared<bool>(false);
            proxy_ = false;
            valid_ = other.valid_;
        } else {
            valid_ = other.valid_;
        }
    }

    Json(Json&& other) noexcept
        : text_(std::move(other.text_)),
          root_(std::move(other.root_)),
          node_(other.node_),
          text_dirty_(std::move(other.text_dirty_)),
          proxy_(other.proxy_),
          valid_(other.valid_) {
        other.node_ = nullptr;
        other.proxy_ = false;
        other.valid_ = true;
    }

    Json& operator=(const Json& other) {
        if (this == &other) {
            return *this;
        }
        if (proxy_) {
            set_value(other.const_value());
            return *this;
        }
        text_ = other.str();
        if (other.node_) {
            root_ = std::make_shared<node_type>(other.const_value());
            node_ = root_.get();
            text_dirty_ = std::make_shared<bool>(false);
        } else {
            root_.reset();
            node_ = nullptr;
            text_dirty_.reset();
        }
        proxy_ = false;
        valid_ = other.valid_;
        return *this;
    }

    Json& operator=(Json&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (proxy_) {
            set_value(other.const_value());
            return *this;
        }
        text_ = std::move(other.text_);
        root_ = std::move(other.root_);
        node_ = other.node_;
        text_dirty_ = std::move(other.text_dirty_);
        proxy_ = other.proxy_;
        valid_ = other.valid_;
        other.node_ = nullptr;
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

    Json(std::nullptr_t)
        : Json(make_null()) {}

    Json(bool value)
        : Json(make_bool(value)) {}

    Json(int value)
        : Json(make_number(value)) {}

    Json(unsigned int value)
        : Json(make_number(value)) {}

    Json(long value)
        : Json(make_number(value)) {}

    Json(unsigned long value)
        : Json(make_number(value)) {}

    Json(long long value)
        : Json(make_number(value)) {}

    Json(unsigned long long value)
        : Json(make_number(value)) {}

    Json(double value)
        : Json(make_number(value)) {}

    Json& operator=(std::nullptr_t) {
        set_value(make_null());
        return *this;
    }

    Json& operator=(bool value) {
        set_value(make_bool(value));
        return *this;
    }

    Json& operator=(int value) {
        set_value(make_number(value));
        return *this;
    }

    Json& operator=(unsigned int value) {
        set_value(make_number(value));
        return *this;
    }

    Json& operator=(long value) {
        set_value(make_number(value));
        return *this;
    }

    Json& operator=(unsigned long value) {
        set_value(make_number(value));
        return *this;
    }

    Json& operator=(long long value) {
        set_value(make_number(value));
        return *this;
    }

    Json& operator=(unsigned long long value) {
        set_value(make_number(value));
        return *this;
    }

    Json& operator=(double value) {
        set_value(make_number(value));
        return *this;
    }

    Json& operator=(const char* value) {
        set_value(value ? make_string(value) : make_null());
        return *this;
    }

    Json& operator=(const std::string& value) {
        set_value(make_string(value));
        return *this;
    }

    Json& operator=(std::string&& value) {
        set_value(make_string(std::move(value)));
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

#if LJSON_HAS_YYJSON
    Json(yyjson_doc* doc)
        : Json(detail::yyjson_to_string(doc)) {}

    Json(yyjson_val* value)
        : Json(detail::yyjson_to_string(value)) {}

    Json(yyjson_mut_doc* doc)
        : Json(detail::yyjson_to_string(doc)) {}

    Json(yyjson_mut_val* value)
        : Json(detail::yyjson_to_string(value)) {}
#endif

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
        return Json(make_object());
    }

    static Json array() {
        return Json(make_array());
    }

    static Json parse(JsonView text) {
        return Json(parse_node(text));
    }

    std::string dump(int indent = -1) const {
        return dump_node_to_string(const_value(), indent);
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
        auto& child = object_child(mutable_value(), key);
        return Json(root_, &child, text_dirty_, true);
    }

    Json operator[](const std::string& key) const {
        return Json(object_child(const_value(), key));
    }

    Json operator[](const char* key) {
        return (*this)[std::string(key ? key : "")];
    }

    Json operator[](const char* key) const {
        return (*this)[std::string(key ? key : "")];
    }

    Json operator[](std::size_t index) {
        auto& child = array_child(mutable_value(), index);
        return Json(root_, &child, text_dirty_, true);
    }

    Json operator[](std::size_t index) const {
        return Json(array_child(const_value(), index));
    }

    Json at(const std::string& key) {
        auto& child = object_child(mutable_value(), key);
        return Json(root_, &child, text_dirty_, true);
    }

    Json at(const std::string& key) const {
        return Json(object_child(const_value(), key));
    }

    Json at(std::size_t index) {
        auto& child = array_child(mutable_value(), index);
        return Json(root_, &child, text_dirty_, true);
    }

    Json at(std::size_t index) const {
        return Json(array_child(const_value(), index));
    }

    Json at_pointer(const std::string& path) {
        auto& child = pointer_value(path, false);
        return Json(root_, &child, text_dirty_, true);
    }

    Json at_pointer(const std::string& path) const {
        return Json(pointer_value(path));
    }

    Json pointer(const std::string& path) {
        auto& child = pointer_value(path, true);
        return Json(root_, &child, text_dirty_, true);
    }

    Json pointer(const std::string& path) const {
        return Json(pointer_value(path));
    }

    void set_pointer(const std::string& path, Json value) {
        pointer_value(path, true) = value.const_value();
    }

    template<class T,
             typename std::enable_if<!std::is_same<detail::remove_cvref_t<T>, Json>::value,
                                     int>::type = 0>
    void set_pointer(const std::string& path, T&& value) {
        pointer_value(path, true) = make_json_value(std::forward<T>(value));
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
        return object_erase(mutable_value(), key);
    }

    void erase(std::size_t index) {
        auto& value = mutable_value();
        if (value.type != kind::array || index >= value.array_values.size()) {
            throw std::out_of_range("LJson::Json array index out of range");
        }
        value.array_values.erase(value.array_values.begin() + static_cast<std::ptrdiff_t>(index));
    }

    bool erase_pointer(const std::string& path) {
        auto parts = parse_pointer(path);
        if (parts.empty()) {
            set_value(make_null());
            return true;
        }

        auto* current = &mutable_value();
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            try {
                if (current->type == kind::array) {
                    current = &array_child(*current, parse_index(parts[i]));
                } else {
                    current = &object_child(*current, parts[i]);
                }
            } catch (...) {
                return false;
            }
        }

        const auto& leaf = parts.back();
        if (current->type == kind::array) {
            if (!is_array_index(leaf)) {
                return false;
            }
            const auto index = parse_index(leaf);
            if (index >= current->array_values.size()) {
                return false;
            }
            current->array_values.erase(current->array_values.begin() +
                                        static_cast<std::ptrdiff_t>(index));
            return true;
        }
        if (current->type == kind::object) {
            return object_erase(*current, leaf) != 0;
        }
        return false;
    }

    void clear() {
        auto& value = mutable_value();
        switch (value.type) {
        case kind::array:
            value.array_values.clear();
            break;
        case kind::object:
            value.object_values.clear();
            break;
        case kind::string:
        case kind::number:
            value.text.clear();
            break;
        case kind::boolean:
            value.bool_value = false;
            break;
        case kind::null_value:
            break;
        }
    }

    void push_back(Json value) {
        auto& target = mutable_value();
        if (target.type == kind::null_value) {
            target = make_array();
        }
        if (target.type != kind::array) {
            throw std::domain_error("LJson::Json value is not an array");
        }
        target.array_values.push_back(value.const_value());
    }

    template<class T,
             typename std::enable_if<!std::is_same<detail::remove_cvref_t<T>, Json>::value,
                                     int>::type = 0>
    void push_back(T&& value) {
        auto& target = mutable_value();
        if (target.type == kind::null_value) {
            target = make_array();
        }
        if (target.type != kind::array) {
            throw std::domain_error("LJson::Json value is not an array");
        }
        target.array_values.push_back(make_json_value(std::forward<T>(value)));
    }

    template<class... Args>
    Json emplace_back(Args&&... args) {
        push_back(Json(std::forward<Args>(args)...));
        auto& value = mutable_value();
        return Json(root_, &value.array_values.back(), text_dirty_, true);
    }

    void update(Json other) {
        auto& target = mutable_value();
        const auto& src = other.const_value();
        if (target.type == kind::null_value) {
            target = make_object();
        }
        if (target.type != kind::object || src.type != kind::object) {
            throw std::domain_error("LJson::Json update requires objects");
        }
        for (const auto& item : src.object_values) {
            object_child(target, item.first) = item.second;
        }
    }

    std::size_t json_size() const {
        const auto& value = const_value();
        if (value.type == kind::array) {
            return value.array_values.size();
        }
        if (value.type == kind::object) {
            return value.object_values.size();
        }
        if (value.type == kind::string) {
            return value.text.size();
        }
        return 0;
    }

    bool is_null() const {
        return const_value().type == kind::null_value;
    }

    bool is_boolean() const {
        return const_value().type == kind::boolean;
    }

    bool is_number() const {
        return const_value().type == kind::number;
    }

    bool is_string() const {
        return const_value().type == kind::string;
    }

    bool is_array() const {
        return const_value().type == kind::array;
    }

    bool is_object() const {
        return const_value().type == kind::object;
    }

    template<class T>
    T get_as() const {
        const auto& value = const_value();
        if constexpr (std::is_same<T, std::string>::value) {
            if (value.type != kind::string) {
                throw std::domain_error("LJson::Json value is not a string");
            }
            return value.text;
        } else if constexpr (std::is_same<T, bool>::value) {
            if (value.type != kind::boolean) {
                throw std::domain_error("LJson::Json value is not a boolean");
            }
            return value.bool_value;
        } else if constexpr (std::is_integral<T>::value) {
            if (value.type != kind::number) {
                throw std::domain_error("LJson::Json value is not a number");
            }
            if constexpr (std::is_unsigned<T>::value) {
                return static_cast<T>(std::stoull(value.text));
            } else {
                return static_cast<T>(std::stoll(value.text));
            }
        } else if constexpr (std::is_floating_point<T>::value) {
            if (value.type != kind::number) {
                throw std::domain_error("LJson::Json value is not a number");
            }
            return static_cast<T>(std::stod(value.text));
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
        auto& root = mutable_value();
        if (auto* found = find_recursive_impl(root, key)) {
            return Json(root_, found, text_dirty_, true);
        }
        Json out;
        out.valid_ = false;
        return out;
    }

    Json find_recursive(const std::string& key) const {
        if (auto* found = find_recursive_impl(const_value(), key)) {
            return Json(*found);
        }
        Json out;
        out.valid_ = false;
        return out;
    }

    std::vector<Json> find_all_recursive(const std::string& key) {
        auto& root = mutable_value();
        std::vector<Json> out;
        collect_recursive_impl(root_, text_dirty_, root, key, out);
        return out;
    }

    std::vector<Json> find_all_recursive(const std::string& key) const {
        std::vector<Json> out;
        collect_recursive_impl(const_value(), key, out);
        return out;
    }

    std::size_t erase_recursive(const std::string& key) {
        auto& root = mutable_value();
        return erase_recursive_into(root, key);
    }

    template<class Visitor>
    void for_each_recursive(Visitor&& visitor) {
        auto& root = mutable_value();
        for_each_recursive_impl(root_, text_dirty_, root, std::forward<Visitor>(visitor));
    }

    template<class Visitor>
    void for_each_recursive(Visitor&& visitor) const {
        for_each_recursive_impl(const_value(), std::forward<Visitor>(visitor));
    }

    friend bool operator==(const Json& lhs, const Json& rhs) {
        return equals(lhs.const_value(), rhs.const_value());
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

private:
    static bool equals(const node_type& lhs, const node_type& rhs) {
        if (lhs.type != rhs.type) {
            return false;
        }
        switch (lhs.type) {
        case kind::null_value:
            return true;
        case kind::boolean:
            return lhs.bool_value == rhs.bool_value;
        case kind::number:
            return std::stod(lhs.text) == std::stod(rhs.text);
        case kind::string:
            return lhs.text == rhs.text;
        case kind::array:
            if (lhs.array_values.size() != rhs.array_values.size()) {
                return false;
            }
            for (std::size_t i = 0; i < lhs.array_values.size(); ++i) {
                if (!equals(lhs.array_values[i], rhs.array_values[i])) {
                    return false;
                }
            }
            return true;
        case kind::object:
            if (lhs.object_values.size() != rhs.object_values.size()) {
                return false;
            }
            for (const auto& left : lhs.object_values) {
                bool found = false;
                for (const auto& right : rhs.object_values) {
                    if (left.first == right.first) {
                        if (!equals(left.second, right.second)) {
                            return false;
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    static std::size_t erase_recursive_into(node_type& value, const std::string& key) {
        std::size_t count = 0;
        if (value.type == kind::object) {
            for (auto it = value.object_values.begin(); it != value.object_values.end();) {
                if (it->first == key) {
                    it = value.object_values.erase(it);
                    ++count;
                } else {
                    count += erase_recursive_into(it->second, key);
                    ++it;
                }
            }
        } else if (value.type == kind::array) {
            for (auto& item : value.array_values) {
                count += erase_recursive_into(item, key);
            }
        }
        return count;
    }

    template<class Visitor>
    static void for_each_recursive_impl(std::shared_ptr<node_type>& root,
                                        std::shared_ptr<bool>& dirty,
                                        node_type& value,
                                        Visitor&& visitor) {
        visitor(Json(root, &value, dirty, true));
        if (value.type == kind::object) {
            for (auto& item : value.object_values) {
                for_each_recursive_impl(root, dirty, item.second, visitor);
            }
        } else if (value.type == kind::array) {
            for (auto& item : value.array_values) {
                for_each_recursive_impl(root, dirty, item, visitor);
            }
        }
    }

    template<class Visitor>
    static void for_each_recursive_impl(const node_type& value, Visitor&& visitor) {
        visitor(Json(value));
        if (value.type == kind::object) {
            for (const auto& item : value.object_values) {
                for_each_recursive_impl(item.second, visitor);
            }
        } else if (value.type == kind::array) {
            for (const auto& item : value.array_values) {
                for_each_recursive_impl(item, visitor);
            }
        }
    }

public:
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
