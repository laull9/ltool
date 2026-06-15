/**
 * @file LString.hpp
 * @brief std::string 的纯头文件薄封装，附带常用文本、编码、格式化、正则、
 *        路径、哈希与 Base64/MD5 工具。
 *
 * LString 的核心定位：
 * - 内部只保存 std::string，默认把内容视为 UTF-8 字节。
 * - 与 std::string / string_view / C 字符串尽量自然互转。
 * - 基础操作如 size()、operator[]、find()、substr()、split() 都按字节语义工作；
 *   不把 UTF-8 自动拆成 Unicode 码点。
 * - Unicode、GBK、GB2312、UTF-16、UTF-32、Latin1 等编码只在 from_encoding()
 *   和 to_encoding() 这类边界 API 中处理。
 *
 * 主要能力：
 * - 字符串基础操作：append、replace、trim、大小写 ASCII 转换、split、join、lines。
 * - 编码转换：UTF-8/UTF-8-BOM/UTF-16/UTF-32/Latin1/GBK/GB2312、wstring 往返、
 *   UTF-8 校验与启发式编码检测。
 * - 格式化：fmt::format 支持；标准库支持时额外启用 std::format 支持。
 * - 枚举：检测到 magic_enum 时启用枚举名序列化和反序列化。
 * - 正则：优先使用 RE2；没有 RE2 头文件时自动退回 std::regex。
 * - 路径：标准库支持 std::filesystem 时启用 filename/stem/extension 等路径辅助。
 * - 数字转换：标准库支持 std::optional/from_chars 时启用 to_int/to_double 等接口。
 * - Range/Span：C++20 起启用 char range 构造、append_range、assign_range、bytes()。
 * - 哈希与编码小工具：std::hash、Base64 编解码、MD5 十六进制和 16 字节摘要。
 *
 * C++ 标准兼容：
 * - C++11：保留核心字符串、fmt、编码转换、Base64、MD5、hash、regex_contains、
 *   regex_find_all、regex_replace 等基础子集。
 * - C++17+：启用 std::optional/string_view/filesystem 相关接口。
 * - C++20+：启用 ranges/span/std::format/<=> 等现代接口。
 *
 * 常用示例：
 * @code
 * LString s = " hello ";
 * auto text = s.trimmed().upper_ascii();          // "HELLO"
 * auto parts = LString("a,b,c").split(',');
 * auto joined = LString::join(parts, "/");        // "a/b/c"
 *
 * auto utf8 = LString::from_gbk(gbk_bytes);
 * auto gbk = utf8.to_gbk();
 *
 * auto b64 = LString("hello").base64_encoded();   // "aGVsbG8="
 * auto md5 = LString("abc").md5();                // "900150983cd24fb0d6963f7d28e17f72"
 *
 * enum class Color { Red, Blue };
 * auto name = LString(Color::Red);                 // "Red"，启用 magic_enum 时
 * auto color = LString("blue").to_enum<Color>(false);
 * @endcode
 */

#ifndef LSTRING_INCLUDE
#define LSTRING_INCLUDE

#include "LConfig.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <ostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "LFmt.hpp"

#ifndef LSTRING_USE_EXTERNAL_FMT
#define LSTRING_USE_EXTERNAL_FMT LTOOL_USE_EXTERNAL_FMT
#endif // !LSTRING_USE_EXTERNAL_FMT

#if LTOOL_HAS_CPP17
#include <optional>
#include <string_view>
#endif // LTOOL_HAS_CPP17

#if LTOOL_HAS_FILESYSTEM
#include <filesystem>
#endif // LTOOL_HAS_FILESYSTEM
#define LSTRING_HAS_FILESYSTEM LTOOL_HAS_FILESYSTEM

#if LTOOL_HAS_CPP20
#include <concepts>
#include <ranges>
#include <span>
#endif // LTOOL_HAS_CPP20
#define LSTRING_HAS_RANGES LTOOL_HAS_RANGES
#define LSTRING_HAS_SPAN LTOOL_HAS_SPAN

#if LTOOL_HAS_CPP20 && LTOOL_HAS_INCLUDE(<format>)
#include <format>
#endif // LTOOL_HAS_CPP20 && LTOOL_HAS_INCLUDE(<format>)
#define LSTRING_HAS_STD_FORMAT LTOOL_HAS_STD_FORMAT

#define LSTRING_HAS_STD_OPTIONAL LTOOL_HAS_OPTIONAL

#ifndef LSTRING_USE_MAGIC_ENUM
#define LSTRING_USE_MAGIC_ENUM LTOOL_USE_MAGIC_ENUM
#endif // !LSTRING_USE_MAGIC_ENUM

#if LTOOL_HAS_MAGIC_ENUM
#include "magic_enum/magic_enum.hpp"
#endif // LTOOL_HAS_MAGIC_ENUM
#define LSTRING_HAS_MAGIC_ENUM LTOOL_HAS_MAGIC_ENUM

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX
#include <windows.h>
#elif LTOOL_HAS_ICONV
#include <iconv.h>
#endif // defined(_WIN32)
#define LSTRING_HAS_ICONV LTOOL_HAS_ICONV

#if LTOOL_HAS_RE2
#include <re2/re2.h>
#endif // LTOOL_HAS_RE2
#define LSTRING_HAS_RE2 LTOOL_HAS_RE2

/**
 * @brief LString 边界转换 API 支持的文本编码。
 *
 * LString 内部始终按 UTF-8 字节保存文本。这些枚举值用于 from_encoding()
 * 和 to_encoding()，表示外部字节进入或离开 LString 时采用的编码。
 */
enum class LEncoding {
    /// 让 LString 推断最可能的输入编码。
    Unknown,
    /// 7 位 ASCII；严格模式下大于 0x7F 的字节非法。
    Ascii,
    /// UTF-8，不要求字节序标记。
    Utf8,
    /// UTF-8，边界处可带或输出字节序标记。
    Utf8Bom,
    /// UTF-16 小端字节序。
    Utf16Le,
    /// UTF-16 大端字节序。
    Utf16Be,
    /// UTF-32 小端字节序。
    Utf32Le,
    /// UTF-32 大端字节序。
    Utf32Be,
    /// ISO-8859-1 / Latin-1 单字节编码。
    Latin1,
    /// GBK 简体中文传统编码。
    Gbk,
    /// GB2312 简体中文传统编码。
    Gb2312
};

namespace LStringDetail {

#if __cplusplus >= 201703L
using string_view = std::string_view;
using wstring_view = std::wstring_view;

template<class T>
using optional = std::optional<T>;

inline constexpr std::nullopt_t nullopt = std::nullopt;
#else
class string_view {
private:
    const char* data_ = nullptr;
    std::size_t size_ = 0;

    static const char* empty_data() noexcept {
        return "";
    }

public:
    static const std::size_t npos = static_cast<std::size_t>(-1);

    string_view() {}

    string_view(const char* text)
        : data_(text), size_(text ? std::strlen(text) : 0) {}

    string_view(const char* text, std::size_t size)
        : data_(text), size_(size) {
        if (!text && size != 0) {
            throw std::invalid_argument("LStringDetail::string_view cannot reference null data");
        }
    }

    string_view(const std::string& text)
        : data_(text.data()), size_(text.size()) {}

    const char* data() const {
        return data_ ? data_ : empty_data();
    }

    std::size_t size() const {
        return size_;
    }

    bool empty() const {
        return size_ == 0;
    }

    const char* begin() const {
        return data();
    }

    const char* end() const {
        return data() + size_;
    }

    char operator[](std::size_t pos) const {
        return data_[pos];
    }

    string_view substr(std::size_t pos = 0, std::size_t count = npos) const {
        if (pos > size_) {
            throw std::out_of_range("LStringDetail::string_view::substr");
        }
        auto available = size_ - pos;
        auto n = count == npos || count > available ? available : count;
        return string_view(data() + pos, n);
    }

    std::string str() const {
        return std::string(data(), size_);
    }

    operator std::string() const {
        return str();
    }
};

inline bool operator==(string_view lhs, string_view rhs) {
    return lhs.size() == rhs.size() &&
           (lhs.size() == 0 || std::char_traits<char>::compare(lhs.data(), rhs.data(), lhs.size()) == 0);
}

inline bool operator!=(string_view lhs, string_view rhs) {
    return !(lhs == rhs);
}

class wstring_view {
private:
    const wchar_t* data_ = nullptr;
    std::size_t size_ = 0;

    static const wchar_t* empty_data() noexcept {
        return L"";
    }

public:
    wstring_view() {}

    wstring_view(const wchar_t* text)
        : data_(text), size_(text ? std::wcslen(text) : 0) {}

    wstring_view(const wchar_t* text, std::size_t size)
        : data_(text), size_(size) {
        if (!text && size != 0) {
            throw std::invalid_argument("LStringDetail::wstring_view cannot reference null data");
        }
    }

    wstring_view(const std::wstring& text)
        : data_(text.data()), size_(text.size()) {}

    const wchar_t* data() const {
        return data_ ? data_ : empty_data();
    }

    std::size_t size() const {
        return size_;
    }

    wchar_t operator[](std::size_t pos) const {
        return data_[pos];
    }
};

struct nullopt_t {};
static const nullopt_t nullopt = nullopt_t();

template<class T>
class optional {
private:
    bool has_ = false;
    T value_ {};

public:
    optional() {}
    optional(nullopt_t) {}
    optional(const T& value) : has_(true), value_(value) {}

    explicit operator bool() const {
        return has_;
    }

    bool has_value() const {
        return has_;
    }

    const T& operator*() const {
        return value_;
    }

    T& operator*() {
        return value_;
    }

