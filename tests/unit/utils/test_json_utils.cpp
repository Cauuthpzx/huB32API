#include <gtest/gtest.h>
#include "utils/json_utils.hpp"

#include <nlohmann/json.hpp>

using namespace hub32api::utils;
using json = nlohmann::json;

// ---- safe_get_string() ----

TEST(JsonUtilsSafeGetString, KeyExists) {
    json j = {{"name", "alice"}};
    auto result = safe_get_string(j, "name");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "alice");
}

TEST(JsonUtilsSafeGetString, KeyMissing) {
    json j = {{"name", "alice"}};
    auto result = safe_get_string(j, "age");
    EXPECT_FALSE(result.has_value());
}

TEST(JsonUtilsSafeGetString, WrongType) {
    json j = {{"count", 42}};
    auto result = safe_get_string(j, "count");
    EXPECT_FALSE(result.has_value());
}

TEST(JsonUtilsSafeGetString, EmptyStringValue) {
    json j = {{"name", ""}};
    auto result = safe_get_string(j, "name");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "");
}

// ---- safe_get_int() ----

TEST(JsonUtilsSafeGetInt, KeyExists) {
    json j = {{"count", 42}};
    auto result = safe_get_int(j, "count");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(JsonUtilsSafeGetInt, KeyMissing) {
    json j = {{"count", 42}};
    auto result = safe_get_int(j, "total");
    EXPECT_FALSE(result.has_value());
}

TEST(JsonUtilsSafeGetInt, WrongType) {
    json j = {{"count", "not a number"}};
    auto result = safe_get_int(j, "count");
    EXPECT_FALSE(result.has_value());
}

TEST(JsonUtilsSafeGetInt, ZeroValue) {
    json j = {{"count", 0}};
    auto result = safe_get_int(j, "count");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0);
}

TEST(JsonUtilsSafeGetInt, NegativeValue) {
    json j = {{"count", -5}};
    auto result = safe_get_int(j, "count");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), -5);
}

// ---- safe_get_bool() ----

TEST(JsonUtilsSafeGetBool, ExistsTrue) {
    json j = {{"active", true}};
    auto result = safe_get_bool(j, "active");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST(JsonUtilsSafeGetBool, ExistsFalse) {
    json j = {{"active", false}};
    auto result = safe_get_bool(j, "active");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}

TEST(JsonUtilsSafeGetBool, KeyMissing) {
    json j = {{"name", "alice"}};
    auto result = safe_get_bool(j, "active");
    EXPECT_FALSE(result.has_value());
}

TEST(JsonUtilsSafeGetBool, WrongType) {
    json j = {{"active", "yes"}};
    auto result = safe_get_bool(j, "active");
    EXPECT_FALSE(result.has_value());
}

TEST(JsonUtilsSafeGetBool, WrongTypeInt) {
    json j = {{"active", 1}};
    auto result = safe_get_bool(j, "active");
    EXPECT_FALSE(result.has_value());
}

// ---- missing_fields() ----

TEST(JsonUtilsMissingFields, AllPresent) {
    json j = {{"name", "alice"}, {"age", 30}, {"active", true}};
    auto missing = missing_fields(j, {"name", "age", "active"});
    EXPECT_TRUE(missing.empty());
}

TEST(JsonUtilsMissingFields, SomeMissing) {
    json j = {{"name", "alice"}};
    auto missing = missing_fields(j, {"name", "age", "active"});
    ASSERT_EQ(missing.size(), 2u);
    EXPECT_NE(std::find(missing.begin(), missing.end(), "age"), missing.end());
    EXPECT_NE(std::find(missing.begin(), missing.end(), "active"), missing.end());
}

TEST(JsonUtilsMissingFields, AllMissing) {
    json j = json::object();
    auto missing = missing_fields(j, {"name", "age"});
    ASSERT_EQ(missing.size(), 2u);
}

TEST(JsonUtilsMissingFields, EmptyRequired) {
    json j = {{"name", "alice"}};
    auto missing = missing_fields(j, {});
    EXPECT_TRUE(missing.empty());
}

TEST(JsonUtilsMissingFields, EmptyJson) {
    json j = json::object();
    auto missing = missing_fields(j, {"field1"});
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0], "field1");
}
