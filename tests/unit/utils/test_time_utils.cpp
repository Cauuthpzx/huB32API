#include <gtest/gtest.h>
#include "utils/time_utils.hpp"

#include <regex>
#include <chrono>
#include <ctime>

using namespace hub32api::utils;

// ---- now_unix() ----

TEST(TimeUtilsNowUnix, ReturnsPositiveValue) {
    auto t = now_unix();
    EXPECT_GT(t, 0);
}

TEST(TimeUtilsNowUnix, CloseToCurrentTime) {
    auto t = now_unix();
    auto sys_now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    // Should be within 2 seconds of system time
    EXPECT_NEAR(static_cast<double>(t), static_cast<double>(sys_now), 2.0);
}

// ---- now_unix_ms() ----

TEST(TimeUtilsNowUnixMs, ReturnsPositiveValue) {
    auto ms = now_unix_ms();
    EXPECT_GT(ms, 0);
}

TEST(TimeUtilsNowUnixMs, GreaterThanSecondsBased) {
    auto sec = now_unix();
    auto ms = now_unix_ms();
    // ms should be at least sec*1000 - 1000 (allowing 1s tolerance)
    EXPECT_GT(ms, sec * 1000 - 1000);
}

TEST(TimeUtilsNowUnixMs, MillisecondPrecision) {
    auto ms1 = now_unix_ms();
    auto ms2 = now_unix_ms();
    // Second call should be >= first call
    EXPECT_GE(ms2, ms1);
}

// ---- format_iso8601() ----

TEST(TimeUtilsFormatIso8601, KnownTimePoint) {
    // Create a known time_point: 2026-01-01T00:00:00Z (Unix epoch 1767225600)
    auto tp = std::chrono::system_clock::from_time_t(1767225600);
    auto result = format_iso8601(tp);
    EXPECT_EQ(result, "2026-01-01T00:00:00Z");
}

TEST(TimeUtilsFormatIso8601, MatchesIso8601Format) {
    auto tp = std::chrono::system_clock::now();
    auto result = format_iso8601(tp);
    // Regex: YYYY-MM-DDTHH:MM:SSZ
    std::regex iso_pattern(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)");
    EXPECT_TRUE(std::regex_match(result, iso_pattern))
        << "Result was: " << result;
}

TEST(TimeUtilsFormatIso8601, EpochZero) {
    auto tp = std::chrono::system_clock::from_time_t(0);
    auto result = format_iso8601(tp);
    EXPECT_EQ(result, "1970-01-01T00:00:00Z");
}

// ---- format_iso8601_now() ----

TEST(TimeUtilsFormatIso8601Now, ReturnsNonEmpty) {
    auto result = format_iso8601_now();
    EXPECT_FALSE(result.empty());
}

TEST(TimeUtilsFormatIso8601Now, ContainsTAndZ) {
    auto result = format_iso8601_now();
    EXPECT_NE(result.find('T'), std::string::npos) << "Missing 'T': " << result;
    EXPECT_NE(result.find('Z'), std::string::npos) << "Missing 'Z': " << result;
}

TEST(TimeUtilsFormatIso8601Now, MatchesIso8601Format) {
    auto result = format_iso8601_now();
    std::regex iso_pattern(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)");
    EXPECT_TRUE(std::regex_match(result, iso_pattern))
        << "Result was: " << result;
}