    const T& value() const {
        if (!has_) {
            throw std::runtime_error("bad optional access");
        }
        return value_;
    }
};
#endif // __cplusplus >= 201703L

#if __cplusplus >= 202002L
template<class T>
using remove_cvref_t = std::remove_cvref_t<T>;
#else
template<class T>
struct remove_cvref {
    using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template<class T>
using remove_cvref_t = typename remove_cvref<T>::type;
#endif // __cplusplus >= 202002L

template<class T>
struct is_char_pointer
    : std::integral_constant<
          bool,
          std::is_pointer<typename std::decay<T>::type>::value &&
              std::is_same<
                  typename std::remove_cv<
                      typename std::remove_pointer<typename std::decay<T>::type>::type>::type,
                  char>::value> {};

template<class T>
struct is_LString_text_source
    : std::integral_constant<
          bool,
          std::is_same<remove_cvref_t<T>, char>::value ||
              is_char_pointer<T>::value ||
              std::is_same<remove_cvref_t<T>, std::string>::value ||
              std::is_same<remove_cvref_t<T>, string_view>::value> {};

template<class T>
struct is_magic_enum_source
    : std::integral_constant<bool,
                             LSTRING_HAS_MAGIC_ENUM &&
                                 std::is_enum<remove_cvref_t<T>>::value> {};

static const char32_t replacement_char = U'\uFFFD';

/// 判断 @p cp 是否为 UTF-16 高代理项码元。
inline bool is_high_surrogate(char32_t cp) noexcept {
    return cp >= 0xD800 && cp <= 0xDBFF;
}

/// 判断 @p cp 是否为 UTF-16 低代理项码元。
inline bool is_low_surrogate(char32_t cp) noexcept {
    return cp >= 0xDC00 && cp <= 0xDFFF;
}

/// 判断 @p cp 是否为合法 Unicode 标量值。
inline bool is_valid_scalar(char32_t cp) noexcept {
    return cp <= 0x10FFFF && !is_high_surrogate(cp) && !is_low_surrogate(cp);
}

/// 将一个 Unicode 标量值以 UTF-8 形式追加到 @p out。
inline void append_utf8(std::string& out, char32_t cp) {
    if (!is_valid_scalar(cp)) {
        cp = replacement_char;
    }

    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// 从 @p i 解码下一个 UTF-8 标量值，并将 @p i 推进到已消费字节之后。
inline optional<char32_t> next_utf8(LStringDetail::string_view text, std::size_t& i) {
    if (i >= text.size()) {
        return nullopt;
    }

    const auto b0 = static_cast<unsigned char>(text[i]);
    if (b0 <= 0x7F) {
        ++i;
        return b0;
    }

    auto fail = [&]() -> optional<char32_t> {
        ++i;
        return nullopt;
    };

    auto continuation = [&](std::size_t offset) -> optional<unsigned char> {
        if (i + offset >= text.size()) {
            return nullopt;
        }
        auto b = static_cast<unsigned char>(text[i + offset]);
        if ((b & 0xC0) != 0x80) {
            return nullopt;
        }
        return b;
    };

    char32_t cp = 0;
    std::size_t width = 0;

    if ((b0 & 0xE0) == 0xC0) {
        auto b1 = continuation(1);
        if (!b1) {
            return fail();
        }
        cp = ((b0 & 0x1F) << 6) | (*b1 & 0x3F);
        width = 2;
        if (cp < 0x80) {
            return fail();
        }
    } else if ((b0 & 0xF0) == 0xE0) {
        auto b1 = continuation(1);
        auto b2 = continuation(2);
        if (!b1 || !b2) {
            return fail();
        }
        cp = ((b0 & 0x0F) << 12) | ((*b1 & 0x3F) << 6) | (*b2 & 0x3F);
        width = 3;
        if (cp < 0x800 || is_high_surrogate(cp) || is_low_surrogate(cp)) {
            return fail();
        }
    } else if ((b0 & 0xF8) == 0xF0) {
        auto b1 = continuation(1);
        auto b2 = continuation(2);
        auto b3 = continuation(3);
        if (!b1 || !b2 || !b3) {
            return fail();
        }
        cp = ((b0 & 0x07) << 18) | ((*b1 & 0x3F) << 12) |
             ((*b2 & 0x3F) << 6) | (*b3 & 0x3F);
        width = 4;
        if (cp < 0x10000 || cp > 0x10FFFF) {
            return fail();
        }
    } else {
        return fail();
    }

    i += width;
    return cp;
}

/// 校验整段字节是否为合法 UTF-8。
inline bool is_valid_utf8(LStringDetail::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        if (!next_utf8(text, i)) {
            return false;
        }
    }
    return true;
}

/// 将 UTF-8 字节解码为 Unicode 标量值序列。
inline std::vector<char32_t> decode_utf8(LStringDetail::string_view text, bool strict) {
    std::vector<char32_t> out;
    out.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        auto before = i;
        auto cp = next_utf8(text, i);
        if (cp) {
            out.push_back(*cp);
        } else if (strict) {
            throw std::runtime_error("invalid UTF-8 sequence");
        } else {
            if (i == before) {
                ++i;
            }
            out.push_back(replacement_char);
        }
    }

    return out;
}

/// 将 Unicode 标量值序列编码为 UTF-8 字节。
inline std::string encode_utf8(const std::vector<char32_t>& codepoints) {
    std::string out;
    out.reserve(codepoints.size());
    for (auto cp : codepoints) {
        append_utf8(out, cp);
    }
    return out;
}

/// 按字节检查前缀，供 BOM、路径和字符串辅助函数使用。
inline bool has_prefix(LStringDetail::string_view text, LStringDetail::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

/// 显式长度的 UTF-8 字节序标记视图。
inline LStringDetail::string_view utf8_bom() noexcept {
    return LStringDetail::string_view("\xEF\xBB\xBF", 3);
}

/// UTF-16 小端字节序标记。
inline LStringDetail::string_view utf16le_bom() noexcept {
    return LStringDetail::string_view("\xFF\xFE", 2);
}

/// UTF-16 大端字节序标记。
inline LStringDetail::string_view utf16be_bom() noexcept {
    return LStringDetail::string_view("\xFE\xFF", 2);
}

/// UTF-32 小端字节序标记。
inline LStringDetail::string_view utf32le_bom() noexcept {
    return LStringDetail::string_view("\xFF\xFE\x00\x00", 4);
}

/// UTF-32 大端字节序标记。
inline LStringDetail::string_view utf32be_bom() noexcept {
    return LStringDetail::string_view("\x00\x00\xFE\xFF", 4);
}

/// 按指定字节序从字节中读取 16 位整数。
inline std::uint16_t read_u16(LStringDetail::string_view bytes, std::size_t pos, bool little) {
    auto b0 = static_cast<unsigned char>(bytes[pos]);
    auto b1 = static_cast<unsigned char>(bytes[pos + 1]);
    if (little) {
        return static_cast<std::uint16_t>(b0 | (b1 << 8));
    }
    return static_cast<std::uint16_t>((b0 << 8) | b1);
}

/// 按指定字节序从字节中读取 32 位整数。
inline std::uint32_t read_u32(LStringDetail::string_view bytes, std::size_t pos, bool little) {
    auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos]));
    auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 1]));
    auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 2]));
    auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + 3]));
    if (little) {
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

/// 按指定字节序把 16 位整数追加到字节串。
inline void append_u16(std::string& out, std::uint16_t value, bool little) {
    if (little) {
        out.push_back(static_cast<char>(value & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
    } else {
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
        out.push_back(static_cast<char>(value & 0xFF));
    }
}

/// 按指定字节序把 32 位整数追加到字节串。
inline void append_u32(std::string& out, std::uint32_t value, bool little) {
    if (little) {
        out.push_back(static_cast<char>(value & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
        out.push_back(static_cast<char>((value >> 16) & 0xFF));
        out.push_back(static_cast<char>((value >> 24) & 0xFF));
    } else {
        out.push_back(static_cast<char>((value >> 24) & 0xFF));
        out.push_back(static_cast<char>((value >> 16) & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
        out.push_back(static_cast<char>(value & 0xFF));
    }
}

/// 将 UTF-16 字节解码为 Unicode 标量值序列。
inline std::vector<char32_t> decode_utf16(LStringDetail::string_view bytes, bool little, bool strict) {
    if (bytes.size() % 2 != 0 && strict) {
        throw std::runtime_error("odd-length UTF-16 byte sequence");
    }

    std::vector<char32_t> out;
    out.reserve(bytes.size() / 2);

    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        auto u = read_u16(bytes, i, little);
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 3 >= bytes.size()) {
                if (strict) {
                    throw std::runtime_error("dangling UTF-16 high surrogate");
                }
                out.push_back(replacement_char);
                continue;
            }
            auto lo = read_u16(bytes, i + 2, little);
            if (lo < 0xDC00 || lo > 0xDFFF) {
                if (strict) {
                    throw std::runtime_error("invalid UTF-16 surrogate pair");
                }
                out.push_back(replacement_char);
                continue;
            }
            auto cp = 0x10000 + (((u - 0xD800) << 10) | (lo - 0xDC00));
            out.push_back(static_cast<char32_t>(cp));
            i += 2;
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            if (strict) {
                throw std::runtime_error("unpaired UTF-16 low surrogate");
            }
            out.push_back(replacement_char);
        } else {
            out.push_back(u);
        }
    }

    return out;
}

/// 将 UTF-32 字节解码为 Unicode 标量值序列。
inline std::vector<char32_t> decode_utf32(LStringDetail::string_view bytes, bool little, bool strict) {
    if (bytes.size() % 4 != 0 && strict) {
        throw std::runtime_error("misaligned UTF-32 byte sequence");
    }

    std::vector<char32_t> out;
    out.reserve(bytes.size() / 4);

    for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) {
        auto cp = static_cast<char32_t>(read_u32(bytes, i, little));
        if (!is_valid_scalar(cp)) {
            if (strict) {
                throw std::runtime_error("invalid UTF-32 code point");
            }
            cp = replacement_char;
        }
        out.push_back(cp);
    }

    return out;
}

/// 将 Unicode 标量值序列编码为 UTF-16 字节。
inline std::string encode_utf16(const std::vector<char32_t>& codepoints, bool little, bool bom) {
    std::string out;
    out.reserve(codepoints.size() * 2 + (bom ? 2 : 0));
    if (bom) {
        append_u16(out, 0xFEFF, little);
    }

    for (auto cp : codepoints) {
        if (!is_valid_scalar(cp)) {
            cp = replacement_char;
        }
        if (cp <= 0xFFFF) {
            append_u16(out, static_cast<std::uint16_t>(cp), little);
        } else {
            cp -= 0x10000;
            append_u16(out, static_cast<std::uint16_t>(0xD800 + (cp >> 10)), little);
            append_u16(out, static_cast<std::uint16_t>(0xDC00 + (cp & 0x3FF)), little);
        }
    }

    return out;
}

/// 将 Unicode 标量值序列编码为 UTF-32 字节。
inline std::string encode_utf32(const std::vector<char32_t>& codepoints, bool little, bool bom) {
    std::string out;
    out.reserve(codepoints.size() * 4 + (bom ? 4 : 0));
    if (bom) {
        append_u32(out, 0xFEFF, little);
    }

    for (auto cp : codepoints) {
        if (!is_valid_scalar(cp)) {
            cp = replacement_char;
        }
        append_u32(out, static_cast<std::uint32_t>(cp), little);
    }

    return out;
}

/// 移除指定编码下可识别的 BOM，并返回剩余字节视图。
inline LStringDetail::string_view trim_bom(LStringDetail::string_view bytes, LEncoding encoding) {
    if ((encoding == LEncoding::Utf8 || encoding == LEncoding::Utf8Bom ||
         encoding == LEncoding::Unknown) &&
        has_prefix(bytes, utf8_bom())) {
        return bytes.substr(3);
    }
    if ((encoding == LEncoding::Utf32Le || encoding == LEncoding::Unknown) &&
        has_prefix(bytes, utf32le_bom())) {
        return bytes.substr(4);
    }
    if ((encoding == LEncoding::Utf32Be || encoding == LEncoding::Unknown) &&
        has_prefix(bytes, utf32be_bom())) {
        return bytes.substr(4);
    }
    if ((encoding == LEncoding::Utf16Le || encoding == LEncoding::Unknown) &&
        has_prefix(bytes, utf16le_bom())) {
        return bytes.substr(2);
    }
    if ((encoding == LEncoding::Utf16Be || encoding == LEncoding::Unknown) &&
        has_prefix(bytes, utf16be_bom())) {
        return bytes.substr(2);
    }
    return bytes;
}

/// 判断是否为平台编码后端处理的简体中文传统编码。
inline bool is_legacy_chinese_encoding(LEncoding encoding) noexcept {
    return encoding == LEncoding::Gbk || encoding == LEncoding::Gb2312;
}

#if defined(_WIN32)

/// 将中文传统编码映射到 Windows 代码页编号。
inline unsigned int windows_code_page(LEncoding encoding) {
    switch (encoding) {
    case LEncoding::Gbk:
        return 936;
    case LEncoding::Gb2312:
        // CP936 是 Windows 上最常见的 GBK 超集，也可处理 GB2312 文本。
        return 936;
    default:
        throw std::invalid_argument("encoding is not a Windows code page");
    }
}

/// 将 Windows 多字节代码页字节转换为宽字符串。
inline std::wstring multibyte_to_wide(LStringDetail::string_view input, unsigned int code_page,
                                      bool strict) {
    if (input.empty()) {
        return {};
    }
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("input is too large for Windows conversion API");
    }

    auto flags = strict ? MB_ERR_INVALID_CHARS : 0;
    auto input_size = static_cast<int>(input.size());
    auto wide_size = MultiByteToWideChar(code_page, flags, input.data(), input_size, nullptr, 0);
    if (wide_size <= 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "MultiByteToWideChar failed");
    }

    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    auto written = MultiByteToWideChar(code_page, flags, input.data(), input_size, wide.data(),
                                       wide_size);
    if (written <= 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "MultiByteToWideChar failed");
    }
    return wide;
}

