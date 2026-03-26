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
    TokenStore store(dbPath);
    EXPECT_FALSE(store.isRevoked("jti-001"));
    store.revoke("jti-001");
    EXPECT_TRUE(store.isRevoked("jti-001"));
}

TEST_F(TokenStoreSqliteTest, PersistAcrossRestart)
{
    {
        TokenStore store(dbPath);
        store.revoke("jti-persist");
    }
    TokenStore store2(dbPath);
    EXPECT_TRUE(store2.isRevoked("jti-persist"));
}

TEST_F(TokenStoreSqliteTest, PurgeExpiredRemovesOld)
{
    TokenStore store(dbPath);
    store.revokeWithExpiry("jti-old", std::chrono::system_clock::now() - std::chrono::hours(1));
    store.revokeWithExpiry("jti-new", std::chrono::system_clock::now() + std::chrono::hours(1));
    store.purgeExpired();
    EXPECT_FALSE(store.isRevoked("jti-old"));
    EXPECT_TRUE(store.isRevoked("jti-new"));
}

TEST_F(TokenStoreSqliteTest, InMemoryModeWorks)
{
    TokenStore store;
    store.revoke("jti-mem");
    EXPECT_TRUE(store.isRevoked("jti-mem"));
}
