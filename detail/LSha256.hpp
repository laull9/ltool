/**
 * @file detail/LSha256.hpp
 * @brief 纯头文件 SHA-256 工具。
 */

#ifndef LTOOL_LSHA256_INCLUDE
#define LTOOL_LSHA256_INCLUDE

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace LTool {
namespace detail {

inline std::uint32_t sha256_rotr(std::uint32_t value, unsigned int count) noexcept {
    return (value >> count) | (value << (32U - count));
}

inline std::uint32_t sha256_ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

inline std::uint32_t sha256_maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
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
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
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

    std::uint32_t words[64] {};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = sha256_read_be32(block + i * 4);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        words[i] = sha256_small_sigma1(words[i - 2]) + words[i - 7] +
                   sha256_small_sigma0(words[i - 15]) + words[i - 16];
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    auto f = state[5];
    auto g = state[6];
    auto h = state[7];

    for (std::size_t i = 0; i < 64; ++i) {
        auto t1 = h + sha256_big_sigma1(e) + sha256_ch(e, f, g) + constants[i] + words[i];
        auto t2 = sha256_big_sigma0(a) + sha256_maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

class Sha256 {
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
