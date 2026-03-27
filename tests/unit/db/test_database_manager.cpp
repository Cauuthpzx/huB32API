#include <gtest/gtest.h>
#include "db/DatabaseManager.hpp"
#include <filesystem>
#include <sqlite3.h>
#include <algorithm>
#include <vector>
#include <string>

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

    /// Helper: query all table names from sqlite_master
    std::vector<std::string> getTableNames(sqlite3* db) {
        std::vector<std::string> tables;
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db,
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",
            -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            tables.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
        return tables;
    }

    /// Helper: query all index names from sqlite_master
    std::vector<std::string> getIndexNames(sqlite3* db) {
        std::vector<std::string> indexes;
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db,
            "SELECT name FROM sqlite_master WHERE type='index' AND name NOT LIKE 'sqlite_%' ORDER BY name",
            -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            indexes.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
        return indexes;
    }

    /// Helper: get PRAGMA value as string
    std::string getPragma(sqlite3* db, const char* pragma) {
        sqlite3_stmt* stmt = nullptr;
        std::string value;
        if (sqlite3_prepare_v2(db, pragma, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                auto* text = sqlite3_column_text(stmt, 0);
                if (text) value = reinterpret_cast<const char*>(text);
            }
        }
        sqlite3_finalize(stmt);
        return value;
    }
};

TEST_F(DatabaseManagerTest, OpensAndCreatesSchema)
{
    DatabaseManager dm(dir);
    EXPECT_TRUE(dm.isOpen());
    EXPECT_TRUE(std::filesystem::exists(dir + "/school.db"));
}

TEST_F(DatabaseManagerTest, AllTablesExist)
{
    DatabaseManager dm(dir);
    auto* db = dm.schoolDb();
    ASSERT_NE(db, nullptr);

    auto tables = getTableNames(db);
    EXPECT_NE(std::find(tables.begin(), tables.end(), "schools"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "locations"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "computers"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "teachers"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "teacher_locations"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "active_sessions"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "tenants"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "classes"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "students"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "registration_tokens"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "pending_requests"), tables.end());
}

TEST_F(DatabaseManagerTest, IndexesExist)
{
    DatabaseManager dm(dir);
    auto* db = dm.schoolDb();
    ASSERT_NE(db, nullptr);

    auto indexes = getIndexNames(db);
    EXPECT_NE(std::find(indexes.begin(), indexes.end(), "idx_computers_location"), indexes.end());
    EXPECT_NE(std::find(indexes.begin(), indexes.end(), "idx_computers_state"), indexes.end());
    EXPECT_NE(std::find(indexes.begin(), indexes.end(), "idx_locations_school"), indexes.end());
    EXPECT_NE(std::find(indexes.begin(), indexes.end(), "idx_teachers_username"), indexes.end());
}

TEST_F(DatabaseManagerTest, WalModeEnabled)
{
    DatabaseManager dm(dir);
    auto* db = dm.schoolDb();
    ASSERT_NE(db, nullptr);

    std::string mode = getPragma(db, "PRAGMA journal_mode");
    EXPECT_EQ(mode, "wal");
}

TEST_F(DatabaseManagerTest, ForeignKeysEnabled)
{
    DatabaseManager dm(dir);
    auto* db = dm.schoolDb();
    ASSERT_NE(db, nullptr);

    std::string fk = getPragma(db, "PRAGMA foreign_keys");
    EXPECT_EQ(fk, "1");
}

TEST_F(DatabaseManagerTest, IdempotentOnSecondOpen)
{
    {
        DatabaseManager dm1(dir);
        EXPECT_TRUE(dm1.isOpen());
    }
    // Reopen same directory — should succeed without errors
    DatabaseManager dm2(dir);
    EXPECT_TRUE(dm2.isOpen());
    auto tables = getTableNames(dm2.schoolDb());
    EXPECT_EQ(tables.size(), 11u);
}
