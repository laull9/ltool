/**
 * @file detail/LWeb/Jwt.hpp
 * @brief LWeb JWT v0.1：HS256 签名/验签、标准 Claims、Base64URL。
 *
 * 定位：轻量认证基础设施，不是 Identity Framework。不涉及用户库/OAuth/
 * Refresh Token/黑名单等业务。
 *
 * 安全要点：
 * - 密码学 primitive 使用项目内已 vendored 的 LSha256（OpenSSL 可用时走
 *   OpenSSL EVP，否则纯头 fallback），禁止自行实现 SHA/HMAC primitive；
 * - alg 严格按服务器配置校验（防算法混淆；alg=none 必然不匹配）；
 * - Verify 默认验证 signature + exp + nbf，可选 iss / aud（含 clock_skew）；
 * - 不验签的解析 API 显式命名为 DecodeUnsafe；
 * - secret 在调试输出中脱敏（Describe()）。
 */

#ifndef LTOOL_LWEB_DETAIL_JWT_HPP
#define LTOOL_LWEB_DETAIL_JWT_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "detail/LSha256.hpp"
#include "detail/LWeb/Types.hpp"

namespace LWeb {

using JwtTimePoint = std::chrono::system_clock::time_point;

/// 支持的签名算法（v0.1 仅 HS256；RS256/ES256 列入 v0.2）。
enum class JwtAlgorithm {
    HS256,
};

/// JWT 验证失败分类（JwtAuth 映射为不同 401 code）。
enum class JwtError {
    ok,
    malformed_token,       ///< 结构 / Base64URL / JSON 非法
    unsupported_algorithm, ///< alg 不在支持列表
    algorithm_mismatch,    ///< 请求 alg != 服务器配置 alg（含 alg=none）
    invalid_signature,
    token_expired,         ///< exp 已过（含 clock skew）
    token_not_active,      ///< nbf 未到（含 clock skew）
    invalid_issuer,
    invalid_audience,
};

namespace detail {

/// 错误 -> HTTP 错误 code（401 场景）。
inline const char* JwtErrorCode(JwtError e) {
    switch (e) {
        case JwtError::ok: return "OK";
        case JwtError::token_expired: return "TOKEN_EXPIRED";
        case JwtError::token_not_active: return "TOKEN_NOT_ACTIVE";
        case JwtError::invalid_issuer: return "INVALID_TOKEN_ISSUER";
        case JwtError::invalid_audience: return "INVALID_TOKEN_AUDIENCE";
        default: return "INVALID_TOKEN";
    }
}

/// 错误 -> 人类可读消息。
inline const char* JwtErrorMessage(JwtError e) {
    switch (e) {
        case JwtError::token_expired: return "access token expired";
        case JwtError::token_not_active: return "token is not yet active";
        case JwtError::invalid_issuer: return "invalid token issuer";
        case JwtError::invalid_audience: return "invalid token audience";
        case JwtError::ok: return "ok";
        default: return "invalid token";
    }
}

/// JOSE 算法名（RFC 7518）：HS256。
inline const char* JwtAlgorithmName(JwtAlgorithm a) {
    (void)a;
    return "HS256";
}

/// 时间点 <-> 纪元秒。
inline std::int64_t ToEpochSeconds(JwtTimePoint tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(
               tp.time_since_epoch())
        .count();
}

/// 纪元秒 -> 时间点。范围校验：超过 ±1e12 秒（约 3.3 万年）拒绝，
/// 防止 duration 换算时（秒->微秒/纳秒）有符号溢出 UB。
inline JwtTimePoint FromEpochSeconds(std::int64_t secs) {
    if (secs > 1000000000000LL || secs < -1000000000000LL) {
        throw std::invalid_argument(
            "LWeb JWT: timestamp out of range");
    }
    return JwtTimePoint(std::chrono::seconds(secs));
}

/// Base64URL 编码（无填充，JWT 标准形态）。
inline std::string Base64UrlEncode(std::string_view bytes) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        const auto c = static_cast<unsigned char>(bytes[i + 2]);
        out.push_back(table[a >> 2]);
        out.push_back(table[((a << 4) | (b >> 4)) & 0x3F]);
        out.push_back(table[((b << 2) | (c >> 6)) & 0x3F]);
        out.push_back(table[c & 0x3F]);
    }
    if (i + 1 == bytes.size()) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        out.push_back(table[a >> 2]);
        out.push_back(table[(a << 4) & 0x3F]);
    }
    else if (i + 2 == bytes.size()) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        out.push_back(table[a >> 2]);
        out.push_back(table[((a << 4) | (b >> 4)) & 0x3F]);
        out.push_back(table[(b << 2) & 0x3F]);
    }
    return out;
}

