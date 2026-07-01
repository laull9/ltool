/**
 * @file detail/LBase64.hpp
 * @brief 纯头文件 Base64 编解码工具。
 */

#ifndef LTOOL_LBASE64_INCLUDE
#define LTOOL_LBASE64_INCLUDE

#include <cstddef>
#include <stdexcept>
#include <string>

namespace LTool {
namespace detail {

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
inline std::string base64_encode(const void* data, std::size_t size) {
    if (!data && size != 0) {
        throw std::invalid_argument("base64_encode cannot read null data");
    }

    const auto* bytes = static_cast<const unsigned char*>(data);
    const char* table = base64_table();
    std::string out;
    out.reserve(((size + 2) / 3) * 4);

    for (std::size_t i = 0; i < size; i += 3) {
        auto b0 = bytes[i];
        auto b1 = i + 1 < size ? bytes[i + 1] : 0;
        auto b2 = i + 2 < size ? bytes[i + 2] : 0;

        out.push_back(table[(b0 >> 2) & 0x3F]);
        out.push_back(table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
        out.push_back(i + 1 < size ? table[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=');
        out.push_back(i + 2 < size ? table[b2 & 0x3F] : '=');
    }

    return out;
}

/// 将 std::string 字节编码为标准 Base64 文本。
inline std::string base64_encode(const std::string& bytes) {
    return base64_encode(bytes.data(), bytes.size());
}

/// 解码 Base64 文本；strict 为 true 时遇到非法字符或非法填充会抛出异常。
inline std::string base64_decode(const void* data, std::size_t size, bool strict = true) {
    if (!data && size != 0) {
        throw std::invalid_argument("base64_decode cannot read null data");
    }

    const auto* encoded = static_cast<const char*>(data);
    std::string clean;
    clean.reserve(size);

    for (std::size_t i = 0; i < size; ++i) {
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

/// 解码 std::string 中的 Base64 文本。
inline std::string base64_decode(const std::string& encoded, bool strict = true) {
    return base64_decode(encoded.data(), encoded.size(), strict);
}

} // namespace detail
} // namespace LTool

#endif // LTOOL_LBASE64_INCLUDE
