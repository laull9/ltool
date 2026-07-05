/**
 * @file detail/LSha256.hpp
 * @brief 纯头文件 SHA-256 工具。
 */

#ifndef LTOOL_LSHA256_INCLUDE
#define LTOOL_LSHA256_INCLUDE

#include "LToolConfig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#if LTOOL_HAS_OPENSSL_SHA256
#include <openssl/evp.h>
#endif

namespace LTool {
namespace detail {

inline std::uint32_t sha256_rotr(std::uint32_t value, unsigned int count) noexcept {
    return (value >> count) | (value << (32U - count));
}

inline std::uint32_t sha256_ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return z ^ (x & (y ^ z));
}

inline std::uint32_t sha256_maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) | (z & (x | y));
}

inline std::uint32_t sha256_big_sigma0(std::uint32_t x) noexcept {
    return sha256_rotr(x, 2) ^ sha256_rotr(x, 13) ^ sha256_rotr(x, 22);
}

inline std::uint32_t sha256_big_sigma1(std::uint32_t x) noexcept {
    return sha256_rotr(x, 6) ^ sha256_rotr(x, 11) ^ sha256_rotr(x, 25);
}

inline std::uint32_t sha256_small_sigma0(std::uint32_t x) noexcept {
    return sha256_rotr(x, 7) ^ sha256_rotr(x, 18) ^ (x >> 3);
}

inline std::uint32_t sha256_small_sigma1(std::uint32_t x) noexcept {
    return sha256_rotr(x, 17) ^ sha256_rotr(x, 19) ^ (x >> 10);
}

inline std::uint32_t sha256_read_be32(const unsigned char* bytes) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return value;
#elif defined(__clang__) || defined(__GNUC__)
    return __builtin_bswap32(value);
#else
    return ((value & 0x000000FFU) << 24) |
           ((value & 0x0000FF00U) << 8) |
           ((value & 0x00FF0000U) >> 8) |
           ((value & 0xFF000000U) >> 24);
#endif
}

inline void sha256_write_be32(std::uint32_t value, unsigned char* bytes) noexcept {
    bytes[0] = static_cast<unsigned char>((value >> 24) & 0xFF);
    bytes[1] = static_cast<unsigned char>((value >> 16) & 0xFF);
    bytes[2] = static_cast<unsigned char>((value >> 8) & 0xFF);
    bytes[3] = static_cast<unsigned char>(value & 0xFF);
}

inline void sha256_write_be64(std::uint64_t value, unsigned char* bytes) noexcept {
    for (int i = 7; i >= 0; --i) {
        bytes[7 - i] = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);
    }
}

