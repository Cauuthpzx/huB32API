#include <gtest/gtest.h>
#include "utils/validation_utils.hpp"

#include <string>

using namespace hub32api::utils;

// ---- validate_username() ----

TEST(ValidationUtilsUsername, ValidAlphanumeric) {
    EXPECT_TRUE(validate_username("alice"));
    EXPECT_TRUE(validate_username("Bob123"));
}

TEST(ValidationUtilsUsername, ValidWithUnderscore) {
    EXPECT_TRUE(validate_username("alice_bob"));
    EXPECT_TRUE(validate_username("_leading"));
}

TEST(ValidationUtilsUsername, ValidWithHyphenAndDot) {
    EXPECT_TRUE(validate_username("alice-bob"));
    EXPECT_TRUE(validate_username("alice.bob"));
}

TEST(ValidationUtilsUsername, EmptyString) {
    EXPECT_FALSE(validate_username(""));
}

TEST(ValidationUtilsUsername, TooLong) {
    std::string long_name(65, 'a');
    EXPECT_FALSE(validate_username(long_name));
}

TEST(ValidationUtilsUsername, ExactMaxLength) {
    std::string name64(64, 'a');
    EXPECT_TRUE(validate_username(name64));
}

TEST(ValidationUtilsUsername, SpecialCharsRejected) {
    EXPECT_FALSE(validate_username("alice@bob"));
    EXPECT_FALSE(validate_username("hello world"));
    EXPECT_FALSE(validate_username("user!name"));
    EXPECT_FALSE(validate_username("user#1"));
}

TEST(ValidationUtilsUsername, SingleChar) {
    EXPECT_TRUE(validate_username("a"));
}

// ---- validate_id() ----

TEST(ValidationUtilsId, ValidUuid) {
    EXPECT_TRUE(validate_id("550e8400-e29b-41d4-a716-446655440000"));
}

TEST(ValidationUtilsId, WrongLength) {
    EXPECT_FALSE(validate_id("550e8400-e29b-41d4-a716"));
    EXPECT_FALSE(validate_id("550e8400-e29b-41d4-a716-446655440000-extra"));
}

TEST(ValidationUtilsId, MissingDashes) {
    EXPECT_FALSE(validate_id("550e8400e29b41d4a716446655440000"));
}

TEST(ValidationUtilsId, InvalidHexChars) {
    EXPECT_FALSE(validate_id("550e8400-e29b-41d4-a716-44665544gggg"));
}

TEST(ValidationUtilsId, EmptyString) {
    EXPECT_FALSE(validate_id(""));
}

TEST(ValidationUtilsId, UppercaseHex) {
    EXPECT_TRUE(validate_id("550E8400-E29B-41D4-A716-446655440000"));
}

// ---- sanitize_input() ----

TEST(ValidationUtilsSanitize, NoTags) {
    EXPECT_EQ(sanitize_input("hello world"), "hello world");
}

TEST(ValidationUtilsSanitize, HtmlTagsStripped) {
    EXPECT_EQ(sanitize_input("<b>hello</b>"), "hello");
}

TEST(ValidationUtilsSanitize, ScriptTagStripped) {
    auto result = sanitize_input("<script>alert('xss')</script>");
    EXPECT_EQ(result, "alert('xss')");
}

TEST(ValidationUtilsSanitize, NestedTags) {
    auto result = sanitize_input("<div><span>text</span></div>");
    EXPECT_EQ(result, "text");
}

TEST(ValidationUtilsSanitize, LeadingTrailingWhitespaceTrimmed) {
    EXPECT_EQ(sanitize_input("  hello  "), "hello");
}

TEST(ValidationUtilsSanitize, TagsAndWhitespace) {
    EXPECT_EQ(sanitize_input("  <b>hello</b>  "), "hello");
}

TEST(ValidationUtilsSanitize, EmptyInput) {
    EXPECT_EQ(sanitize_input(""), "");
}

TEST(ValidationUtilsSanitize, OnlyTags) {
    EXPECT_EQ(sanitize_input("<br><hr>"), "");
}

// ---- check_length() ----

TEST(ValidationUtilsCheckLength, WithinRange) {
    EXPECT_TRUE(check_length("hello", 1, 10));
}

TEST(ValidationUtilsCheckLength, BelowMin) {
    EXPECT_FALSE(check_length("", 1, 10));
}

TEST(ValidationUtilsCheckLength, AboveMax) {
    EXPECT_FALSE(check_length("hello world", 1, 5));
}

TEST(ValidationUtilsCheckLength, ExactMin) {
    EXPECT_TRUE(check_length("a", 1, 10));
}

TEST(ValidationUtilsCheckLength, ExactMax) {
    EXPECT_TRUE(check_length("hello", 1, 5));
}

TEST(ValidationUtilsCheckLength, MinEqualsMax) {
    EXPECT_TRUE(check_length("abc", 3, 3));
    EXPECT_FALSE(check_length("ab", 3, 3));
    EXPECT_FALSE(check_length("abcd", 3, 3));
}

TEST(ValidationUtilsCheckLength, ZeroMinAllowsEmpty) {
    EXPECT_TRUE(check_length("", 0, 10));
}
