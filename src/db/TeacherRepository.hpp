#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

struct TeacherRecord {
    std::string id;
    std::string username;
    std::string fullName;
    std::string role;          // "admin" or "teacher"
    int64_t     createdAt = 0;
    // NOTE: password_hash intentionally NOT included — never expose to API
};

class HUB32API_EXPORT TeacherRepository
{
public:
    explicit TeacherRepository(DatabaseManager& dbManager);

    Result<std::string> create(const std::string& username, const std::string& password,
                                const std::string& fullName, const std::string& role = "teacher");
    Result<TeacherRecord> findById(const std::string& id);
    Result<TeacherRecord> findByUsername(const std::string& username);
    Result<std::string> authenticate(const std::string& username, const std::string& password);
    Result<std::vector<TeacherRecord>> listAll();
    Result<void> update(const std::string& id, const std::string& fullName, const std::string& role);
    Result<void> changePassword(const std::string& id, const std::string& newPassword);
    Result<void> remove(const std::string& id);

private:
    DatabaseManager& m_dbManager;
    sqlite3* m_db;
};

} // namespace hub32api::db
