/**
 * @file LDateTime.hpp
 * @brief 日历日期时间、ISO-8601/RFC 3339 格式化与解析、以及时长文本工具。
 *
 * LDateTime 基于 std::chrono::system_clock 封装时间戳，提供线程安全的
 * UTC / 本地日历拆分、ISO-8601 互转、strftime 格式化和人类可读时长解析。
 *
 * 主要接口：
 * - LDateTime::now() / from_unix_seconds() / from_unix_ms() / from_utc() / from_local()。
 * - parse_iso8601() / try_parse_iso8601()：解析 ISO-8601 / RFC 3339（含毫秒与时区偏移）。
 * - to_iso8601() / to_iso8601_local() / format()：格式化输出。
 * - utc_fields() / local_fields()：获取年/月/日/时/分/秒/毫秒/星期。
 * - LDateTime::parse_duration() / format_duration()：解析与格式化 `"1h30m500ms"` 等时长文本。
 *
 * 常用示例：
 * @code
 * auto now = LDateTime::now();
 * auto iso = now.to_iso8601();                       // "2026-11-18T12:30:00Z"
 * auto dt = LDateTime::parse_iso8601("2026-11-18T20:30:00.250+08:00");
 * auto d = LDateTime::parse_duration("1m30s");       // 90s
 * @endcode
 */

#ifndef LTOOL_LDATETIME_INCLUDE
#define LTOOL_LDATETIME_INCLUDE

#include "detail/LToolConfig.hpp"
#include "detail/LFmt.hpp"

#if !LTOOL_HAS_CPP17
#error "LDateTime requires C++17 or later"
#endif

#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace LDateTimeDetail {

/// 线程安全地将 time_t 拆分为 UTC tm 结构。
inline std::tm to_utc_tm(std::time_t value) {
    std::tm out{};
#if LTOOL_PLATFORM_WINDOWS
    if (gmtime_s(&out, &value) != 0) {
        throw std::runtime_error("LDateTime gmtime_s failed");
    }
#else
    if (!gmtime_r(&value, &out)) {
        throw std::runtime_error("LDateTime gmtime_r failed");
    }
#endif
    return out;
}

/// 线程安全地将 time_t 拆分为本地时区 tm 结构。
inline std::tm to_local_tm(std::time_t value) {
    std::tm out{};
#if LTOOL_PLATFORM_WINDOWS
    if (localtime_s(&out, &value) != 0) {
        throw std::runtime_error("LDateTime localtime_s failed");
    }
#else
    if (!localtime_r(&value, &out)) {
        throw std::runtime_error("LDateTime localtime_r failed");
    }
#endif
    return out;
}

/// 线程安全地将 UTC tm 结构转回 time_t。
inline std::time_t from_utc_tm(std::tm tm_val) {
#if LTOOL_PLATFORM_WINDOWS
    const std::time_t t = _mkgmtime(&tm_val);
#else
    const std::time_t t = timegm(&tm_val);
#endif
    if (t == static_cast<std::time_t>(-1)) {
        throw std::invalid_argument("LDateTime invalid UTC calendar fields");
    }
    return t;
}

/// 线程安全地将本地 tm 结构转回 time_t。
inline std::time_t from_local_tm(std::tm tm_val) {
    tm_val.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm_val);
    if (t == static_cast<std::time_t>(-1)) {
        throw std::invalid_argument("LDateTime invalid local calendar fields");
    }
    return t;
}

/// 解析定长正整数片段。
inline bool parse_digits(std::string_view sv, std::size_t pos, std::size_t len, int& out) noexcept {
    if (pos + len > sv.size()) {
        return false;
    }
    int val = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char ch = static_cast<unsigned char>(sv[pos + i]);
        if (ch < '0' || ch > '9') {
            return false;
        }
        val = val * 10 + (ch - '0');
    }
    out = val;
    return true;
}

/// 判断是否为闰年。
inline constexpr bool is_leap_year(int year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/// 返回某年某月的天数。
inline constexpr int days_in_month(int year, int month) noexcept {
    constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    if (month < 1 || month > 12) {
        return 0;
    }
    return kDays[month - 1];
}

/// 分割 time_point 为秒与毫秒余数 [0, 999]（向负无穷取整）。
inline void split_seconds_and_ms(std::chrono::system_clock::time_point tp,
                                 std::time_t& out_sec, int& out_ms) noexcept {
    const auto total_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    auto sec = total_ms / 1000;
    auto rem = static_cast<int>(total_ms % 1000);
    if (rem < 0) {
        rem += 1000;
        sec -= 1;
    }
    out_sec = static_cast<std::time_t>(sec);
    out_ms = rem;
}

} // namespace LDateTimeDetail

