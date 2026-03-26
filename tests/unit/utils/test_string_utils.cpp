#include <gtest/gtest.h>
#include "utils/string_utils.hpp"

using namespace hub32api::utils;

// ---- trim() ----

TEST(StringUtilsTrim, EmptyString) {
    EXPECT_EQ(trim(""), "");
}

TEST(StringUtilsTrim, WhitespaceOnly) {
    EXPECT_EQ(trim("   "), "");
    EXPECT_EQ(trim("\t\t"), "");
    EXPECT_EQ(trim(" \t \n "), "");
}

TEST(StringUtilsTrim, LeadingSpaces) {
    EXPECT_EQ(trim("   hello"), "hello");
}

TEST(StringUtilsTrim, TrailingSpaces) {
    EXPECT_EQ(trim("hello   "), "hello");
}

TEST(StringUtilsTrim, LeadingAndTrailingSpaces) {
    EXPECT_EQ(trim("  hello world  "), "hello world");
}

TEST(StringUtilsTrim, Tabs) {
    EXPECT_EQ(trim("\thello\t"), "hello");
}

TEST(StringUtilsTrim, AlreadyTrimmed) {
    EXPECT_EQ(trim("hello"), "hello");
}

// ---- to_lower() ----

TEST(StringUtilsToLower, MixedCase) {
    EXPECT_EQ(to_lower("Hello World"), "hello world");
}

TEST(StringUtilsToLower, AlreadyLower) {
    EXPECT_EQ(to_lower("hello"), "hello");
}

TEST(StringUtilsToLower, AllUpper) {
    EXPECT_EQ(to_lower("HELLO"), "hello");
}

TEST(StringUtilsToLower, EmptyString) {
    EXPECT_EQ(to_lower(""), "");
}

TEST(StringUtilsToLower, NumbersAndSymbolsUnchanged) {
    EXPECT_EQ(to_lower("ABC123!@#"), "abc123!@#");
}

// ---- split() ----

TEST(StringUtilsSplit, NormalDelimiter) {
    auto result = split("a,b,c", ',');
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
}

TEST(StringUtilsSplit, NoDelimiterFound) {
    auto result = split("hello", ',');
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "hello");
}

TEST(StringUtilsSplit, EmptyInput) {
    auto result = split("", ',');
    // Expect either empty vector or single empty string
    if (!result.empty()) {
        EXPECT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0], "");
    }
}

TEST(StringUtilsSplit, ConsecutiveDelimiters) {
    auto result = split("a,,b", ',');
    ASSERT_GE(result.size(), 2u);
    EXPECT_EQ(result[0], "a");
    // Middle element may be empty string depending on implementation
    EXPECT_EQ(result.back(), "b");
}

// ---- join() ----

TEST(StringUtilsJoin, Normal) {
    std::vector<std::string> v = {"a", "b", "c"};
    EXPECT_EQ(join(v, ", "), "a, b, c");
}

TEST(StringUtilsJoin, EmptyVector) {
    std::vector<std::string> v;
    EXPECT_EQ(join(v, ", "), "");
}

TEST(StringUtilsJoin, SingleElement) {
    std::vector<std::string> v = {"hello"};
    EXPECT_EQ(join(v, ", "), "hello");
}

// ---- starts_with() ----

TEST(StringUtilsStartsWith, TrueCase) {
    EXPECT_TRUE(starts_with("hello world", "hello"));
}

TEST(StringUtilsStartsWith, FalseCase) {
    EXPECT_FALSE(starts_with("hello world", "world"));
}

TEST(StringUtilsStartsWith, EmptyPrefix) {
    EXPECT_TRUE(starts_with("hello", ""));
}

TEST(StringUtilsStartsWith, EmptyString) {
    EXPECT_FALSE(starts_with("", "hello"));
}

TEST(StringUtilsStartsWith, EmptyBoth) {
    EXPECT_TRUE(starts_with("", ""));
}

TEST(StringUtilsStartsWith, PrefixLongerThanString) {
    EXPECT_FALSE(starts_with("hi", "hello"));
}

// ---- ends_with() ----

TEST(StringUtilsEndsWith, TrueCase) {
    EXPECT_TRUE(ends_with("hello world", "world"));
}

TEST(StringUtilsEndsWith, FalseCase) {
    EXPECT_FALSE(ends_with("hello world", "hello"));
}

TEST(StringUtilsEndsWith, EmptySuffix) {
    EXPECT_TRUE(ends_with("hello", ""));
}

TEST(StringUtilsEndsWith, EmptyString) {
    EXPECT_FALSE(ends_with("", "world"));
}

TEST(StringUtilsEndsWith, EmptyBoth) {
    EXPECT_TRUE(ends_with("", ""));
}

TEST(StringUtilsEndsWith, SuffixLongerThanString) {
    EXPECT_FALSE(ends_with("hi", "hello"));
}

// ---- bytes_to_hex() ----

TEST(StringUtilsBytesToHex, KnownBytes) {
    unsigned char data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_EQ(bytes_to_hex(data, 4), "deadbeef");
}

TEST(StringUtilsBytesToHex, SingleByte) {
    unsigned char data[] = {0x0A};
    EXPECT_EQ(bytes_to_hex(data, 1), "0a");
}

TEST(StringUtilsBytesToHex, EmptyInput) {
    EXPECT_EQ(bytes_to_hex(nullptr, 0), "");
}

TEST(StringUtilsBytesToHex, ZeroByte) {
    unsigned char data[] = {0x00};
    EXPECT_EQ(bytes_to_hex(data, 1), "00");
}

// ---- hex_to_bytes() ----

TEST(StringUtilsHexToBytes, KnownHex) {
    auto result = hex_to_bytes("deadbeef");
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], 0xDE);
    EXPECT_EQ(result[1], 0xAD);
    EXPECT_EQ(result[2], 0xBE);
    EXPECT_EQ(result[3], 0xEF);
}

TEST(StringUtilsHexToBytes, OddLength) {
    auto result = hex_to_bytes("abc");
    // Odd-length hex is invalid, should return empty
    EXPECT_TRUE(result.empty());
}

TEST(StringUtilsHexToBytes, EmptyInput) {
    auto result = hex_to_bytes("");
    EXPECT_TRUE(result.empty());
}

TEST(StringUtilsHexToBytes, UppercaseHex) {
    auto result = hex_to_bytes("DEADBEEF");
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], 0xDE);
    EXPECT_EQ(result[1], 0xAD);
}

TEST(StringUtilsHexToBytes, RoundTrip) {
    unsigned char original[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    auto hex = bytes_to_hex(original, 8);
    auto bytes = hex_to_bytes(hex);
    ASSERT_EQ(bytes.size(), 8u);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(bytes[i], original[i]);
    }
}
