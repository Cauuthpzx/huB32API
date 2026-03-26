#include <gtest/gtest.h>
#include "db/DatabaseManager.hpp"
#include <filesystem>
#include <sqlite3.h>

using hub32api::db::DatabaseManager;

class DatabaseManagerTest : public ::testing::Test {
protected:
    std::string dir;
    void SetUp() override {
        dir = "test_data_dm_" + std::to_string(reinterpret_cast<uintptr_t>(this));
    }
    void TearDown() override {
        std::filesystem::remove_all(dir);
    }
};

TEST_F(DatabaseManagerTest, OpensAndCreatesSchema)
{
    DatabaseManager dm(dir);
    EXPECT_TRUE(dm.isOpen());
    EXPECT_TRUE(std::filesystem::exists(dir + "/school.db"));
}

TEST_F(DatabaseManagerTest, SchoolDbHasTables)
{
    DatabaseManager dm(dir);
    auto* db = dm.schoolDb();
    ASSERT_NE(db, nullptr);

    // Check schools table exists
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='schools'",
        -1, &stmt, nullptr);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    sqlite3_finalize(stmt);
}

TEST_F(DatabaseManagerTest, ForeignKeysEnabled)
{
    DatabaseManager dm(dir);
    auto* db = dm.schoolDb();
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "PRAGMA foreign_keys", -1, &stmt, nullptr);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 1);
    sqlite3_finalize(stmt);
}