/// Base64URL 解码；字符非法/长度非法抛 std::invalid_argument。
/// 接受可选 '=' padding。
inline std::string Base64UrlDecode(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() % 4 == 1) {
        throw std::invalid_argument("LWeb JWT: invalid base64url length");
    }
    static constexpr std::int8_t table[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };
    std::string out;
    out.reserve(text.size() / 4 * 3);
    std::size_t i = 0;
    const std::size_t end = text.find('=');  // 有 padding 时提前终止
    // '=' 之后只允许再出现 '='（拒绝 "AB=C" 这类非规范形态）。
    if (end != std::string_view::npos &&
        text.find_first_not_of('=', end) != std::string_view::npos) {
        throw std::invalid_argument("LWeb JWT: invalid base64url padding");
    }
    const std::size_t len = (end == std::string_view::npos) ? text.size() : end;
    for (; i + 4 <= len; i += 4) {
        const auto d0 = table[static_cast<unsigned char>(text[i])];
        const auto d1 = table[static_cast<unsigned char>(text[i + 1])];
        const auto d2 = table[static_cast<unsigned char>(text[i + 2])];
        const auto d3 = table[static_cast<unsigned char>(text[i + 3])];
        if (d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0) {
            throw std::invalid_argument("LWeb JWT: invalid base64url char");
        }
        out.push_back(static_cast<char>((d0 << 2) | (d1 >> 4)));
        out.push_back(static_cast<char>(((d1 & 0x0F) << 4) | (d2 >> 2)));
        out.push_back(static_cast<char>(((d2 & 0x03) << 6) | d3));
    }
    if (i + 2 == len) {  // 2 个有效字符 -> 1 字节
        const auto d0 = table[static_cast<unsigned char>(text[i])];
        const auto d1 = table[static_cast<unsigned char>(text[i + 1])];
        if (d0 < 0 || d1 < 0) {
            throw std::invalid_argument("LWeb JWT: invalid base64url char");
        }
        out.push_back(static_cast<char>((d0 << 2) | (d1 >> 4)));
    }
    else if (i + 3 == len) {  // 3 个有效字符 -> 2 字节
        const auto d0 = table[static_cast<unsigned char>(text[i])];
        const auto d1 = table[static_cast<unsigned char>(text[i + 1])];
        const auto d2 = table[static_cast<unsigned char>(text[i + 2])];
        if (d0 < 0 || d1 < 0 || d2 < 0) {
            throw std::invalid_argument("LWeb JWT: invalid base64url char");
        }
        out.push_back(static_cast<char>((d0 << 2) | (d1 >> 4)));
        out.push_back(static_cast<char>(((d1 & 0x0F) << 4) | (d2 >> 2)));
    }
    return out;
}

/// HMAC-SHA256（RFC 2104），基于 LSha256 增量式实现，零额外大分配。
inline std::array<unsigned char, 32> HmacSha256(std::string_view key,
                                                std::string_view data) {
    constexpr std::size_t kBlock = 64;
    std::array<unsigned char, kBlock> key_block{};
    if (key.size() > kBlock) {
        auto digest = LTool::detail::sha256_digest(key.data(), key.size());
        std::copy(digest.begin(), digest.end(), key_block.begin());
    }
    else {
        std::copy(key.begin(), key.end(), key_block.begin());
    }

    std::array<unsigned char, kBlock> ipad{};
    std::array<unsigned char, kBlock> opad{};
    for (std::size_t i = 0; i < kBlock; ++i) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    LTool::detail::Sha256 inner;
    inner.update(ipad.data(), ipad.size());
    inner.update(data.data(), data.size());
    auto inner_digest = inner.final_digest();

    LTool::detail::Sha256 outer;
    outer.update(opad.data(), opad.size());
    outer.update(inner_digest.data(), inner_digest.size());
    return outer.final_digest();
}