/// 拆解后的日历字段。
struct LDateTimeFields {
    int year = 1970;     ///< 公历年份。
    int month = 1;       ///< 月份 [1, 12]。
    int day = 1;         ///< 日期 [1, 31]。
    int hour = 0;        ///< 小时 [0, 23]。
    int minute = 0;      ///< 分钟 [0, 59]。
    int second = 0;      ///< 秒 [0, 60]。
    int millisecond = 0; ///< 毫秒 [0, 999]。
    int weekday = 0;     ///< 星期几（0=周日, 1=周一, ..., 6=周六）。
};

/// 基于 system_clock 的日期时间封装。
class LDateTime {
public:
    using clock = std::chrono::system_clock;     ///< 底层时钟类型。
    using time_point = clock::time_point;        ///< 时间点类型。
    using duration = clock::duration;            ///< 时长类型。

    /// 默认构造：Unix 纪元起点（1970-01-01T00:00:00Z）。
    LDateTime() = default;

    /// 从 std::chrono::system_clock::time_point 构造。
    explicit LDateTime(time_point tp) noexcept
        : tp_(tp) {}

    /// 返回当前时刻。
    [[nodiscard]] static LDateTime now() noexcept {
        return LDateTime(clock::now());
    }

    /// 从 Unix 秒时间戳构造。
    [[nodiscard]] static LDateTime from_unix_seconds(std::int64_t seconds) noexcept {
        return LDateTime(time_point(std::chrono::seconds(seconds)));
    }

    /// 从 Unix 毫秒时间戳构造。
    [[nodiscard]] static LDateTime from_unix_ms(std::int64_t milliseconds) noexcept {
        return LDateTime(time_point(std::chrono::milliseconds(milliseconds)));
    }

    /// 从 UTC 日历字段构造；非法字段抛出 std::invalid_argument。
    [[nodiscard]] static LDateTime from_utc(int year, int month, int day,
                                            int hour = 0, int minute = 0, int second = 0,
                                            int millisecond = 0) {
        validate_fields(year, month, day, hour, minute, second, millisecond);
        std::tm tm_val{};
        tm_val.tm_year = year - 1900;
        tm_val.tm_mon = month - 1;
        tm_val.tm_mday = day;
        tm_val.tm_hour = hour;
        tm_val.tm_min = minute;
        tm_val.tm_sec = second;
        const auto sec = LDateTimeDetail::from_utc_tm(tm_val);
        return LDateTime(clock::from_time_t(sec) + std::chrono::milliseconds(millisecond));
    }

    /// 从本地时区日历字段构造；非法字段抛出 std::invalid_argument。
    [[nodiscard]] static LDateTime from_local(int year, int month, int day,
                                              int hour = 0, int minute = 0, int second = 0,
                                              int millisecond = 0) {
        validate_fields(year, month, day, hour, minute, second, millisecond);
        std::tm tm_val{};
        tm_val.tm_year = year - 1900;
        tm_val.tm_mon = month - 1;
        tm_val.tm_mday = day;
        tm_val.tm_hour = hour;
        tm_val.tm_min = minute;
        tm_val.tm_sec = second;
        const auto sec = LDateTimeDetail::from_local_tm(tm_val);
        return LDateTime(clock::from_time_t(sec) + std::chrono::milliseconds(millisecond));
    }

