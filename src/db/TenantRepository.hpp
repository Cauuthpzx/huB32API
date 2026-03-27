#pragma once
#include <string>
#include <mutex>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

struct TenantRecord {
    std::string id;
    std::string slug;
    std::string name;
    std::string ownerEmail;
    std::string status;       // "pending" / "active" / "suspended"
    std::string plan;         // "trial" / "basic" / "pro"
    int64_t     createdAt    = 0;
    int64_t     activatedAt  = 0;  // 0 = not activated
};

class HUB32API_EXPORT TenantRepository
{
public:
    explicit TenantRepository(DatabaseManager& dbManager);

    Result<std::string>  create(const std::string& name, const std::string& slug, const std::string& ownerEmail);
    Result<void>         activate(const std::string& tenantId);
    Result<TenantRecord> findById(const std::string& id);
    Result<TenantRecord> findBySlug(const std::string& slug);
    Result<void>         suspend(const std::string& id);
    Result<void>         unsuspend(const std::string& id);

private:
    DatabaseManager& m_dbManager;
    sqlite3*         m_db;
};

} // namespace hub32api::db