/// 将宽字符串转换为 Windows 多字节代码页字节。
inline std::string wide_to_multibyte(LStringDetail::wstring_view input, unsigned int code_page,
                                     bool strict) {
    if (input.empty()) {
        return {};
    }
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("input is too large for Windows conversion API");
    }

    auto flags = strict ? WC_NO_BEST_FIT_CHARS : 0;
    auto input_size = static_cast<int>(input.size());
    BOOL used_default = FALSE;
    auto used_default_ptr = strict ? &used_default : nullptr;
    auto byte_size = WideCharToMultiByte(code_page, flags, input.data(), input_size, nullptr, 0,
                                         nullptr, used_default_ptr);
    if (byte_size <= 0 || (strict && used_default)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "WideCharToMultiByte failed");
    }

    std::string out(static_cast<std::size_t>(byte_size), '\0');
    used_default = FALSE;
    auto written = WideCharToMultiByte(code_page, flags, input.data(), input_size, out.data(),
                                       byte_size, nullptr, used_default_ptr);
    if (written <= 0 || (strict && used_default)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "WideCharToMultiByte failed");
    }
    return out;
}

#else

/// 返回 iconv 可识别的编码名称。
inline const char* iconv_name(LEncoding encoding) {
    switch (encoding) {
    case LEncoding::Gbk:
        return "GBK";
    case LEncoding::Gb2312:
        return "GB2312";
    case LEncoding::Utf8:
    case LEncoding::Utf8Bom:
        return "UTF-8";
    default:
        throw std::invalid_argument("encoding is not supported by iconv backend");
    }
}

/// 判断 iconv 输出目标是否为本头文件使用的 UTF-8 名称。
inline bool iconv_target_is_utf8(const char* to_encoding) noexcept {
    return LStringDetail::string_view(to_encoding) == "UTF-8";
}

/// 使用 iconv 在两个外部编码之间转换字节。
inline std::string iconv_convert(LStringDetail::string_view input, const char* from_encoding,
                                 const char* to_encoding, bool strict) {
#if LSTRING_HAS_ICONV
    auto cd = iconv_open(to_encoding, from_encoding);
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        throw std::runtime_error(fmt::format("iconv_open failed for {} -> {}", from_encoding,
                                             to_encoding));
    }

    struct IconvCloser {
        iconv_t cd;
        ~IconvCloser() {
            iconv_close(cd);
        }
    } closer {cd};

    auto* in = const_cast<char*>(input.data());
    auto in_left = input.size();
    std::array<char, 4096> buffer {};
    std::string out;
    out.reserve(input.size() * 2 + 16);

    while (in_left > 0) {
        auto* out_ptr = buffer.data();
        auto out_left = buffer.size();
        auto result = iconv(cd, &in, &in_left, &out_ptr, &out_left);
        out.append(buffer.data(), buffer.size() - out_left);

        if (result != static_cast<std::size_t>(-1)) {
            continue;
        }
        if (errno == E2BIG) {
            continue;
        }
        if (strict) {
            throw std::runtime_error(fmt::format("iconv failed for {} -> {}: {}", from_encoding,
                                                 to_encoding, std::strerror(errno)));
        }
        if (errno == EILSEQ || errno == EINVAL) {
            ++in;
            --in_left;
            if (iconv_target_is_utf8(to_encoding)) {
                append_utf8(out, replacement_char);
            } else {
                out.push_back('?');
            }
            iconv(cd, nullptr, nullptr, nullptr, nullptr);
            continue;
        }
        throw std::runtime_error(fmt::format("iconv failed for {} -> {}: {}", from_encoding,
                                             to_encoding, std::strerror(errno)));
    }

    while (true) {
        auto* out_ptr = buffer.data();
        auto out_left = buffer.size();
        auto result = iconv(cd, nullptr, nullptr, &out_ptr, &out_left);
        out.append(buffer.data(), buffer.size() - out_left);
        if (result != static_cast<std::size_t>(-1)) {
            break;
        }
        if (errno != E2BIG) {
            if (strict) {
                throw std::runtime_error(fmt::format("iconv flush failed: {}", std::strerror(errno)));
            }
            break;
        }
    }

    return out;
#else
    (void)input;
    (void)from_encoding;
    (void)to_encoding;
    (void)strict;
    throw std::runtime_error("GBK/GB2312 conversion requires iconv on this platform");
#endif // LSTRING_HAS_ICONV
}

#endif // defined(_WIN32)

/// 将平台宽字符串视图转换为 UTF-8。
inline std::string wide_to_utf8(LStringDetail::wstring_view value, bool strict) {
    std::vector<char32_t> codepoints;
    codepoints.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        auto cp = static_cast<char32_t>(value[i]);
        if (sizeof(wchar_t) == 2) {
            if (is_high_surrogate(cp)) {
                if (i + 1 >= value.size() ||
                    !is_low_surrogate(static_cast<char32_t>(value[i + 1]))) {
                    if (strict) {
                        throw std::runtime_error("invalid wide surrogate pair");
                    }
                    codepoints.push_back(replacement_char);
                    continue;
                }
                auto lo = static_cast<char32_t>(value[++i]);
                codepoints.push_back(0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00)));
            } else if (is_low_surrogate(cp)) {
                if (strict) {
                    throw std::runtime_error("unpaired wide low surrogate");
                }
                codepoints.push_back(replacement_char);
            } else {
                codepoints.push_back(cp);
            }
        } else {
            if (!is_valid_scalar(cp)) {
                if (strict) {
                    throw std::runtime_error("invalid wide code point");
                }
                cp = replacement_char;
            }
            codepoints.push_back(cp);
        }
    }

    return encode_utf8(codepoints);
}

/// 使用当前平台后端将 GBK/GB2312 字节转换为 UTF-8。
inline std::string legacy_to_utf8(LStringDetail::string_view input, LEncoding encoding, bool strict) {
    if (!is_legacy_chinese_encoding(encoding)) {
        throw std::invalid_argument("encoding is not GBK/GB2312");
    }

#if defined(_WIN32)
    auto wide = multibyte_to_wide(input, windows_code_page(encoding), strict);
    return wide_to_utf8(wide, strict);
#else
    return iconv_convert(input, iconv_name(encoding), "UTF-8", strict);
#endif // defined(_WIN32)
}

/// 使用当前平台后端将 UTF-8 字节转换为 GBK/GB2312。
inline std::string utf8_to_legacy(LStringDetail::string_view input, LEncoding encoding, bool strict) {
    if (!is_legacy_chinese_encoding(encoding)) {
        throw std::invalid_argument("encoding is not GBK/GB2312");
    }

#if defined(_WIN32)
    auto wide = multibyte_to_wide(input, CP_UTF8, strict);
    return wide_to_multibyte(wide, windows_code_page(encoding), strict);
#else
    return iconv_convert(input, "UTF-8", iconv_name(encoding), strict);
#endif // defined(_WIN32)
}

/// 探测字节是否可以按某个中文传统编码解码。
inline bool can_decode_legacy(LStringDetail::string_view input, LEncoding encoding) noexcept {
    try {
        (void)legacy_to_utf8(input, encoding, true);
        return true;
    } catch (...) {
        return false;
    }
}