inline std::string hex_lower(const unsigned char* bytes, std::size_t size) {
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

inline void sha256_transform(std::array<std::uint32_t, 8>& state,
                             const unsigned char block[64]) noexcept {
    static const std::uint32_t constants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };

    std::uint32_t words[16] {};
    words[0] = sha256_read_be32(block);
    words[1] = sha256_read_be32(block + 4);
    words[2] = sha256_read_be32(block + 8);
    words[3] = sha256_read_be32(block + 12);
    words[4] = sha256_read_be32(block + 16);
    words[5] = sha256_read_be32(block + 20);
    words[6] = sha256_read_be32(block + 24);
    words[7] = sha256_read_be32(block + 28);
    words[8] = sha256_read_be32(block + 32);
    words[9] = sha256_read_be32(block + 36);
    words[10] = sha256_read_be32(block + 40);
    words[11] = sha256_read_be32(block + 44);
    words[12] = sha256_read_be32(block + 48);
    words[13] = sha256_read_be32(block + 52);
    words[14] = sha256_read_be32(block + 56);
    words[15] = sha256_read_be32(block + 60);

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    auto f = state[5];
    auto g = state[6];
    auto h = state[7];

#define LTOOL_SHA256_WORD(index) \
    (words[(index) & 15] += \
         sha256_small_sigma1(words[((index) - 2) & 15]) + words[((index) - 7) & 15] + \
         sha256_small_sigma0(words[((index) - 15) & 15]))

#define LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constant, word) \
    do { \
        const auto t1 = (h) + sha256_big_sigma1(e) + sha256_ch((e), (f), (g)) + \
                        (constant) + (word); \
        const auto t2 = sha256_big_sigma0(a) + sha256_maj((a), (b), (c)); \
        (d) += t1; \
        (h) = t1 + t2; \
    } while (false)

    LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constants[0], words[0]);
    LTOOL_SHA256_ROUND(h, a, b, c, d, e, f, g, constants[1], words[1]);
    LTOOL_SHA256_ROUND(g, h, a, b, c, d, e, f, constants[2], words[2]);
    LTOOL_SHA256_ROUND(f, g, h, a, b, c, d, e, constants[3], words[3]);
    LTOOL_SHA256_ROUND(e, f, g, h, a, b, c, d, constants[4], words[4]);
    LTOOL_SHA256_ROUND(d, e, f, g, h, a, b, c, constants[5], words[5]);
    LTOOL_SHA256_ROUND(c, d, e, f, g, h, a, b, constants[6], words[6]);
    LTOOL_SHA256_ROUND(b, c, d, e, f, g, h, a, constants[7], words[7]);
    LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constants[8], words[8]);
    LTOOL_SHA256_ROUND(h, a, b, c, d, e, f, g, constants[9], words[9]);
    LTOOL_SHA256_ROUND(g, h, a, b, c, d, e, f, constants[10], words[10]);
    LTOOL_SHA256_ROUND(f, g, h, a, b, c, d, e, constants[11], words[11]);
    LTOOL_SHA256_ROUND(e, f, g, h, a, b, c, d, constants[12], words[12]);
    LTOOL_SHA256_ROUND(d, e, f, g, h, a, b, c, constants[13], words[13]);
    LTOOL_SHA256_ROUND(c, d, e, f, g, h, a, b, constants[14], words[14]);
    LTOOL_SHA256_ROUND(b, c, d, e, f, g, h, a, constants[15], words[15]);
    LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constants[16], LTOOL_SHA256_WORD(16));
    LTOOL_SHA256_ROUND(h, a, b, c, d, e, f, g, constants[17], LTOOL_SHA256_WORD(17));
    LTOOL_SHA256_ROUND(g, h, a, b, c, d, e, f, constants[18], LTOOL_SHA256_WORD(18));
    LTOOL_SHA256_ROUND(f, g, h, a, b, c, d, e, constants[19], LTOOL_SHA256_WORD(19));
    LTOOL_SHA256_ROUND(e, f, g, h, a, b, c, d, constants[20], LTOOL_SHA256_WORD(20));
    LTOOL_SHA256_ROUND(d, e, f, g, h, a, b, c, constants[21], LTOOL_SHA256_WORD(21));
    LTOOL_SHA256_ROUND(c, d, e, f, g, h, a, b, constants[22], LTOOL_SHA256_WORD(22));
    LTOOL_SHA256_ROUND(b, c, d, e, f, g, h, a, constants[23], LTOOL_SHA256_WORD(23));
    LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constants[24], LTOOL_SHA256_WORD(24));
    LTOOL_SHA256_ROUND(h, a, b, c, d, e, f, g, constants[25], LTOOL_SHA256_WORD(25));
    LTOOL_SHA256_ROUND(g, h, a, b, c, d, e, f, constants[26], LTOOL_SHA256_WORD(26));
    LTOOL_SHA256_ROUND(f, g, h, a, b, c, d, e, constants[27], LTOOL_SHA256_WORD(27));
    LTOOL_SHA256_ROUND(e, f, g, h, a, b, c, d, constants[28], LTOOL_SHA256_WORD(28));
    LTOOL_SHA256_ROUND(d, e, f, g, h, a, b, c, constants[29], LTOOL_SHA256_WORD(29));
    LTOOL_SHA256_ROUND(c, d, e, f, g, h, a, b, constants[30], LTOOL_SHA256_WORD(30));
    LTOOL_SHA256_ROUND(b, c, d, e, f, g, h, a, constants[31], LTOOL_SHA256_WORD(31));
    LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constants[32], LTOOL_SHA256_WORD(32));
    LTOOL_SHA256_ROUND(h, a, b, c, d, e, f, g, constants[33], LTOOL_SHA256_WORD(33));
    LTOOL_SHA256_ROUND(g, h, a, b, c, d, e, f, constants[34], LTOOL_SHA256_WORD(34));
    LTOOL_SHA256_ROUND(f, g, h, a, b, c, d, e, constants[35], LTOOL_SHA256_WORD(35));
    LTOOL_SHA256_ROUND(e, f, g, h, a, b, c, d, constants[36], LTOOL_SHA256_WORD(36));
    LTOOL_SHA256_ROUND(d, e, f, g, h, a, b, c, constants[37], LTOOL_SHA256_WORD(37));
    LTOOL_SHA256_ROUND(c, d, e, f, g, h, a, b, constants[38], LTOOL_SHA256_WORD(38));
    LTOOL_SHA256_ROUND(b, c, d, e, f, g, h, a, constants[39], LTOOL_SHA256_WORD(39));
    LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constants[40], LTOOL_SHA256_WORD(40));
    LTOOL_SHA256_ROUND(h, a, b, c, d, e, f, g, constants[41], LTOOL_SHA256_WORD(41));
    LTOOL_SHA256_ROUND(g, h, a, b, c, d, e, f, constants[42], LTOOL_SHA256_WORD(42));
    LTOOL_SHA256_ROUND(f, g, h, a, b, c, d, e, constants[43], LTOOL_SHA256_WORD(43));
    LTOOL_SHA256_ROUND(e, f, g, h, a, b, c, d, constants[44], LTOOL_SHA256_WORD(44));
    LTOOL_SHA256_ROUND(d, e, f, g, h, a, b, c, constants[45], LTOOL_SHA256_WORD(45));
    LTOOL_SHA256_ROUND(c, d, e, f, g, h, a, b, constants[46], LTOOL_SHA256_WORD(46));
    LTOOL_SHA256_ROUND(b, c, d, e, f, g, h, a, constants[47], LTOOL_SHA256_WORD(47));
    LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constants[48], LTOOL_SHA256_WORD(48));
    LTOOL_SHA256_ROUND(h, a, b, c, d, e, f, g, constants[49], LTOOL_SHA256_WORD(49));
    LTOOL_SHA256_ROUND(g, h, a, b, c, d, e, f, constants[50], LTOOL_SHA256_WORD(50));
    LTOOL_SHA256_ROUND(f, g, h, a, b, c, d, e, constants[51], LTOOL_SHA256_WORD(51));
    LTOOL_SHA256_ROUND(e, f, g, h, a, b, c, d, constants[52], LTOOL_SHA256_WORD(52));
    LTOOL_SHA256_ROUND(d, e, f, g, h, a, b, c, constants[53], LTOOL_SHA256_WORD(53));
    LTOOL_SHA256_ROUND(c, d, e, f, g, h, a, b, constants[54], LTOOL_SHA256_WORD(54));
    LTOOL_SHA256_ROUND(b, c, d, e, f, g, h, a, constants[55], LTOOL_SHA256_WORD(55));
    LTOOL_SHA256_ROUND(a, b, c, d, e, f, g, h, constants[56], LTOOL_SHA256_WORD(56));
    LTOOL_SHA256_ROUND(h, a, b, c, d, e, f, g, constants[57], LTOOL_SHA256_WORD(57));
    LTOOL_SHA256_ROUND(g, h, a, b, c, d, e, f, constants[58], LTOOL_SHA256_WORD(58));
    LTOOL_SHA256_ROUND(f, g, h, a, b, c, d, e, constants[59], LTOOL_SHA256_WORD(59));
    LTOOL_SHA256_ROUND(e, f, g, h, a, b, c, d, constants[60], LTOOL_SHA256_WORD(60));
    LTOOL_SHA256_ROUND(d, e, f, g, h, a, b, c, constants[61], LTOOL_SHA256_WORD(61));
    LTOOL_SHA256_ROUND(c, d, e, f, g, h, a, b, constants[62], LTOOL_SHA256_WORD(62));
    LTOOL_SHA256_ROUND(b, c, d, e, f, g, h, a, constants[63], LTOOL_SHA256_WORD(63));

