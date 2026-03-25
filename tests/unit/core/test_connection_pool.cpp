#include <gtest/gtest.h>
#include "core/internal/ConnectionPool.hpp"

using namespace veyon32api::core::internal;

TEST(ConnectionPoolTest, InitialActiveCountIsZero)
{
    ConnectionPool pool({ .perHost=2, .global=4, .lifetimeSec=60, .idleSec=30 });
    EXPECT_EQ(pool.activeCount(), 0);
}

// TODO: add tests for acquire/release/evictExpired after implementation
