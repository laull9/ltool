#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "LDateTime.hpp"

TEST_CASE("LDateTime parses and formats ISO-8601 UTC timestamps", "[LDateTime]") {
    auto dt = LDateTime::parse_iso8601("2026-11-18T12:30:45.250Z");
    auto fields = dt.utc_fields();
    REQUIRE(fields.year == 2026);
    REQUIRE(fields.month == 11);
    REQUIRE(fields.day == 18);
    REQUIRE(fields.hour == 12);
    REQUIRE(fields.minute == 30);
    REQUIRE(fields.second == 45);
    REQUIRE(fields.millisecond == 250);

    REQUIRE(dt.to_iso8601(false) == "2026-11-18T12:30:45Z");
    REQUIRE(dt.to_iso8601(true) == "2026-11-18T12:30:45.250Z");
    REQUIRE(dt.format("%Y/%m/%d", true) == "2026/11/18");

    auto dt_offset = LDateTime::parse_iso8601("2026-11-18T20:30:45+08:00");
    REQUIRE(dt_offset.to_iso8601() == "2026-11-18T12:30:45Z");

    auto dt_space = LDateTime::parse_iso8601("2026-11-18 12:30:45");
    REQUIRE(dt_space.to_iso8601() == "2026-11-18T12:30:45Z");

    auto invalid = LDateTime::try_parse_iso8601("2026-02-30T00:00:00Z");
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE_THROWS(LDateTime::parse_iso8601("not-a-date"));
}

TEST_CASE("LDateTime arithmetic and epoch conversions", "[LDateTime]") {
    auto base = LDateTime::from_utc(2024, 2, 28, 23, 0, 0);
    auto leap = base + std::chrono::hours(2); // 2024 是闰年
    REQUIRE(leap.to_iso8601() == "2024-02-29T01:00:00Z");

    auto diff = leap - base;
    REQUIRE(std::chrono::duration_cast<std::chrono::hours>(diff).count() == 2);

    auto roundtrip = LTool::DateTime::from_unix_ms(base.unix_ms());
    REQUIRE(roundtrip == base);
}

TEST_CASE("LDateTime parses and formats durations", "[LDateTime]") {
    auto d1 = LDateTime::parse_duration("1h30m15s250ms");
    REQUIRE(d1.has_value());
    REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(*d1).count() ==
            ((1LL * 3600 + 30 * 60 + 15) * 1000 + 250));
    REQUIRE(LDateTime::format_duration(*d1) == "1h30m15s250ms");

    auto d2 = LDateTime::parse_duration("-2d 500us");
    REQUIRE(d2.has_value());
    REQUIRE(d2->count() < 0);
    REQUIRE(LDateTime::format_duration(*d2) == "-2d500us");
    REQUIRE(LDateTime::format_duration(std::chrono::nanoseconds(0)) == "0s");

    REQUIRE_FALSE(LDateTime::parse_duration("10x").has_value());
    REQUIRE_FALSE(LDateTime::parse_duration("").has_value());
}
