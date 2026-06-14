/**
 * @file ljson.hpp
 * @brief ltool 的 JSON 统一入口，按可用头文件接入多个 JSON 后端。
 */

#ifndef LTOOL_LJSON_INCLUDE
#define LTOOL_LJSON_INCLUDE

#include "lconfig.hpp"

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
#include "rfl/thirdparty/yyjson_impl.hpp"
#else
#include <yyjson.h>
#endif
#endif

#if LJSON_HAS_RFL_JSON
#if LJSON_HAS_BUNDLED_RFL_JSON
#include "rfl/json.hpp"
#else
#include <rfl/json.hpp>
#endif
#endif

namespace ljson {

enum class backend {
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

#if LJSON_HAS_JSONCPP
inline std::string jsoncpp_to_string(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
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

class json_view {
private:
    const char* data_ = "";
    std::size_t size_ = 0;

public:
    json_view() = default;

    json_view(const char* text)
        : data_(text ? text : ""), size_(text ? std::strlen(text) : 0) {}

    json_view(const char* text, std::size_t size)
        : data_(text ? text : ""), size_(text ? size : 0) {
        if (!text && size != 0) {
            throw std::invalid_argument("ljson::json_view cannot reference null data");
        }
    }

    json_view(const std::string& text)
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

class json {
private:
    std::string text_;

public:
    json() = default;

    json(const char* text)
        : text_(text ? text : "") {}

    json(const char* text, std::size_t size)
        : text_(text ? std::string(text, size) : std::string()) {
        if (!text && size != 0) {
            throw std::invalid_argument("ljson::json cannot copy null data");
        }
    }

    json(json_view text)
        : text_(text.data(), text.size()) {}

    json(std::string text)
        : text_(std::move(text)) {}

#if LJSON_HAS_NLOHMANN_JSON
    json(const nlohmann::json& value)
        : text_(value.dump()) {}
#endif

#if LJSON_HAS_JSONCPP
    json(const Json::Value& value)
        : text_(detail::jsoncpp_to_string(value)) {}
#endif

#if LJSON_HAS_SIMDJSON
    json(simdjson::dom::element value)
        : text_(simdjson::to_string(value)) {}

    json(simdjson::dom::document& value)
        : text_(simdjson::to_string(value.root())) {}
#endif

#if LJSON_HAS_YYJSON
    json(yyjson_doc* doc)
        : text_(detail::yyjson_to_string(doc)) {}

    json(yyjson_val* value)
        : text_(detail::yyjson_to_string(value)) {}

    json(yyjson_mut_doc* doc)
        : text_(detail::yyjson_to_string(doc)) {}

    json(yyjson_mut_val* value)
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

    json_view view() const noexcept {
        return json_view(text_.data(), text_.size());
    }

    operator json_view() const noexcept {
        return view();
    }

    operator const std::string&() const noexcept {
        return text_;
    }

#if LJSON_HAS_RFL_JSON
    template<class T>
    static json from(const T& value) {
        return json(rfl::json::write(value));
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
    static json from(const T&) {
        static_assert(detail::dependent_false<T>::value,
                      "ljson::json::from<T> requires C++20 and reflect-cpp rfl/json");
        return json();
    }

    template<class T>
    T read() const {
        static_assert(detail::dependent_false<T>::value,
                      "ljson::json::read<T> requires C++20 and reflect-cpp rfl/json");
        return T();
    }

    template<class T>
    T to() const {
        static_assert(detail::dependent_false<T>::value,
                      "ljson::json::to<T> requires C++20 and reflect-cpp rfl/json");
        return T();
    }

    template<class T>
    T to_or_throw() const {
        return to<T>();
    }
#endif
};

inline json_view view(const char* text) {
    return json_view(text);
}

inline json_view view(const std::string& text) {
    return json_view(text);
}

inline json_view view(const json& text) noexcept {
    return text.view();
}

inline json text(json_view value) {
    return json(value);
}

inline backend active_backend() noexcept {
#if LJSON_HAS_RFL_JSON
    return backend::rfl_json;
#elif LJSON_HAS_YYJSON
    return backend::yyjson;
#elif LJSON_HAS_SIMDJSON
    return backend::simdjson;
#elif LJSON_HAS_NLOHMANN_JSON
    return backend::nlohmann_json;
#elif LJSON_HAS_JSONCPP
    return backend::jsoncpp;
#else
    return backend::text;
#endif
}

inline const char* backend_name(backend value) noexcept {
    switch (value) {
    case backend::text:
        return "text";
    case backend::nlohmann_json:
        return "nlohmann/json";
    case backend::jsoncpp:
        return "jsoncpp";
    case backend::simdjson:
        return "simdjson";
    case backend::yyjson:
        return LJSON_HAS_RFL_YYJSON ? "rfl/thirdparty/yyjson" : "yyjson";
    case backend::rfl_json:
        return "rfl/json";
    case backend::none:
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

inline std::string to_string(json_view value) {
    return value.str();
}

inline std::string to_string(const json& value) {
    return value.str();
}

#if LJSON_HAS_NLOHMANN_JSON

inline nlohmann::json parse_nlohmann(json_view text) {
    return nlohmann::json::parse(text.data(), text.data() + text.size());
}

inline nlohmann::json to_nlohmann(json_view text) {
    return parse_nlohmann(text);
}

inline std::string to_string(const nlohmann::json& value) {
    return value.dump();
}

#endif

#if LJSON_HAS_JSONCPP

inline Json::Value parse_jsoncpp(json_view text) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors)) {
        throw std::runtime_error(errors.empty() ? "JsonCpp failed to parse JSON" : errors);
    }
    return root;
}

inline Json::Value parse_cppjson(json_view text) {
    return parse_jsoncpp(text);
}

inline Json::Value to_jsoncpp(json_view text) {
    return parse_jsoncpp(text);
}

inline Json::Value to_cppjson(json_view text) {
    return parse_jsoncpp(text);
}

inline std::string to_string(const Json::Value& value) {
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

    explicit simdjson_document(json_view text)
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

    json text() const {
        return json(simdjson::to_string(root_));
    }

    operator json() const {
        return text();
    }
};

inline simdjson_document parse_simdjson(json_view text) {
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

    explicit yyjson_document(json_view text, yyjson_read_flag flags = YYJSON_READ_NOFLAG) {
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

    json text() const {
        return json(detail::yyjson_to_string(doc_));
    }

    operator json() const {
        return text();
    }
};

inline yyjson_document parse_yyjson(json_view text,
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
inline json from(const T& value) {
    return json::from(value);
}

template<class T>
inline auto read(const std::string& text) -> decltype(rfl::json::read<T>(text)) {
    return rfl::json::read<T>(text);
}

template<class T>
inline auto read(json_view text) -> decltype(rfl::json::read<T>(std::string())) {
    return rfl::json::read<T>(text.str());
}

template<class T>
inline auto read(const json& text) -> decltype(rfl::json::read<T>(text.str())) {
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
inline T read_or_throw(json_view text) {
    return read<T>(text).value();
}

template<class T>
inline T read_or_throw(const json& text) {
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
inline T to(json_view text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T to(const json& text) {
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
                  "ljson::write<T> requires C++20 and reflect-cpp rfl/json");
    return std::string();
}

template<class T>
inline json from(const T&) {
    static_assert(detail::dependent_false<T>::value,
                  "ljson::from<T> requires C++20 and reflect-cpp rfl/json");
    return json();
}

template<class T>
inline T read(const std::string&) {
    static_assert(detail::dependent_false<T>::value,
                  "ljson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

template<class T>
inline T read(json_view) {
    static_assert(detail::dependent_false<T>::value,
                  "ljson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

template<class T>
inline T read(const json&) {
    static_assert(detail::dependent_false<T>::value,
                  "ljson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

template<class T>
inline T read(const char*) {
    static_assert(detail::dependent_false<T>::value,
                  "ljson::read<T> requires C++20 and reflect-cpp rfl/json");
    return T();
}

template<class T>
inline T read_or_throw(const std::string& text) {
    return read<T>(text);
}

template<class T>
inline T read_or_throw(json_view text) {
    return read<T>(text);
}

template<class T>
inline T read_or_throw(const json& text) {
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
inline T to(json_view text) {
    return read_or_throw<T>(text);
}

template<class T>
inline T to(const json& text) {
    return text.template to<T>();
}

template<class T>
inline T to(const char* text) {
    return read_or_throw<T>(text);
}

#endif

} // namespace ljson

#endif // LTOOL_LJSON_INCLUDE