    /// 尝试解析 ISO-8601 / RFC 3339 文本（支持日期、空格/T 分隔、小数秒、Z/±HH:MM 偏移）。
    [[nodiscard]] static std::optional<LDateTime> try_parse_iso8601(std::string_view text) noexcept {
        int year = 0;
        int month = 0;
        int day = 0;
        if (text.size() < 10 || text[4] != '-' || text[7] != '-') {
            return std::nullopt;
        }
        if (!LDateTimeDetail::parse_digits(text, 0, 4, year) ||
            !LDateTimeDetail::parse_digits(text, 5, 2, month) ||
            !LDateTimeDetail::parse_digits(text, 8, 2, day)) {
            return std::nullopt;
        }

        int hour = 0;
        int minute = 0;
        int second = 0;
        int ms = 0;
        std::size_t pos = 10;
        bool has_tz = false;
        int tz_offset_sec = 0;

        if (pos < text.size()) {
            if (text[pos] != 'T' && text[pos] != 't' && text[pos] != ' ') {
                return std::nullopt;
            }
            ++pos;
            if (pos + 8 > text.size() || text[pos + 2] != ':' || text[pos + 5] != ':') {
                return std::nullopt;
            }
            if (!LDateTimeDetail::parse_digits(text, pos, 2, hour) ||
                !LDateTimeDetail::parse_digits(text, pos + 3, 2, minute) ||
                !LDateTimeDetail::parse_digits(text, pos + 6, 2, second)) {
                return std::nullopt;
            }
            pos += 8;

            // 可选小数秒（截取/补齐到毫秒）。
            if (pos < text.size() && (text[pos] == '.' || text[pos] == ',')) {
                ++pos;
                std::size_t frac_digits = 0;
                int frac_ms = 0;
                while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                    if (frac_digits < 3) {
                        frac_ms = frac_ms * 10 + (text[pos] - '0');
                    }
                    ++frac_digits;
                    ++pos;
                }
                if (frac_digits == 0) {
                    return std::nullopt;
                }
                while (frac_digits < 3) {
                    frac_ms *= 10;
                    ++frac_digits;
                }
                ms = frac_ms;
            }

            // 可选时区：Z 或 ±HH:MM / ±HHMM。
            if (pos < text.size()) {
                has_tz = true;
                if (text[pos] == 'Z' || text[pos] == 'z') {
                    ++pos;
                } else if (text[pos] == '+' || text[pos] == '-') {
                    const int sign = text[pos] == '-' ? -1 : 1;
                    ++pos;
                    int tz_h = 0;
                    int tz_m = 0;
                    if (!LDateTimeDetail::parse_digits(text, pos, 2, tz_h)) {
                        return std::nullopt;
                    }
                    pos += 2;
                    if (pos < text.size() && text[pos] == ':') {
                        ++pos;
                    }
                    if (!LDateTimeDetail::parse_digits(text, pos, 2, tz_m)) {
                        return std::nullopt;
                    }
                    pos += 2;
                    if (tz_h > 23 || tz_m > 59) {
                        return std::nullopt;
                    }
                    tz_offset_sec = sign * (tz_h * 3600 + tz_m * 60);
                } else {
                    return std::nullopt;
                }
            }
        }

        if (pos != text.size()) {
            return std::nullopt;
        }

