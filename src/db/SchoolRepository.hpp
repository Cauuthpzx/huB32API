#pragma once
#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

struct SchoolRecord {
    std::string id;
    std::string name;
    std::string address;
    int64_t     createdAt = 0;
};

class HUB32API_EXPORT SchoolRepository
{
public:
    explicit SchoolRepository(sqlite3* db);

    Result<std::string> create(const std::string& name, const std::string& address);
    Result<SchoolRecord> findById(const std::string& id);
    Result<std::vector<SchoolRecord>> listAll();
    Result<void> update(const std::string& id, const std::string& name, const std::string& address);
    Result<void> remove(const std::string& id);

private:
    sqlite3* m_db;
};

} // namespace hub32api::db
