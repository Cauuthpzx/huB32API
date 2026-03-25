#include <gtest/gtest.h>
#include "hub32api/core/Result.hpp"

using namespace hub32api;

TEST(ResultTest, OkHoldsValue)
{
    auto r = Result<int>::ok(42);
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, FailHoldsError)
{
    auto r = Result<int>::fail(ApiError{ErrorCode::NotFound, "missing"});
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error().code, ErrorCode::NotFound);
}

TEST(ResultTest, VoidOk)
{
    auto r = Result<void>::ok();
    EXPECT_TRUE(r.is_ok());
}

TEST(ResultTest, MapTransformsValue)
{
    auto r = Result<int>::ok(10).map<std::string>([](int v){ return std::to_string(v); });
    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), "10");
}

TEST(ResultTest, MapPropagatesError)
{
    auto r = Result<int>::fail(ApiError{ErrorCode::InternalError})
             .map<std::string>([](int){ return "never"; });
    EXPECT_TRUE(r.is_err());
}