        try {
            if (!is_valid_fields(year, month, day, hour, minute, second, ms)) {
                return std::nullopt;
            }
            if (has_tz) {
                auto dt = from_utc(year, month, day, hour, minute, second, ms);
                return LDateTime(dt.tp_ - std::chrono::seconds(tz_offset_sec));
            }
            // 无时区后缀的纯日期/时间按 UTC 处理（保证跨环境可复现）。
            return from_utc(year, month, day, hour, minute, second, ms);
        } catch (...) {
            return std::nullopt;
        }
    }

    /// 解析 ISO-8601 / RFC 3339 文本；格式非法抛出 std::invalid_argument。
    [[nodiscard]] static LDateTime parse_iso8601(std::string_view text) {
        auto parsed = try_parse_iso8601(text);
        if (!parsed) {
            throw std::invalid_argument("LDateTime invalid ISO-8601 timestamp: " +
                                        std::string(text));
        }
        return *parsed;
    }

    /// 返回底层 system_clock::time_point。
    [[nodiscard]] time_point to_time_point() const noexcept {
        return tp_;
    }

    /// 返回 Unix 秒时间戳（向负无穷取整）。
    [[nodiscard]] std::int64_t unix_seconds() const noexcept {
        std::time_t sec = 0;
        int ms = 0;
        LDateTimeDetail::split_seconds_and_ms(tp_, sec, ms);
        return static_cast<std::int64_t>(sec);
    }

    /// 返回 Unix 毫秒时间戳。
    [[nodiscard]] std::int64_t unix_ms() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(tp_.time_since_epoch())
            .count();
    }

    /// 拆分为 UTC 日历字段。
    [[nodiscard]] LDateTimeFields utc_fields() const {
        std::time_t sec = 0;
        int ms = 0;
        LDateTimeDetail::split_seconds_and_ms(tp_, sec, ms);
        const auto tm_val = LDateTimeDetail::to_utc_tm(sec);
        return LDateTimeFields{
            tm_val.tm_year + 1900,
            tm_val.tm_mon + 1,
            tm_val.tm_mday,
            tm_val.tm_hour,
            tm_val.tm_min,
            tm_val.tm_sec,
            ms,
            tm_val.tm_wday
        };
    }

    /// 拆分为本地时区日历字段。
    [[nodiscard]] LDateTimeFields local_fields() const {
        std::time_t sec = 0;
        int ms = 0;
        LDateTimeDetail::split_seconds_and_ms(tp_, sec, ms);
        const auto tm_val = LDateTimeDetail::to_local_tm(sec);
        return LDateTimeFields{
            tm_val.tm_year + 1900,
            tm_val.tm_mon + 1,
            tm_val.tm_mday,
            tm_val.tm_hour,
            tm_val.tm_min,
            tm_val.tm_sec,
            ms,
            tm_val.tm_wday
        };
    }

    /// 格式化为 UTC ISO-8601 字符串（如 "2026-11-18T12:30:00Z"）。
    [[nodiscard]] std::string to_iso8601(bool with_ms = false) const {
        const auto f = utc_fields();
        char buf[40];
        if (with_ms) {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                          f.year, f.month, f.day, f.hour, f.minute, f.second, f.millisecond);
        } else {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                          f.year, f.month, f.day, f.hour, f.minute, f.second);
        }
        return std::string(buf);
    }

    /// 格式化为本地时间字符串（如 "2026-11-18 20:30:00"）。
    [[nodiscard]] std::string to_local_string(bool with_ms = false) const {
        const auto f = local_fields();
        char buf[40];
        if (with_ms) {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                          f.year, f.month, f.day, f.hour, f.minute, f.second, f.millisecond);
        } else {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                          f.year, f.month, f.day, f.hour, f.minute, f.second);
        }
        return std::string(buf);
    }

    /// 使用 strftime 格式串格式化日期时间（默认本地时区，utc=true 时用 UTC）。
    [[nodiscard]] std::string format(const char* pattern, bool utc = false) const {
        std::time_t sec = 0;
        int ms = 0;
        LDateTimeDetail::split_seconds_and_ms(tp_, sec, ms);
        const auto tm_val = utc ? LDateTimeDetail::to_utc_tm(sec)
                                : LDateTimeDetail::to_local_tm(sec);
        std::ostringstream out;
        out << std::put_time(&tm_val, pattern ? pattern : "%Y-%m-%d %H:%M:%S");
        return out.str();
    }

    /// 增加时长并返回新 LDateTime。
    template<class Rep, class Period>
    [[nodiscard]] LDateTime add(const std::chrono::duration<Rep, Period>& d) const {
        return LDateTime(tp_ + std::chrono::duration_cast<duration>(d));
    }

    /// 加上时长运算符。
    template<class Rep, class Period>
    friend LDateTime operator+(const LDateTime& dt, const std::chrono::duration<Rep, Period>& d) {
        return dt.add(d);
    }

    /// 减去时长运算符。
    template<class Rep, class Period>
    friend LDateTime operator-(const LDateTime& dt, const std::chrono::duration<Rep, Period>& d) {
        return LDateTime(dt.tp_ - std::chrono::duration_cast<duration>(d));
    }

    /// 计算两个时间点之差。
    friend duration operator-(const LDateTime& lhs, const LDateTime& rhs) noexcept {
        return lhs.tp_ - rhs.tp_;
    }

    /// 相等比较。
    friend bool operator==(const LDateTime& lhs, const LDateTime& rhs) noexcept {
        return lhs.tp_ == rhs.tp_;
    }

#if LTOOL_HAS_CPP20
    /// 三路比较。
    friend auto operator<=>(const LDateTime& lhs, const LDateTime& rhs) noexcept = default;
#else
    /// 小于比较。
    friend bool operator<(const LDateTime& lhs, const LDateTime& rhs) noexcept {
        return lhs.tp_ < rhs.tp_;
    }
