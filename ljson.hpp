/**
 * @file LJson.hpp
 * @brief ltool 的 JSON 统一入口，按可用头文件接入多个 JSON 后端。
 */

#ifndef LTOOL_LJSON_INCLUDE
#define LTOOL_LJSON_INCLUDE

#include "detail/LConfig.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#define LJSON_HAS_RFL_JSON LTOOL_HAS_RFL_JSON
#define LJSON_HAS_BUNDLED_RFL_JSON LTOOL_HAS_BUNDLED_RFL_JSON
#define LJSON_HAS_STATIC_REFLECTION LTOOL_HAS_RFL_JSON
#define LJSON_HAS_NLOHMANN_JSON LTOOL_HAS_NLOHMANN_JSON
#define LJSON_HAS_JSONCPP LTOOL_HAS_JSONCPP
#define LJSON_HAS_CPPJSON LTOOL_HAS_JSONCPP
#define LJSON_HAS_SIMDJSON LTOOL_HAS_SIMDJSON
#define LJSON_HAS_YYJSON LTOOL_HAS_YYJSON
#define LJSON_HAS_RFL_YYJSON LTOOL_HAS_RFL_YYJSON

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
    std::string text_;

public:
    Json() = default;

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
        : text_(value.dump()) {}
#endif

#if LJSON_HAS_JSONCPP
    Json(const ::Json::Value& value)
        : text_(detail::jsoncpp_to_string(value)) {}
#endif

#if LJSON_HAS_SIMDJSON
    Json(simdjson::dom::element value)
        : text_(simdjson::to_string(value)) {}

    Json(simdjson::dom::document& value)
        : text_(simdjson::to_string(value.root())) {}
#endif

#if LJSON_HAS_YYJSON
    Json(yyjson_doc* doc)
        : text_(detail::yyjson_to_string(doc)) {}

    Json(yyjson_val* value)
        : text_(detail::yyjson_to_string(value)) {}

    Json(yyjson_mut_doc* doc)
        : text_(detail::yyjson_to_string(doc)) {}

    Json(yyjson_mut_val* value)
        : text_(detail::yyjson_to_string(value)) {}
#endif

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

    JsonView view() const noexcept {
        return JsonView(text_.data(), text_.size());
    }

    operator JsonView() const noexcept {
        return view();
    }

    operator const std::string&() const noexcept {
        return text_;
    }

#if LJSON_HAS_RFL_JSON
    template<class T>
    static Json from(const T& value) {
        return Json(rfl::json::write(value));
    }

    template<class T>
    auto read() const -> decltype(rfl::json::read<T>(std::declval<const std::string&>())) {
        return rfl::json::read<T>(text_);
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

} // namespace LJson

#endif // LTOOL_LJSON_INCLUDE