/// 常量时间比较（防时序侧信道）。
inline bool ConstantTimeEquals(const unsigned char* a, const unsigned char* b,
                               std::size_t n) {
    unsigned char diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

/// 解码后的 token 三部分。
struct DecodedToken {
    LJson::Json header;
    LJson::Json payload;
    std::string signing_input; ///< "header.payload"（验签输入）
    std::string signature;     ///< 原始签名字节
};

/// token 长度上限（与 cinatra 8KB 请求头上限一致；超限直接拒绝，
/// 防止未认证输入在验签前触发大 JSON 解析 / 大分配 DoS）。
inline constexpr std::size_t kMaxJwtTokenSize = 8 * 1024;

/// 结构解析：header.payload.signature。
/// 失败返回 nullopt（不抛异常，供 Verify 映射 JwtError）。
inline std::optional<DecodedToken> DecodeToken(std::string_view token) {
    if (token.size() > kMaxJwtTokenSize) {
        return std::nullopt;
    }
    const auto first = token.find('.');
    const auto last = token.rfind('.');
    if (first == std::string_view::npos || last == std::string_view::npos ||
        first == last || first == 0 || last + 1 >= token.size()) {
        return std::nullopt;
    }
    try {
        DecodedToken out;
        out.header = LJson::Json::parse(
            Base64UrlDecode(token.substr(0, first)));
        out.payload = LJson::Json::parse(
            Base64UrlDecode(token.substr(first + 1, last - first - 1)));
        out.signing_input = std::string(token.substr(0, last));
        out.signature = Base64UrlDecode(token.substr(last + 1));
        return out;
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace detail

/// 标准 + 自定义 Claims。
///
/// 构建（Encode）：直接填字段 + Set 自定义；
/// 解析（Verify/DecodeUnsafe）：标准字段有 typed 成员，自定义用
/// Get/Contains（读完整 payload）。
struct JwtClaims {
    std::optional<std::string> issuer; ///< iss
    std::optional<std::string> subject; ///< sub
    std::vector<std::string> audience;  ///< aud（单值编码为字符串，多值为数组）
    std::optional<JwtTimePoint> expires_at; ///< exp
    std::optional<JwtTimePoint> not_before; ///< nbf
    std::optional<JwtTimePoint> issued_at;  ///< iat
    std::optional<std::string> jwt_id;      ///< jti

    /// 完整 payload（解码所得；Get/Contains 的读取来源）。
    LJson::Json payload = LJson::Json::object();

    /// 自定义 claims（Encode 时的写入来源）。
    std::unordered_map<std::string, LJson::Json> extra;

    /// 便捷：subject（缺省返回空 view，与 Header() 一致）。
    std::string_view Subject() const noexcept {
        return subject ? std::string_view(*subject) : std::string_view{};
    }

    /// 便捷：jti（缺省返回空 view）。
    std::string_view JwtId() const noexcept {
        return jwt_id ? std::string_view(*jwt_id) : std::string_view{};
    }

    /// 设置自定义 claim（Encode 时写入）。值支持字符串/数字/bool/Json。
    template <typename T>
    void Set(std::string key, T&& value) {
        LJson::Json holder = LJson::Json::object();
        holder[std::string(key)] = std::forward<T>(value);
        const LJson::Json& readonly = holder;
        extra[std::move(key)] = readonly[std::string(key)];
    }

    /// 是否包含自定义 claim（extra 或 payload）。
    bool Contains(std::string_view key) const {
        const std::string k(key);
        if (extra.find(k) != extra.end()) {
            return true;
        }
        return payload.contains(k);
    }

    /// 读取自定义 claim（类型不符/不存在返回 nullopt）。
    template <typename T>
    std::optional<T> Get(std::string_view key) const {
        const std::string k(key);
        if (auto it = extra.find(k); it != extra.end()) {
            try {
                return it->second.template get_as<T>();
            }
            catch (...) {
                return std::nullopt;
            }
        }
        if (payload.contains(k)) {
            try {
                return payload[k].template get_as<T>();
            }
            catch (...) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    /// 序列化为 payload JSON（标准字段 + extra；payload 保留自定义）。
    LJson::Json ToJson() const {
        LJson::Json out = payload.is_object() ? payload
                                              : LJson::Json::object();
        if (issuer) {
            out["iss"] = *issuer;
        }
        if (subject) {
            out["sub"] = *subject;
        }
        if (audience.size() == 1) {
            out["aud"] = audience[0];
        }
        else if (audience.size() > 1) {
            LJson::Json arr = LJson::Json::array();
            for (const auto& a : audience) {
                arr.push_back(a);
            }
            out["aud"] = std::move(arr);
        }
        if (expires_at) {
            out["exp"] = detail::ToEpochSeconds(*expires_at);
        }
        if (not_before) {
            out["nbf"] = detail::ToEpochSeconds(*not_before);
        }
        if (issued_at) {
            out["iat"] = detail::ToEpochSeconds(*issued_at);
        }
        if (jwt_id) {
            out["jti"] = *jwt_id;
        }
        for (const auto& [k, v] : extra) {
            out[k] = v;
        }
        return out;
    }

    /// 从 payload JSON 解析（容忍缺失字段；aud 支持字符串与数组）。
    static JwtClaims FromJson(const LJson::Json& json) {
        JwtClaims claims;
        claims.payload = json;
        if (json.contains("iss")) {
            claims.issuer = json["iss"].get_as<std::string>();
        }
        if (json.contains("sub")) {
            claims.subject = json["sub"].get_as<std::string>();
        }
        if (json.contains("aud")) {
            const auto aud = json["aud"];
            if (aud.is_array()) {
                const auto n = aud.size();
                for (std::size_t i = 0; i < n; ++i) {
                    claims.audience.push_back(
                        aud[i].get_as<std::string>());
                }
            }
            else if (aud.is_string()) {
                claims.audience.push_back(aud.get_as<std::string>());
            }
        }
        if (json.contains("exp")) {
            claims.expires_at =
                detail::FromEpochSeconds(json["exp"].get_as<std::int64_t>());
        }
        if (json.contains("nbf")) {
            claims.not_before =
                detail::FromEpochSeconds(json["nbf"].get_as<std::int64_t>());
        }
        if (json.contains("iat")) {
            claims.issued_at =
                detail::FromEpochSeconds(json["iat"].get_as<std::int64_t>());
        }
        if (json.contains("jti")) {
            claims.jwt_id = json["jti"].get_as<std::string>();
        }
        return claims;
    }
};

/// JWT 配置（secret 调试输出脱敏）。
struct JwtOptions {
    JwtAlgorithm algorithm = JwtAlgorithm::HS256;
    std::string secret;      ///< HS256 密钥（不得为空；输出脱敏）。
    std::string issuer;      ///< 可选：Verify 校验 iss。
    std::string audience;    ///< 可选：Verify 校验 aud。
    std::chrono::seconds clock_skew{0}; ///< 时钟偏差容差（exp/nbf）。

    /// 脱敏描述：secret / key 不输出。
    std::string Describe() const {
        std::string out = "algorithm=";
        out += detail::JwtAlgorithmName(algorithm);
        out += " secret=<redacted>";
        if (!issuer.empty()) {
            out += " issuer=" + issuer;
        }
        if (!audience.empty()) {
            out += " audience=" + audience;
        }
        return out;
    }
};

/// JWT 验证结果。
struct JwtVerifyResult {
    JwtError error = JwtError::ok;
    std::optional<JwtClaims> claims;

    bool ok() const noexcept { return error == JwtError::ok; }

    /// HTTP 错误 code（供 JwtAuth 映射 401）。
    const char* ErrorCode() const noexcept {
        return detail::JwtErrorCode(error);
    }

    /// 人类可读消息。
    const char* Message() const noexcept {
        return detail::JwtErrorMessage(error);
    }
};

/// JWT 编解码/验证器（不可变、线程安全；secret 由 options_ 持有）。
class Jwt {
public:
    explicit Jwt(JwtOptions options) : options_(std::move(options)) {
        if (options_.algorithm == JwtAlgorithm::HS256 &&
            options_.secret.empty()) {
            throw std::invalid_argument(
                "LWeb: JWT HS256 requires a non-empty secret");
        }
    }

    /// 编码生成 token（不校验任何东西，仅签名）。
    std::string Encode(const JwtClaims& claims) const {
        const std::string header =
            std::string("{\"alg\":\"") +
            detail::JwtAlgorithmName(options_.algorithm) +
            "\",\"typ\":\"JWT\"}";
        const std::string signing_input =
            detail::Base64UrlEncode(header) + "." +
            detail::Base64UrlEncode(claims.ToJson().dump());
        auto sig = detail::HmacSha256(options_.secret, signing_input);
        return signing_input + "." +
               detail::Base64UrlEncode(
                   std::string_view(reinterpret_cast<const char*>(sig.data()),
                                    sig.size()));
    }

    /// 便捷生成：sub + 有效期（iat=now）。完整定制请用 Encode(claims)。
    std::string CreateToken(std::string subject,
                            std::chrono::seconds lifetime) const {
        JwtClaims claims;
        claims.subject = std::move(subject);
        claims.issued_at = JwtTimePoint::clock::now();
        if (lifetime.count() > 0) {
            claims.expires_at = *claims.issued_at + lifetime;
        }
        return Encode(claims);
    }

    /// 完整验证：signature + exp + nbf +（可选 iss/aud）。
    /// alg 严格按服务器配置校验（防算法混淆；alg=none 必然拒绝）。
    JwtVerifyResult Verify(std::string_view token) const {
        auto decoded = detail::DecodeToken(token);
        if (!decoded) {
            return {JwtError::malformed_token, std::nullopt};
        }

        // 算法混淆防护：请求 alg 必须等于服务器配置。
        std::string alg;
        try {
            if (!decoded->header.contains("alg")) {
                return {JwtError::malformed_token, std::nullopt};
            }
            alg = decoded->header["alg"].get_as<std::string>();
        }
        catch (const std::exception&) {
            return {JwtError::malformed_token, std::nullopt};
        }
        if (alg != detail::JwtAlgorithmName(options_.algorithm)) {
            return {JwtError::algorithm_mismatch, std::nullopt};
        }

        // 验签（常量时间）。
        auto expected = detail::HmacSha256(options_.secret,
                                           decoded->signing_input);
        if (decoded->signature.size() != expected.size() ||
            !detail::ConstantTimeEquals(
                reinterpret_cast<const unsigned char*>(decoded->signature.data()),
                expected.data(), expected.size())) {
            return {JwtError::invalid_signature, std::nullopt};
        }

        // Claims 验证。
        JwtClaims claims;
        try {
            claims = JwtClaims::FromJson(decoded->payload);
        }
        catch (const std::exception&) {
            return {JwtError::malformed_token, std::nullopt};
        }

        const auto now = JwtTimePoint::clock::now();
        const auto skew = options_.clock_skew;
        // exp：过期判断允许 clock skew（服务器时钟可能领先签发者最多 skew）。
        if (claims.expires_at && now - skew >= *claims.expires_at) {
            return {JwtError::token_expired, std::nullopt};
        }
        // nbf：未生效判断允许 clock skew（服务器时钟可能落后签发者最多 skew）。
        if (claims.not_before && *claims.not_before > now + skew) {
            return {JwtError::token_not_active, std::nullopt};
        }
        if (!options_.issuer.empty() &&
            (!claims.issuer || *claims.issuer != options_.issuer)) {
            return {JwtError::invalid_issuer, std::nullopt};
        }
        if (!options_.audience.empty() &&
            std::find(claims.audience.begin(), claims.audience.end(),
                      options_.audience) == claims.audience.end()) {
            return {JwtError::invalid_audience, std::nullopt};
        }

        return {JwtError::ok, std::move(claims)};
    }

    /// 仅解析，不验签、不验证 claims。名称显式含 Unsafe，仅供调试/展示。
    /// 注意：返回的 claims 字段未经验证，仅表示 token 内容。
    /// 结构或字段非法时抛 std::invalid_argument（统一异常类型）。
    JwtClaims DecodeUnsafe(std::string_view token) const {
        auto decoded = detail::DecodeToken(token);
        if (!decoded) {
            throw std::invalid_argument("LWeb JWT: malformed token");
        }
        try {
            return JwtClaims::FromJson(decoded->payload);
        }
        catch (const std::exception&) {
            throw std::invalid_argument("LWeb JWT: malformed claims");
        }
    }

    const JwtOptions& Options() const { return options_; }

private:
    JwtOptions options_;
};

// ---- 全局默认 JWT（便捷入口） ----
// 配合 LWeb::middleware::JwtAuth{}（缺省构造）使用，让普通路由无需持有
// Jwt 实例即可挂认证。
// 线程模型：必须在 Start()/Run() 之前设置（与路由注册同一阶段）；
// 设置后不可变、并发只读安全（shared_ptr<const Jwt>）。

/// 全局默认 JWT 存储（内部）。
inline std::shared_ptr<const Jwt>& DefaultJwtStorage() {
    static std::shared_ptr<const Jwt> instance;
    return instance;
}

/// 以实例安装全局默认 JWT（共享持有，生命周期由 shared_ptr 保证）。
inline void SetDefaultJwt(std::shared_ptr<const Jwt> jwt) {
    if (!jwt) {
        throw std::invalid_argument(
            "LWeb: SetDefaultJwt requires a non-null Jwt");
    }
    DefaultJwtStorage() = std::move(jwt);
}

/// 以配置安装全局默认 JWT。
inline void SetDefaultJwt(JwtOptions options) {
    SetDefaultJwt(std::make_shared<Jwt>(std::move(options)));
}

/// 获取全局默认 JWT 的共享持有（未设置时返回空）。
inline std::shared_ptr<const Jwt> DefaultJwtShared() {
    return DefaultJwtStorage();
}

/// 获取全局默认 JWT 引用（未设置时抛异常，便于失败快速暴露）。
inline const Jwt& DefaultJwt() {
    auto p = DefaultJwtStorage();
    if (!p) {
        throw std::logic_error(
            "LWeb: 全局默认 JWT 未设置，请先调用 LWeb::SetDefaultJwt(...)");
    }
    return *p;
}

} // namespace LWeb

#endif // LTOOL_LWEB_DETAIL_JWT_HPP