#endif

    /// 输出 ISO-8601 UTC 文本到流。
    friend std::ostream& operator<<(std::ostream& os, const LDateTime& dt) {
        return os << dt.to_iso8601();
    }

    /// 解析人类可读时长串（支持 ns/us/ms/s/m/h/d 组合，如 "1h30m500ms"）；失败返回 nullopt。
    [[nodiscard]] static std::optional<std::chrono::nanoseconds> parse_duration(
        std::string_view text) noexcept {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
            text.remove_prefix(1);
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
            text.remove_suffix(1);
        }
        if (text.empty()) {
            return std::nullopt;
        }

        bool negative = false;
        if (text.front() == '-') {
            negative = true;
            text.remove_prefix(1);
        } else if (text.front() == '+') {
            text.remove_prefix(1);
        }
        if (text.empty()) {
            return std::nullopt;
        }

        std::int64_t total_ns = 0;
        std::size_t pos = 0;
        while (pos < text.size()) {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            if (pos >= text.size()) {
                break;
            }
            const std::size_t num_start = pos;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            if (pos == num_start || pos >= text.size()) {
                return std::nullopt;
            }
            std::int64_t value = 0;
            const auto [ptr, ec] =
                std::from_chars(text.data() + num_start, text.data() + pos, value);
            if (ec != std::errc{} || ptr != text.data() + pos) {
                return std::nullopt;
            }

            const std::size_t unit_start = pos;
            while (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            const auto unit = text.substr(unit_start, pos - unit_start);
            std::int64_t scale = 0;
            if (unit == "ns") {
                scale = 1LL;
            } else if (unit == "us") {
                scale = 1000LL;
            } else if (unit == "ms") {
                scale = 1000000LL;
            } else if (unit == "s") {
                scale = 1000000000LL;
            } else if (unit == "m") {
                scale = 60LL * 1000000000LL;
            } else if (unit == "h") {
                scale = 3600LL * 1000000000LL;
            } else if (unit == "d") {
                scale = 86400LL * 1000000000LL;
            } else {
                return std::nullopt;
            }

            if (value > (std::numeric_limits<std::int64_t>::max)() / scale) {
                return std::nullopt;
            }
            const std::int64_t part = value * scale;
            if (total_ns > (std::numeric_limits<std::int64_t>::max)() - part) {
                return std::nullopt;
            }
            total_ns += part;
        }

        return std::chrono::nanoseconds(negative ? -total_ns : total_ns);
    }

    /// 把时长格式化为可读文本（如 "1h2m3s" / "250ms"）。
    template<class Rep, class Period>
    [[nodiscard]] static std::string format_duration(const std::chrono::duration<Rep, Period>& d) {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
        if (ns == 0) {
            return "0s";
        }
        std::string out;
        if (ns < 0) {
            out.push_back('-');
            ns = -ns;
        }
        constexpr std::int64_t kNsPerUs = 1000LL;
        constexpr std::int64_t kNsPerMs = 1000LL * kNsPerUs;
        constexpr std::int64_t kNsPerS = 1000LL * kNsPerMs;
        constexpr std::int64_t kNsPerM = 60LL * kNsPerS;
        constexpr std::int64_t kNsPerH = 60LL * kNsPerM;
        constexpr std::int64_t kNsPerD = 24LL * kNsPerH;

        const auto append_unit = [&](std::int64_t unit_ns, const char* suffix) {
            if (ns >= unit_ns) {
                const auto count = ns / unit_ns;
                ns %= unit_ns;
                out += std::to_string(count);
                out += suffix;
            }
        };
        append_unit(kNsPerD, "d");
        append_unit(kNsPerH, "h");
        append_unit(kNsPerM, "m");
        append_unit(kNsPerS, "s");
        append_unit(kNsPerMs, "ms");
        append_unit(kNsPerUs, "us");
        append_unit(1LL, "ns");
        return out;
    }

private:
    static bool is_valid_fields(int year, int month, int day,
                                int hour, int minute, int second, int ms) noexcept {
        if (month < 1 || month > 12) {
            return false;
        }
        const int max_day = LDateTimeDetail::days_in_month(year, month);
        if (day < 1 || day > max_day) {
            return false;
        }
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
            second < 0 || second > 59 || ms < 0 || ms > 999) {
            return false;
        }
        return true;
    }

    static void validate_fields(int year, int month, int day,
                                int hour, int minute, int second, int ms) {
        if (!is_valid_fields(year, month, day, hour, minute, second, ms)) {
            throw std::invalid_argument("LDateTime calendar field out of range");
        }
    }

    time_point tp_{}; ///< 底层时间点（UTC 基准）。
};

namespace LTool {
using DateTime = ::LDateTime;             ///< 日期时间类型别名。
using DateTimeFields = ::LDateTimeFields; ///< 日历字段结构别名。
} // namespace LTool

/// fmt 格式化器：LDateTime 按 ISO-8601 UTC 输出。
template<>
struct fmt::formatter<LDateTime> : fmt::formatter<fmt::string_view> {
    /// 将 LDateTime 写入 fmt 输出上下文。
    template<class FormatContext>
    typename FormatContext::iterator format(const LDateTime& value, FormatContext& ctx) const {
        const auto text = value.to_iso8601();
        return fmt::formatter<fmt::string_view>::format(
            fmt::string_view(text.data(), text.size()), ctx);
    }
};

#if LTOOL_HAS_STD_FORMAT
/// std::format 格式化器：LDateTime 按 ISO-8601 UTC 输出。
template<>
struct std::formatter<LDateTime, char> : std::formatter<std::string, char> {
    /// 将 LDateTime 写入 std::format 输出上下文。
    template<class FormatContext>
    auto format(const LDateTime& value, FormatContext& ctx) const {
        return std::formatter<std::string, char>::format(value.to_iso8601(), ctx);
    }
};
#endif // LTOOL_HAS_STD_FORMAT

#endif // LTOOL_LDATETIME_INCLUDE