#undef LTOOL_SHA256_ROUND
#undef LTOOL_SHA256_WORD

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

class Sha256Fallback {
private:
    std::array<std::uint32_t, 8> state_ {{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    }};
    std::array<unsigned char, 64> buffer_ {};
    std::uint64_t bit_count_ = 0;
    std::size_t buffer_size_ = 0;

    std::array<unsigned char, 32> finish_digest() {
        auto total_bits = bit_count_;

        buffer_[buffer_size_++] = 0x80;
        if (buffer_size_ > 56) {
            std::memset(buffer_.data() + buffer_size_, 0, buffer_.size() - buffer_size_);
            sha256_transform(state_, buffer_.data());
            buffer_size_ = 0;
        }

        std::memset(buffer_.data() + buffer_size_, 0, 56 - buffer_size_);
        sha256_write_be64(total_bits, buffer_.data() + 56);
        sha256_transform(state_, buffer_.data());
        buffer_size_ = 0;

        std::array<unsigned char, 32> digest {};
        for (std::size_t i = 0; i < state_.size(); ++i) {
            sha256_write_be32(state_[i], digest.data() + i * 4);
        }
        return digest;
    }

public:
    void update(const void* data, std::size_t size) {
        if (size == 0) {
            return;
        }

        auto bytes = static_cast<const unsigned char*>(data);
        bit_count_ += static_cast<std::uint64_t>(size) * 8U;

        if (buffer_size_ != 0) {
            auto copied = (size < buffer_.size() - buffer_size_)
                              ? size
                              : buffer_.size() - buffer_size_;
            std::memcpy(buffer_.data() + buffer_size_, bytes, copied);
            buffer_size_ += copied;
            bytes += copied;
            size -= copied;

            if (buffer_size_ == buffer_.size()) {
                sha256_transform(state_, buffer_.data());
                buffer_size_ = 0;
            }
        }

        while (size >= buffer_.size()) {
            sha256_transform(state_, bytes);
            bytes += buffer_.size();
            size -= buffer_.size();
        }

        if (size != 0) {
            std::memcpy(buffer_.data(), bytes, size);
            buffer_size_ = size;
        }
    }

