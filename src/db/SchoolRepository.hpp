#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

struct SchoolRecord {
    std::string id;
    std::string name;
    std::string address;
    int64_t     createdAt = 0;
};

class HUB32API_EXPORT SchoolRepository
{
public:
    explicit SchoolRepository(DatabaseManager& dbManager);

    Result<std::string> create(const std::string& name, const std::string& address);
    Result<SchoolRecord> findById(const std::string& id);
    Result<std::vector<SchoolRecord>> listAll();
    /// Returns schools scoped to a specific tenant (WHERE tenant_id = tenantId).
    Result<std::vector<SchoolRecord>> listByTenant(const std::string& tenantId);
    Result<void> update(const std::string& id, const std::string& name, const std::string& address);
    Result<void> remove(const std::string& id);

private:
    DatabaseManager& m_dbManager;
    sqlite3* m_db;
};

} // namespace hub32api::db
