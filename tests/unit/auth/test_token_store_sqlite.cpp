#include <gtest/gtest.h>
#include "auth/internal/TokenStore.hpp"
#include <filesystem>

using hub32api::auth::internal::TokenStore;

class TokenStoreSqliteTest : public ::testing::Test {
protected:
    std::string dbPath = "test_tokens.db";
    void TearDown() override {
        std::filesystem::remove(dbPath);
        std::filesystem::remove(dbPath + "-wal");
        std::filesystem::remove(dbPath + "-shm");
    }
};

TEST_F(TokenStoreSqliteTest, RevokeAndCheck)
{
    auto result = TokenStore::create(dbPath);
    ASSERT_TRUE(result.is_ok());
    auto store = result.take();
    EXPECT_FALSE(store->isRevoked("jti-001"));
    store->revoke("jti-001");
    EXPECT_TRUE(store->isRevoked("jti-001"));
}

TEST_F(TokenStoreSqliteTest, PersistAcrossRestart)
{
    {
        auto result = TokenStore::create(dbPath);
        ASSERT_TRUE(result.is_ok());
        auto store = result.take();
        store->revoke("jti-persist");
    }
    auto result2 = TokenStore::create(dbPath);
    ASSERT_TRUE(result2.is_ok());
    auto store2 = result2.take();
    EXPECT_TRUE(store2->isRevoked("jti-persist"));
}

TEST_F(TokenStoreSqliteTest, PurgeExpiredRemovesOld)
{
    auto result = TokenStore::create(dbPath);
    ASSERT_TRUE(result.is_ok());
    auto store = result.take();
    store->revokeWithExpiry("jti-old", std::chrono::system_clock::now() - std::chrono::hours(1));
    store->revokeWithExpiry("jti-new", std::chrono::system_clock::now() + std::chrono::hours(1));
    store->purgeExpired();
    EXPECT_FALSE(store->isRevoked("jti-old"));
    EXPECT_TRUE(store->isRevoked("jti-new"));
}

TEST_F(TokenStoreSqliteTest, InMemoryModeWorks)
{
    auto result = TokenStore::create();
    ASSERT_TRUE(result.is_ok());
    auto store = result.take();
    store->revoke("jti-mem");
    EXPECT_TRUE(store->isRevoked("jti-mem"));
}

TEST_F(TokenStoreSqliteTest, FailsOnBadDbPath)
{
    auto result = TokenStore::create("/nonexistent/dir/impossible.db");
    EXPECT_TRUE(result.is_err());
}
