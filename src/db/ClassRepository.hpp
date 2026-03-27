#pragma once
#include <string>
#include <mutex>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

struct ClassRecord {
    std::string id;
    std::string tenantId;
    std::string schoolId;
    std::string name;
    std::string teacherId;  // empty if no teacher assigned
    int64_t     createdAt = 0;
};

class HUB32API_EXPORT ClassRepository
{
public:
    explicit ClassRepository(DatabaseManager& dbManager);

    Result<std::string>              create(const std::string& tenantId, const std::string& schoolId,
                                             const std::string& name, const std::string& teacherId);
    Result<ClassRecord>              findById(const std::string& id);
    Result<std::vector<ClassRecord>> listByTenant(const std::string& tenantId);
    Result<std::vector<ClassRecord>> listByTeacher(const std::string& teacherId);
    Result<void>                     update(const std::string& id, const std::string& name,
                                             const std::string& teacherId);
    Result<void>                     remove(const std::string& id);

private:
    DatabaseManager& m_dbManager;
    sqlite3*         m_db;
};

} // namespace hub32api::db