/// 返回 Base64 编码表。
inline const char* base64_table() noexcept {
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

/// 判断字节是否为 Base64 解码时可忽略的 ASCII 空白。
inline bool is_base64_space(char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

/// 将单个 Base64 字符映射为 6 位值；非法字符返回 -1。
inline int base64_value(char ch) noexcept {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

/// 将任意字节编码为标准 Base64 文本。
inline std::string base64_encode(LStringDetail::string_view bytes) {
    const char* table = base64_table();
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        auto b0 = static_cast<unsigned char>(bytes[i]);
        auto b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        auto b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;

        out.push_back(table[(b0 >> 2) & 0x3F]);
        out.push_back(table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
        out.push_back(i + 1 < bytes.size() ? table[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=');
        out.push_back(i + 2 < bytes.size() ? table[b2 & 0x3F] : '=');
    }

    return out;
}

/// 解码 Base64 文本；strict 为 true 时遇到非法字符或非法填充会抛出异常。
inline std::string base64_decode(LStringDetail::string_view encoded, bool strict) {
    std::string clean;
    clean.reserve(encoded.size());

    for (std::size_t i = 0; i < encoded.size(); ++i) {
        char ch = encoded[i];
        if (is_base64_space(ch)) {
            continue;
        }
        if (ch == '=' || base64_value(ch) >= 0) {
            clean.push_back(ch);
            continue;
        }
        if (strict) {
            throw std::runtime_error("invalid Base64 character");
        }
    }

    if (clean.empty()) {
        return {};
    }
    if (clean.size() % 4 == 1) {
        if (strict) {
            throw std::runtime_error("invalid Base64 length");
        }
        return {};
    }
    while (clean.size() % 4 != 0) {
        clean.push_back('=');
    }

    std::string out;
    out.reserve((clean.size() / 4) * 3);

    for (std::size_t i = 0; i < clean.size(); i += 4) {
        int vals[4] = {0, 0, 0, 0};
        int padding = 0;

        for (int j = 0; j < 4; ++j) {
            char ch = clean[i + static_cast<std::size_t>(j)];
            if (ch == '=') {
                ++padding;
                vals[j] = 0;
                continue;
            }
            if (padding > 0 && strict) {
                throw std::runtime_error("invalid Base64 padding");
            }
            vals[j] = base64_value(ch);
            if (vals[j] < 0) {
                if (strict) {
                    throw std::runtime_error("invalid Base64 character");
                }
                vals[j] = 0;
            }
        }

        if (padding > 0 && i + 4 != clean.size() && strict) {
            throw std::runtime_error("Base64 padding before final block");
        }
        if (padding > 2 && strict) {
            throw std::runtime_error("invalid Base64 padding");
        }
        if (strict) {
            if (clean[i + 2] == '=' && (vals[1] & 0x0F) != 0) {
                throw std::runtime_error("invalid Base64 padding bits");
            }
            if (clean[i + 2] != '=' && clean[i + 3] == '=' && (vals[2] & 0x03) != 0) {
                throw std::runtime_error("invalid Base64 padding bits");
            }
        }

        out.push_back(static_cast<char>((vals[0] << 2) | (vals[1] >> 4)));
        if (clean[i + 2] != '=') {
            out.push_back(static_cast<char>(((vals[1] & 0x0F) << 4) | (vals[2] >> 2)));
        }
        if (clean[i + 3] != '=') {
            out.push_back(static_cast<char>(((vals[2] & 0x03) << 6) | vals[3]));
        }
    }

    return out;
}

/// MD5 使用的循环左移。
inline std::uint32_t md5_rotate_left(std::uint32_t value, std::uint32_t shift) noexcept {
    return (value << shift) | (value >> (32 - shift));
}

/// 计算 MD5 的 16 字节摘要。
inline std::array<unsigned char, 16> md5_digest(LStringDetail::string_view input) {
    static const std::uint32_t shifts[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    static const std::uint32_t constants[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };

    std::vector<unsigned char> message(input.begin(), input.end());
    std::uint64_t bit_length = static_cast<std::uint64_t>(message.size()) * 8;

    message.push_back(0x80);
    while ((message.size() % 64) != 56) {
        message.push_back(0);
    }
    for (int i = 0; i < 8; ++i) {
        message.push_back(static_cast<unsigned char>((bit_length >> (8 * i)) & 0xFF));
    }

    std::uint32_t a0 = 0x67452301;
    std::uint32_t b0 = 0xefcdab89;
    std::uint32_t c0 = 0x98badcfe;
    std::uint32_t d0 = 0x10325476;

    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::uint32_t words[16] = {};
        for (int i = 0; i < 16; ++i) {
            std::size_t j = offset + static_cast<std::size_t>(i) * 4;
            words[i] = static_cast<std::uint32_t>(message[j]) |
                       (static_cast<std::uint32_t>(message[j + 1]) << 8) |
                       (static_cast<std::uint32_t>(message[j + 2]) << 16) |
                       (static_cast<std::uint32_t>(message[j + 3]) << 24);
        }

        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;

        for (std::uint32_t i = 0; i < 64; ++i) {
            std::uint32_t f = 0;
            std::uint32_t g = 0;

            if (i < 16) {
                f = (b & c) | ((~b) & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | ((~d) & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | (~d));
                g = (7 * i) % 16;
            }

            auto temp = d;
            d = c;
            c = b;
            b = b + md5_rotate_left(a + f + constants[i] + words[g], shifts[i]);
            a = temp;
        }

        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::array<unsigned char, 16> digest {};
    std::uint32_t state[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) {
        digest[static_cast<std::size_t>(i) * 4] = static_cast<unsigned char>(state[i] & 0xFF);
        digest[static_cast<std::size_t>(i) * 4 + 1] = static_cast<unsigned char>((state[i] >> 8) & 0xFF);
        digest[static_cast<std::size_t>(i) * 4 + 2] = static_cast<unsigned char>((state[i] >> 16) & 0xFF);
        digest[static_cast<std::size_t>(i) * 4 + 3] = static_cast<unsigned char>((state[i] >> 24) & 0xFF);
    }
    return digest;
}

/// 将二进制摘要转换为小写十六进制文本。
inline std::string bytes_to_hex(const unsigned char* bytes, std::size_t size) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        auto value = bytes[i];
        out.push_back(hex[(value >> 4) & 0x0F]);
        out.push_back(hex[value & 0x0F]);
    }
    return out;
}

/// 将 MD5 的 16 字节摘要转换为小写十六进制文本。
inline std::string md5_hex(LStringDetail::string_view input) {
    auto digest = md5_digest(input);
    return bytes_to_hex(digest.data(), digest.size());
}

/// 将 std::string 适配为 string_view，供通用拼接类 API 使用。
inline LStringDetail::string_view as_view(const std::string& value) noexcept {
    return value;
}

/// 保持 LStringDetail::string_view 不变，供通用拼接类 API 使用。
inline LStringDetail::string_view as_view(LStringDetail::string_view value) noexcept {
    return value;
}

/// 将 C 字符串适配为 string_view；nullptr 视为空字符串。
inline LStringDetail::string_view as_view(const char* value) noexcept {
    return value ? LStringDetail::string_view(value) : LStringDetail::string_view();
}

/// 禁止把单个 char 误当作 string_view。
inline LStringDetail::string_view as_view(char value) = delete;

#if LSTRING_HAS_RANGES
/// 表示可无损视作 string_view 的类型。
template<class T>
concept StringLike = requires(const T& value) {
    { as_view(value) } -> std::convertible_to<LStringDetail::string_view>;
};

/// 判断一个 range 是否产出 char 字节。
template<class Range>
concept CharRange =
    std::ranges::input_range<Range> &&
    std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, char>;
#endif // LSTRING_HAS_RANGES

} // namespace LStringDetail

/**
 * @brief 围绕 std::string 的纯头文件薄封装，并提供 UTF-8 方向的辅助能力。
 *
 * LString 在存储、索引、长度、迭代器和大多数字节级操作上刻意保持
 * std::string 语义。Unicode 转换函数会把内部字节视为 UTF-8；除非特别说明，
 * substr()、find()、split()、trim() 等基础操作都按字节或 ASCII 分隔符工作。
 */
class LString {
private:
    std::string data_;

public:
    using value_type = char;
    using size_type = std::string::size_type;
    using iterator = std::string::iterator;
    using const_iterator = std::string::const_iterator;
    using reverse_iterator = std::string::reverse_iterator;
    using const_reverse_iterator = std::string::const_reverse_iterator;

    static constexpr size_type npos = std::string::npos;

    /// 创建空字符串。
    LString() = default;

    /// 复制另一个 LString，精确保留其存储字节。
    LString(const LString&) = default;

    /// 移动另一个 LString，不改变字节内容。
    LString(LString&&) noexcept = default;

    /// 从另一个 LString 复制赋值。
    LString& operator=(const LString&) = default;

    /// 从另一个 LString 移动赋值。
    LString& operator=(LString&&) noexcept = default;

    /// 从以 NUL 结尾的 C 字符串构造；nullptr 视为空字符串。
    LString(const char* value)
        : data_(value ? value : "") {}

    /// 从带显式长度的字节缓冲区构造。
    LString(const char* value, size_type count) {
        if (!value) {
            if (count != 0) {
                throw std::invalid_argument("LString cannot copy non-empty null data");
            }
            return;
        }
        data_.assign(value, count);
    }

    /// 构造包含 @p count 个 @p value 字节的字符串。
    LString(char value, size_type count = 1)
        : data_(count, value) {}

    /// 接管已有 std::string 的内容。
    LString(std::string value)
        : data_(std::move(value)) {}

    /// 从 LStringDetail::string_view 复制字节。
    LString(LStringDetail::string_view value)
        : data_(value) {}

#if LSTRING_HAS_FILESYSTEM
    /// 使用 path::string() 从文件系统路径构造。
    LString(const std::filesystem::path& path)
        : data_(path.string()) {}
#endif // LSTRING_HAS_FILESYSTEM

    /// 将宽字符串转换为内部 UTF-8 存储。
    LString(LStringDetail::wstring_view value)
        : data_(from_wstring(value).data_) {}

#if LSTRING_HAS_MAGIC_ENUM
    /// 使用 magic_enum 将枚举值序列化为枚举项名称；未知值得到空字符串。
    template<class E,
             typename std::enable_if<std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
                                     int>::type = 0>
    LString(E value)
        : data_(std::string(magic_enum::enum_name(value))) {}
#endif // LSTRING_HAS_MAGIC_ENUM

#if LSTRING_HAS_RANGES
    /// 从任意 char range 复制字节；字符串类已有专门构造函数，因此这里排除它们。
    template<class Range>
        requires LStringDetail::CharRange<Range> &&
                 (!LStringDetail::StringLike<std::remove_cvref_t<Range>>) &&
                 (!std::same_as<std::remove_cvref_t<Range>, LString>)
    explicit LString(Range&& range) {
        append_range(std::forward<Range>(range));
    }

    /// 从任意 fmt 可格式化对象构造；字符串和 char range 保持原有字节语义。
    template<class T>
        requires fmt::is_formattable<LStringDetail::remove_cvref_t<T>, char>::value &&
                 (!LStringDetail::is_LString_text_source<T>::value) &&
                 (!LStringDetail::is_magic_enum_source<T>::value) &&
                 (!std::same_as<LStringDetail::remove_cvref_t<T>, LString>) &&
                 (!LStringDetail::CharRange<T>)
    LString(const T& value)
        : data_(fmt::format("{}", value)) {}
#else
    /// 从任意 fmt 可格式化对象构造；字符串来源保持原有字节语义。
    template<class T,
             typename std::enable_if<
                 fmt::is_formattable<LStringDetail::remove_cvref_t<T>, char>::value &&
                     !LStringDetail::is_LString_text_source<T>::value &&
                     !LStringDetail::is_magic_enum_source<T>::value &&
                     !std::is_same<LStringDetail::remove_cvref_t<T>, LString>::value,
                 int>::type = 0>
    LString(const T& value)
        : data_(fmt::format("{}", value)) {}
#endif // LSTRING_HAS_RANGES

    /// 从以 NUL 结尾的 C 字符串赋值；nullptr 视为空字符串。
    LString& operator=(const char* value) {
        data_ = value ? value : "";
        return *this;
    }

    /// 从 std::string 移动字节赋值。
    LString& operator=(std::string value) {
        data_ = std::move(value);
        return *this;
    }

    /// 从 LStringDetail::string_view 复制字节赋值。
    LString& operator=(LStringDetail::string_view value) {
        data_ = value;
        return *this;
    }

#if LSTRING_HAS_MAGIC_ENUM
    /// 使用 magic_enum 将枚举值序列化为枚举项名称并赋值；未知值得到空字符串。
    template<class E,
             typename std::enable_if<std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
                                     int>::type = 0>
    LString& operator=(E value) {
        data_ = std::string(magic_enum::enum_name(value));
        return *this;
    }
#endif // LSTRING_HAS_MAGIC_ENUM

#if LSTRING_HAS_RANGES
    /// 使用 fmt 将任意可格式化对象字符串化后赋值。
    template<class T>
        requires fmt::is_formattable<LStringDetail::remove_cvref_t<T>, char>::value &&
                 (!LStringDetail::is_LString_text_source<T>::value) &&
                 (!LStringDetail::is_magic_enum_source<T>::value) &&
                 (!std::same_as<LStringDetail::remove_cvref_t<T>, LString>) &&
                 (!LStringDetail::CharRange<T>)
    LString& operator=(const T& value) {
        data_ = fmt::format("{}", value);
        return *this;
    }
#else
    /// 使用 fmt 将任意可格式化对象字符串化后赋值。
    template<class T,
             typename std::enable_if<
                 fmt::is_formattable<LStringDetail::remove_cvref_t<T>, char>::value &&
                     !LStringDetail::is_LString_text_source<T>::value &&
                     !LStringDetail::is_magic_enum_source<T>::value &&
                     !std::is_same<LStringDetail::remove_cvref_t<T>, LString>::value,
                 int>::type = 0>
    LString& operator=(const T& value) {
        data_ = fmt::format("{}", value);
        return *this;
    }
#endif // LSTRING_HAS_RANGES

    /// 对左值对象隐式暴露底层 std::string 的可写引用。
    operator std::string&() & noexcept {
        return data_;
    }

    /// 隐式暴露底层 std::string 的只读引用。
    operator const std::string&() const& noexcept {
        return data_;
    }

    /// 从右值 LString 中移出底层 std::string。
    operator std::string() && noexcept {
        return std::move(data_);
    }

    /// 隐式生成存储字节的只读视图。
    operator LStringDetail::string_view() const noexcept {
        return data_;
    }

    /// 返回底层 std::string 的可写引用。
    std::string& str() & noexcept {
        return data_;
    }

    /// 返回底层 std::string 的只读引用。
    const std::string& str() const& noexcept {
        return data_;
    }

    /// 从右值 LString 中移出底层 std::string。
    std::string&& str() && noexcept {
        return std::move(data_);
    }

    /// 返回指向存储字节的非拥有视图。
    LStringDetail::string_view view() const noexcept {
        return data_;
    }

    /// 返回非拥有的字节子视图；pos 越界时行为同 LStringDetail::string_view::substr。
    LStringDetail::string_view subview(size_type pos = 0, size_type count = npos) const {
        return LStringDetail::string_view(data_).substr(pos, count);
    }

    /// 返回以 NUL 结尾的指针；下次修改前保持有效。
    const char* c_str() const noexcept {
        return data_.c_str();
    }

    /// 返回指向存储字节的指针。
    const char* data() const noexcept {
        return data_.data();
    }

    /// 返回指向存储字节的可写指针。
    char* data() noexcept {
#if __cplusplus >= 201703L
        return data_.data();
#else
        return data_.empty() ? nullptr : &data_[0];
#endif // __cplusplus >= 201703L
    }

    /// 判断当前是否未存储任何字节。
    bool empty() const noexcept {
        return data_.empty();
    }

    /// 返回字节长度，不是 Unicode 码点数量。
    size_type size() const noexcept {
        return data_.size();
    }

    /// size() 的同义函数；返回字节长度。
    size_type length() const noexcept {
        return data_.length();
    }

    /// 返回底层 std::string 当前容量。
    size_type capacity() const noexcept {
        return data_.capacity();
    }

    /// 为底层 std::string 预留容量。
    void reserve(size_type capacity) {
        data_.reserve(capacity);
    }

    /// 清空所有存储字节。
    void clear() noexcept {
        data_.clear();
    }

    /// 调整底层字节串长度。
    void resize(size_type count) {
        data_.resize(count);
    }

#if LSTRING_HAS_SPAN
    /// 返回可写字节 span，便于和 std::span/std::ranges API 协作。
    std::span<char> bytes() noexcept {
        return std::span<char>(data_.data(), data_.size());
    }

    /// 返回只读字节 span，便于和 std::span/std::ranges API 协作。
    std::span<const char> bytes() const noexcept {
        return std::span<const char>(data_.data(), data_.size());
    }
#endif // LSTRING_HAS_SPAN

    /// 不做边界检查的可写字节访问。
    char& operator[](size_type pos) noexcept {
        return data_[pos];
    }

    /// 不做边界检查的只读字节访问。
    const char& operator[](size_type pos) const noexcept {
        return data_[pos];
    }

    /// 做边界检查的可写字节访问；索引非法时抛出 std::out_of_range。
    char& at(size_type pos) {
        return data_.at(pos);
    }

    /// 做边界检查的只读字节访问；索引非法时抛出 std::out_of_range。
    const char& at(size_type pos) const {
        return data_.at(pos);
    }

    /// 访问第一个字节的可写引用；要求字符串非空。
    char& front() noexcept {
        return data_.front();
    }

    /// 访问第一个字节的只读引用；要求字符串非空。
    const char& front() const noexcept {
        return data_.front();
    }

    /// 访问最后一个字节的可写引用；要求字符串非空。
    char& back() noexcept {
        return data_.back();
    }

    /// 访问最后一个字节的只读引用；要求字符串非空。
    const char& back() const noexcept {
        return data_.back();
    }

    /// 返回指向第一个字节的可写迭代器。
    iterator begin() noexcept {
        return data_.begin();
    }

    /// 返回指向第一个字节的只读迭代器。
    const_iterator begin() const noexcept {
        return data_.begin();
    }

    /// 返回指向第一个字节的只读迭代器。
    const_iterator cbegin() const noexcept {
        return data_.cbegin();
    }

    /// 返回末尾后一位的可写迭代器。
    iterator end() noexcept {
        return data_.end();
    }

    /// 返回末尾后一位的只读迭代器。
    const_iterator end() const noexcept {
        return data_.end();
    }

    /// 返回末尾后一位的只读迭代器。
    const_iterator cend() const noexcept {
        return data_.cend();
    }

    /// 返回指向最后一个字节的反向迭代器。
    reverse_iterator rbegin() noexcept {
        return data_.rbegin();
    }

    /// 返回指向最后一个字节的只读反向迭代器。
    const_reverse_iterator rbegin() const noexcept {
        return data_.rbegin();
    }

    /// 返回指向最后一个字节的只读反向迭代器。
    const_reverse_iterator crbegin() const noexcept {
        return data_.crbegin();
    }

    /// 返回反向遍历终点。
    reverse_iterator rend() noexcept {
        return data_.rend();
    }

    /// 返回只读反向遍历终点。
    const_reverse_iterator rend() const noexcept {
        return data_.rend();
    }

    /// 返回只读反向遍历终点。
    const_reverse_iterator crend() const noexcept {
        return data_.crend();
    }

    /// 从 string_view 追加原始字节，并返回 *this 以便链式调用。
    LString& append(LStringDetail::string_view value) {
        data_.append(value);
        return *this;
    }

    /// 追加一个字节/字符，并返回 *this 以便链式调用。
    LString& append(char value) {
        data_.push_back(value);
        return *this;
    }

#if LSTRING_HAS_RANGES
    /// 追加任意 char range 产生的字节，并返回 *this 以便链式调用。
    template<class Range>
        requires LStringDetail::CharRange<Range>
    LString& append_range(Range&& range) {
        if constexpr (std::ranges::sized_range<Range>) {
            data_.reserve(data_.size() + static_cast<size_type>(std::ranges::size(range)));
        }
        for (char ch : range) {
            data_.push_back(ch);
        }
        return *this;
    }

    /// 清空当前内容，并用 char range 产生的字节重新赋值。
    template<class Range>
        requires LStringDetail::CharRange<Range>
    LString& assign_range(Range&& range) {
        data_.clear();
        return append_range(std::forward<Range>(range));
    }

    /// 从任意 char range 构造 LString，适合 CTAD 不方便的调用点。
    template<class Range>
        requires LStringDetail::CharRange<Range>
    static LString from_range(Range&& range) {
        LString out;
        out.append_range(std::forward<Range>(range));
        return out;
    }
#endif // LSTRING_HAS_RANGES

    /**
     * @brief 使用 fmt 格式化并追加结果。
     *
     * 在 fmt 支持的场景下，格式字符串会进行编译期检查。
     */
    template<class... Args>
    LString& append_format(fmt::format_string<Args...> fmtstr, Args&&... args) {
        fmt::format_to(std::back_inserter(data_), fmtstr, std::forward<Args>(args)...);
        return *this;
    }

    /// 从 string_view 追加原始字节。
    LString& operator+=(LStringDetail::string_view value) {
        return append(value);
    }

    /// 追加以 NUL 结尾的 C 字符串；nullptr 不追加任何内容。
    LString& operator+=(const char* value) {
        return append(value ? LStringDetail::string_view(value) : LStringDetail::string_view());
    }

    /// 追加一个字节/字符。
    LString& operator+=(char value) {
        return append(value);
    }

    /// 拼接 LString 与类字符串字节。
    friend LString operator+(LString lhs, LStringDetail::string_view rhs) {
        lhs += rhs;
        return lhs;
    }

    /// 拼接类字符串字节与 LString。
    friend LString operator+(LStringDetail::string_view lhs, const LString& rhs) {
        LString out(lhs);
        out += rhs.view();
        return out;
    }

#if __cplusplus >= 202002L
    /// 按字节字典序进行三路比较。
    friend auto operator<=>(const LString&, const LString&) = default;
#endif // __cplusplus >= 202002L

    /// 按存储字节比较两个 LString。
    friend bool operator==(const LString& lhs, const LString& rhs) noexcept {
        return lhs.view() == rhs.view();
    }

    /// 按字节比较 LString 与 string_view。
    friend bool operator==(const LString& lhs, LStringDetail::string_view rhs) noexcept {
        return lhs.view() == rhs;
    }

    /// 按字节比较 string_view 与 LString。
    friend bool operator==(LStringDetail::string_view lhs, const LString& rhs) noexcept {
        return lhs == rhs.view();
    }

    /// 按字节比较 LString 与 std::string，避免隐式转换二义性。
    friend bool operator==(const LString& lhs, const std::string& rhs) noexcept {
        return lhs.view() == LStringDetail::string_view(rhs);
    }

    /// 按字节比较 std::string 与 LString，避免隐式转换二义性。
    friend bool operator==(const std::string& lhs, const LString& rhs) noexcept {
        return LStringDetail::string_view(lhs) == rhs.view();
    }

    /// 按字节比较 LString 与 C 字符串；nullptr 视为空字符串。
    friend bool operator==(const LString& lhs, const char* rhs) noexcept {
        return lhs.view() == (rhs ? LStringDetail::string_view(rhs) : LStringDetail::string_view());
    }

    /// 按字节比较 C 字符串与 LString；nullptr 视为空字符串。
    friend bool operator==(const char* lhs, const LString& rhs) noexcept {
        return (lhs ? LStringDetail::string_view(lhs) : LStringDetail::string_view()) == rhs.view();
    }

    /// 将存储字节写入 ostream。
    friend std::ostream& operator<<(std::ostream& os, const LString& value) {
        return os.write(value.data(), static_cast<std::streamsize>(value.size()));
    }

    /// 判断字节子串是否出现在任意位置。
    bool contains(LStringDetail::string_view needle) const noexcept {
        return data_.find(needle) != npos;
    }

    /// 判断字符串是否以指定字节前缀开头。
    bool starts_with(LStringDetail::string_view prefix) const noexcept {
        return LStringDetail::has_prefix(view(), prefix);
    }

    /// 判断字符串是否以指定字节后缀结尾。
    bool ends_with(LStringDetail::string_view suffix) const noexcept {
        return data_.size() >= suffix.size() &&
               LStringDetail::string_view(data_.data() + data_.size() - suffix.size(),
                                            suffix.size()) == suffix;
    }

    /// 从 @p pos 开始查找字节子串；不存在时返回 npos。
    size_type find(LStringDetail::string_view needle, size_type pos = 0) const noexcept {
        return data_.find(needle, pos);
    }

    /// 查找 @p pos 之前或当前位置最后一次出现的字节子串。
    size_type rfind(LStringDetail::string_view needle, size_type pos = npos) const noexcept {
        return data_.rfind(needle, pos);
    }

    /// 返回拥有所有权的字节子串。
    LString substr(size_type pos = 0, size_type count = npos) const {
        return data_.substr(pos, count);
    }

    /// 返回将当前字节序列重复 @p count 次形成的新字符串。
    LString repeat(size_type count) const {
        LString out;
        if (!data_.empty() && count > data_.max_size() / data_.size()) {
            throw std::length_error("LString repeat result is too large");
        }
        out.data_.reserve(data_.size() * count);
        for (size_type i = 0; i < count; ++i) {
            out.data_ += data_;
        }
        return out;
    }

    /// 在 @p pos 处插入字节，并返回 *this。
    LString& insert(size_type pos, LStringDetail::string_view value) {
        data_.insert(pos, value);
        return *this;
    }

    /// 从 @p pos 开始删除 @p count 个字节，并返回 *this。
    LString& erase(size_type pos = 0, size_type count = npos) {
        data_.erase(pos, count);
        return *this;
    }

    /// 用 @p value 替换 @p pos 处开始的 @p count 个字节，并返回 *this。
    LString& replace(size_type pos, size_type count, LStringDetail::string_view value) {
        data_.replace(pos, count, value);
        return *this;
    }

    /// 返回副本，其中每个非重叠的 @p from 都被替换。
    LString replaced_all(LStringDetail::string_view from, LStringDetail::string_view to) const {
        LString out(*this);
        out.replace_all(from, to);
        return out;
    }

    /// 原地替换每个非重叠的 @p from；空 @p from 会被忽略。
    LString& replace_all(LStringDetail::string_view from, LStringDetail::string_view to) {
        if (from.empty()) {
            return *this;
        }

        size_type pos = 0;
        while ((pos = data_.find(from, pos)) != npos) {
            data_.replace(pos, from.size(), to);
            pos += to.size();
        }
        return *this;
    }

    /// 仅在存在 @p prefix 时移除该前缀。
    LString& remove_prefix(LStringDetail::string_view prefix) {
        if (starts_with(prefix)) {
            data_.erase(0, prefix.size());
        }
        return *this;
    }

    /// 仅在存在 @p suffix 时移除该后缀。
    LString& remove_suffix(LStringDetail::string_view suffix) {
        if (ends_with(suffix)) {
            data_.erase(data_.size() - suffix.size());
        }
        return *this;
    }

    /// 判断是否为 trim 辅助函数识别的 ASCII 空白字节。
    static bool is_space_ascii(char ch) noexcept {
        auto c = static_cast<unsigned char>(ch);
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

    /// 返回移除首尾 ASCII 空白后的副本。
    LString trimmed() const {
        auto first = std::find_if_not(data_.begin(), data_.end(), is_space_ascii);
        auto last = std::find_if_not(data_.rbegin(), data_.rend(), is_space_ascii).base();
        if (first >= last) {
            return {};
        }
        return LStringDetail::string_view(&*first, static_cast<size_type>(last - first));
    }

    /// 返回移除开头 ASCII 空白后的副本。
    LString ltrimmed() const {
        auto first = std::find_if_not(data_.begin(), data_.end(), is_space_ascii);
        if (first == data_.end()) {
            return {};
        }
        return LStringDetail::string_view(&*first, static_cast<size_type>(data_.end() - first));
    }

    /// 返回移除结尾 ASCII 空白后的副本。
    LString rtrimmed() const {
        auto last = std::find_if_not(data_.rbegin(), data_.rend(), is_space_ascii).base();
        return LStringDetail::string_view(data_.data(), static_cast<size_type>(last - data_.begin()));
    }

    /// 原地移除首尾 ASCII 空白。
    LString& trim() {
        *this = trimmed();
        return *this;
    }

    /// 原地移除开头 ASCII 空白。
    LString& ltrim() {
        *this = ltrimmed();
        return *this;
    }

    /// 原地移除结尾 ASCII 空白。
    LString& rtrim() {
        *this = rtrimmed();
        return *this;
    }

    /// 返回仅转换 ASCII 大写字母的小写副本；非 ASCII 字节保持不变。
    LString lower_ascii() const {
        LString out(*this);
        std::transform(out.data_.begin(), out.data_.end(), out.data_.begin(), [](unsigned char c) {
            if (c >= 'A' && c <= 'Z') {
                return static_cast<char>(c - 'A' + 'a');
            }
            return static_cast<char>(c);
        });
        return out;
    }

    /// 返回仅转换 ASCII 小写字母的大写副本；非 ASCII 字节保持不变。
    LString upper_ascii() const {
        LString out(*this);
        std::transform(out.data_.begin(), out.data_.end(), out.data_.begin(), [](unsigned char c) {
            if (c >= 'a' && c <= 'z') {
                return static_cast<char>(c - 'a' + 'A');
            }
            return static_cast<char>(c);
        });
        return out;
    }

    /**
     * @brief 按字节分隔符字符串切分。
     *
     * @param delimiter 字节分隔符；为空时每个字节成为一个元素。
     * @param keep_empty 是否保留相邻分隔符之间的空字段。
     * @param max_parts 最大结果数量；0 表示不限制。
     */
    std::vector<LString> split(LStringDetail::string_view delimiter, bool keep_empty = false,
                               size_type max_parts = 0) const {
        std::vector<LString> out;
        if (delimiter.empty()) {
            out.reserve(data_.size());
            for (char ch : data_) {
                out.emplace_back(ch);
            }
            return out;
        }

        size_type start = 0;
        size_type parts = 0;
        while (start <= data_.size()) {
            if (max_parts != 0 && parts + 1 >= max_parts) {
                auto tail = view().substr(start);
                if (keep_empty || !tail.empty()) {
                    out.emplace_back(tail);
                }
                break;
            }

            auto pos = data_.find(delimiter, start);
            auto piece = pos == npos ? view().substr(start) : view().substr(start, pos - start);
            if (keep_empty || !piece.empty()) {
                out.emplace_back(piece);
                ++parts;
            }
            if (pos == npos) {
                break;
            }
            start = pos + delimiter.size();
        }
        return out;
    }

    /// 按单字节分隔符切分。
    std::vector<LString> split(char delimiter, bool keep_empty = false, size_type max_parts = 0) const {
        return split(LStringDetail::string_view(&delimiter, 1), keep_empty, max_parts);
    }

    /// 按 '\n' 切分行，并移除每行末尾的 '\r'。
    std::vector<LString> lines(bool keep_empty = true) const {
        auto raw = split('\n', keep_empty);
        for (auto& line : raw) {
            line.remove_suffix("\r");
        }
        return raw;
    }

    /// 将 join() 中的单个 char 元素追加到输出。
    static void append_join_part(LString& out, char part) {
        out += part;
    }

    /// 将 join() 中的类字符串元素追加到输出。
    template<class Part>
    static void append_join_part(LString& out, const Part& part) {
        out += part;
    }

    /// 使用字节分隔符拼接一段类字符串 range。
    template<class Range>
    static LString join(const Range& parts, LStringDetail::string_view delimiter) {
        LString out;
        bool first = true;
        for (auto it = std::begin(parts); it != std::end(parts); ++it) {
            if (!first) {
                out += delimiter;
            }
            first = false;
            append_join_part(out, *it);
        }
        return out;
    }

    /// 使用字节分隔符拼接 initializer_list<string_view>。
    static LString join(std::initializer_list<LStringDetail::string_view> parts, LStringDetail::string_view delimiter) {
        return join<std::initializer_list<LStringDetail::string_view>>(parts, delimiter);
    }

    /// 使用 fmt 格式化，并以 LString 返回结果。
    template<class... Args>
    static LString format(fmt::format_string<Args...> fmtstr, Args&&... args) {
        return fmt::format(fmtstr, std::forward<Args>(args)...);
    }

#if LSTRING_HAS_STD_FORMAT
    /// 使用 std::format 格式化并追加结果。
    template<class... Args>
    LString& append_std_format(std::format_string<Args...> fmtstr, Args&&... args) {
        data_ += std::format(fmtstr, std::forward<Args>(args)...);
        return *this;
    }

    /// 使用 std::format 格式化并返回 LString。
    template<class... Args>
    static LString std_format(std::format_string<Args...> fmtstr, Args&&... args) {
        return std::format(fmtstr, std::forward<Args>(args)...);
    }
#endif // LSTRING_HAS_STD_FORMAT

#if LSTRING_HAS_MAGIC_ENUM
    /// 使用 magic_enum 将枚举值序列化为枚举项名称；未知值得到空字符串。
    template<class E>
    static typename std::enable_if<std::is_enum<LStringDetail::remove_cvref_t<E>>::value, LString>::type
    from_enum(E value) {
        return LStringDetail::string_view(magic_enum::enum_name(value).data(),
                                           magic_enum::enum_name(value).size());
    }

    /// from_enum() 的语义化别名。
    template<class E>
    static typename std::enable_if<std::is_enum<LStringDetail::remove_cvref_t<E>>::value, LString>::type
    enum_name(E value) {
        return from_enum(value);
    }

    /// 使用 magic_enum 反序列化枚举名；失败时返回 std::nullopt。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        std::optional<LStringDetail::remove_cvref_t<E>>>::type
    to_enum(LStringDetail::string_view text, bool case_sensitive = true) {
        using D = LStringDetail::remove_cvref_t<E>;
        auto trimmed_text = LString(text).trimmed();
        auto view = magic_enum::string_view(trimmed_text.data(), trimmed_text.size());
        if (case_sensitive) {
            return magic_enum::enum_cast<D>(view);
        }
        return magic_enum::enum_cast<D>(view, magic_enum::case_insensitive);
    }

    /// 使用 magic_enum 反序列化枚举名；nullptr 视为空字符串。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        std::optional<LStringDetail::remove_cvref_t<E>>>::type
    to_enum(const char* text, bool case_sensitive = true) {
        return to_enum<E>(LStringDetail::as_view(text), case_sensitive);
    }

    /// 使用 magic_enum 反序列化当前字符串；失败时返回 std::nullopt。
    template<class E>
    typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        std::optional<LStringDetail::remove_cvref_t<E>>>::type
    to_enum(bool case_sensitive = true) const {
        return LString::to_enum<E>(view(), case_sensitive);
    }

    /// 使用 magic_enum 反序列化枚举名；失败时返回 @p default_value。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        LStringDetail::remove_cvref_t<E>>::type
    to_enum_or(LStringDetail::string_view text, LStringDetail::remove_cvref_t<E> default_value,
               bool case_sensitive = true) {
        auto value = to_enum<E>(text, case_sensitive);
        return value ? *value : default_value;
    }

    /// 使用 magic_enum 反序列化枚举名；nullptr 视为空字符串，失败时返回 @p default_value。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        LStringDetail::remove_cvref_t<E>>::type
    to_enum_or(const char* text, LStringDetail::remove_cvref_t<E> default_value,
               bool case_sensitive = true) {
        return to_enum_or<E>(LStringDetail::as_view(text), default_value, case_sensitive);
    }

    /// 使用 magic_enum 反序列化当前字符串；失败时返回 @p default_value。
    template<class E>
    typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        LStringDetail::remove_cvref_t<E>>::type
    to_enum_or(LStringDetail::remove_cvref_t<E> default_value,
               bool case_sensitive = true) const {
        auto value = to_enum<E>(case_sensitive);
        return value ? *value : default_value;
    }

    /// 使用 magic_enum 反序列化枚举名；失败时抛出 std::runtime_error。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        LStringDetail::remove_cvref_t<E>>::type
    to_enum_checked(LStringDetail::string_view text, bool case_sensitive = true) {
        auto value = to_enum<E>(text, case_sensitive);
        if (!value) {
            throw std::runtime_error("invalid enum name: " + std::string(text));
        }
        return *value;
    }

    /// 使用 magic_enum 反序列化枚举名；nullptr 视为空字符串，失败时抛出 std::runtime_error。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        LStringDetail::remove_cvref_t<E>>::type
    to_enum_checked(const char* text, bool case_sensitive = true) {
        return to_enum_checked<E>(LStringDetail::as_view(text), case_sensitive);
    }

    /// 使用 magic_enum 反序列化当前字符串；失败时抛出 std::runtime_error。
    template<class E>
    typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        LStringDetail::remove_cvref_t<E>>::type
    to_enum_checked(bool case_sensitive = true) const {
        return LString::to_enum_checked<E>(view(), case_sensitive);
    }

    /// 判断文本是否能反序列化为指定枚举。
    template<class E>
    static typename std::enable_if<std::is_enum<LStringDetail::remove_cvref_t<E>>::value, bool>::type
    is_enum_name(LStringDetail::string_view text, bool case_sensitive = true) {
        return to_enum<E>(text, case_sensitive).has_value();
    }

    /// 判断文本是否能反序列化为指定枚举；nullptr 视为空字符串。
    template<class E>
    static typename std::enable_if<std::is_enum<LStringDetail::remove_cvref_t<E>>::value, bool>::type
    is_enum_name(const char* text, bool case_sensitive = true) {
        return is_enum_name<E>(LStringDetail::as_view(text), case_sensitive);
    }

    /// 判断当前字符串是否能反序列化为指定枚举。
    template<class E>
    typename std::enable_if<std::is_enum<LStringDetail::remove_cvref_t<E>>::value, bool>::type
    is_enum_name(bool case_sensitive = true) const {
        return is_enum_name<E>(view(), case_sensitive);
    }

    /// 返回枚举类型名。
    template<class E>
    static typename std::enable_if<std::is_enum<LStringDetail::remove_cvref_t<E>>::value, LString>::type
    enum_type_name() {
        using D = LStringDetail::remove_cvref_t<E>;
        auto name = magic_enum::enum_type_name<D>();
        return LStringDetail::string_view(name.data(), name.size());
    }

    /// 返回 magic_enum 可反射到的全部枚举名。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        std::vector<LString>>::type
    enum_names() {
        using D = LStringDetail::remove_cvref_t<E>;
        std::vector<LString> out;
        auto names = magic_enum::enum_names<D>();
        out.reserve(names.size());
        for (auto name : names) {
            out.emplace_back(LStringDetail::string_view(name.data(), name.size()));
        }
        return out;
    }

    /// 返回 magic_enum 可反射到的全部枚举值。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        std::vector<LStringDetail::remove_cvref_t<E>>>::type
    enum_values() {
        using D = LStringDetail::remove_cvref_t<E>;
        auto values = magic_enum::enum_values<D>();
        return std::vector<D>(values.begin(), values.end());
    }

    /// 返回 magic_enum 可反射到的全部枚举值和名称。
    template<class E>
    static typename std::enable_if<
        std::is_enum<LStringDetail::remove_cvref_t<E>>::value,
        std::vector<std::pair<LStringDetail::remove_cvref_t<E>, LString>>>::type
    enum_entries() {
        using D = LStringDetail::remove_cvref_t<E>;
        std::vector<std::pair<D, LString>> out;
        auto entries = magic_enum::enum_entries<D>();
        out.reserve(entries.size());
        for (auto entry : entries) {
            out.emplace_back(entry.first,
                             LStringDetail::string_view(entry.second.data(), entry.second.size()));
        }
        return out;
    }
#endif // LSTRING_HAS_MAGIC_ENUM

#if LSTRING_HAS_STD_OPTIONAL
    /**
     * @brief 将 trim 后的字符串解析为整数。
     *
     * 必须完整消费 trim 后的字符串。解析失败或溢出时返回 std::nullopt，
     * 不抛出异常。
     */
    template<class T>
    typename std::enable_if<std::is_integral<T>::value, std::optional<T>>::type
    to_number(int base = 10) const {
        auto text = trimmed().view();
        T value {};
        auto begin = text.data();
        auto end = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(begin, end, value, base);
        if (ec == std::errc() && ptr == end) {
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief 将 trim 后的字符串解析为浮点数。
     *
     * 必须完整消费 trim 后的字符串。解析失败或溢出时返回 std::nullopt，
     * 不抛出异常。
     */
    template<class T>
    typename std::enable_if<std::is_floating_point<T>::value, std::optional<T>>::type
    to_number() const {
        auto text = trimmed().view();
        T value {};
        auto begin = text.data();
        auto end = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec == std::errc() && ptr == end) {
            return value;
        }
        return std::nullopt;
    }

    /// 按指定进制将 trim 后的字符串解析为 int。
    std::optional<int> to_int(int base = 10) const {
        return to_number<int>(base);
    }

    /// 按指定进制将 trim 后的字符串解析为 long long。
    std::optional<long long> to_i64(int base = 10) const {
        return to_number<long long>(base);
    }

    /// 将 trim 后的字符串解析为 double。
    std::optional<double> to_double() const {
        return to_number<double>();
    }
#endif // LSTRING_HAS_STD_OPTIONAL

    /// 使用 fmt 默认格式化器格式化算术值。
    template<class T>
    static typename std::enable_if<std::is_arithmetic<T>::value, LString>::type from_number(T value) {
        return fmt::format("{}", value);
    }

    /**
     * @brief 推断字节序列最可能的编码。
     *
     * 对带 BOM 的 UTF 编码和合法 UTF-8 可以精确判断。对不带 BOM 的 UTF-16
     * 和中文传统编码只能启发式判断；如果调用方已知来源编码，应优先显式传入
     * LEncoding。
     */
    static LEncoding detect_encoding(LStringDetail::string_view bytes) {
        if (LStringDetail::has_prefix(bytes, LStringDetail::utf32be_bom())) {
            return LEncoding::Utf32Be;
        }
        if (LStringDetail::has_prefix(bytes, LStringDetail::utf32le_bom())) {
            return LEncoding::Utf32Le;
        }
        if (LStringDetail::has_prefix(bytes, LStringDetail::utf8_bom())) {
            return LEncoding::Utf8Bom;
        }
        if (LStringDetail::has_prefix(bytes, LStringDetail::utf16be_bom())) {
            return LEncoding::Utf16Be;
        }
        if (LStringDetail::has_prefix(bytes, LStringDetail::utf16le_bom())) {
            return LEncoding::Utf16Le;
        }

        if (LStringDetail::is_valid_utf8(bytes)) {
            auto ascii = std::all_of(bytes.begin(), bytes.end(), [](char ch) {
                return static_cast<unsigned char>(ch) <= 0x7F;
            });
            return ascii ? LEncoding::Ascii : LEncoding::Utf8;
        }

        if (bytes.size() >= 4) {
            std::size_t even_nuls = 0;
            std::size_t odd_nuls = 0;
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                if (bytes[i] == '\0') {
                    (i % 2 == 0 ? even_nuls : odd_nuls)++;
                }
            }
            if (odd_nuls > bytes.size() / 4 && even_nuls == 0) {
                return LEncoding::Utf16Le;
            }
            if (even_nuls > bytes.size() / 4 && odd_nuls == 0) {
                return LEncoding::Utf16Be;
            }
        }

        if (LStringDetail::can_decode_legacy(bytes, LEncoding::Gb2312)) {
            return LEncoding::Gb2312;
        }
        if (LStringDetail::can_decode_legacy(bytes, LEncoding::Gbk)) {
            return LEncoding::Gbk;
        }

        return LEncoding::Latin1;
    }

    /// 推断当前存储字节最可能的编码。
    LEncoding detect_encoding() const {
        return detect_encoding(data_);
    }

    /// 返回编码枚举值稳定的小写显示名称。
    static LStringDetail::string_view encoding_name(LEncoding encoding) noexcept {
        switch (encoding) {
        case LEncoding::Ascii:
            return "ascii";
        case LEncoding::Utf8:
            return "utf-8";
        case LEncoding::Utf8Bom:
            return "utf-8-bom";
        case LEncoding::Utf16Le:
            return "utf-16le";
        case LEncoding::Utf16Be:
            return "utf-16be";
        case LEncoding::Utf32Le:
            return "utf-32le";
        case LEncoding::Utf32Be:
            return "utf-32be";
        case LEncoding::Latin1:
            return "latin1";
        case LEncoding::Gbk:
            return "gbk";
        case LEncoding::Gb2312:
            return "gb2312";
        case LEncoding::Unknown:
        default:
            return "unknown";
        }
    }

    /// 检查字节序列是否为格式正确的 UTF-8。
    static bool valid_utf8(LStringDetail::string_view text) {
        return LStringDetail::is_valid_utf8(text);
    }

    /// 检查当前存储字节是否为格式正确的 UTF-8。
    bool valid_utf8() const {
        return valid_utf8(data_);
    }

    /**
     * @brief 将外部编码字节转换为 LString 内部 UTF-8 形式。
     *
     * @param bytes 使用 @p encoding 编码的输入字节。
     * @param encoding 源编码；Unknown 会触发 detect_encoding()。
     * @param strict 为 true 时，格式错误的输入会抛出异常；为 false 时，
     *        后端可恢复的位置会使用替换字符。
     *
     * GBK/GB2312 在 Windows 上使用 Win32 转换 API，在类 Unix 系统上使用
     * iconv。返回的 LString 始终存储 UTF-8 字节。
     */
    static LString from_encoding(LStringDetail::string_view bytes, LEncoding encoding = LEncoding::Unknown,
                                 bool strict = true) {
        if (encoding == LEncoding::Unknown) {
            encoding = detect_encoding(bytes);
        }

        auto input = LStringDetail::trim_bom(bytes, encoding);
        switch (encoding) {
        case LEncoding::Ascii: {
            for (char ch : input) {
                if (static_cast<unsigned char>(ch) > 0x7F) {
                    if (strict) {
                        throw std::runtime_error("non-ASCII byte in ASCII input");
                    }
                }
            }
            return std::string(input);
        }
        case LEncoding::Utf8:
        case LEncoding::Utf8Bom:
            if (strict && !valid_utf8(input)) {
                throw std::runtime_error("invalid UTF-8 input");
            }
            return LStringDetail::encode_utf8(LStringDetail::decode_utf8(input, strict));
        case LEncoding::Utf16Le:
            return LStringDetail::encode_utf8(LStringDetail::decode_utf16(input, true, strict));
        case LEncoding::Utf16Be:
            return LStringDetail::encode_utf8(LStringDetail::decode_utf16(input, false, strict));
        case LEncoding::Utf32Le:
            return LStringDetail::encode_utf8(LStringDetail::decode_utf32(input, true, strict));
        case LEncoding::Utf32Be:
            return LStringDetail::encode_utf8(LStringDetail::decode_utf32(input, false, strict));
        case LEncoding::Latin1: {
            std::string out;
            out.reserve(input.size());
            for (auto ch : input) {
                LStringDetail::append_utf8(out, static_cast<unsigned char>(ch));
            }
            return out;
        }
        case LEncoding::Gbk:
        case LEncoding::Gb2312:
            return LStringDetail::legacy_to_utf8(input, encoding, strict);
        case LEncoding::Unknown:
        default:
            return std::string(input);
        }
    }

    /**
     * @brief 将内部 UTF-8 文本转换为外部编码。
     *
     * @param encoding 目标编码。
     * @param write_bom 对 UTF-8-BOM、UTF-16、UTF-32 目标写入 BOM。
     * @param strict 为 true 时，无法表示的字符会抛出异常；为 false 时，
     *        适用位置会用 '?' 替代。
     *
     * 返回的 std::string 是任意字节缓冲区，可能包含 NUL 字节。
     */
    std::string to_encoding(LEncoding encoding, bool write_bom = false, bool strict = true) const {
        auto codepoints = LStringDetail::decode_utf8(data_, strict);

        switch (encoding) {
        case LEncoding::Ascii: {
            std::string out;
            out.reserve(codepoints.size());
            for (auto cp : codepoints) {
                if (cp > 0x7F) {
                    if (strict) {
                        throw std::runtime_error("code point cannot be represented as ASCII");
                    }
                    cp = '?';
                }
                out.push_back(static_cast<char>(cp));
            }
            return out;
        }
        case LEncoding::Utf8:
        case LEncoding::Unknown:
            return data_;
        case LEncoding::Utf8Bom: {
            std::string out = write_bom ? "\xEF\xBB\xBF" : "";
            out += data_;
            return out;
        }
        case LEncoding::Utf16Le:
            return LStringDetail::encode_utf16(codepoints, true, write_bom);
        case LEncoding::Utf16Be:
            return LStringDetail::encode_utf16(codepoints, false, write_bom);
        case LEncoding::Utf32Le:
            return LStringDetail::encode_utf32(codepoints, true, write_bom);
        case LEncoding::Utf32Be:
            return LStringDetail::encode_utf32(codepoints, false, write_bom);
        case LEncoding::Latin1: {
            std::string out;
            out.reserve(codepoints.size());
            for (auto cp : codepoints) {
                if (cp > 0xFF) {
                    if (strict) {
                        throw std::runtime_error("code point cannot be represented as Latin-1");
                    }
                    cp = '?';
                }
                out.push_back(static_cast<char>(cp));
            }
            return out;
        }
        case LEncoding::Gbk:
        case LEncoding::Gb2312:
            return LStringDetail::utf8_to_legacy(data_, encoding, strict);
        }
        return data_;
    }

    /// 将 GBK 字节转换为内部 UTF-8 存储。
    static LString from_gbk(LStringDetail::string_view bytes, bool strict = true) {
        return from_encoding(bytes, LEncoding::Gbk, strict);
    }

    /// 将 GB2312 字节转换为内部 UTF-8 存储。
    static LString from_gb2312(LStringDetail::string_view bytes, bool strict = true) {
        return from_encoding(bytes, LEncoding::Gb2312, strict);
    }

    /// 将内部 UTF-8 文本转换为 GBK 字节。
    std::string to_gbk(bool strict = true) const {
        return to_encoding(LEncoding::Gbk, false, strict);
    }

    /// 将内部 UTF-8 文本转换为 GB2312 字节。
    std::string to_gb2312(bool strict = true) const {
        return to_encoding(LEncoding::Gb2312, false, strict);
    }

    /**
     * @brief 将宽字符串转换为内部 UTF-8 存储。
     *
     * 同时处理 16 位 wchar_t 平台（代理项对）和 32 位 wchar_t 平台
     *（直接 Unicode 标量值）。
     */
    static LString from_wstring(LStringDetail::wstring_view value, bool strict = true) {
        std::vector<char32_t> codepoints;
        codepoints.reserve(value.size());

        for (std::size_t i = 0; i < value.size(); ++i) {
            auto cp = static_cast<char32_t>(value[i]);
            if (sizeof(wchar_t) == 2) {
                if (LStringDetail::is_high_surrogate(cp)) {
                    if (i + 1 >= value.size() ||
                        !LStringDetail::is_low_surrogate(static_cast<char32_t>(value[i + 1]))) {
                        if (strict) {
                            throw std::runtime_error("invalid wide surrogate pair");
                        }
                        codepoints.push_back(LStringDetail::replacement_char);
                        continue;
                    }
                    auto lo = static_cast<char32_t>(value[++i]);
                    codepoints.push_back(0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00)));
                } else if (LStringDetail::is_low_surrogate(cp)) {
                    if (strict) {
                        throw std::runtime_error("unpaired wide low surrogate");
                    }
                    codepoints.push_back(LStringDetail::replacement_char);
                } else {
                    codepoints.push_back(cp);
                }
            } else {
                if (!LStringDetail::is_valid_scalar(cp)) {
                    if (strict) {
                        throw std::runtime_error("invalid wide code point");
                    }
                    cp = LStringDetail::replacement_char;
                }
                codepoints.push_back(cp);
            }
        }

        return LStringDetail::encode_utf8(codepoints);
    }

    /**
     * @brief 将内部 UTF-8 文本转换为 std::wstring。
     *
     * 在 Windows 风格的 16 位 wchar_t 目标上，非 BMP 码点会输出为代理项对；
     * 在 32 位 wchar_t 目标上，每个标量值映射为一个 wchar_t。
     */
    std::wstring to_wstring(bool strict = true) const {
        auto codepoints = LStringDetail::decode_utf8(data_, strict);
        std::wstring out;
        out.reserve(codepoints.size());

        for (auto cp : codepoints) {
            if (sizeof(wchar_t) == 2) {
                if (cp <= 0xFFFF) {
                    out.push_back(static_cast<wchar_t>(cp));
                } else {
                    cp -= 0x10000;
                    out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                    out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
                }
            } else {
                out.push_back(static_cast<wchar_t>(cp));
            }
        }

        return out;
    }

#if LSTRING_HAS_FILESYSTEM
    /// 将存储字节解释为文件系统路径。
    std::filesystem::path path() const {
        return std::filesystem::path(data_);
    }

    /// 以 LString 返回 path().filename()。
    LString filename() const {
        return path().filename().string();
    }

    /// 以 LString 返回 path().stem()。
    LString stem() const {
        return path().stem().string();
    }

    /// 以 LString 返回 path().extension()。
    LString extension() const {
        return path().extension().string();
    }

    /// 以 LString 返回 path().parent_path()。
    LString parent_path() const {
        return path().parent_path().string();
    }

    /// 以 LString 返回 path().lexically_normal()；不访问真实文件系统。
    LString normalized_path() const {
        return path().lexically_normal().string();
    }

    /// 判断当前平台下存储路径是否为绝对路径。
    bool is_absolute_path() const {
        return path().is_absolute();
    }

    /// 判断存储路径是否存在；文件系统错误会被吞掉。
    bool path_exists() const {
        std::error_code ec;
        return std::filesystem::exists(path(), ec);
    }

    /// 按文件系统路径规则追加另一个路径组件。
    LString join_path(const std::filesystem::path& rhs) const {
        return (path() / rhs).string();
    }

    /// 路径拼接运算符，等价于 join_path()。
    friend LString operator/(const LString& lhs, const std::filesystem::path& rhs) {
        return lhs.join_path(rhs);
    }
#endif // LSTRING_HAS_FILESYSTEM

    /**
     * @brief 判断正则表达式是否匹配任意子串。
     *
     * 编译期可用 <re2/re2.h> 时使用 RE2，否则使用 std::regex。因此 pattern
     * 语法跟随当前启用的后端。
     */
    bool regex_contains(LStringDetail::string_view pattern) const {
#if LSTRING_HAS_RE2
        return RE2::PartialMatch(data_, RE2(std::string(pattern)));
#else
        return std::regex_search(data_, std::regex(std::string(pattern)));
#endif // LSTRING_HAS_RE2
    }

#if LSTRING_HAS_STD_OPTIONAL
    /**
     * @brief 查找第一个正则匹配。
     *
     * 没有匹配时返回 std::nullopt。返回的字符串是完整匹配文本，不是某个捕获组。
     */
    std::optional<LString> regex_find(LStringDetail::string_view pattern) const {
#if LSTRING_HAS_RE2
        auto wrapped = fmt::format("({})", std::string(pattern));
        RE2 re(wrapped);
        re2::StringPiece input(data_);
        re2::StringPiece match;
        if (RE2::FindAndConsume(&input, re, &match)) {
            return LString(LStringDetail::string_view(match.data(), match.size()));
        }
        return std::nullopt;
#else
        std::smatch match;
        if (std::regex_search(data_, match, std::regex(std::string(pattern)))) {
            return match.str(0);
        }
        return std::nullopt;
#endif // LSTRING_HAS_RE2
    }
#endif // LSTRING_HAS_STD_OPTIONAL

    /// 按从左到右顺序返回所有非重叠正则匹配。
    std::vector<LString> regex_find_all(LStringDetail::string_view pattern) const {
        std::vector<LString> out;
#if LSTRING_HAS_RE2
        auto wrapped = fmt::format("({})", std::string(pattern));
        RE2 re(wrapped);
        re2::StringPiece input(data_);
        re2::StringPiece match;
        while (RE2::FindAndConsume(&input, re, &match)) {
            out.emplace_back(LStringDetail::string_view(match.data(), match.size()));
        }
#else
        std::regex re {std::string(pattern)};
        for (auto it = std::sregex_iterator(data_.begin(), data_.end(), re);
             it != std::sregex_iterator(); ++it) {
            out.emplace_back(it->str(0));
        }
#endif // LSTRING_HAS_RE2
        return out;
    }

    /**
     * @brief 返回替换正则匹配后的副本。
     *
     * @param pattern 当前正则后端使用的匹配模式。
     * @param rewrite 当前正则后端使用的替换表达式/字符串。
     * @param replace_all 为 true 时替换全部匹配，为 false 时只替换第一个。
     */
    LString regex_replaced(LStringDetail::string_view pattern, LStringDetail::string_view rewrite,
                           bool replace_all = true) const {
        LString out(*this);
        out.regex_replace(pattern, rewrite, replace_all);
        return out;
    }

    /**
     * @brief 原地替换正则匹配。
     *
     * 编译进 RE2 时使用 RE2 替换语义；否则使用 std::regex_replace 语义。
     */
    LString& regex_replace(LStringDetail::string_view pattern, LStringDetail::string_view rewrite,
                           bool replace_all = true) {
#if LSTRING_HAS_RE2
        RE2 re(std::string(pattern));
        auto replacement = std::string(rewrite);
        if (replace_all) {
            RE2::GlobalReplace(&data_, re, replacement);
        } else {
            RE2::Replace(&data_, re, replacement);
        }
#else
        auto flags = replace_all ? std::regex_constants::match_default
                                 : std::regex_constants::format_first_only;
        data_ = std::regex_replace(data_, std::regex(std::string(pattern)), std::string(rewrite),
                                   flags);
#endif // LSTRING_HAS_RE2
        return *this;
    }

    /// 将当前存储字节编码为标准 Base64 文本。
    LString base64_encoded() const {
        return LStringDetail::base64_encode(view());
    }

    /// 将当前内容按 Base64 文本解码为原始字节。
    LString base64_decoded(bool strict = true) const {
        return LStringDetail::base64_decode(view(), strict);
    }

    /// 将任意字节编码为标准 Base64 文本。
    static LString base64_encode(LStringDetail::string_view bytes) {
        return LStringDetail::base64_encode(bytes);
    }

    /// 将 Base64 文本解码为原始字节。
    static LString base64_decode(LStringDetail::string_view encoded, bool strict = true) {
        return LStringDetail::base64_decode(encoded, strict);
    }

    /// 将 Base64 文本解码为 LString；语义同 base64_decode()，名称更适合构造场景。
    static LString from_base64(LStringDetail::string_view encoded, bool strict = true) {
        return base64_decode(encoded, strict);
    }

    /// 返回当前存储字节的 MD5 摘要，格式为 32 位小写十六进制文本。
    LString md5() const {
        return LStringDetail::md5_hex(view());
    }

    /// 返回当前存储字节的 16 字节 MD5 二进制摘要。
    std::array<unsigned char, 16> md5_digest() const {
        return LStringDetail::md5_digest(view());
    }

    /// 计算任意字节的 MD5 摘要，格式为 32 位小写十六进制文本。
    static LString md5(LStringDetail::string_view bytes) {
        return LStringDetail::md5_hex(bytes);
    }

    /// 计算任意字节的 16 字节 MD5 二进制摘要。
    static std::array<unsigned char, 16> md5_digest(LStringDetail::string_view bytes) {
        return LStringDetail::md5_digest(bytes);
    }

    /// 使用当前标准下可用的标准哈希器对存储字节求哈希。
    std::size_t hash() const noexcept {
#if __cplusplus >= 201703L
        return std::hash<LStringDetail::string_view> {}(view());
#else
        return std::hash<std::string> {}(data_);
#endif // __cplusplus >= 201703L
    }
};

/// PascalCase 别名，供偏好类型命名风格的代码使用。

/// 用户自定义字面量，用于从字符串字面量简洁构造 LString。
inline LString operator""_ls(const char* text, std::size_t size) {
    return LString(LStringDetail::string_view(text, size));
}

/// fmt 格式化器：LString 按 LStringDetail::string_view 的规则输出。
template<>
struct fmt::formatter<LString> : fmt::formatter<fmt::string_view> {
    /// 将存储字节写入 fmt 输出上下文。
    template<class FormatContext>
    typename FormatContext::iterator format(const LString& value, FormatContext& ctx) const {
        return fmt::formatter<fmt::string_view>::format(
            fmt::string_view(value.data(), value.size()), ctx);
    }
};

#if LSTRING_HAS_STD_FORMAT
/// std::format 格式化器：LString 按 LStringDetail::string_view 的规则输出。
template<>
struct std::formatter<LString, char> : std::formatter<std::string_view, char> {
    /// 将 LString 的字节视图写入 std::format 输出上下文。
    template<class FormatContext>
    auto format(const LString& value, FormatContext& ctx) const {
        return std::formatter<std::string_view, char>::format(
            std::string_view(value.data(), value.size()), ctx);
    }
};
#endif // LSTRING_HAS_STD_FORMAT

/// std::hash 特化，使 LString 可用于无序容器。
template<>
struct std::hash<LString> {
    /// 按 LString 的字节视图求哈希。
    std::size_t operator()(const LString& value) const noexcept {
        return value.hash();
    }
};

#endif // !LSTRING_INCLUDE