    std::array<unsigned char, 32> final_digest() const {
        auto copy = *this;
        return copy.finish_digest();
    }

    std::string final_hex() const {
        auto digest = final_digest();
        return hex_lower(digest.data(), digest.size());
    }
};

#if LTOOL_HAS_OPENSSL_SHA256

inline void sha256_openssl_require(int ok, const char* action) {
    if (ok != 1) {
        throw std::runtime_error(std::string("OpenSSL SHA-256 failed to ") + action);
    }
}

class Sha256 {
private:
    EVP_MD_CTX* context_ = nullptr;

public:
    Sha256()
        : context_(EVP_MD_CTX_new()) {
        if (!context_) {
            throw std::runtime_error("OpenSSL SHA-256 failed to allocate context");
        }
        sha256_openssl_require(EVP_DigestInit_ex(context_, EVP_sha256(), nullptr), "initialize");
    }

    Sha256(const Sha256& other)
        : context_(EVP_MD_CTX_new()) {
        if (!context_) {
            throw std::runtime_error("OpenSSL SHA-256 failed to allocate context");
        }
        sha256_openssl_require(EVP_MD_CTX_copy_ex(context_, other.context_), "copy context");
    }

    Sha256& operator=(const Sha256& other) {
        if (this != &other) {
            Sha256 copy(other);
            std::swap(context_, copy.context_);
        }
        return *this;
    }

#if LTOOL_HAS_CPP11
    Sha256(Sha256&& other) noexcept
        : context_(other.context_) {
        other.context_ = nullptr;
    }

    Sha256& operator=(Sha256&& other) noexcept {
        if (this != &other) {
            EVP_MD_CTX_free(context_);
            context_ = other.context_;
            other.context_ = nullptr;
        }
        return *this;
    }
#endif

    ~Sha256() {
        EVP_MD_CTX_free(context_);
    }

    void update(const void* data, std::size_t size) {
        if (size == 0) {
            return;
        }
        if (!data) {
            throw std::invalid_argument("Sha256 cannot read null data");
        }
        sha256_openssl_require(EVP_DigestUpdate(context_, data, size), "update");
    }

    std::array<unsigned char, 32> final_digest() const {
        Sha256 copy(*this);
        std::array<unsigned char, 32> digest {};
        unsigned int digest_size = 0;
        sha256_openssl_require(
            EVP_DigestFinal_ex(copy.context_, digest.data(), &digest_size),
            "finalize"
        );
        if (digest_size != digest.size()) {
            throw std::runtime_error("OpenSSL SHA-256 returned an unexpected digest size");
        }
        return digest;
    }

    std::string final_hex() const {
        auto digest = final_digest();
        return hex_lower(digest.data(), digest.size());
    }
};

#else

using Sha256 = Sha256Fallback;

#endif

inline std::array<unsigned char, 32> sha256_digest(const void* data, std::size_t size) {
    Sha256 sha256;
    sha256.update(data, size);
    return sha256.final_digest();
}

inline std::string sha256_hex(const void* data, std::size_t size) {
    Sha256 sha256;
    sha256.update(data, size);
    return sha256.final_hex();
}

inline std::string sha256_hex(const std::string& bytes) {
    return sha256_hex(bytes.data(), bytes.size());
}

} // namespace detail
} // namespace LTool

#endif // LTOOL_LSHA256_INCLUDE
